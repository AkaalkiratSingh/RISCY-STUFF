#include "simulator_core/cpu.h"

#include "simulator_core/decoder.h"

#include <cstddef>

namespace risc201 {

    namespace {

        bool condHolds(BranchCond c, const Flags& f) {
            switch (c) {
            case BranchCond::None:     return false;
            case BranchCond::Zero:     return f.zero;
            case BranchCond::NotZero:  return !f.zero;
            case BranchCond::Negative: return f.negative;
            case BranchCond::Carry:    return f.carry;
            case BranchCond::Overflow: return f.overflow;
            }
            return false;
        }

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
        stack_guard_(memory_words, 0),
        controls_(encoding == MicrocodeEncoding::Vertical
                      ? buildStageControls(VerticalControlUnit{})
                      : buildStageControls(control_unit_)) {
        state_.sp = memory_words;
        resetPipeline();
    }

    void Cpu::resetPipeline() {
        regs_.fill(Latch{});
        trace_.clear();
        fetch_pc_ = 0;
        stop_fetch_ = false;

        const StageLayout layout = layoutFor(variant_);
        n_slots_ = static_cast<int>(layout.count);
        ex_slot_ = stageWith(layout, &StageSlot::EX);
    }

    void Cpu::loadWords(const std::vector<uint32_t>& words) {
        code_ = words;
        state_ = CpuState{};
        state_.sp = static_cast<uint32_t>(memory_.size());
        cycles_ = 0;
        retired_ = 0;
        resetPipeline();
    }

    void Cpu::reconfigure(PipelineVariant variant, MicrocodeEncoding encoding) {
        variant_ = variant;
        encoding_ = encoding;
        control_unit_ = ControlUnit(MicrocodeEncoding::Horizontal);
        controls_ = encoding == MicrocodeEncoding::Vertical
                        ? buildStageControls(VerticalControlUnit{})
                        : buildStageControls(control_unit_);
        memory_.assign(memory_.size(), 0);
        loadWords(code_);
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
        if (stop_fetch_) return out;

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
        if (!stall) fetch_pc_ += 1;
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

        if (!o.reg_write) return false;

        const Register dst = older.instr.rd;
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
            if (c.reg_write && ex_mem.instr.rd == src) {
                return c.mem_to_reg ? current : ex_mem.alu_result;
            }
        }

        const Latch& mem_wb = regs_[static_cast<size_t>(ex_slot_ + 1)];
        if (mem_wb.valid && mem_wb.decoded) {
            const StageControls& c = controls_[static_cast<uint8_t>(mem_wb.instr.opcode)];
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
        if (out.fault) return;

        const uint8_t op = static_cast<uint8_t>(out.instr.opcode);

        if (slot.ID && !out.decoded) {
            out.instr = decodeInstruction(out.word);
            out.decoded = true;
            if (controls_[static_cast<uint8_t>(out.instr.opcode)].halting) fx.stop_fetch = true;
        }

        if (slot.OF && out.decoded) {
            out.rs1_val = state_.registers[static_cast<size_t>(out.instr.rs1)];
            out.rs2_val = state_.registers[static_cast<size_t>(out.instr.rs2)];
            out.rd_val  = state_.registers[static_cast<size_t>(out.instr.rd)];
        }

        if (slot.EX && out.decoded) {
            const StageControls& c = controls_[op];

            if (forwarding_) {
                out.rs1_val = bypass(out.instr.rs1, out.rs1_val);
                out.rs2_val = bypass(out.instr.rs2, out.rs2_val);
                out.rd_val  = bypass(out.instr.rd, out.rd_val);
            }

            if (c.alu_enable) {
                const int32_t rhs = (c.alu_src_imm || out.instr.I) ? out.instr.imm : out.rs2_val;
                const Alu::Result r = alu_.execute(c.alu_op, out.rs1_val, rhs);
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

            if (c.pc_from_target && (!c.is_branch || condHolds(c.cond, state_.flags))) {
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
            stop_fetch_ = true;
            return;
        }

        const StageControls& c = controls_[static_cast<uint8_t>(out.instr.opcode)];

        if (c.reg_write) {
            state_.registers[static_cast<size_t>(out.instr.rd)] =
                c.mem_to_reg ? out.mem_data : out.alu_result;
        }
        if (out.writes_sp) state_.sp = out.sp_commit;
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

    void Cpu::step() {
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
