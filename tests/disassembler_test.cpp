#include "assembler/disassembler.h"
#include "assembler/assembler.h"
#include <cassert>
#include <iostream>

using namespace risc201;

namespace {

void printErrors(const Assembler& asmblr) {
    for (const auto& err : asmblr.errors()) {
        std::cout << "  [error] line " << err.line_number << ": " << err.message << "\n";
    }
}

}

void testDecodeRType() {
    Instruction instr;
    instr.opcode = Opcode::ADD;
    instr.format = InstrFormat::RI_TYPE;
    instr.I = false;
    instr.rd = Register::R1;
    instr.rs1 = Register::R2;
    instr.rs2 = Register::R3;
    instr.imm = 0;

    Assembler asmblr;
    uint32_t word = asmblr.encodeInstruction(instr);

    Disassembler disasm;
    Instruction decoded;
    bool ok = disasm.decodeWord(word, decoded);
    assert(ok);
    assert(decoded.opcode == Opcode::ADD);
    assert(decoded.I == false);
    assert(decoded.rd == Register::R1);
    assert(decoded.rs1 == Register::R2);
    assert(decoded.rs2 == Register::R3);
    std::cout << "testDecodeRType passed\n";
}

void testDecodeIType() {
    Instruction instr;
    instr.opcode = Opcode::ADD;
    instr.format = InstrFormat::RI_TYPE;
    instr.I = true;
    instr.rd = Register::R1;
    instr.rs1 = Register::R2;
    instr.imm = 5;

    Assembler asmblr;
    uint32_t word = asmblr.encodeInstruction(instr);

    Disassembler disasm;
    Instruction decoded;
    bool ok = disasm.decodeWord(word, decoded);
    assert(ok);
    assert(decoded.I == true);
    assert(decoded.rd == Register::R1);
    assert(decoded.rs1 == Register::R2);
    assert(decoded.imm == 5);
    std::cout << "testDecodeIType passed\n";
}

void testDecodeITypeNegative() {
    Instruction instr;
    instr.opcode = Opcode::ADD;
    instr.format = InstrFormat::RI_TYPE;
    instr.I = true;
    instr.rd = Register::R1;
    instr.rs1 = Register::R2;
    instr.imm = -100;

    Assembler asmblr;
    uint32_t word = asmblr.encodeInstruction(instr);

    Disassembler disasm;
    Instruction decoded;
    bool ok = disasm.decodeWord(word, decoded);
    assert(ok);
    assert(decoded.imm == -100);
    std::cout << "testDecodeITypeNegative passed\n";
}

void testDecodeJType() {
    Instruction instr;
    instr.opcode = Opcode::JMP;
    instr.format = InstrFormat::J_TYPE;
    instr.imm = -4;

    Assembler asmblr;
    uint32_t word = asmblr.encodeInstruction(instr);

    Disassembler disasm;
    Instruction decoded;
    bool ok = disasm.decodeWord(word, decoded);
    assert(ok);
    assert(decoded.opcode == Opcode::JMP);
    assert(decoded.imm == -4);
    std::cout << "testDecodeJType passed\n";
}

void testDecodeJTypePositive() {
    Instruction instr;
    instr.opcode = Opcode::CALL;
    instr.format = InstrFormat::J_TYPE;
    instr.imm = 1000;

    Assembler asmblr;
    uint32_t word = asmblr.encodeInstruction(instr);

    Disassembler disasm;
    Instruction decoded;
    bool ok = disasm.decodeWord(word, decoded);
    assert(ok);
    assert(decoded.opcode == Opcode::CALL);
    assert(decoded.imm == 1000);
    std::cout << "testDecodeJTypePositive passed\n";
}

void testDecodeUnknownOpcode() {
    // 0x1E (31) is outside the currently-assigned opcode range, so this
    // should fail to decode.
    uint32_t word = (0x1Eu << 27);

    Disassembler disasm;
    Instruction decoded;
    bool ok = disasm.decodeWord(word, decoded);
    assert(!ok);
    std::cout << "testDecodeUnknownOpcode passed\n";
}

