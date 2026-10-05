#include "simulator_core/cpu.h"

#include "simulator_core/decoder.h"

#include <cstddef>

namespace risc201 {

    // Constructor
    Cpu::Cpu(PipelineVariant variant, uint32_t memory_words) :
        variant_(variant),
        memory_(memory_words, 0),
        control_unit_(MicrocodeEncoding::Horizontal),
        stack_guard_(memory_words, 0) {
        state_.sp = memory_words;
    }

    
    void Cpu::loadWords(const std::vector<uint32_t>& words) {
        code_ = words;
        state_ = CpuState{};
        state_.sp = static_cast<uint32_t>(memory_.size());
        cycles_ = 0;
        retired_ = 0;
    }

    void Cpu::raiseException(ExceptionCode code) {
        state_.pending_exception = code;
        state_.halted = true;
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
        const uint64_t byte_target = 4 * static_cast<uint64_t>(instr_pc) + static_cast<uint64_t>(instr.imm) + 4;
        return static_cast<uint32_t>(byte_target / 4);
    }

    // EX + WB 
    void Cpu::executeInstruction(const Instruction& instr) {
        const uint32_t instr_pc = state_.pc;

        int32_t alu_result = 0;
        int32_t mem_data = 0;

        control_unit_.reset();
        for (int guard = 0; guard < 64 && !control_unit_.routineDone(); ++guard) {
            const ControlWord ctl = control_unit_.step(instr.opcode, state_.flags);

            // --- IF: fetch row (PC advance) ---
            if (ctl.pc_inc) {
                state_.pc = instr_pc + 1;
            }

            // --- EX: begin [MEM is folded into EX]---
            if (ctl.alu_enable) {
                const int32_t lhs = state_.registers[static_cast<size_t>(instr.rs1)];

                const int32_t rhs = (ctl.alu_src_imm || instr.I)
                    ? instr.imm
                    : state_.registers[static_cast<size_t>(instr.rs2)];
                
                const Alu::Result r = alu_.execute(ctl.alu_op, lhs, rhs);
                alu_result = r.value;

                if (ctl.flag_update)
                    state_.flags = r.flags;
                
            }

            if (ctl.mem_read) {
                if (!readMemory(alu_result, mem_data)) {
                    raiseException(ExceptionCode::INVALID_MEMORY_ACCESS);
                    return;
                }
            }

            if (ctl.mem_write) {
                const int32_t value = state_.registers[static_cast<size_t>(instr.rd)];
                if (!writeMemory(alu_result, value)) {
                    raiseException(ExceptionCode::INVALID_MEMORY_ACCESS);
                    return;
                }
            }

            if (ctl.pc_write) {
                state_.pc = branchTarget(instr_pc, instr);
            }

            if (ctl.halt) {
                state_.halted = true;
            }
            // --- EX: end ---

            // --- WB: begin — register writeback ---
            if (ctl.reg_write) {
                state_.registers[static_cast<size_t>(instr.rd)] =
                    ctl.mem_to_reg ? mem_data : alu_result;
            }
            // --- WB: end ---
        }
    }

    void Cpu::step() {
        // === IF (instruction fetch): begin ===
        if (state_.halted) return; // running step() on a halted program does nothing

        // pc moved out of the bounds of code
        if (static_cast<size_t>(state_.pc) >= code_.size()) {
            raiseException(ExceptionCode::INVALID_MEMORY_ACCESS);
            return;
        }

        const uint32_t word = code_[static_cast<size_t>(state_.pc)];
        // === IF: end ===

        // === ID (decode): begin ===
        const Instruction instr = decodeInstruction(word); // decode the fetched word
        // === ID: end ===

        // === EX + WB: begin ... end (see executeInstruction) ===
        executeInstruction(instr);

        ++cycles_;
        if (!state_.halted) ++retired_;
    }

    void Cpu::run() {
        while (!state_.halted) {
            step();
        }
    }

} // namespace risc201
