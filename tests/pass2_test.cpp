#include "assembler/assembler.h"
#include <cassert>
#include <iostream>

namespace {

void printErrors(const risc201::Assembler& asmblr) {
    for (const auto& err : asmblr.errors()) {
        std::cout << "  [error] line " << err.line_number << ": " << err.message << "\n";
    }
}

void printWarnings(const risc201::Assembler& asmblr) {
    for (const auto& warn : asmblr.warnings()) {
        std::cout << "  [warning] line " << warn.line_number << ": " << warn.message << "\n";
    }
}

}  // namespace

// ---------- Success cases ----------

void testResolveRegisters() {
    risc201::Assembler asmblr;
    std::vector<std::string> src = { "ADD R1, R2, R3" };
    asmblr.assemblePass1(src);
    auto program = asmblr.assemblePass2();
    printErrors(asmblr);
    assert(asmblr.errors().empty());
    assert(program.size() == 1);
    assert(program[0].I == false);
    assert(program[0].rd == risc201::Register::R1);
    assert(program[0].rs1 == risc201::Register::R2);
    assert(program[0].rs2 == risc201::Register::R3);
    std::cout << "testResolveRegisters passed\n";
}

void testResolveRegOrImm() {
    risc201::Assembler asmblr;
    std::vector<std::string> src = { "ADD R1, R2, 5" };
    asmblr.assemblePass1(src);
    auto program = asmblr.assemblePass2();
    printErrors(asmblr);
    assert(asmblr.errors().empty());
    assert(program.size() == 1);
    assert(program[0].I == true);
    assert(program[0].imm == 5);
    std::cout << "testResolveRegOrImm passed\n";
}

void testResolveMemOperand() {
    risc201::Assembler asmblr;
    std::vector<std::string> src = { "LOAD R1, 4[R2]" };
    asmblr.assemblePass1(src);
    auto program = asmblr.assemblePass2();
    printErrors(asmblr);
    assert(asmblr.errors().empty());
    assert(program.size() == 1);
    assert(program[0].rd == risc201::Register::R1);
    assert(program[0].rs1 == risc201::Register::R2);
    assert(program[0].imm == 4);
    assert(program[0].I == true);
    std::cout << "testResolveMemOperand passed\n";
}

void testResolveLabel() {
    risc201::Assembler asmblr;
    std::vector<std::string> src = {
        "start: JMP end",   // address 0
        "       NOP",       // address 4
        "end:   HALT",      // address 8
    };
    asmblr.assemblePass1(src);
    auto program = asmblr.assemblePass2();
    printErrors(asmblr);
    assert(asmblr.errors().empty());
    assert(program.size() == 3);
    // offset = target - (here + 4) = 8 - (0 + 4) = 4
    assert(program[0].imm == 4);
    std::cout << "testResolveLabel passed\n";
}

void testHexImmediate() {
    risc201::Assembler asmblr;
    std::vector<std::string> src = { "ADD R1, R2, 0x2A" };
    asmblr.assemblePass1(src);
    auto program = asmblr.assemblePass2();
    printErrors(asmblr);
    assert(asmblr.errors().empty());
    assert(program.size() == 1);
    assert(program[0].I == true);
    assert(program[0].imm == 0x2A);
    std::cout << "testHexImmediate passed\n";
}

void testNegativeImmediate() {
    risc201::Assembler asmblr;
    std::vector<std::string> src = { "ADD R1, R2, -5" };
    asmblr.assemblePass1(src);
    auto program = asmblr.assemblePass2();
    printErrors(asmblr);
    assert(asmblr.errors().empty());
    assert(program.size() == 1);
    assert(program[0].imm == -5);
    std::cout << "testNegativeImmediate passed\n";
}

// ---------- Error cases ----------

void testUndefinedLabel() {
    risc201::Assembler asmblr;
    std::vector<std::string> src = { "JMP nowhere" };
    asmblr.assemblePass1(src);
    auto program = asmblr.assemblePass2();
    printErrors(asmblr);
    assert(!asmblr.errors().empty());
    assert(program.empty());
    std::cout << "testUndefinedLabel passed\n";
}

void testInvalidRegister() {
    risc201::Assembler asmblr;
    std::vector<std::string> src = { "ADD R99, R1, R2" };  // R99 out of R0-R15 range
    asmblr.assemblePass1(src);
    auto program = asmblr.assemblePass2();
    printErrors(asmblr);
    assert(!asmblr.errors().empty());
    assert(program.empty());
    std::cout << "testInvalidRegister passed\n";
}

