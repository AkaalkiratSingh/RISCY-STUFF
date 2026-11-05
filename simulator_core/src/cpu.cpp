#include "simulator_core/cpu.h"

#include "control_unit/control_path.h"
#include "simulator_core/decoder.h"

#include <cstddef>
#include <stdexcept>

namespace risc201 {

    namespace {

        int stageWith(const StageLayout& layout, bool StageSlot::* which) {
            for (size_t i = 0; i < layout.count; ++i) {
                if (layout[i].*which) return static_cast<int>(i);
            }
            return -1;
        }

    }

    Cpu::Cpu(PipelineVariant variant, uint32_t memory_words, MicrocodeEncoding encoding) :
        variant_(variant),
        encoding_(encoding),
        memory_(memory_words, 0),
        control_unit_(MicrocodeEncoding::Horizontal),
        vertical_unit_(),
        stack_guard_(memory_words, 0),
        controls_(encoding == MicrocodeEncoding::Vertical
                      ? buildStageControls(vertical_unit_)
                      : buildStageControls(control_unit_)) {
        frontend_ = encoding == MicrocodeEncoding::Vertical
            ? frontendControls(vertical_unit_) : frontendControls(control_unit_);
        state_.sp = memory_words;
        state_.registers[static_cast<size_t>(Register::R15)] = static_cast<int32_t>(state_.sp);
        resetPipeline();
    }

    void Cpu::resetPipeline() {
        regs_.fill(Latch{});
        trace_.clear();
        micro_trace_.clear();
        fetch_pc_ = 0;
        stop_fetch_ = false;
        fetch_held_ = false;

        const StageLayout layout = layoutFor(variant_);
        n_slots_ = static_cast<int>(layout.count);
        ex_slot_ = stageWith(layout, &StageSlot::EX);
    }

    void Cpu::loadWords(const std::vector<uint32_t>& words) {
        source_words_ = words;
        code_.clear();

        std::vector<uint32_t> literals;
        bool in_literals = false;
        for (uint32_t word : words) {
            if (!in_literals && word == kLiteralsMarker) {
                in_literals = true;
                continue;
            }
            if (in_literals) literals.push_back(word);
            else code_.push_back(word);
        }

        state_ = CpuState{};
        state_.sp = static_cast<uint32_t>(memory_.size());
        state_.registers[static_cast<size_t>(Register::R15)] = static_cast<int32_t>(state_.sp);
        cycles_ = 0;
        retired_ = 0;
        resetPipeline();

        for (size_t i = 0; i < literals.size() && i < memory_.size(); ++i) {
            memory_[i] = static_cast<int32_t>(literals[i]);
        }
    }

    void Cpu::reconfigure(PipelineVariant variant, MicrocodeEncoding encoding) {
        variant_ = variant;
        encoding_ = encoding;
        control_unit_ = ControlUnit(MicrocodeEncoding::Horizontal);
        vertical_unit_ = VerticalControlUnit{};
        controls_ = encoding == MicrocodeEncoding::Vertical
                        ? buildStageControls(vertical_unit_)
                        : buildStageControls(control_unit_);
        frontend_ = encoding == MicrocodeEncoding::Vertical
            ? frontendControls(vertical_unit_) : frontendControls(control_unit_);
        routine_ends_valid_ = false;
        memory_.assign(memory_.size(), 0);
        loadWords(source_words_);
    }

    bool Cpu::pipelineEmpty() const {
        for (const Latch& l : regs_) {
            if (l.valid) return false;
        }
        return true;
    }

    bool Cpu::readMemory(int64_t address, int32_t& out) const {
        if (address < 0 || static_cast<size_t>(address) >= memory_.size()) return false;
        out = memory_[static_cast<size_t>(address)];
        return true;
    }

    bool Cpu::writeMemory(int64_t address, int32_t value) {
        if (address < 0 || static_cast<size_t>(address) >= memory_.size()) return false;
        memory_[static_cast<size_t>(address)] = value;
        return true;
    }

    uint32_t Cpu::branchTarget(uint32_t instr_pc, const Instruction& instr) const {
        const uint64_t byte_target =
            4 * static_cast<uint64_t>(instr_pc) + static_cast<uint64_t>(instr.imm) + 4;
        return static_cast<uint32_t>(byte_target / 4);
    }

