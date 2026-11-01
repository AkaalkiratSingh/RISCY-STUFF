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

// Pass 1 inserts 'JMP .main' as the first instruction, so program[0] is always
// that jump and the first instruction from the source is program[kUser].
// Every source also needs a '.main:' label (placed on its own first line here,
// so the first real instruction is on source line 2).
constexpr size_t kUser = 1;

// ---------- Success cases ----------

void testResolveRegisters() {
    risc201::Assembler asmblr;
    std::vector<std::string> src = { ".main:", "ADD R1, R2, R3" };
    asmblr.assemblePass1(src);
    auto program = asmblr.assemblePass2();
    printErrors(asmblr);
    assert(asmblr.errors().empty());
    assert(program.size() == kUser + 1);
    assert(program[kUser].I == false);
    assert(program[kUser].rd == risc201::Register::R1);
    assert(program[kUser].rs1 == risc201::Register::R2);
    assert(program[kUser].rs2 == risc201::Register::R3);
    std::cout << "testResolveRegisters passed\n";
}

void testResolveRegOrImm() {
    risc201::Assembler asmblr;
    std::vector<std::string> src = { ".main:", "ADD R1, R2, 5" };
    asmblr.assemblePass1(src);
    auto program = asmblr.assemblePass2();
    printErrors(asmblr);
    assert(asmblr.errors().empty());
    assert(program.size() == kUser + 1);
    assert(program[kUser].I == true);
    assert(program[kUser].imm == 5);
    std::cout << "testResolveRegOrImm passed\n";
}

void testResolveMemOperand() {
    risc201::Assembler asmblr;
    std::vector<std::string> src = { ".main:", "LOAD R1, 4[R2]" };
    asmblr.assemblePass1(src);
    auto program = asmblr.assemblePass2();
    printErrors(asmblr);
    assert(asmblr.errors().empty());
    assert(program.size() == kUser + 1);
    assert(program[kUser].rd == risc201::Register::R1);
    assert(program[kUser].rs1 == risc201::Register::R2);
    assert(program[kUser].imm == 4);
    assert(program[kUser].I == true);
    std::cout << "testResolveMemOperand passed\n";
}

void testResolveLabel() {
    risc201::Assembler asmblr;
    std::vector<std::string> src = {
        ".main:",
        "start: JMP end",   // address 4
        "       NOP",       // address 8
        "end:   HALT",      // address 12
    };
    asmblr.assemblePass1(src);
    auto program = asmblr.assemblePass2();
    printErrors(asmblr);
    assert(asmblr.errors().empty());
    assert(program.size() == kUser + 3);
    // offset = target - (here + 4) = 12 - (4 + 4) = 4
    assert(program[kUser].imm == 4);
    std::cout << "testResolveLabel passed\n";
}

void testHexImmediate() {
    risc201::Assembler asmblr;
    std::vector<std::string> src = { ".main:", "ADD R1, R2, 0x2A" };
    asmblr.assemblePass1(src);
    auto program = asmblr.assemblePass2();
    printErrors(asmblr);
    assert(asmblr.errors().empty());
    assert(program.size() == kUser + 1);
    assert(program[kUser].I == true);
    assert(program[kUser].imm == 0x2A);
    std::cout << "testHexImmediate passed\n";
}

void testNegativeImmediate() {
    risc201::Assembler asmblr;
    std::vector<std::string> src = { ".main:", "ADD R1, R2, -5" };
    asmblr.assemblePass1(src);
    auto program = asmblr.assemblePass2();
    printErrors(asmblr);
    assert(asmblr.errors().empty());
    assert(program.size() == kUser + 1);
    assert(program[kUser].imm == -5);
    std::cout << "testNegativeImmediate passed\n";
}

// ---------- Error cases ----------

void testUndefinedLabel() {
    risc201::Assembler asmblr;
    std::vector<std::string> src = { ".main:", "JMP nowhere" };
    asmblr.assemblePass1(src);
    auto program = asmblr.assemblePass2();
    printErrors(asmblr);
    assert(!asmblr.errors().empty());
    assert(program.empty());
    std::cout << "testUndefinedLabel passed\n";
}

void testInvalidRegister() {
    risc201::Assembler asmblr;
    std::vector<std::string> src = { ".main:", "ADD R99, R1, R2" };  // R99 out of R0-R15 range
    asmblr.assemblePass1(src);
    auto program = asmblr.assemblePass2();
    printErrors(asmblr);
    assert(!asmblr.errors().empty());
    assert(program.empty());
    std::cout << "testInvalidRegister passed\n";
}

void testInvalidRegOrImmToken() {
    risc201::Assembler asmblr;
    std::vector<std::string> src = { ".main:", "ADD R1, R2, notanumber" };  // fails both register and immediate parse
    asmblr.assemblePass1(src);
    auto program = asmblr.assemblePass2();
    printErrors(asmblr);
    assert(!asmblr.errors().empty());
    assert(program.empty());
    std::cout << "testInvalidRegOrImmToken passed\n";
}

