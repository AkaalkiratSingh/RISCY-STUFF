#include "assembler/disassembler.h"
#include "assembler/assembler.h"
#include "utils/utils.h"
#include <algorithm>
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

std::vector<Instruction> assembleProgram(const std::vector<std::string>& source,
                                         Assembler& asmblr) {
    asmblr.assemblePass1(source);
    return asmblr.assemblePass2();
}

void testFullPipelineRoundtrip() {
    Assembler asmblr;
    std::vector<std::string> src = {
        ".main:",
        "ADD R1, R2, R3",
        "ADD R4, R5, 10",
        "LOAD R6, 8[R7]",
    };
    auto program = assembleProgram(src, asmblr);
    printErrors(asmblr);
    assert(asmblr.errors().empty());

    std::vector<uint32_t> words;
    for (const auto& instr : program) words.push_back(asmblr.encodeInstruction(instr));

    Disassembler disasm;
    std::vector<Instruction> decoded = disasm.decodeProgram(words);

    assert(disasm.decodeErrors().empty());
    assert(decoded.size() == program.size());
    assert(program.size() == 4);                 // JMP .main + 3 user instructions
    assert(decoded[0].opcode == Opcode::JMP);    // auto-inserted entry jump survives the roundtrip

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
    std::vector<std::string> src = { ".main:", "ADD R1, R2, R3", "HALT" };
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
    std::vector<std::string> src = { ".main:", "ADD R1, R2, R3", "HALT" };
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

void testDisassembleWithLabels() {
    Instruction i0; i0.opcode = Opcode::ADD; i0.format = InstrFormat::RI_TYPE;
    i0.I = false; i0.rd = Register::R1; i0.rs1 = Register::R2; i0.rs2 = Register::R3;

    Instruction i1; i1.opcode = Opcode::JMP; i1.format = InstrFormat::J_TYPE;
    i1.imm = -8;  // JMP is at address 4, so target = 4 + 4 - 8 = 0

    Disassembler disasm;
    std::vector<Instruction> program = { i0, i1 };
    std::vector<std::string> lines = disasm.disassembleWithLabels(program);

    // Expect: a label line before instruction 0, JMP referencing that label.
    assert(lines.size() == 3);  // label line + 2 instructions
    assert(lines[0].back() == ':');
    assert(lines[1] == "ADD R1, R2, R3");
    assert(lines[2].substr(0, 4) == "JMP ");
    assert(lines[2].find(lines[0].substr(0, lines[0].size() - 1)) != std::string::npos);
    std::cout << "testDisassembleWithLabels passed\n";
}

// ---------- EMPTY operand slots (MOV / NOT / CMP) and BGT ----------

namespace {

Instruction makeRI(Opcode op, bool i_bit, Register rd, Register rs1, Register rs2, int32_t imm) {
    Instruction instr;
    instr.opcode = op;
    instr.format = InstrFormat::RI_TYPE;
    instr.I = i_bit;
    instr.rd = rd;
    instr.rs1 = rs1;
    instr.rs2 = rs2;
    instr.imm = imm;
    return instr;
}

}  // namespace

void testFormatEmptyOperandForms() {
    Disassembler disasm;
    const Register R0 = Register::R0;

    assert(disasm.formatInstruction(makeRI(Opcode::MOV, false, Register::R1, R0, Register::R2, 0)) == "MOV R1, R2");
    assert(disasm.formatInstruction(makeRI(Opcode::MOV, true,  Register::R3, R0, R0, 5)) == "MOV R3, 5");
    assert(disasm.formatInstruction(makeRI(Opcode::NOT, false, Register::R4, R0, Register::R5, 0)) == "NOT R4, R5");
    assert(disasm.formatInstruction(makeRI(Opcode::NOT, true,  Register::R6, R0, R0, 255)) == "NOT R6, 255");

    // CMP's empty slot is rd: the printed operands are rs1 and rs2/imm.
    assert(disasm.formatInstruction(makeRI(Opcode::CMP, false, R0, Register::R7, Register::R8, 0)) == "CMP R7, R8");
    assert(disasm.formatInstruction(makeRI(Opcode::CMP, true,  R0, Register::R9, R0, -3)) == "CMP R9, -3");
    std::cout << "testFormatEmptyOperandForms passed\n";
}

void testFormatEmptySlotIgnoresStrayBits() {
    // Whatever sits in the unused slot is not printed.
    Disassembler disasm;
    assert(disasm.formatInstruction(makeRI(Opcode::MOV, false, Register::R1, Register::R9, Register::R2, 0)) == "MOV R1, R2");
    assert(disasm.formatInstruction(makeRI(Opcode::CMP, false, Register::R5, Register::R7, Register::R8, 0)) == "CMP R7, R8");
    std::cout << "testFormatEmptySlotIgnoresStrayBits passed\n";
}

void testDecodeEmptyOperandWords() {
    Assembler asmblr;
    Disassembler disasm;

    uint32_t mov_word = (static_cast<uint32_t>(Opcode::MOV) << 27) | (1u << 22) | (2u << 14);
    Instruction mov;
    assert(disasm.decodeWord(mov_word, mov));
    assert(mov.opcode == Opcode::MOV);
    assert(mov.I == false);
    assert(mov.rd == Register::R1);
    assert(mov.rs1 == Register::R0);
    assert(mov.rs2 == Register::R2);

    uint32_t cmp_word = (static_cast<uint32_t>(Opcode::CMP) << 27) | (7u << 18) | (8u << 14);
    Instruction cmp;
    assert(disasm.decodeWord(cmp_word, cmp));
    assert(cmp.opcode == Opcode::CMP);
    assert(cmp.rd == Register::R0);
    assert(cmp.rs1 == Register::R7);
    assert(cmp.rs2 == Register::R8);
    std::cout << "testDecodeEmptyOperandWords passed\n";
}

void testEmptyOperandSourceRoundtrip() {
    const std::vector<std::string> body = {
        "MOV R1, R2", "MOV R3, 5", "MOV R4, -7",
        "NOT R4, R5", "NOT R6, 255",
        "CMP R7, R8", "CMP R9, -3",
        "ADD R1, R2, R3",
        "HALT",
    };
    std::vector<std::string> src = { ".main:" };
    src.insert(src.end(), body.begin(), body.end());

    Assembler asmblr;
    auto program = assembleProgram(src, asmblr);
    printErrors(asmblr);
    assert(asmblr.errors().empty());

    std::vector<uint32_t> words;
    for (const auto& instr : program) words.push_back(asmblr.encodeInstruction(instr));

    // binary -> instructions -> text: the output is exactly the source we started from.
    Disassembler disasm;
    auto decoded = disasm.decodeProgram(words);
    assert(disasm.decodeErrors().empty());
    auto lines = disasm.disassemble(decoded);
    assert(lines.size() == body.size() + 1);        // + the JMP .main prelude
    for (size_t i = 0; i < body.size(); ++i) assert(lines[i + 1] == body[i]);

    // text -> binary: reassembling the disassembly gives identical words.
    std::vector<std::string> again = { ".main:" };
    again.insert(again.end(), lines.begin() + 1, lines.end());
    Assembler second;
    auto program2 = assembleProgram(again, second);
    assert(second.errors().empty());
    std::vector<uint32_t> words2;
    for (const auto& instr : program2) words2.push_back(second.encodeInstruction(instr));
    assert(words2 == words);
    std::cout << "testEmptyOperandSourceRoundtrip passed\n";
}

void testDisassembleBgtWithLabels() {
    Assembler asmblr;
    std::vector<std::string> src = { ".main:", "CMP R1, R2", "BGT end", "ADD R1, R1, 1", "end: HALT" };
    auto program = assembleProgram(src, asmblr);
    assert(asmblr.errors().empty());

    Disassembler disasm;
    auto lines = disasm.disassembleWithLabels(program);

    size_t bgt = lines.size();
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].rfind("BGT ", 0) == 0) bgt = i;
    }
    assert(bgt < lines.size());

    // BGT names a generated label, and that label line exists right before HALT.
    std::string label = lines[bgt].substr(4);
    assert(label.size() > 1 && label[0] == 'L');
    assert(lines.back() == "HALT");
    assert(lines[lines.size() - 2] == label + ":");
    std::cout << "testDisassembleBgtWithLabels passed\n";
}

