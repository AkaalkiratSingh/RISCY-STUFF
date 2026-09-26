#include "simulator_core/cpu.h"

#include "simulator_core/decoder.h"

#include <algorithm>
#include <cstddef>

namespace risc201 {

    namespace {

        // TRUE if the Instruction modifies FLAGS
        // LOAD/STORE don't modify the flags 
        bool isFlagSettingOp(Opcode op) {
            switch (op) {
            case Opcode::ADD:
            case Opcode::SUB:
            case Opcode::AND:
            case Opcode::OR:
            case Opcode::XOR:
            case Opcode::SHL:
            case Opcode::SHR:
            case Opcode::SAR:
            case Opcode::NOT:
            case Opcode::CMP:
                return true;
            default:
                return false;
            }
        }

        // The name of the Register that is gonna hold the instruction's output (-1 if result is not stored in a register)
        int destinationRegister(const Instruction& instr) {
            switch (instr.opcode) {
            case Opcode::ADD:
            case Opcode::SUB:
            case Opcode::AND:
            case Opcode::OR:
            case Opcode::XOR:
            case Opcode::SHL:
            case Opcode::SHR:
            case Opcode::SAR:
            case Opcode::NOT:
            case Opcode::LOAD:
                return static_cast<int>(instr.rd);

            default:
                return -1;
            }
        }

        bool readsRegister(const Instruction& instr, int reg) {
            if (reg < 0) return false;

            switch (instr.opcode) {
            case Opcode::ADD:
            case Opcode::SUB:
            case Opcode::AND:
            case Opcode::OR:
            case Opcode::XOR:
            case Opcode::SHL:
            case Opcode::SHR:
            case Opcode::SAR:
            case Opcode::CMP:
                if (static_cast<int>(instr.rs1) == reg) return true;
                if (!instr.I && static_cast<int>(instr.rs2) == reg) return true;
                return false;

            case Opcode::NOT:
                return static_cast<int>(instr.rs1) == reg;
            case Opcode::LOAD:
                return static_cast<int>(instr.rs1) == reg;
            case Opcode::STORE:
                return static_cast<int>(instr.rs1) == reg ||
                    static_cast<int>(instr.rd) == reg;
            case Opcode::PUSH:
                return static_cast<int>(instr.rd) == reg;

            default:
                return false;
            }
        }

    } 

    Cpu::Cpu(PipelineVariant variant, uint32_t memory_words) :
        variant_(variant),
        memory_(memory_words, 0),
        control_unit_(MicrocodeEncoding::Horizontal) {
    }

    void Cpu::loadProgram(const std::vector<Instruction>& program) {
        program_ = program;
        state_ = CpuState{};
        cycles_ = 0;
        retired_ = 0;
        next_fetch_cycle_ = 1;
        prev_dest_ = -1;
    }

    void Cpu::loadWords(const std::vector<uint32_t>& words) {
        std::vector<Instruction> program;
        program.reserve(words.size());
        for (uint32_t word : words) {
            program.push_back(decodeInstruction(word));
        }
        loadProgram(program);
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
        const u_int64_t byte_target = 4 * static_cast<u_int64_t>(instr_pc) + static_cast<u_int64_t>(instr.imm) + 4;
        return static_cast<uint32_t>(byte_target / 4);
    }

    // EX + WB — the Control Unit walks one micro-op per iteration and hands back
    // a control word that selects what the datapath does this cycle. Stage
    // boundaries inside the loop are flagged inline: EX begins at the ALU /
    // address arithmetic, EX ends and WB begins at the register writeback, and
    // WB ends with the branch/halt resolution.
    void Cpu::executeInstruction(const Instruction& instr) {
        const uint32_t instr_pc = state_.pc;

        int32_t alu_result = 0;
        int32_t mem_data = 0;

        control_unit_.reset();
        // Every current micro-routine is at most four rows; the guard turns a
        // broken `next` chain into a clean halt instead of a spin.
        for (int guard = 0; guard < 64 && !control_unit_.routineDone(); ++guard) {
            const ControlWord ctl = control_unit_.step(instr.opcode, state_.flags);

            if (ctl.pc_inc) {
                state_.pc = instr_pc + 1;
            }

            // --- EX: begin — ALU op / effective-address arithmetic ---
            if (ctl.alu_enable) {
                const int32_t lhs = state_.registers[static_cast<size_t>(instr.rs1)];
                const int32_t rhs = instr.I
                    ? instr.imm
                    : state_.registers[static_cast<size_t>(instr.rs2)];
                const Alu::Result r = alu_.execute(ctl.alu_op, lhs, rhs);
                alu_result = r.value;
                if (isFlagSettingOp(instr.opcode)) {
                    state_.flags = r.flags;
                }
            }

            // --- EX: end (MEM folded into EX) — load/store data access ---
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

            // --- WB: begin — register writeback ---
            if (ctl.reg_write) {
                state_.registers[static_cast<size_t>(instr.rd)] =
                    ctl.mem_to_reg ? mem_data : alu_result;
            }

            // --- WB: end — branch target / halt resolve ---
            if (ctl.pc_write) {
                state_.pc = branchTarget(instr_pc, instr);
            }

            if (ctl.halt) {
                state_.halted = true;
            }
        }
    }

    void Cpu::step() {
        // === IF (instruction fetch): begin ===
        // Read the program word at PC. PC is not advanced here — the Control
        // Unit's fetch micro-op drives pc_inc from inside executeInstruction().
        if (state_.halted) return;

        if (static_cast<size_t>(state_.pc) >= program_.size()) {
            raiseException(ExceptionCode::INVALID_MEMORY_ACCESS);
            return;
        }

        const Instruction instr = program_[static_cast<size_t>(state_.pc)];
        // === IF: end ===

        // === ID / OF (decode + operand fetch): begin ===
        // Hazard interlock: without forwarding, a consumer of the register the
        // previous instruction produces must wait one cycle for the writeback.
        // Operand values themselves are read on demand in EX below.
        const bool stall = readsRegister(instr, prev_dest_);
        uint32_t fetch_cycle = next_fetch_cycle_;
        if (stall) ++fetch_cycle;
        next_fetch_cycle_ = fetch_cycle + 1;
        // === ID / OF: end ===

        // === EX + WB: begin ... end (see executeInstruction) ===
        executeInstruction(instr);

        // === WB retirement accounting ===
        // Charge pipeline cycles for the instruction that just left the pipe.
        if (state_.halted) {
            // HALT (or a fault) resolves in EX; younger work never reaches WB.
            cycles_ = std::max<uint64_t>(cycles_, fetch_cycle + 2);
        }
        else {
            ++retired_;
            cycles_ = std::max<uint64_t>(cycles_, fetch_cycle + 3);
        }

        prev_dest_ = destinationRegister(instr);
    }

    void Cpu::run() {
        while (!state_.halted) {
            step();
        }
    }

} // namespace risc201
