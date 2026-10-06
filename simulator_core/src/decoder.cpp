#include "simulator_core/decoder.h"

namespace risc201 {

    namespace {

        // Only these transfer the program counter through a whole-word offset;
        // every other opcode uses the RI layout.
        bool isJType(Opcode op) {
            return op == Opcode::JMP || op == Opcode::BEQ || op == Opcode::CALL;
        }

        int32_t signExtend(uint32_t value, int bits) {
            const uint32_t sign = 1u << (bits - 1);
            if (value & sign) value |= ~((1u << bits) - 1u);
            return static_cast<int32_t>(value);
        }

    } // namespace

    Instruction decodeInstruction(uint32_t word) {
        Instruction instr{};
        instr.opcode = static_cast<Opcode>((word >> 27) & 0x1Fu);
        instr.rd = Register::R0;
        instr.rs1 = Register::R0;
        instr.rs2 = Register::R0;
        instr.imm = 0;
        instr.I = false;

        if (isJType(instr.opcode)) {
            instr.format = InstrFormat::J_TYPE;
            instr.imm = signExtend(word & 0x07FFFFFFu, 27);
            return instr;
        }

        instr.format = InstrFormat::RI_TYPE;
        instr.I = ((word >> 26) & 0x1u) != 0;
        instr.rd = static_cast<Register>((word >> 22) & 0xFu);
        instr.rs1 = static_cast<Register>((word >> 18) & 0xFu);

        if (instr.I) {
            instr.imm = signExtend(word & 0x03FFFFu, 18);
        }
        else {
            instr.rs2 = static_cast<Register>((word >> 14) & 0xFu);
        }

        return instr;
    }

} // namespace risc201