    Latch Cpu::fetchStage(bool stall) {
        Latch out;
        if (stop_fetch_ || fetch_held_ || !frontend_.ir_load) return out;

        if (fetch_pc_ >= code_.size()) {
            out.valid = true;
            out.pc = fetch_pc_;
            out.instr = Instruction{};
            out.decoded = true;
            out.fault = true;
            out.fault_code = ExceptionCode::INVALID_MEMORY_ACCESS;
            return out;
        }

        out.valid = true;
        out.pc = fetch_pc_;
        out.word = code_[fetch_pc_];
        if (!stall && frontend_.pc_inc) fetch_pc_ += 1;
        return out;
    }

    bool Cpu::dependsOn(const Latch& candidate, const Latch& older) const {
        if (!older.valid || !older.decoded) return false;

        const StageControls& c = controls_[static_cast<uint8_t>(candidate.instr.opcode)];
        const StageControls& o = controls_[static_cast<uint8_t>(older.instr.opcode)];

        if ((o.sp_dec || o.sp_inc) &&
            (c.sp_dec || c.sp_inc || c.mem_addr_sp || c.pc_from_mem)) {
            return true;
        }

        if (o.ra_write &&
            (c.pc_from_ra ||
             candidate.instr.rs1 == Register::R14 ||
             (!c.alu_src_imm && candidate.instr.rs2 == Register::R14) ||
             candidate.instr.rd == Register::R14)) {
            return true;
        }

        if (!o.reg_write) return false;

        const Register dst = older.instr.rd;
        if (c.pc_from_ra && dst == Register::R14) return true;
        if (c.alu_enable &&
            (candidate.instr.rs1 == dst || (!c.alu_src_imm && candidate.instr.rs2 == dst))) {
            return true;
        }
        if (c.mem_write && !c.mem_data_pc && candidate.instr.rd == dst) return true;

        return false;
    }

    int32_t Cpu::bypass(Register src, int32_t current) const {
        const Latch& ex_mem = regs_[static_cast<size_t>(ex_slot_)];
        if (ex_mem.valid && ex_mem.decoded) {
            const StageControls& c = controls_[static_cast<uint8_t>(ex_mem.instr.opcode)];
            if (src == Register::R14 && c.ra_write) {
                return static_cast<int32_t>(ex_mem.pc + 1);
            }
            if (c.reg_write && ex_mem.instr.rd == src) {
                return c.mem_to_reg ? current : ex_mem.alu_result;
            }
        }

        const Latch& mem_wb = regs_[static_cast<size_t>(ex_slot_ + 1)];
        if (mem_wb.valid && mem_wb.decoded) {
            const StageControls& c = controls_[static_cast<uint8_t>(mem_wb.instr.opcode)];
            if (src == Register::R14 && c.ra_write) {
                return static_cast<int32_t>(mem_wb.pc + 1);
            }
            if (c.reg_write && mem_wb.instr.rd == src) {
                return c.mem_to_reg ? mem_wb.mem_data : mem_wb.alu_result;
            }
        }

        return current;
    }

    uint32_t Cpu::bypassSp() const {
        const Latch& ex_mem = regs_[static_cast<size_t>(ex_slot_)];
        if (ex_mem.valid && ex_mem.writes_sp) return ex_mem.sp_commit;

        const Latch& mem_wb = regs_[static_cast<size_t>(ex_slot_ + 1)];
        if (mem_wb.valid && mem_wb.writes_sp) return mem_wb.sp_commit;

        return state_.sp;
    }

    bool Cpu::wouldStall(uint32_t* pc) const {
        if (fetch_held_) return false;

        Latch candidate;

        if (ex_slot_ == 1) {
            if (fetch_pc_ >= code_.size()) return false;
            candidate.valid = true;
            candidate.decoded = true;
            candidate.pc = fetch_pc_;
            candidate.instr = decodeInstruction(code_[fetch_pc_]);
        } else {
            candidate = regs_[static_cast<size_t>(ex_slot_ - 2)];
            if (!candidate.valid || !candidate.decoded) return false;
        }

        for (int i = ex_slot_ - 1; i <= n_slots_ - 3; ++i) {
            if (i < 0) continue;
            const Latch& p = regs_[static_cast<size_t>(i)];
            if (!dependsOn(candidate, p)) continue;

            if (!forwarding_) {
                if (pc) *pc = candidate.pc;
                return true;
            }

            // The producer that reaches EX/MEM exactly when this instruction runs
            // is past bypassing when its value is still in the memory stage.
            if (i == ex_slot_ - 1 &&
                controls_[static_cast<uint8_t>(p.instr.opcode)].mem_to_reg) {
                if (pc) *pc = candidate.pc;
                return true;
            }
        }
        return false;
    }