// ---------- jump offsets print as numbers and reassemble ----------

void testDisassembledJumpsReassemble() {
    const std::vector<std::string> sources[] = {
        { ".main:", "ADD R1, R0, 1", "BEQ skip", "ADD R1, R1, 1", "skip: CALL fn", "HALT", "fn: RET" },
        { ".main:", "top: ADD R1, R1, 1", "BGT top", "JMP end", "NOP", "end: HALT" },
    };
    for (const auto& src : sources) {
        Assembler asmblr;
        auto program = assembleProgram(src, asmblr);
        assert(asmblr.errors().empty());
        std::vector<uint32_t> words;
        for (const auto& instr : program) words.push_back(asmblr.encodeInstruction(instr));

        Disassembler disasm;
        auto decoded = disasm.decodeProgram(words);
        auto lines = disasm.disassemble(decoded);          // jump targets are raw numbers

        // Reassemble the plain disassembly (without the JMP .main prelude line).
        std::vector<std::string> again = { ".main:" };
        again.insert(again.end(), lines.begin() + 1, lines.end());
        Assembler second;
        auto program2 = assembleProgram(again, second);
        assert(second.errors().empty());
        std::vector<uint32_t> words2;
        for (const auto& instr : program2) words2.push_back(second.encodeInstruction(instr));
        assert(words2 == words);
    }
    std::cout << "testDisassembledJumpsReassemble passed\n";
}

