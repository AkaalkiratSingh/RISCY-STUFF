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

// ---------- EMPTY operand slots (MOV / NOT / CMP) and BGT ----------
// The slot that is not written in the source must be encoded as 0000.

namespace {

Instruction assembleOne(Assembler& asmblr, const std::string& line) {
    asmblr.assemblePass1({ ".main:", line });
    auto program = asmblr.assemblePass2();
    assert(asmblr.errors().empty());
    assert(program.size() == 2);        // JMP .main + the instruction under test
    return program[1];
}

}  // namespace

void testEncodeMovRegisterLeavesRs1Zero() {
    Assembler asmblr;
    uint32_t word = asmblr.encodeInstruction(assembleOne(asmblr, "MOV R1, R2"));

    assert(((word >> 27) & 0x1F) == static_cast<uint32_t>(Opcode::MOV));
    assert(((word >> 26) & 0x1) == 0);
    assert(((word >> 22) & 0xF) == 1);      // rd
    assert(((word >> 18) & 0xF) == 0);      // the empty slot
    assert(((word >> 14) & 0xF) == 2);      // rs2
    assert(word == ((static_cast<uint32_t>(Opcode::MOV) << 27) | (1u << 22) | (2u << 14)));
    std::cout << "testEncodeMovRegisterLeavesRs1Zero passed\n";
}

void testEncodeMovImmediate() {
    Assembler asmblr;
    uint32_t word = asmblr.encodeInstruction(assembleOne(asmblr, "MOV R3, 5"));

    assert(((word >> 26) & 0x1) == 1);
    assert(((word >> 22) & 0xF) == 3);
    assert(((word >> 18) & 0xF) == 0);      // the empty slot
    assert((word & 0x3FFFF) == 5);
    std::cout << "testEncodeMovImmediate passed\n";
}

void testEncodeNot() {
    Assembler asmblr;
    uint32_t word = asmblr.encodeInstruction(assembleOne(asmblr, "NOT R4, R5"));

    assert(((word >> 27) & 0x1F) == static_cast<uint32_t>(Opcode::NOT));
    assert(((word >> 22) & 0xF) == 4);
    assert(((word >> 18) & 0xF) == 0);
    assert(((word >> 14) & 0xF) == 5);
    std::cout << "testEncodeNot passed\n";
}

void testEncodeCmpLeavesRdZero() {
    Assembler asmblr;

    uint32_t reg_word = asmblr.encodeInstruction(assembleOne(asmblr, "CMP R7, R8"));
    assert((((reg_word >> 27) & 0x1F)) == static_cast<uint32_t>(Opcode::CMP));
    assert(((reg_word >> 26) & 0x1) == 0);
    assert(((reg_word >> 22) & 0xF) == 0);  // the empty slot is rd
    assert(((reg_word >> 18) & 0xF) == 7);  // rs1
    assert(((reg_word >> 14) & 0xF) == 8);  // rs2

    uint32_t imm_word = asmblr.encodeInstruction(assembleOne(asmblr, "CMP R9, -3"));
    assert(((imm_word >> 26) & 0x1) == 1);
    assert(((imm_word >> 22) & 0xF) == 0);
    assert(((imm_word >> 18) & 0xF) == 9);
    assert((imm_word & 0x3FFFF) == 0x3FFFD);    // -3 in 18 bits
    std::cout << "testEncodeCmpLeavesRdZero passed\n";
}

void testEncodeBgt() {
    Instruction instr;
    instr.opcode = Opcode::BGT;
    instr.format = InstrFormat::J_TYPE;
    instr.imm = 8;

    Assembler asmblr;
    uint32_t word = asmblr.encodeInstruction(instr);

    assert(((word >> 27) & 0x1F) == static_cast<uint32_t>(Opcode::BGT));
    assert((word & 0x7FFFFFFu) == 8);
    std::cout << "testEncodeBgt passed\n";
}

void testNewEncodingsAreNeverTheLiteralsMarker() {
    Assembler asmblr;
    for (const char* line : { "MOV R15, R15", "MOV R15, -1", "NOT R15, R15", "CMP R15, R15", "CMP R15, -1" }) {
        uint32_t word = asmblr.encodeInstruction(assembleOne(asmblr, line));
        assert(word != kLiteralsMarker);
    }
    std::cout << "testNewEncodingsAreNeverTheLiteralsMarker passed\n";
}

int main() {
    testEncodeRType();
    testEncodeIType();
    testEncodeJType();
    testEncodeMovRegisterLeavesRs1Zero();
    testEncodeMovImmediate();
    testEncodeNot();
    testEncodeCmpLeavesRdZero();
    testEncodeBgt();
    testNewEncodingsAreNeverTheLiteralsMarker();
    std::cout << "All encoder tests passed.\n";
    return 0;
}