void testDecodeProgram() {
    Instruction i1;
    i1.opcode = Opcode::ADD;
    i1.format = InstrFormat::RI_TYPE;
    i1.I = false;
    i1.rd = Register::R1;
    i1.rs1 = Register::R2;
    i1.rs2 = Register::R3;

    Instruction i2;
    i2.opcode = Opcode::JMP;
    i2.format = InstrFormat::J_TYPE;
    i2.imm = -4;

    Assembler asmblr;
    std::vector<uint32_t> words = { asmblr.encodeInstruction(i1), asmblr.encodeInstruction(i2) };

    Disassembler disasm;
    auto program = disasm.decodeProgram(words);

    assert(program.size() == 2);
    assert(disasm.decodeErrors().empty());
    assert(program[0].opcode == Opcode::ADD);
    assert(program[1].opcode == Opcode::JMP);
    std::cout << "testDecodeProgram passed\n";
}

void testDecodeProgramWithUnknownWord() {
    Instruction i1;
    i1.opcode = Opcode::NOP;
    i1.format = InstrFormat::RI_TYPE;

    Assembler asmblr;
    uint32_t good_word = asmblr.encodeInstruction(i1);
    uint32_t bad_word = (0x1Eu << 27);

    std::vector<uint32_t> words = { good_word, bad_word, good_word };

    Disassembler disasm;
    auto program = disasm.decodeProgram(words);

    assert(program.size() == 3);
    assert(disasm.decodeErrors().size() == 1);
    assert(disasm.decodeErrors()[0].first == 1);
    assert(disasm.decodeErrors()[0].second == bad_word);
    assert(program[1].opcode == static_cast<Opcode>(0xFF));
    assert(disasm.formatInstruction(program[1]) == "; unknown opcode");
    std::cout << "testDecodeProgramWithUnknownWord passed\n";
}

void testFormatRType() {
    Instruction instr;
    instr.opcode = Opcode::ADD;
    instr.format = InstrFormat::RI_TYPE;
    instr.I = false;
    instr.rd = Register::R1;
    instr.rs1 = Register::R2;
    instr.rs2 = Register::R3;

    Disassembler disasm;
    std::string text = disasm.formatInstruction(instr);
    assert(text == "ADD R1, R2, R3");
    std::cout << "testFormatRType passed\n";
}

void testFormatIType() {
    Instruction instr;
    instr.opcode = Opcode::ADD;
    instr.format = InstrFormat::RI_TYPE;
    instr.I = true;
    instr.rd = Register::R1;
    instr.rs1 = Register::R2;
    instr.imm = 5;

    Disassembler disasm;
    std::string text = disasm.formatInstruction(instr);
    assert(text == "ADD R1, R2, 5");
    std::cout << "testFormatIType passed\n";
}

void testFormatJType() {
    Instruction instr;
    instr.opcode = Opcode::JMP;
    instr.format = InstrFormat::J_TYPE;
    instr.imm = -4;

    Disassembler disasm;
    std::string text = disasm.formatInstruction(instr);
    assert(text == "JMP -4");
    std::cout << "testFormatJType passed\n";
}

void testFormatMemOperand() {
    Instruction instr;
    instr.opcode = Opcode::LOAD;
    instr.format = InstrFormat::RI_TYPE;
    instr.I = true;
    instr.rd = Register::R1;
    instr.rs1 = Register::R2;
    instr.imm = 4;

    Disassembler disasm;
    std::string text = disasm.formatInstruction(instr);
    assert(text == "LOAD R1, 4[R2]");
    std::cout << "testFormatMemOperand passed\n";
}

void testFormatNoOperands() {
    Instruction instr;
    instr.opcode = Opcode::HALT;
    instr.format = InstrFormat::RI_TYPE;

    Disassembler disasm;
    std::string text = disasm.formatInstruction(instr);
    assert(text == "HALT");
    std::cout << "testFormatNoOperands passed\n";
}