void testDisassembledJumpOffsetText() {
    Assembler asmblr;
    auto program = assembleProgram({ ".main:", "top: NOP", "BGT top", "BEQ next", "next: HALT" }, asmblr);
    assert(asmblr.errors().empty());
    Disassembler disasm;
    auto lines = disasm.disassemble(program);
    // lines: JMP 0 | NOP | BGT -8 | BEQ 0 | HALT
    assert(lines[2] == "BGT -8");
    assert(lines[3] == "BEQ 0");
    std::cout << "testDisassembledJumpOffsetText passed\n";
}

// ---------- one formatter: every opcode prints exactly what the assembler accepts ----------

namespace {

// Canonical source text for one op-table entry: R1, R2, ... for register slots.
std::string canonicalText(const std::string& mnemonic, const OpInfo& info) {
    std::string text = mnemonic;
    int next_reg = 1;
    bool first = true;
    for (OperandKind kind : info.operand_kinds) {
        std::string operand;
        switch (kind) {
            case OperandKind::REG:
            case OperandKind::REG_OR_IMM: operand = "R" + std::to_string(next_reg++); break;
            case OperandKind::MEM:        operand = "4[R" + std::to_string(next_reg++) + "]"; break;
            case OperandKind::LABEL:      operand = "8"; break;
            case OperandKind::IMM:        operand = "5"; break;
            case OperandKind::EMPTY:      continue;
        }
        text += first ? " " : ", ";
        text += operand;
        first = false;
    }
    return text;
}

}  // namespace