    void Cpu::applySlot(const StageSlot& slot, Latch& out, Effects& fx) {
        if (out.fault) {
            if (slot.EX) {
                fx.fault = true;
                fx.code = out.fault_code;
            }
            return;
        }

        if (slot.ID && !out.decoded && frontend_.instr_decode && frontend_.opcode_dispatch) {
            out.instr = decodeInstruction(out.word);
            out.decoded = true;
            if (control_unit_.routineEntry(out.instr.opcode) ==
                control_unit_.routineEntry(static_cast<Opcode>(0xFF))) {
                out.fault = true;
                out.fault_code = ExceptionCode::ILLEGAL_INSTRUCTION;
                return;
            }
            if (controls_[static_cast<uint8_t>(out.instr.opcode)].halting) fx.stop_fetch = true;
        }

        const uint8_t op = static_cast<uint8_t>(out.instr.opcode);

        if (slot.OF && out.decoded && controls_[op].operand_read) {
            out.rs1_val = state_.registers[static_cast<size_t>(out.instr.rs1)];
            out.rs2_val = state_.registers[static_cast<size_t>(out.instr.rs2)];
            out.rd_val  = state_.registers[static_cast<size_t>(out.instr.rd)];
            out.ra_val = state_.registers[static_cast<size_t>(Register::R14)];
        }

        if (slot.EX && out.decoded) {
            out.execution_flags = state_.flags;
            const StageControls& c = controls_[op];

            if (forwarding_) {
                out.rs1_val = bypass(out.instr.rs1, out.rs1_val);
                out.rs2_val = bypass(out.instr.rs2, out.rs2_val);
                out.rd_val  = bypass(out.instr.rd, out.rd_val);
            }

            if (c.alu_enable) {
                const int32_t rhs = (c.alu_src_imm || out.instr.I) ? out.instr.imm : out.rs2_val;
                const Alu::Result r = alu_.execute(c.alu_op, out.rs1_val, rhs);
                if (r.fault != ExceptionCode::NONE) {
                    fx.fault = true;
                    fx.code = r.fault;
                    return;
                }
                out.alu_result = r.value;
                if (c.flag_update) state_.flags = r.flags;
            }

            const uint32_t sp = forwarding_ ? bypassSp() : state_.sp;
            if (c.sp_dec) {
                out.mem_sp_addr = sp - 1;
                out.sp_commit = sp - 1;
                out.writes_sp = true;
            }
            if (c.sp_inc) {
                out.mem_sp_addr = sp;
                out.sp_commit = sp + 1;
                out.writes_sp = true;
            }

            if (c.pc_from_ra) {
                const int32_t ra = forwarding_
                    ? bypass(Register::R14, out.ra_val)
                    : out.ra_val;
                out.redirect = true;
                out.next_pc = static_cast<uint32_t>(ra);
                fx.taken = true;
                fx.target = out.next_pc;
            } else if (c.pc_from_target && (!c.is_branch || condHolds(c.cond, state_.flags))) {
                out.redirect = true;
                out.next_pc = branchTarget(out.pc, out.instr);
                fx.taken = true;
                fx.target = out.next_pc;
            }
        }

        if (slot.MEM && out.decoded) {
            const StageControls& c = controls_[op];
            const uint32_t address =
                c.mem_addr_sp ? out.mem_sp_addr : static_cast<uint32_t>(out.alu_result);

            if (c.stack_check) {
                const auto fault = c.mem_write ? stack_guard_.checkPush(out.mem_sp_addr + 1)
                                               : stack_guard_.checkPop(out.mem_sp_addr);
                if (fault) {
                    fx.fault = true;
                    fx.code = *fault;
                    return;
                }
            }

            if (c.mem_read) {
                int32_t loaded = 0;
                if (!readMemory(address, loaded)) {
                    fx.fault = true;
                    fx.code = ExceptionCode::INVALID_MEMORY_ACCESS;
                    return;
                }
                out.mem_data = loaded;
            }

            if (c.mem_write) {
                const int32_t value = c.mem_data_pc
                    ? static_cast<int32_t>(out.pc + 1)
                    : out.rd_val;
                if (!writeMemory(address, value)) {
                    fx.fault = true;
                    fx.code = ExceptionCode::INVALID_MEMORY_ACCESS;
                    return;
                }
            }

            if (c.pc_from_mem) {
                out.redirect = true;
                out.next_pc = static_cast<uint32_t>(out.mem_data);
                fx.taken = true;
                fx.target = out.next_pc;
            }
        }
    }

