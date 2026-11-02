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

// Assembles `src` and returns the program (errors/warnings stay on `asmblr`).
std::vector<risc201::Instruction> assembleSource(risc201::Assembler& asmblr, const std::vector<std::string>& src) {
    asmblr.assemblePass1(src);
    auto program = asmblr.assemblePass2();
    printErrors(asmblr);
    return program;
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

// ---------- EMPTY operand slots (MOV / NOT / CMP) and BGT ----------
// MOV and NOT: rd, <empty rs1>, rs2|imm.   CMP: <empty rd>, rs1, rs2|imm.
// The empty slot is not written in the source and stays R0 (its 4 bits encode as 0).

void testResolveMovRegister() {
    using namespace risc201;
    Assembler asmblr;
    auto program = assembleSource(asmblr, { ".main:", "MOV R1, R2" });
    assert(asmblr.errors().empty());
    assert(program.size() == kUser + 1);
    const Instruction& i = program[kUser];
    assert(i.opcode == Opcode::MOV);
    assert(i.format == InstrFormat::RI_TYPE);
    assert(i.I == false);
    assert(i.rd == Register::R1);
    assert(i.rs1 == Register::R0);      // the empty slot
    assert(i.rs2 == Register::R2);      // second written operand lands in rs2, not rs1
    std::cout << "testResolveMovRegister passed\n";
}

void testResolveMovImmediate() {
    using namespace risc201;
    Assembler asmblr;
    auto program = assembleSource(asmblr, { ".main:", "MOV R3, 5", "MOV R4, -7", "MOV R5, 0x10" });
    assert(asmblr.errors().empty());
    assert(program.size() == kUser + 3);

    assert(program[kUser].opcode == Opcode::MOV);
    assert(program[kUser].I == true);
    assert(program[kUser].rd == Register::R3);
    assert(program[kUser].rs1 == Register::R0);
    assert(program[kUser].imm == 5);

    assert(program[kUser + 1].rd == Register::R4);
    assert(program[kUser + 1].imm == -7);

    assert(program[kUser + 2].rd == Register::R5);
    assert(program[kUser + 2].imm == 16);
    std::cout << "testResolveMovImmediate passed\n";
}

void testResolveNot() {
    using namespace risc201;
    Assembler asmblr;
    auto program = assembleSource(asmblr, { ".main:", "NOT R4, R5", "NOT R6, 0xFF" });
    assert(asmblr.errors().empty());
    assert(program.size() == kUser + 2);

    assert(program[kUser].opcode == Opcode::NOT);
    assert(program[kUser].I == false);
    assert(program[kUser].rd == Register::R4);
    assert(program[kUser].rs1 == Register::R0);
    assert(program[kUser].rs2 == Register::R5);

    assert(program[kUser + 1].I == true);
    assert(program[kUser + 1].rd == Register::R6);
    assert(program[kUser + 1].imm == 255);
    std::cout << "testResolveNot passed\n";
}

void testResolveCmpEmptyRd() {
    using namespace risc201;
    Assembler asmblr;
    auto program = assembleSource(asmblr, { ".main:", "CMP R7, R8", "CMP R9, -3" });
    assert(asmblr.errors().empty());
    assert(program.size() == kUser + 2);

    // CMP's empty slot is rd: the first written operand is rs1, the second is rs2/imm.
    assert(program[kUser].opcode == Opcode::CMP);
    assert(program[kUser].I == false);
    assert(program[kUser].rd == Register::R0);
    assert(program[kUser].rs1 == Register::R7);
    assert(program[kUser].rs2 == Register::R8);

    assert(program[kUser + 1].I == true);
    assert(program[kUser + 1].rd == Register::R0);
    assert(program[kUser + 1].rs1 == Register::R9);
    assert(program[kUser + 1].imm == -3);
    std::cout << "testResolveCmpEmptyRd passed\n";
}

void testEmptyOperandDoesNotLeakToNextInstruction() {
    using namespace risc201;
    Assembler asmblr;
    auto program = assembleSource(asmblr, { ".main:", "MOV R1, R2", "ADD R3, R4, R5" });
    assert(asmblr.errors().empty());
    assert(program.size() == kUser + 2);

    // The slot skipped for MOV must not shift the registers of the instruction after it.
    assert(program[kUser + 1].opcode == Opcode::ADD);
    assert(program[kUser + 1].rd == Register::R3);
    assert(program[kUser + 1].rs1 == Register::R4);
    assert(program[kUser + 1].rs2 == Register::R5);
    std::cout << "testEmptyOperandDoesNotLeakToNextInstruction passed\n";
}

void testEmptyOperandSpAlias() {
    using namespace risc201;
    Assembler asmblr;
    auto program = assembleSource(asmblr, { ".main:", "MOV sp, R1", "CMP sp, 4" });
    assert(asmblr.errors().empty());
    assert(program.size() == kUser + 2);
    assert(program[kUser].rd == Register::R15);
    assert(program[kUser].rs2 == Register::R1);
    assert(program[kUser + 1].rs1 == Register::R15);
    assert(program[kUser + 1].imm == 4);
    std::cout << "testEmptyOperandSpAlias passed\n";
}

void testEmptyOperandInvalidOperands() {
    using namespace risc201;
    const std::vector<std::string> bad = {
        "MOV R1, R99",      // second operand is neither a register nor an immediate
        "MOV R16, R1",      // first operand is not a register
        "NOT R1, hello",
        "CMP R16, R1",
        "CMP R1, R99",
    };
    for (const auto& line : bad) {
        Assembler asmblr;
        auto program = assembleSource(asmblr, { ".main:", line });
        assert(asmblr.errors().size() == 1);
        assert(asmblr.errors()[0].line_number == 2);
        assert(program.empty());
    }
    std::cout << "testEmptyOperandInvalidOperands passed\n";
}

void testEmptyOperandImmediateOutOfRangeWarns() {
    using namespace risc201;
    Assembler asmblr;
    auto program = assembleSource(asmblr, { ".main:", "MOV R1, 200000" });
    assert(asmblr.errors().empty());
    assert(asmblr.warnings().size() == 1);
    assert(program[kUser].imm == -62144);       // 200000 truncated to 18 bits, sign-extended
    std::cout << "testEmptyOperandImmediateOutOfRangeWarns passed\n";
}

void testResolveBgtLabel() {
    using namespace risc201;
    Assembler asmblr;
    auto program = assembleSource(asmblr, { ".main:", "BGT end", "NOP", "end: HALT" });
    assert(asmblr.errors().empty());
    assert(program.size() == kUser + 3);

    assert(program[kUser].opcode == Opcode::BGT);
    assert(program[kUser].format == InstrFormat::J_TYPE);
    // BGT at address 4, end at address 12: offset = 12 - (4 + 4) = 4
    assert(program[kUser].imm == 4);
    std::cout << "testResolveBgtLabel passed\n";
}

void testResolveBgtBackwardLabel() {
    using namespace risc201;
    Assembler asmblr;
    auto program = assembleSource(asmblr, { ".main:", "top: NOP", "BGT top", "HALT" });
    assert(asmblr.errors().empty());
    // BGT at address 8, top at address 4: offset = 4 - (8 + 4) = -8
    assert(program[kUser + 1].opcode == Opcode::BGT);
    assert(program[kUser + 1].imm == -8);
    std::cout << "testResolveBgtBackwardLabel passed\n";
}

void testBgtUndefinedLabel() {
    using namespace risc201;
    Assembler asmblr;
    auto program = assembleSource(asmblr, { ".main:", "BGT nowhere", "HALT" });
    assert(asmblr.errors().size() == 1);
    assert(asmblr.errors()[0].message.find("undefined label") != std::string::npos);
    assert(program.empty());
    std::cout << "testBgtUndefinedLabel passed\n";
}

void testWriteToR0Rejected() {
    const char* ops[] = { "ADD", "SUB", "MUL", "DIV", "REM", "AND", "OR", "XOR", "SHL", "SHR", "SAR" };
    for (const char* op : ops) {
        risc201::Assembler asmblr;
        std::vector<std::string> src = { ".main:", std::string(op) + " R0, R1, R2" };
        auto program = assembleSource(asmblr, src);
        assert(asmblr.errors().size() == 1);
        assert(asmblr.errors()[0].line_number == 2);
        assert(asmblr.errors()[0].message == "'" + std::string(op) + "' cannot write to R0 (R0 is always zero)");
        assert(program.empty());
    }
    std::cout << "testWriteToR0Rejected passed\n";
}

void testWriteToR0RejectedMovNotLoad() {
    const std::vector<std::string> cases[] = {
        { ".main:", "MOV R0, R1" },
        { ".main:", "MOV R0, 5" },
        { ".main:", "NOT R0, R1" },
        { ".main:", "LOAD R0, 4[R1]" },
        { ".main:", "ADD R0, R1, 7" },
    };
    for (const auto& src : cases) {
        risc201::Assembler asmblr;
        assembleSource(asmblr, src);
        assert(asmblr.errors().size() == 1);
    }
    std::cout << "testWriteToR0RejectedMovNotLoad passed\n";
}

void testR0AllowedWhereNotADestination() {
    risc201::Assembler asmblr;
    std::vector<std::string> src = {
        ".main:",
        "STORE R0, 4[R1]",   // R0 is the value being stored, not a destination
        "CMP R0, 5",          // first slot is empty
        "ADD R1, R0, R0",     // R0 as a source
        "LOAD R2, 0[R0]",     // R0 as a base register
        "NOP",
    };
    auto program = assembleSource(asmblr, src);
    assert(asmblr.errors().empty());
    assert(program.size() == kUser + 5);
    assert(program[kUser].opcode == risc201::Opcode::STORE);
    assert(program[kUser].rd == risc201::Register::R0);
    std::cout << "testR0AllowedWhereNotADestination passed\n";
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

    testResolveMovRegister();
    testResolveMovImmediate();
    testResolveNot();
    testResolveCmpEmptyRd();
    testEmptyOperandDoesNotLeakToNextInstruction();
    testEmptyOperandSpAlias();
    testEmptyOperandInvalidOperands();
    testEmptyOperandImmediateOutOfRangeWarns();
    testResolveBgtLabel();
    testResolveBgtBackwardLabel();
    testBgtUndefinedLabel();

    testWriteToR0Rejected();
    testWriteToR0RejectedMovNotLoad();
    testR0AllowedWhereNotADestination();

    std::cout << "All Pass 2 tests passed.\n";
    return 0;
}