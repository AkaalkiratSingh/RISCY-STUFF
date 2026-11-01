#include "assembler/assembler.h"
#include "assembler/disassembler.h"
#include <cassert>
#include <iostream>

using namespace risc201;

// Known IEEE-754 single-precision bit patterns used below:
//   3.14f -> 0x4048F5C3   0.5f -> 0x3F000000   1.0f -> 0x3F800000
//  -2.5f  -> 0xC0200000   0.0f -> 0x00000000
// Literals always come after the code, in a '.literals:' section.

namespace {

// Runs both passes on src and returns the instruction vector.
std::vector<Instruction> assemble(Assembler& a, const std::vector<std::string>& src) {
    a.assemblePass1(src);
    return a.assemblePass2();
}

void printErrors(const Assembler& a) {
    for (const auto& e : a.errors()) {
        std::cout << "  [error] line " << e.line_number << ": " << e.message << "\n";
    }
}

// Assembles src and expects exactly one error, on expected_line.
void expectSingleError(const std::vector<std::string>& src, int expected_line) {
    Assembler a;
    auto program = assemble(a, src);
    printErrors(a);
    assert(a.errors().size() == 1);
    assert(a.errors()[0].line_number == expected_line);
    assert(program.empty());
}

}  // namespace

// ======================= Assembler side =======================

void testLiteralsParsedToFloatBits() {
    Assembler a;
    std::vector<std::string> src = {
        ".main:",
        "HALT",
        ".literals:",
        "pi: 3.14f",
        "half: 0.5f",
    };
    auto program = assemble(a, src);
    printErrors(a);
    assert(a.errors().empty());
    assert(a.literalWords().size() == 2);
    assert(a.literalWords()[0] == 0x4048F5C3u);
    assert(a.literalWords()[1] == 0x3F000000u);
    std::cout << "testLiteralsParsedToFloatBits passed\n";
}

void testLiteralsDoNotEmitInstructions() {
    Assembler a;
    std::vector<std::string> src = {
        ".main:",
        "HALT",
        ".literals:",
        "pi: 3.14f",
        "half: 0.5f",
    };
    auto program = assemble(a, src);
    assert(a.errors().empty());
    assert(program.size() == 2);                       // JMP .main + HALT, no literal words
    assert(program[1].opcode == Opcode::HALT);
    std::cout << "testLiteralsDoNotEmitInstructions passed\n";
}

void testNegativeAndZeroLiterals() {
    Assembler a;
    std::vector<std::string> src = {
        ".main:",
        "HALT",
        ".literals:",
        "neg: -2.5f",
        "zero: 0.0f",
        "one: 1.0f",
        "upper: 1.0F",      // capital F suffix is accepted too
    };
    assemble(a, src);
    printErrors(a);
    assert(a.errors().empty());
    assert(a.literalWords().size() == 4);
    assert(a.literalWords()[0] == 0xC0200000u);
    assert(a.literalWords()[1] == 0x00000000u);
    assert(a.literalWords()[2] == 0x3F800000u);
    assert(a.literalWords()[3] == 0x3F800000u);
    std::cout << "testNegativeAndZeroLiterals passed\n";
}

void testLiteralAddresses() {
    Assembler a;
    std::vector<std::string> src = {
        ".main:",           // address 4
        "HALT",             // address 4
        ".literals:",       // address 8 (header itself takes no space)
        "pi: 3.14f",        // address 8
        "half: 0.5f",       // address 12
    };
    assemble(a, src);
    assert(a.errors().empty());
    assert(a.symbolTable().at(".literals") == 8);
    assert(a.symbolTable().at("pi") == 8);
    assert(a.symbolTable().at("half") == 12);
    std::cout << "testLiteralAddresses passed\n";
}

void testLiteralSectionLayout() {
    Assembler a;
    std::vector<std::string> src = {
        ".main:",
        "HALT",
        ".literals:",
        "pi: 3.14f",
        "half: 0.5f",
    };
    assemble(a, src);
    std::vector<uint32_t> section = a.literalSection();
    assert(section.size() == 3);
    assert(section[0] == kLiteralsMarker);             // marker comes first
    assert(section[1] == 0x4048F5C3u);
    assert(section[2] == 0x3F000000u);
    std::cout << "testLiteralSectionLayout passed\n";
}

void testNoLiteralsMeansNoSection() {
    Assembler a;
    std::vector<std::string> src = { ".main:", "HALT" };
    assemble(a, src);
    assert(a.errors().empty());
    assert(a.literalWords().empty());
    assert(a.literalSection().empty());                // no marker either
    std::cout << "testNoLiteralsMeansNoSection passed\n";
}

void testEmptyLiteralsHeader() {
    Assembler a;
    std::vector<std::string> src = { ".main:", "HALT", ".literals:" };
    assemble(a, src);
    assert(a.errors().empty());
    assert(a.literalWords().empty());
    assert(a.literalSection().empty());                // header alone adds nothing
    std::cout << "testEmptyLiteralsHeader passed\n";
}