    void Cpu::retire(const Latch& out) {
        if (!out.valid || !out.decoded) return;

        if (out.fault) {
            state_.pending_exception = out.fault_code;
            state_.fault_info = FaultInfo{out.pc, n_slots_ - 1, out.pc};
            ++trace_.faults;
            stop_fetch_ = true;
            return;
        }

        const StageControls& c = controls_[static_cast<uint8_t>(out.instr.opcode)];

        if (c.reg_write) {
            const int32_t value = c.mem_to_reg ? out.mem_data : out.alu_result;
            state_.registers[static_cast<size_t>(out.instr.rd)] = value;
            if (out.instr.rd == Register::R15) state_.sp = static_cast<uint32_t>(value);
        }
        if (out.writes_sp) {
            state_.sp = out.sp_commit;
            state_.registers[static_cast<size_t>(Register::R15)] = static_cast<int32_t>(out.sp_commit);
        }
        if (c.ra_write) {
            state_.registers[static_cast<size_t>(Register::R14)] = static_cast<int32_t>(out.pc + 1);
        }
        recordMicroTrace(out);
        state_.pc = out.pc;
        if (!c.halting) ++retired_;
    }

    void Cpu::tick() {
        const StageLayout layout = layoutFor(variant_);
        const int n = n_slots_;
        const int ex = ex_slot_;

        std::array<Latch, kMaxPipelineSlots> next{};

        uint32_t stall_pc = 0;
        const bool stall = !stop_fetch_ && wouldStall(&stall_pc);

        int effect_slot = -1;
        uint32_t effect_pc = 0;
        int discarded = 0;
        Effects oldest;
        bool have_effect = false;

        for (int i = n - 1; i >= 0; --i) {
            const StageSlot& slot = layout[static_cast<size_t>(i)];
            const bool wrong_path = effect_slot >= 0 && i < effect_slot;

            Effects here;
            Latch out;

            if (stall && i < ex - 1) {
                out = regs_[static_cast<size_t>(i)];
            } else {
                if (i == 0) {
                    out = fetchStage(stall);
                } else {
                    out = regs_[static_cast<size_t>(i - 1)];
                }

                const bool bubble = stall && i == ex - 1;
                if (out.valid && !bubble && !wrong_path) {
                    applySlot(slot, out, here);
                }
                if (bubble) out = Latch{};
            }

            if (i == n - 1) {
                if (!wrong_path) retire(out);
            } else {
                if (wrong_path) {
                    if (out.valid) ++discarded;
                    out = Latch{};
                }
                next[static_cast<size_t>(i)] = out;
            }

            if (!wrong_path && !have_effect &&
                (here.fault || here.taken || here.stop_fetch)) {
                if (here.fault) {
                    FaultInfo info{out.pc, i, std::nullopt};
                    if (here.code == ExceptionCode::INVALID_MEMORY_ACCESS) {
                        const auto& c = controls_[static_cast<uint8_t>(out.instr.opcode)];
                        info.address = out.fault ? out.pc : (c.mem_addr_sp ? out.mem_sp_addr
                                                   : static_cast<uint32_t>(out.alu_result));
                    }
                    state_.fault_info = info;
                    recordMicroTrace(out, here.code);
                }
                oldest = here;
                have_effect = true;
                effect_slot = i;
                effect_pc = out.pc;
            }
        }

        if (have_effect) {
            if (oldest.fault) {
                for (int i = 0; i <= effect_slot && i < n - 1; ++i) {
                    next[static_cast<size_t>(i)].valid = false;
                }
                stop_fetch_ = true;
                state_.pending_exception = oldest.code;
                ++trace_.faults;
                if (tracing_) {
                    trace_.events.push_back(
                        {cycles_, HazardKind::Fault, effect_pc, effect_slot, 0,
                         effect_slot, discarded});
                }
            } else if (oldest.taken) {
                fetch_pc_ = oldest.target;
                stop_fetch_ = false;
                ++trace_.flushes;
                trace_.flush_bubbles += static_cast<uint64_t>(effect_slot);
                trace_.squashed += static_cast<uint64_t>(discarded);
                if (tracing_) {
                    trace_.events.push_back(
                        {cycles_, HazardKind::Flush, effect_pc, effect_slot,
                         oldest.target, effect_slot, discarded});
                }
            }
            if (oldest.stop_fetch) stop_fetch_ = true;
        }

        // A redirect in the same clock already discards the slot the interlock
        // was holding, so counting the stall too would double-count that clock.
        if (stall && !(have_effect && oldest.taken)) {
            ++trace_.stalls;
            if (tracing_) {
                trace_.events.push_back(
                    {cycles_, HazardKind::Stall, stall_pc, ex - 1, 0, 0, 0});
            }
        }

        regs_ = next;

        if (stop_fetch_ && pipelineEmpty()) state_.halted = true;
    }

