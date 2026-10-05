#include "assembler/assembler.h"
#include <cassert>
#include <iostream>

using namespace risc201;

void testEncodeRType() {
    risc201::Assembler asmblr;
    Instruction instr;
    instr.opcode = Opcode::ADD;
    instr.format = InstrFormat::RI_TYPE;
    instr.I = false;
    instr.rd = Register::R1;
    instr.rs1 = Register::R2;
    instr.rs2 = Register::R3;
    instr.imm = 0;

    uint32_t word = asmblr.encodeInstruction(instr);

    assert(((word >> 27) & 0x1F) == static_cast<uint32_t>(Opcode::ADD));
    assert(((word >> 26) & 0x1) == 0);
    assert(((word >> 22) & 0xF) == 1);  // rd
    assert(((word >> 18) & 0xF) == 2);  // rs1
    assert(((word >> 14) & 0xF) == 3);  // rs2
    std::cout << "testEncodeRType passed\n";
}

void testEncodeIType() {
    risc201::Assembler asmblr;
    Instruction instr;
    instr.opcode = Opcode::ADD;
    instr.format = InstrFormat::RI_TYPE;
    instr.I = true;
    instr.rd = Register::R1;
    instr.rs1 = Register::R2;
    instr.rs2 = Register::R0;
    instr.imm = 5;

    uint32_t word = asmblr.encodeInstruction(instr);

    assert(((word >> 26) & 0x1) == 1);
    assert(((word >> 22) & 0xF) == 1);
    assert(((word >> 18) & 0xF) == 2);
    assert((word & 0x3FFFF) == 5);
    std::cout << "testEncodeIType passed\n";
}

void testEncodeJType() {
    risc201::Assembler asmblr;
    Instruction instr;
    instr.opcode = Opcode::JMP;
    instr.format = InstrFormat::J_TYPE;
    instr.imm = -4;

    uint32_t word = asmblr.encodeInstruction(instr);

    assert(((word >> 27) & 0x1F) == static_cast<uint32_t>(Opcode::JMP));
    int32_t offset = static_cast<int32_t>(word & 0x7FFFFFFu);
    // sign-extend from 27 bits for comparison
    if (offset & 0x4000000) offset |= ~0x7FFFFFF;
    assert(offset == -4);
    std::cout << "testEncodeJType passed\n";
}

int main() {
    testEncodeRType();
    testEncodeIType();
    testEncodeJType();
    std::cout << "All encoder tests passed.\n";
    return 0;
}