// ---------- Literal errors (each reported on its own source line) ----------

void testLiteralMissingName() {
    expectSingleError({ ".main:", "HALT", ".literals:", "3.14f" }, 4);
    std::cout << "testLiteralMissingName passed\n";
}

void testLiteralMissingValue() {
    expectSingleError({ ".main:", "HALT", ".literals:", "pi:" }, 4);
    std::cout << "testLiteralMissingValue passed\n";
}

void testLiteralMissingFSuffix() {
    expectSingleError({ ".main:", "HALT", ".literals:", "pi: 3.14" }, 4);
    std::cout << "testLiteralMissingFSuffix passed\n";
}

void testLiteralGarbageValue() {
    expectSingleError({ ".main:", "HALT", ".literals:", "pi: abcf" }, 4);
    std::cout << "testLiteralGarbageValue passed\n";
}

void testLiteralTrailingJunk() {
    expectSingleError({ ".main:", "HALT", ".literals:", "pi: 3.14xf" }, 4);
    std::cout << "testLiteralTrailingJunk passed\n";
}

void testLiteralOverflowRejected() {
    // 1e50 does not fit in a float, so it must not silently become infinity.
    expectSingleError({ ".main:", "HALT", ".literals:", "big: 1e50f" }, 4);
    std::cout << "testLiteralOverflowRejected passed\n";
}

void testLiteralInfinityRejected() {
    expectSingleError({ ".main:", "HALT", ".literals:", "inf: inff" }, 4);
    std::cout << "testLiteralInfinityRejected passed\n";
}

void testDuplicateLiteralName() {
    expectSingleError({ ".main:", "HALT", ".literals:", "pi: 3.14f", "pi: 3.15f" }, 5);
    std::cout << "testDuplicateLiteralName passed\n";
}

void testTextAfterLiteralsHeader() {
    expectSingleError({ ".main:", "HALT", ".literals: 1.0f" }, 3);
    std::cout << "testTextAfterLiteralsHeader passed\n";
}

void testInstructionAfterLiteralsIsError() {
    // Literals always come last: once '.literals:' starts, every following
    // line is parsed as a literal, so an instruction there is rejected.
    expectSingleError({ ".main:", ".literals:", "pi: 3.14f", "ADD R1, R2, R3" }, 4);
    std::cout << "testInstructionAfterLiteralsIsError passed\n";
}

void testMultipleLiteralErrorsAllReported() {
    Assembler a;
    std::vector<std::string> src = {
        ".main:",
        "HALT",
        ".literals:",
        "a: 3.14",          // line 4: missing f
        "b:",               // line 5: no value
        "6.0f",             // line 6: no name
    };
    auto program = assemble(a, src);
    printErrors(a);
    assert(a.errors().size() == 3);
    assert(a.errors()[0].line_number == 4);
    assert(a.errors()[1].line_number == 5);
    assert(a.errors()[2].line_number == 6);
    assert(program.empty());
    std::cout << "testMultipleLiteralErrorsAllReported passed\n";
}

// ======================= Disassembler side =======================

void testDecodeSplitsAtMarker() {
    Assembler a;
    std::vector<std::string> src = {
        ".main:",
        "ADD R1, R2, R3",
        "HALT",
        ".literals:",
        "pi: 3.14f",
        "half: 0.5f",
    };
    auto program = assemble(a, src);
    assert(a.errors().empty());

    std::vector<uint32_t> words;
    for (const auto& instr : program) words.push_back(a.encodeInstruction(instr));
    for (uint32_t w : a.literalSection()) words.push_back(w);

    Disassembler d;
    auto decoded = d.decodeProgram(words);

    assert(d.decodeErrors().empty());                  // the marker is not "an unknown opcode"
    assert(decoded.size() == program.size());          // literal words are not decoded as instructions
    assert(d.literalWords().size() == 2);
    assert(d.literalWords()[0] == 0x4048F5C3u);
    assert(d.literalWords()[1] == 0x3F000000u);
    std::cout << "testDecodeSplitsAtMarker passed\n";
}

void testDecodeWithoutLiterals() {
    Assembler a;
    auto program = assemble(a, { ".main:", "HALT" });
    std::vector<uint32_t> words;
    for (const auto& instr : program) words.push_back(a.encodeInstruction(instr));

    Disassembler d;
    auto decoded = d.decodeProgram(words);
    assert(decoded.size() == program.size());
    assert(d.literalWords().empty());
    assert(d.formatLiterals().empty());
    std::cout << "testDecodeWithoutLiterals passed\n";
}