void testFormattersAgreeWithOpTable() {
    Disassembler disasm;
    for (const auto& [mnemonic, info] : opTable()) {
        const std::string text = canonicalText(mnemonic, info);

        Assembler asmblr;
        auto program = assembleProgram({ ".main:", text }, asmblr);
        printErrors(asmblr);
        assert(asmblr.errors().empty());
        assert(program.size() == 2);                       // JMP .main + the instruction

        // Both formatters must print the same text the assembler read.
        if (disasm.formatInstruction(program[1]) != text) {
            std::cout << "  mismatch for '" << text << "': disassembler printed '"
                      << disasm.formatInstruction(program[1]) << "'\n";
        }
        assert(disasm.formatInstruction(program[1]) == text);
        assert(risc201::formatInstruction(program[1]) == text);
    }
    std::cout << "testFormattersAgreeWithOpTable passed\n";
}

void testFcmpFormat() {
    Instruction instr = makeRI(Opcode::FCMP, false, Register::R0, Register::R1, Register::R2, 0);
    Disassembler disasm;
    assert(disasm.formatInstruction(instr) == "FCMP R1, R2");
    assert(risc201::formatInstruction(instr) == "FCMP R1, R2");

    // Stray bits in the empty slot or the I bit do not change the written form.
    instr = makeRI(Opcode::FCMP, true, Register::R9, Register::R3, Register::R4, 77);
    assert(risc201::formatInstruction(instr) == "FCMP R3, R4");
    std::cout << "testFcmpFormat passed\n";
}

// ---------- disassembleSource restores '.main:' and reassembles identically ----------

namespace {

std::vector<uint32_t> assembleToWords(const std::vector<std::string>& src, Assembler& asmblr) {
    auto program = assembleProgram(src, asmblr);
    printErrors(asmblr);
    assert(asmblr.errors().empty());
    std::vector<uint32_t> words;
    for (const auto& instr : program) words.push_back(asmblr.encodeInstruction(instr));
    for (uint32_t word : asmblr.literalSection()) words.push_back(word);
    return words;
}

std::vector<std::string> sourceFromWords(const std::vector<uint32_t>& words, bool use_labels) {
    Disassembler disasm;
    auto decoded = disasm.decodeProgram(words);
    assert(disasm.decodeErrors().empty());
    auto lines = disasm.disassembleSource(decoded, use_labels);
    for (const auto& lit : disasm.formatLiterals()) lines.push_back(lit);
    return lines;
}

size_t countLines(const std::vector<std::string>& lines, const std::string& wanted) {
    return static_cast<size_t>(std::count(lines.begin(), lines.end(), wanted));
}

// source -> words -> source -> words must give the same words, in both modes.
void expectSourceRoundtrip(const std::vector<std::string>& src) {
    Assembler first;
    const auto words = assembleToWords(src, first);
    for (bool use_labels : { false, true }) {
        const auto lines = sourceFromWords(words, use_labels);
        assert(countLines(lines, ".main:") == 1);
        Assembler second;
        const auto words2 = assembleToWords(lines, second);
        if (words2 != words) {
            for (const auto& line : lines) std::cout << "    | " << line << "\n";
        }
        assert(words2 == words);
    }
}

}  // namespace

void testSourceRestoresMain() {
    // .main first: no JMP line, and .main: is the first line.
    {
        Assembler a;
        auto words = assembleToWords({ ".main:", "ADD R1, R2, R3", "HALT" }, a);
        auto lines = sourceFromWords(words, false);
        assert(lines.size() == 3);
        assert(lines[0] == ".main:" && lines[1] == "ADD R1, R2, R3" && lines[2] == "HALT");
    }
    // Code before .main: the label lands in the middle, on the instruction the entry JMP targets.
    {
        Assembler a;
        auto words = assembleToWords({ "fn: ADD R1, R1, 1", "RET", ".main:", "CALL fn", "HALT" }, a);
        auto lines = sourceFromWords(words, false);
        assert(lines.size() == 5);
        assert(lines[0] == "ADD R1, R1, 1" && lines[1] == "RET");
        assert(lines[2] == ".main:");
        assert(lines[3] == "CALL -12");
    }
    std::cout << "testSourceRestoresMain passed\n";
}

