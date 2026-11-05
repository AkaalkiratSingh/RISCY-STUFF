#include "assembler/assembler.h"
#include <cassert>
#include <iostream>

void testCleanProgram() {
    risc201::Assembler asmblr;
    std::vector<std::string> src = {
        ".main:",                   // line 1, address 4
        "start: ADD R1, R2, R3",    // line 2, address 4
        "       BEQ end",           // line 3, address 8
        "       JMP start",         // line 4, address 12
        "end:   HALT",              // line 5, address 16
    };
    bool ok = asmblr.assemblePass1(src);
    assert(ok);
    assert(asmblr.symbolTable().at(".main") == 4);
    assert(asmblr.symbolTable().at("start") == 4);
    assert(asmblr.symbolTable().at("end") == 16);
    std::cout << "testCleanProgram passed\n";
}

void testDuplicateLabel() {
    risc201::Assembler asmblr;
    std::vector<std::string> src = { ".main:", "foo: NOP", "foo: NOP" };
    bool ok = asmblr.assemblePass1(src);
    assert(!ok);
    assert(asmblr.errors().size() == 1);
    assert(asmblr.errors()[0].line_number == 3);
    std::cout << "testDuplicateLabel passed\n";
}

void testUnknownMnemonic() {
    risc201::Assembler asmblr;
    std::vector<std::string> src = { ".main:", "ADDD R1, R2, R3" };
    bool ok = asmblr.assemblePass1(src);
    assert(!ok);
    assert(asmblr.errors().size() == 1);
    assert(asmblr.errors()[0].line_number == 2);
    std::cout << "testUnknownMnemonic passed\n";
}

void testWrongOperandCount() {
    risc201::Assembler asmblr;
    std::vector<std::string> src = { ".main:", "ADD R1, R2" }; // needs 3
    bool ok = asmblr.assemblePass1(src);
    assert(!ok);
    assert(asmblr.errors().size() == 1);
    assert(asmblr.errors()[0].line_number == 2);
    std::cout << "testWrongOperandCount passed\n";
}

void testMissingMain() {
    risc201::Assembler asmblr;
    std::vector<std::string> src = { "ADD R1, R2, R3", "HALT" };
    bool ok = asmblr.assemblePass1(src);
    assert(!ok);
    assert(asmblr.errors().size() == 1);
    assert(asmblr.errors()[0].line_number == 0);   // not tied to a source line
    std::cout << "testMissingMain passed\n";
}

void testDuplicateMain() {
    risc201::Assembler asmblr;
    std::vector<std::string> src = { ".main: NOP", ".main: NOP" };
    bool ok = asmblr.assemblePass1(src);
    assert(!ok);
    assert(asmblr.errors().size() == 1);
    assert(asmblr.errors()[0].line_number == 2);   // reported on the later one
    std::cout << "testDuplicateMain passed\n";
}

// ---------- EMPTY operand slots (MOV / NOT / CMP) ----------
// MOV, NOT and CMP have a register slot in the encoding that is not written in the
// source, so the op table has 3 operand kinds but the programmer writes only 2.

void testEmptyOperandsAreNotWritten() {
    risc201::Assembler asmblr;
    std::vector<std::string> src = {
        ".main:",
        "a: MOV R1, R2",    // two written operands, three slots in the op table
        "b: NOT R3, R4",
        "c: CMP R5, R6",
        "d: HALT",
    };
    bool ok = asmblr.assemblePass1(src);
    assert(ok);
    assert(asmblr.errors().empty());

    // JMP .main is word 0, then one word per instruction.
    assert(asmblr.symbolTable().at("a") == 4);
    assert(asmblr.symbolTable().at("b") == 8);
    assert(asmblr.symbolTable().at("c") == 12);
    assert(asmblr.symbolTable().at("d") == 16);
    std::cout << "testEmptyOperandsAreNotWritten passed\n";
}

void testEmptyOperandWrongCount() {
    const std::vector<std::string> bad = {
        "MOV R1", "MOV R1, R2, R3",
        "NOT R1", "NOT R1, R2, R3",
        "CMP R1", "CMP R1, R2, R3",
        "MOV",
    };
    for (const auto& line : bad) {
        risc201::Assembler asmblr;
        std::vector<std::string> src = { ".main:", line };
        bool ok = asmblr.assemblePass1(src);
        assert(!ok);
        assert(asmblr.errors().size() == 1);
        assert(asmblr.errors()[0].line_number == 2);
        assert(asmblr.errors()[0].message.find("expects 2 operand(s)") != std::string::npos);
    }
    std::cout << "testEmptyOperandWrongCount passed\n";
}

void testEmptyOperandErrorReportsWrittenCount() {
    // The message must count what the programmer writes (2), not the 3 slots in the table.
    risc201::Assembler asmblr;
    std::vector<std::string> src = { ".main:", "MOV R1, R2, R3" };
    asmblr.assemblePass1(src);
    assert(asmblr.errors().size() == 1);
    assert(asmblr.errors()[0].message == "'MOV' expects 2 operand(s), got 3");
    std::cout << "testEmptyOperandErrorReportsWrittenCount passed\n";
}