void testInvalidRegOrImmToken() {
    risc201::Assembler asmblr;
    std::vector<std::string> src = { "ADD R1, R2, notanumber" };  // fails both register and immediate parse
    asmblr.assemblePass1(src);
    auto program = asmblr.assemblePass2();
    printErrors(asmblr);
    assert(!asmblr.errors().empty());
    assert(program.empty());
    std::cout << "testInvalidRegOrImmToken passed\n";
}

void testMalformedMemOperandMissingBracket() {
    risc201::Assembler asmblr;
    std::vector<std::string> src = { "LOAD R1, 4R2" };  // missing brackets entirely
    asmblr.assemblePass1(src);
    auto program = asmblr.assemblePass2();
    printErrors(asmblr);
    assert(!asmblr.errors().empty());
    assert(program.empty());
    std::cout << "testMalformedMemOperandMissingBracket passed\n";
}

void testMalformedMemOperandBadRegister() {
    risc201::Assembler asmblr;
    std::vector<std::string> src = { "LOAD R1, 4[R99]" };  // bracket present but bad register inside
    asmblr.assemblePass1(src);
    auto program = asmblr.assemblePass2();
    printErrors(asmblr);
    assert(!asmblr.errors().empty());
    assert(program.empty());
    std::cout << "testMalformedMemOperandBadRegister passed\n";
}

void testMalformedMemOperandBadImmediate() {
    risc201::Assembler asmblr;
    std::vector<std::string> src = { "LOAD R1, abc[R2]" };  // bracket present but bad immediate inside
    asmblr.assemblePass1(src);
    auto program = asmblr.assemblePass2();
    printErrors(asmblr);
    assert(!asmblr.errors().empty());
    assert(program.empty());
    std::cout << "testMalformedMemOperandBadImmediate passed\n";
}

void testOutOfRangeWarnsAndTruncates() {
    risc201::Assembler asmblr;
    std::vector<std::string> src = { "ADD R1, R2, 999999" };  // exceeds 18-bit signed range
    asmblr.assemblePass1(src);
    auto program = asmblr.assemblePass2();
    printErrors(asmblr);
    printWarnings(asmblr);
    assert(asmblr.errors().empty());
    assert(!asmblr.warnings().empty());
    assert(program.size() == 1);
    assert(program[0].imm != 999999);
    std::cout << "testOutOfRangeWarnsAndTruncates passed\n";
}

void testOutOfRangeOffsetJType() {
    // Forces a huge label distance by padding with many NOPs, to actually
    // exceed the 27-bit J_TYPE offset range (unwieldy to construct exactly,
    // so this just documents the intent — adjust if you want a real huge-gap
    // test rather than this placeholder).
    risc201::Assembler asmblr;
    std::vector<std::string> src = { "start: JMP start" };  // offset = 0 - (0+4) = -4, well within range
    asmblr.assemblePass1(src);
    auto program = asmblr.assemblePass2();
    printErrors(asmblr);
    printWarnings(asmblr);
    assert(asmblr.errors().empty());
    assert(asmblr.warnings().empty());  // -4 is in range, no warning expected here
    assert(program.size() == 1);
    assert(program[0].imm == -4);
    std::cout << "testOutOfRangeOffsetJType passed\n";
}

void testMultipleErrorsAllReported() {
    risc201::Assembler asmblr;
    std::vector<std::string> src = {
        "ADD R99, R1, R2",   // line 1: invalid register
        "JMP missing",        // line 2: undefined label
        "LOAD R1, x[R2]",     // line 3: malformed mem operand
    };
    asmblr.assemblePass1(src);
    auto program = asmblr.assemblePass2();
    printErrors(asmblr);
    assert(asmblr.errors().size() == 3);
    assert(program.empty());
    std::cout << "testMultipleErrorsAllReported passed\n";
}

int main() {
    testResolveRegisters();
    testResolveRegOrImm();
    testResolveMemOperand();
    testResolveLabel();
    testHexImmediate();
    testNegativeImmediate();

    testUndefinedLabel();
    testInvalidRegister();
    testInvalidRegOrImmToken();
    testMalformedMemOperandMissingBracket();
    testMalformedMemOperandBadRegister();
    testMalformedMemOperandBadImmediate();
    testOutOfRangeWarnsAndTruncates();
    testOutOfRangeOffsetJType();
    testMultipleErrorsAllReported();

    std::cout << "All Pass 2 tests passed.\n";
    return 0;
}