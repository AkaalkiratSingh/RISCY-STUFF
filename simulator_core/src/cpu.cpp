#include "simulator_core/cpu.h"

#include "control_unit/control_path.h"
#include "simulator_core/decoder.h"

#include <algorithm>
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
        layout_(layoutFor(variant)),
        memory_(memory_words, 0),
        control_unit_(MicrocodeEncoding::Horizontal),
        vertical_unit_(),
        stack_guard_(memory_words, 0),
        controls_(encoding == MicrocodeEncoding::Vertical
                      ? buildStageControls(vertical_unit_)
                      : buildStageControls(control_unit_)) {
        state_.sp = memory_words;
        state_.registers[static_cast<size_t>(Register::R15)] = static_cast<int32_t>(state_.sp);
        resetPipeline();
    }

    Cpu::Cpu(const StageLayout& layout, uint32_t memory_words, MicrocodeEncoding encoding) :
        Cpu(PipelineVariant::FourStage, memory_words, encoding) {
        layout_ = layout;
        resetPipeline();
    }

    void Cpu::resetPipeline() {
        regs_.fill(Latch{});
        trace_.clear();
        micro_trace_.clear();
        fetch_pc_ = 0;
        seq_counter_ = 0;
        stop_fetch_ = false;
        fetch_held_ = false;

        n_slots_ = static_cast<int>(layout_.count);
        ex_slot_ = stageWith(layout_, &StageSlot::EX);
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
        layout_ = layoutFor(variant);
        control_unit_ = ControlUnit(MicrocodeEncoding::Horizontal);
        vertical_unit_ = VerticalControlUnit{};
        controls_ = encoding == MicrocodeEncoding::Vertical
                        ? buildStageControls(vertical_unit_)
                        : buildStageControls(control_unit_);
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

    void Cpu::holdFetch(bool hold) {
        fetch_held_ = hold;
        if (hold && regs_[0].valid && regs_[0].row_cycle == 0 &&
            regs_[0].pc == fetch_pc_) {
            regs_[0] = Latch{};
        }
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

    bool Cpu::dependsOn(const Latch& candidate, const Latch& older) const {
        if (!older.valid || !older.decoded) return false;

        const StageControls& c = controls_[static_cast<uint8_t>(candidate.instr.opcode)];
        const StageControls& o = controls_[static_cast<uint8_t>(older.instr.opcode)];

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
        if (c.mem_write && candidate.instr.rd == dst) return true;

        return false;
    }

    void Cpu::tick() {
        const auto& rom = controlMemory();
        std::array<Latch, kMaxPipelineSlots> next{};
        std::vector<int> order;
        for (int i = 0; i < n_slots_; ++i) {
            if (regs_[static_cast<size_t>(i)].valid) order.push_back(i);
        }
        std::sort(order.begin(), order.end(), [&](int a, int b) {
            return regs_[static_cast<size_t>(a)].seq < regs_[static_cast<size_t>(b)].seq;
        });

        uint64_t squash_after = 0;
        int effect_slot = -1;
        uint32_t effect_pc = 0;
        uint32_t redirect_target = 0;
        bool redirect = false;
        bool fault = false;
        int discarded = 0;

        for (int slot : order) {
            Latch l = regs_[static_cast<size_t>(slot)];
            if (squash_after && l.seq > squash_after) {
                ++discarded;
                continue;
            }

            const MicroOp& row = rom.at(l.micro_pc);
            const RowStage stage = rowStage(row);
            const int expected = slotForLayout(layout_, stage);
            if (expected >= 0 && expected != slot) {
                bool blocked = next[static_cast<size_t>(expected)].valid;
                for (int i = slot; i <= expected && !blocked; ++i) {
                    const Latch& ahead = next[static_cast<size_t>(i)];
                    if (ahead.valid && ahead.seq < l.seq) blocked = true;
                }
                if (blocked) {
                    next[static_cast<size_t>(slot)] = l;
                } else {
                    next[static_cast<size_t>(expected)] = l;
                }
                continue;
            }
            const ControlWord& c = row.ctl;
            bool stalled = false;
            if (c.ir_load && l.pc >= code_.size()) {
                for (const Latch& older : regs_) {
                    if (older.valid && older.seq < l.seq) {
                        stalled = true;
                        break;
                    }
                }
            }
            if (c.operand_read && l.decoded) {
                for (const Latch& older : regs_) {
                    if (older.valid && older.seq < l.seq && dependsOn(l, older)) {
                        const StageControls& producer = controls_[static_cast<uint8_t>(older.instr.opcode)];
                        const bool register_ready = !producer.reg_write ||
                            (producer.mem_to_reg ? older.mem_done : older.alu_done);
                        const bool ra_ready = !producer.ra_write || older.ra_done;
                        if (!forwarding_ || !register_ready || !ra_ready) {
                            stalled = true;
                            break;
                        }
                    }
                }
            }
            if (!stalled && row.next == kMicroEnd && !c.opcode_dispatch) {
                for (const Latch& older : regs_) {
                    if (older.valid && older.seq < l.seq) {
                        stalled = true;
                        break;
                    }
                }
            }
            if (stalled) {
                next[static_cast<size_t>(slot)] = l;
                ++trace_.stalls;
                if (tracing_) trace_.events.push_back(
                    {cycles_, HazardKind::Stall, l.pc, slot, 0, 0, 0});
                continue;
            }

            ++trace_.rows;

            ExceptionCode error = ExceptionCode::NONE;
            std::optional<uint32_t> fault_address;
            if (c.ir_load) {
                if (l.pc >= code_.size()) {
                    error = ExceptionCode::INVALID_MEMORY_ACCESS;
                    fault_address = l.pc;
                } else {
                    l.word = code_[l.pc];
                    if (c.pc_inc) fetch_pc_ = l.pc + 1;
                }
            }
            if (error == ExceptionCode::NONE && c.instr_decode) {
                l.instr = decodeInstruction(l.word);
                l.decoded = true;
            }
            if (error == ExceptionCode::NONE && c.operand_read) {
                l.rs1_val = state_.registers[static_cast<size_t>(l.instr.rs1)];
                l.rs2_val = state_.registers[static_cast<size_t>(l.instr.rs2)];
                l.rd_val = state_.registers[static_cast<size_t>(l.instr.rd)];
                l.ra_val = state_.registers[static_cast<size_t>(Register::R14)];
                if (forwarding_) {
                    const auto readForwarded = [&](Register reg, int32_t value) {
                        uint64_t newest = 0;
                        for (const Latch& older : regs_) {
                            if (!older.valid || !older.decoded || older.seq >= l.seq || older.seq <= newest) continue;
                            const StageControls& producer = controls_[static_cast<uint8_t>(older.instr.opcode)];
                            if (producer.reg_write && older.instr.rd == reg &&
                                (producer.mem_to_reg ? older.mem_done : older.alu_done)) {
                                value = producer.mem_to_reg ? older.mem_data : older.alu_result;
                                newest = older.seq;
                            }
                            if (producer.ra_write && reg == Register::R14 && older.ra_done) {
                                value = static_cast<int32_t>(older.pc + 1);
                                newest = older.seq;
                            }
                        }
                        return value;
                    };
                    l.rs1_val = readForwarded(l.instr.rs1, l.rs1_val);
                    l.rs2_val = readForwarded(l.instr.rs2, l.rs2_val);
                    l.rd_val = readForwarded(l.instr.rd, l.rd_val);
                    l.ra_val = readForwarded(Register::R14, l.ra_val);
                }
            }
            if (error == ExceptionCode::NONE && c.alu_enable) {
                const int32_t rhs = (c.alu_src_imm || l.instr.I) ? l.instr.imm : l.rs2_val;
                const Alu::Result result = alu_.execute(c.alu_op, l.rs1_val, rhs);
                error = result.fault;
                if (error == ExceptionCode::NONE) {
                    l.alu_result = result.value;
                    l.alu_done = true;
                    if (c.flag_update) state_.flags = result.flags;
                }
            }
            if (error == ExceptionCode::NONE && (c.mem_read || c.mem_write)) {
                const uint32_t address = static_cast<uint32_t>(l.alu_result);
                fault_address = address;
                if (error == ExceptionCode::NONE && c.mem_read &&
                    !readMemory(address, l.mem_data)) {
                    error = ExceptionCode::INVALID_MEMORY_ACCESS;
                }
                if (error == ExceptionCode::NONE && c.mem_write) {
                    if (!writeMemory(address, l.rd_val)) error = ExceptionCode::INVALID_MEMORY_ACCESS;
                }
                if (error == ExceptionCode::NONE) l.mem_done = true;
            }
            if (error == ExceptionCode::NONE && c.reg_write) {
                const int32_t value = c.mem_to_reg ? l.mem_data : l.alu_result;
                state_.registers[static_cast<size_t>(l.instr.rd)] = value;
                if (l.instr.rd == Register::R15) state_.sp = static_cast<uint32_t>(value);
                l.wb_done = true;
            }
            if (error == ExceptionCode::NONE && c.ra_write) {
                state_.registers[static_cast<size_t>(Register::R14)] =
                    static_cast<int32_t>(l.pc + 1);
                l.ra_done = true;
            }
            if (error == ExceptionCode::NONE && c.trap) {
                error = ExceptionCode::ILLEGAL_INSTRUCTION;
            }
            if (error != ExceptionCode::NONE) {
                state_.pending_exception = error;
                state_.fault_info = FaultInfo{l.pc, slot, fault_address};
                stop_fetch_ = true;
                fault = true;
                squash_after = l.seq;
                effect_slot = slot;
                effect_pc = l.pc;
                ++trace_.faults;
                recordMicroTrace(l, error);
                continue;
            }

            l.execution_flags = state_.flags;
            const bool condition_taken = row.cond != BranchCond::None &&
                condHolds(row.cond, state_.flags);
            int32_t successor = condition_taken ? row.branch_target : row.next;
            if (c.opcode_dispatch) {
                successor = encoding_ == MicrocodeEncoding::Vertical
                    ? vertical_unit_.routineEntry(l.instr.opcode)
                    : control_unit_.routineEntry(l.instr.opcode);
                if (l.instr.opcode == Opcode::HALT ||
                    successor == control_unit_.routineEntry(static_cast<Opcode>(0xFF))) {
                    stop_fetch_ = true;
                    squash_after = l.seq;
                }
            }
            if (c.pc_write) {
                redirect_target = c.pc_src_ra ? static_cast<uint32_t>(l.ra_val)
                    : branchTarget(l.pc, l.instr);
                fetch_pc_ = redirect_target;
                redirect = true;
                stop_fetch_ = false;
                squash_after = l.seq;
                effect_slot = slot;
                effect_pc = l.pc;
            }
            if (c.halt) stop_fetch_ = true;
            l.row_cycle = cycles_;
            if (successor == kMicroEnd) {
                recordMicroTrace(l);
                state_.pc = l.pc;
                if (!c.halt) ++retired_;
                continue;
            }
            l.micro_pc = static_cast<uint32_t>(successor);
            const RowStage next_stage = rowStage(rom.at(l.micro_pc));
            const int target = next_stage == RowStage::Terminal ? slot
                : slotForLayout(layout_, next_stage);
            bool blocked = next[static_cast<size_t>(target)].valid;
            for (int i = slot; i <= target && !blocked; ++i) {
                const Latch& ahead = next[static_cast<size_t>(i)];
                if (ahead.valid && ahead.seq < l.seq) blocked = true;
            }
            if (blocked && target != slot) {
                next[static_cast<size_t>(slot)] = l;
            } else {
                next[static_cast<size_t>(target)] = l;
            }
        }

        if (redirect) {
            ++trace_.flushes;
            trace_.squashed += static_cast<uint64_t>(discarded);
            trace_.flush_bubbles += static_cast<uint64_t>(discarded);
            if (tracing_) trace_.events.push_back(
                {cycles_, HazardKind::Flush, effect_pc, effect_slot,
                 redirect_target, effect_slot, discarded});
        }
        if (fault && tracing_) trace_.events.push_back(
            {cycles_, HazardKind::Fault, effect_pc, effect_slot, 0, effect_slot, discarded});
        regs_ = next;
        if (!stop_fetch_ && !fetch_held_ && !regs_[0].valid) {
            Latch admitted;
            admitted.valid = true;
            admitted.seq = ++seq_counter_;
            admitted.pc = fetch_pc_;
            admitted.micro_pc = static_cast<uint32_t>(encoding_ == MicrocodeEncoding::Vertical
                ? vertical_unit_.frontendEntry() : control_unit_.frontendEntry());
            regs_[0] = admitted;
        }
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
        std::vector<MicroPcView> views;
        const auto& rom = controlMemory();
        for (int i = 0; i < n_slots_; ++i) {
            const Latch& latch = regs_[static_cast<size_t>(i)];
            if (!latch.valid) continue;
            const MicroOp& row = rom.at(latch.micro_pc);
            views.push_back({latch.pc, layout_[static_cast<size_t>(i)].name,
                             static_cast<int>(latch.micro_pc), row.ctl});
        }
        return views;
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