    void Cpu::recordMicroTrace(const Latch& instruction, ExceptionCode fault) {
        if (!micro_tracing_ || (instruction.fault &&
            instruction.fault_code == ExceptionCode::INVALID_MEMORY_ACCESS)) return;

        const auto record = [&](const auto& unit) {
            const auto& cm = unit.controlMemory();
            for (const ControlStep& step : controlPath(unit, instruction.instr.opcode,
                                                       instruction.execution_flags, fault)) {
                const MicroOp& row = cm[static_cast<size_t>(step.micro_pc)];
                const uint64_t packed = encoding_ == MicrocodeEncoding::Vertical
                    ? packVertical(row, step.micro_pc)
                    : ControlUnit::packHorizontal(row);
                micro_trace_.push_back({cycles_, instruction.pc, instruction.instr.opcode,
                    static_cast<uint32_t>(step.micro_pc), step.next, packed, step.signals,
                    step.condition, step.exception});
            }
        };

        if (encoding_ == MicrocodeEncoding::Vertical) {
            record(vertical_unit_);
        } else {
            record(control_unit_);
        }
    }

    std::vector<MicroPcView> Cpu::microSnapshot() const {
        const StageLayout layout = layoutFor(variant_);

        const auto snapshotFor = [&](const auto& unit) {
            std::vector<MicroPcView> views;

            const auto appendFor = [&](const std::vector<PathStep>& rows,
                                       PipelineStage stage, uint32_t pc) {
                const StageMask bit = stageBit(stage);
                for (const PathStep& step : rows) {
                    if (step.stages & bit) {
                        views.push_back({pc, pipelineStageName(stage), step.micro_pc,
                                         step.signals});
                        return;
                    }
                }
            };

            for (int i = 0; i < n_slots_; ++i) {
                const Latch& l = regs_[static_cast<size_t>(i)];
                if (!l.valid) continue;

                const StageSlot& slot = layout[static_cast<size_t>(i)];
                const std::vector<PathStep>& rows =
                    opcodeLayout(unit, l.decoded ? l.instr.opcode : Opcode::NOP);

                if (slot.IF)  appendFor(rows, PipelineStage::IF, l.pc);
                if (!l.decoded) continue;
                if (slot.ID)  appendFor(rows, PipelineStage::ID, l.pc);
                if (slot.OF)  appendFor(rows, PipelineStage::OF, l.pc);
                if (slot.EX)  appendFor(rows, PipelineStage::EX, l.pc);
                if (slot.MEM) appendFor(rows, PipelineStage::MEM, l.pc);
                if (slot.WB)  appendFor(rows, PipelineStage::WB, l.pc);
            }

            if (fetching() && !fetch_held_) {
                const std::vector<PathStep>& rows = opcodeLayout(unit, Opcode::NOP);
                for (const PathStep& step : rows) {
                    if (step.stages & stageBit(PipelineStage::IF)) {
                        views.push_back({fetch_pc_, "IF", step.micro_pc, step.signals});
                        break;
                    }
                }
            }
            return views;
        };

        return encoding_ == MicrocodeEncoding::Vertical ? snapshotFor(vertical_unit_)
                                                        : snapshotFor(control_unit_);
    }

    const std::vector<MicroOp>& Cpu::controlMemory() const {
        return encoding_ == MicrocodeEncoding::Vertical ? vertical_unit_.controlMemory()
                                                        : control_unit_.controlMemory();
    }

    const std::vector<int>& Cpu::routineEnds() const {
        if (!routine_ends_valid_) {
            routine_ends_ = encoding_ == MicrocodeEncoding::Vertical
                ? risc201::routineEnds(vertical_unit_)
                : risc201::routineEnds(control_unit_);
            routine_ends_valid_ = true;
        }
        return routine_ends_;
    }

    void Cpu::step() {
        micro_trace_.clear();
        if (state_.halted) return;
        ++cycles_;
        tick();
    }

    void Cpu::stepInstruction() {
        const uint64_t before = retired_;
        for (int guard = 0; guard < 64 && !state_.halted && retired_ == before; ++guard) {
            step();
        }
    }

    void Cpu::run() {
        while (!state_.halted) step();
    }

} // namespace risc201