void testBgtOperandCount() {
    {
        risc201::Assembler asmblr;
        std::vector<std::string> src = { ".main:", "BGT end", "end: HALT" };
        assert(asmblr.assemblePass1(src));
        assert(asmblr.errors().empty());
    }
    {
        risc201::Assembler asmblr;
        std::vector<std::string> src = { ".main:", "BGT", "HALT" };
        assert(!asmblr.assemblePass1(src));
        assert(asmblr.errors().size() == 1);
        assert(asmblr.errors()[0].message == "'BGT' expects 1 operand(s), got 0");
    }
    {
        risc201::Assembler asmblr;
        std::vector<std::string> src = { ".main:", "BGT a, b", "a: HALT" };
        assert(!asmblr.assemblePass1(src));
        assert(asmblr.errors()[0].message == "'BGT' expects 1 operand(s), got 2");
    }
    std::cout << "testBgtOperandCount passed\n";
}

// ---------- label names: no leading digit or '-' ----------

void testLabelCannotStartWithDigit() {
    for (const std::string label : { "1loop", "4", "9pi", "0x10", "-x", "-4" }) {
        risc201::Assembler asmblr;
        std::vector<std::string> src = { ".main:", label + ": NOP", "HALT" };
        bool ok = asmblr.assemblePass1(src);
        assert(!ok);
        assert(asmblr.errors().size() == 1);
        assert(asmblr.errors()[0].line_number == 2);
        assert(asmblr.errors()[0].message ==
               "invalid label '" + label + "': label names cannot start with a digit or '-'");
        assert(asmblr.symbolTable().count(label) == 0);     // a rejected label is not defined
    }
    std::cout << "testLabelCannotStartWithDigit passed\n";
}

void testLabelOnItsOwnLineCannotStartWithDigit() {
    risc201::Assembler asmblr;
    std::vector<std::string> src = { ".main:", "4:", "HALT" };
    assert(!asmblr.assemblePass1(src));
    assert(asmblr.errors().size() == 1);
    assert(asmblr.errors()[0].line_number == 2);
    std::cout << "testLabelOnItsOwnLineCannotStartWithDigit passed\n";
}

void testLiteralNameCannotStartWithDigit() {
    // Literal names are labels too, so the same rule applies.
    risc201::Assembler asmblr;
    std::vector<std::string> src = { ".main:", "HALT", ".literals:", "1pi: 3.14f" };
    assert(!asmblr.assemblePass1(src));
    assert(asmblr.errors().size() == 1);
    assert(asmblr.errors()[0].line_number == 4);
    assert(asmblr.errors()[0].message.find("invalid label '1pi'") != std::string::npos);
    std::cout << "testLiteralNameCannotStartWithDigit passed\n";
}

void testLabelsContainingDigitsStillAllowed() {
    risc201::Assembler asmblr;
    std::vector<std::string> src = { ".main:", "a1: NOP", "_1: NOP", "L4: NOP", "loop_2: NOP", ".x9: NOP", "HALT" };
    assert(asmblr.assemblePass1(src));
    assert(asmblr.errors().empty());
    for (const char* name : { "a1", "_1", "L4", "loop_2", ".x9" }) {
        assert(asmblr.symbolTable().count(name) == 1);
    }
    std::cout << "testLabelsContainingDigitsStillAllowed passed\n";
}

void printErrorsDemo() {
    risc201::Assembler assembler;
    std::vector<std::string> src = {
        ".main:",
        "foo: NOP",
        "foo: NOP",
        "ADDD R1, R2, R3",
        "ADD R1, R2",
        "     BEQ end",
        "end: HALT",
    };

    bool ok = assembler.assemblePass1(src);

    std::cout << "\n--- printErrorsDemo ---\n";
    std::cout << "assemblePass1 returned: " << (ok ? "true (no errors)" : "false (errors found)") << "\n";
    std::cout << "Error count: " << assembler.errors().size() << "\n\n";

    for (const auto& err : assembler.errors()) {
        std::cout << "line " << err.line_number << ": " << err.message << "\n";
    }

    std::cout << "\nSymbol table:\n";
    for (const auto& [name, addr] : assembler.symbolTable()) {
        std::cout << "  " << name << " -> 0x" << std::hex << addr << std::dec << "\n";
    }
    std::cout << "-----------------------\n\n";
}

int main() {
    testCleanProgram();
    testDuplicateLabel();
    testUnknownMnemonic();
    testWrongOperandCount();
    testMissingMain();
    testDuplicateMain();
    testEmptyOperandsAreNotWritten();
    testEmptyOperandWrongCount();
    testEmptyOperandErrorReportsWrittenCount();
    testBgtOperandCount();
    testLabelCannotStartWithDigit();
    testLabelOnItsOwnLineCannotStartWithDigit();
    testLiteralNameCannotStartWithDigit();
    testLabelsContainingDigitsStillAllowed();
    printErrorsDemo();
    std::cout << "All Pass 1 tests passed.\n";
    return 0;
}