void testFormatLiterals() {
    Assembler a;
    auto program = assemble(a, {
        ".main:", "HALT", ".literals:", "pi: 3.14f", "half: 0.5f", "neg: -2.5f", "one: 1.0f",
    });
    std::vector<uint32_t> words;
    for (const auto& instr : program) words.push_back(a.encodeInstruction(instr));
    for (uint32_t w : a.literalSection()) words.push_back(w);

    Disassembler d;
    d.decodeProgram(words);
    auto lines = d.formatLiterals();

    assert(lines.size() == 5);
    assert(lines[0] == ".literals:");
    assert(lines[1] == "lit0: 3.14f");                 // shortest text that round-trips
    assert(lines[2] == "lit1: 0.5f");
    assert(lines[3] == "lit2: -2.5f");
    assert(lines[4] == "lit3: 1.0f");                  // ".0" added so it stays a float literal
    std::cout << "testFormatLiterals passed\n";
}

void testFormatNonFiniteLiteral() {
    // NaN (0x7FC00000) cannot be written as a literal, so it is emitted as a
    // comment with the raw bits instead of an invalid 'nanf'.
    std::vector<uint32_t> words = { kLiteralsMarker, 0x7FC00000u };

    Disassembler d;
    d.decodeProgram(words);
    auto lines = d.formatLiterals();

    assert(lines.size() == 2);
    assert(lines[0] == ".literals:");
    assert(lines[1].substr(0, 2) == "; ");
    assert(lines[1].find("7FC00000") != std::string::npos);
    std::cout << "testFormatNonFiniteLiteral passed\n";
}

void testLiteralsFileRoundtrip(bool hex) {
    Assembler a;
    auto program = assemble(a, {
        ".main:", "ADD R1, R2, R3", "HALT", ".literals:", "pi: 3.14f", "neg: -2.5f",
    });
    assert(a.errors().empty());

    std::vector<uint32_t> words;
    for (const auto& instr : program) words.push_back(a.encodeInstruction(instr));
    for (uint32_t w : a.literalSection()) words.push_back(w);

    std::string path = hex ? "/tmp/risc201_literals_test.hex" : "/tmp/risc201_literals_test.bin";
    bool write_ok = hex ? a.writeHexFile(path, words) : a.writeBinaryFile(path, words);
    assert(write_ok);

    Disassembler d;
    std::vector<uint32_t> read_back;
    bool read_ok = hex ? d.readHexFile(path, read_back) : d.readBinaryFile(path, read_back);
    assert(read_ok);
    assert(read_back == words);

    auto decoded = d.decodeProgram(read_back);
    assert(decoded.size() == program.size());
    assert(d.decodeErrors().empty());
    assert(d.literalWords() == a.literalWords());      // bit-exact through the file
    std::cout << "testLiteralsFileRoundtrip(" << (hex ? "hex" : "binary") << ") passed\n";
}

void testLiteralTextRoundtrip() {
    // Assemble -> disassemble -> feed the printed '.literals:' lines back into
    // the assembler -> the literal bits must be identical. Values chosen to
    // exercise the shortest-round-trip printing (0.1f, 1e10f, 16777216.0f...).
    std::vector<std::string> original = {
        ".main:", "HALT", ".literals:",
        "a: 3.14f", "b: 0.1f", "c: -2.5f", "d: 1.0f",
        "e: 1e10f", "f: 16777216.0f", "g: 0.0f", "h: 1.5e-5f",
    };
    Assembler first;
    auto program = assemble(first, original);
    assert(first.errors().empty());

    std::vector<uint32_t> words;
    for (const auto& instr : program) words.push_back(first.encodeInstruction(instr));
    for (uint32_t w : first.literalSection()) words.push_back(w);

    Disassembler d;
    d.decodeProgram(words);
    std::vector<std::string> printed = d.formatLiterals();

    std::vector<std::string> again = { ".main:", "HALT" };
    again.insert(again.end(), printed.begin(), printed.end());

    Assembler second;
    assemble(second, again);
    printErrors(second);
    assert(second.errors().empty());
    assert(second.literalWords() == first.literalWords());
    std::cout << "testLiteralTextRoundtrip passed\n";
}

int main() {
    // assembler side
    testLiteralsParsedToFloatBits();
    testLiteralsDoNotEmitInstructions();
    testNegativeAndZeroLiterals();
    testLiteralAddresses();
    testLiteralSectionLayout();
    testNoLiteralsMeansNoSection();
    testEmptyLiteralsHeader();

    testLiteralMissingName();
    testLiteralMissingValue();
    testLiteralMissingFSuffix();
    testLiteralGarbageValue();
    testLiteralTrailingJunk();
    testLiteralOverflowRejected();
    testLiteralInfinityRejected();
    testDuplicateLiteralName();
    testTextAfterLiteralsHeader();
    testInstructionAfterLiteralsIsError();
    testMultipleLiteralErrorsAllReported();

    // disassembler side
    testDecodeSplitsAtMarker();
    testDecodeWithoutLiterals();
    testFormatLiterals();
    testFormatNonFiniteLiteral();
    testLiteralsFileRoundtrip(true);
    testLiteralsFileRoundtrip(false);
    testLiteralTextRoundtrip();

    std::cout << "All literals tests passed.\n";
    return 0;
}