void testSourceRoundtripIsBitIdentical() {
    expectSourceRoundtrip({ ".main:", "ADD R1, R2, R3", "HALT" });
    expectSourceRoundtrip({ "fn: ADD R1, R1, 1", "RET", ".main:", "CALL fn", "HALT" });
    expectSourceRoundtrip({ ".main:", "ADD R1, R1, 1", "CMP R1, 5", "BGT .main", "HALT" });
    expectSourceRoundtrip({ ".main:", "BEQ end", "HALT", "end:" });            // jump past the last instruction
    expectSourceRoundtrip({ ".main:", "LOAD R1, pi", "LOAD R2, half", "FCMP R1, R2", "HALT",
                            ".literals:", "pi: 3.14f", "half: 0.5f" });
    std::cout << "testSourceRoundtripIsBitIdentical passed\n";
}

void testSourceLabelsUseMain() {
    Assembler a;
    auto words = assembleToWords({ ".main:", "ADD R1, R1, 1", "CMP R1, 5", "BGT .main", "HALT" }, a);
    auto lines = sourceFromWords(words, true);
    assert(countLines(lines, ".main:") == 1);
    assert(countLines(lines, "BGT .main") == 1);
    for (const auto& line : lines) assert(!(line.size() > 1 && line[0] == 'L' && line.back() == ':'));
    std::cout << "testSourceLabelsUseMain passed\n";
}

void testSourceJumpPastEndStaysRaw() {
    Assembler a;
    auto words = assembleToWords({ ".main:", "BEQ end", "HALT", "end:" }, a);
    auto lines = sourceFromWords(words, true);
    assert(countLines(lines, "BEQ 4") == 1);      // no generated label for an address with no instruction
    std::cout << "testSourceJumpPastEndStaysRaw passed\n";
}

void testSourceWithoutEntryJump() {
    Disassembler disasm;

    // Hand-made program with no entry JMP: .main goes on the first instruction.
    Instruction add = makeRI(Opcode::ADD, false, Register::R1, Register::R2, Register::R3, 0);
    Instruction halt = makeRI(Opcode::HALT, false, Register::R0, Register::R0, Register::R0, 0);
    auto lines = disasm.disassembleSource({ add, halt }, false);
    assert(lines.size() == 4);
    assert(lines[0][0] == ';');
    assert(lines[1] == ".main:" && lines[2] == "ADD R1, R2, R3" && lines[3] == "HALT");
    Assembler a;
    auto program = assembleProgram(lines, a);
    assert(a.errors().empty() && program.size() == 3);     // the assembler adds its own entry JMP

    // A JMP at word 0 that does not point at an instruction is kept as an ordinary instruction.
    Instruction far_jump; far_jump.opcode = Opcode::JMP; far_jump.format = InstrFormat::J_TYPE; far_jump.imm = 400;
    lines = disasm.disassembleSource({ far_jump, halt }, false);
    assert(lines.size() == 4 && lines[1] == ".main:" && lines[2] == "JMP 400");

    // Nothing to print still gives source that assembles.
    lines = disasm.disassembleSource({}, false);
    assert(lines.size() == 1 && lines[0] == ".main:");
    std::cout << "testSourceWithoutEntryJump passed\n";
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

    testDisassembleWithLabels();

    testFormatEmptyOperandForms();
    testFormatEmptySlotIgnoresStrayBits();
    testDecodeEmptyOperandWords();
    testEmptyOperandSourceRoundtrip();
    testDisassembleBgtWithLabels();

    testDisassembledJumpsReassemble();
    testDisassembledJumpOffsetText();

    testFormattersAgreeWithOpTable();
    testFcmpFormat();
    testSourceRestoresMain();
    testSourceRoundtripIsBitIdentical();
    testSourceLabelsUseMain();
    testSourceJumpPastEndStaysRaw();
    testSourceWithoutEntryJump();

    std::cout << "All disassembler tests passed.\n";
    return 0;
}