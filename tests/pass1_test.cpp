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
    printErrorsDemo();
    std::cout << "All Pass 1 tests passed.\n";
    return 0;
}