void testMalformedMemOperandMissingBracket() {
    risc201::Assembler asmblr;
    std::vector<std::string> src = { ".main:", "LOAD R1, 4R2" };  // missing brackets entirely
    asmblr.assemblePass1(src);
    auto program = asmblr.assemblePass2();
    printErrors(asmblr);
    assert(!asmblr.errors().empty());
    assert(program.empty());
    std::cout << "testMalformedMemOperandMissingBracket passed\n";
}

void testMalformedMemOperandBadRegister() {
    risc201::Assembler asmblr;
    std::vector<std::string> src = { ".main:", "LOAD R1, 4[R99]" };  // bracket present but bad register inside
    asmblr.assemblePass1(src);
    auto program = asmblr.assemblePass2();
    printErrors(asmblr);
    assert(!asmblr.errors().empty());
    assert(program.empty());
    std::cout << "testMalformedMemOperandBadRegister passed\n";
}

void testMalformedMemOperandBadImmediate() {
    risc201::Assembler asmblr;
    std::vector<std::string> src = { ".main:", "LOAD R1, abc[R2]" };  // bracket present but bad immediate inside
    asmblr.assemblePass1(src);
    auto program = asmblr.assemblePass2();
    printErrors(asmblr);
    assert(!asmblr.errors().empty());
    assert(program.empty());
    std::cout << "testMalformedMemOperandBadImmediate passed\n";
}

void testOutOfRangeWarnsAndTruncates() {
    risc201::Assembler asmblr;
    std::vector<std::string> src = { ".main:", "ADD R1, R2, 999999" };  // exceeds 18-bit signed range
    asmblr.assemblePass1(src);
    auto program = asmblr.assemblePass2();
    printErrors(asmblr);
    printWarnings(asmblr);
    assert(asmblr.errors().empty());
    assert(!asmblr.warnings().empty());
    assert(program.size() == kUser + 1);
    assert(program[kUser].imm != 999999);
    std::cout << "testOutOfRangeWarnsAndTruncates passed\n";
}

void testOutOfRangeOffsetJType() {
    // Placeholder: documents the intent. A real J_TYPE overflow needs a
    // label more than 2^26 words away, which is impractical to build here.
    risc201::Assembler asmblr;
    std::vector<std::string> src = { ".main:", "start: JMP start" };  // start = 4, offset = 4 - (4+4) = -4, well within range
    asmblr.assemblePass1(src);
    auto program = asmblr.assemblePass2();
    printErrors(asmblr);
    printWarnings(asmblr);
    assert(asmblr.errors().empty());
    assert(asmblr.warnings().empty());  // -4 is in range, no warning expected here
    assert(program.size() == kUser + 1);
    assert(program[kUser].imm == -4);
    std::cout << "testOutOfRangeOffsetJType passed\n";
}

void testMultipleErrorsAllReported() {
    risc201::Assembler asmblr;
    std::vector<std::string> src = {
        ".main:",
        "ADD R99, R1, R2",   // line 2: invalid register
        "JMP missing",        // line 3: undefined label
        "LOAD R1, x[R2]",     // line 4: malformed mem operand
    };
    asmblr.assemblePass1(src);
    auto program = asmblr.assemblePass2();
    printErrors(asmblr);
    assert(asmblr.errors().size() == 3);
    assert(program.empty());
    std::cout << "testMultipleErrorsAllReported passed\n";
}

// ---------- Entry-point jump ----------

void testMainJumpWhenMainIsFirst() {
    risc201::Assembler asmblr;
    std::vector<std::string> src = { ".main:", "HALT" };
    asmblr.assemblePass1(src);
    auto program = asmblr.assemblePass2();
    printErrors(asmblr);
    assert(asmblr.errors().empty());
    assert(program.size() == 2);
    assert(program[0].opcode == risc201::Opcode::JMP);
    assert(program[0].format == risc201::InstrFormat::J_TYPE);
    // JMP .main is at address 0, .main is at address 4: 4 - (0 + 4) = 0
    assert(program[0].imm == 0);
    std::cout << "testMainJumpWhenMainIsFirst passed\n";
}

void testMainJumpSkipsCodeBeforeMain() {
    risc201::Assembler asmblr;
    std::vector<std::string> src = {
        "helper: NOP",      // address 4
        "        RET",      // address 8
        ".main:",           // address 12
        "        HALT",     // address 12
    };
    asmblr.assemblePass1(src);
    auto program = asmblr.assemblePass2();
    printErrors(asmblr);
    assert(asmblr.errors().empty());
    assert(program.size() == 4);
    assert(program[0].opcode == risc201::Opcode::JMP);
    // 12 - (0 + 4) = 8: skips over helper's two instructions
    assert(program[0].imm == 8);
    std::cout << "testMainJumpSkipsCodeBeforeMain passed\n";
}

void testMissingMainFailsPass2() {
    risc201::Assembler asmblr;
    std::vector<std::string> src = { "ADD R1, R2, R3" };
    asmblr.assemblePass1(src);
    auto program = asmblr.assemblePass2();
    printErrors(asmblr);
    assert(!asmblr.errors().empty());
    assert(program.empty());
    std::cout << "testMissingMainFailsPass2 passed\n";
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

    testMainJumpWhenMainIsFirst();
    testMainJumpSkipsCodeBeforeMain();
    testMissingMainFailsPass2();

    std::cout << "All Pass 2 tests passed.\n";
    return 0;
}