void testDisassembleProgram() {
    Instruction i1;
    i1.opcode = Opcode::ADD;
    i1.format = InstrFormat::RI_TYPE;
    i1.I = false;
    i1.rd = Register::R1;
    i1.rs1 = Register::R2;
    i1.rs2 = Register::R3;

    Instruction i2;
    i2.opcode = Opcode::HALT;
    i2.format = InstrFormat::RI_TYPE;

    Disassembler disasm;
    std::vector<Instruction> program = { i1, i2 };
    std::vector<std::string> lines = disasm.disassemble(program);

    assert(lines.size() == 2);
    assert(lines[0] == "ADD R1, R2, R3");
    assert(lines[1] == "HALT");
    std::cout << "testDisassembleProgram passed\n";
}

void testFullPipelineRoundtrip() {
    Assembler asmblr;
    std::vector<std::string> src = {
        "ADD R1, R2, R3",
        "ADD R4, R5, 10",
        "LOAD R6, 8[R7]",
    };
    asmblr.assemblePass1(src);
    auto program = asmblr.assemblePass2();
    printErrors(asmblr);
    assert(asmblr.errors().empty());

    std::vector<uint32_t> words;
    for (const auto& instr : program) words.push_back(asmblr.encodeInstruction(instr));

    Disassembler disasm;
    std::vector<Instruction> decoded = disasm.decodeProgram(words);

    assert(disasm.decodeErrors().empty());
    assert(decoded.size() == program.size());

    for (size_t i = 0; i < program.size(); ++i) {
        assert(decoded[i].opcode == program[i].opcode);
        assert(decoded[i].I == program[i].I);
        assert(decoded[i].rd == program[i].rd);
        assert(decoded[i].rs1 == program[i].rs1);
        assert(decoded[i].rs2 == program[i].rs2);
        assert(decoded[i].imm == program[i].imm);
    }
    std::cout << "testFullPipelineRoundtrip passed\n";
}

void testFileRoundtripBinary() {
    Assembler asmblr;
    std::vector<std::string> src = { "ADD R1, R2, R3", "HALT" };
    asmblr.assemblePass1(src);
    auto program = asmblr.assemblePass2();
    assert(asmblr.errors().empty());

    std::vector<uint32_t> words;
    for (const auto& instr : program) words.push_back(asmblr.encodeInstruction(instr));

    bool write_ok = asmblr.writeBinaryFile("/tmp/risc201_disasm_test.bin", words);
    assert(write_ok);

    Disassembler disasm;
    std::vector<uint32_t> read_back;
    bool read_ok = disasm.readBinaryFile("/tmp/risc201_disasm_test.bin", read_back);
    assert(read_ok);
    assert(read_back == words);
    std::cout << "testFileRoundtripBinary passed\n";
}

void testFileRoundtripHex() {
    Assembler asmblr;
    std::vector<std::string> src = { "ADD R1, R2, R3", "HALT" };
    asmblr.assemblePass1(src);
    auto program = asmblr.assemblePass2();
    assert(asmblr.errors().empty());

    std::vector<uint32_t> words;
    for (const auto& instr : program) words.push_back(asmblr.encodeInstruction(instr));

    bool write_ok = asmblr.writeHexFile("/tmp/risc201_disasm_test.hex", words);
    assert(write_ok);

    Disassembler disasm;
    std::vector<uint32_t> read_back;
    bool read_ok = disasm.readHexFile("/tmp/risc201_disasm_test.hex", read_back);
    assert(read_ok);
    assert(read_back == words);
    std::cout << "testFileRoundtripHex passed\n";
}

int main() {
    testDecodeRType();
    testDecodeIType();
    testDecodeITypeNegative();
    testDecodeJType();
    testDecodeJTypePositive();
    testDecodeUnknownOpcode();
    testDecodeProgram();
    testDecodeProgramWithUnknownWord();

    testFormatRType();
    testFormatIType();
    testFormatJType();
    testFormatMemOperand();
    testFormatNoOperands();
    testDisassembleProgram();

    testFullPipelineRoundtrip();
    testFileRoundtripBinary();
    testFileRoundtripHex();

    std::cout << "All disassembler tests passed.\n";
    return 0;
}