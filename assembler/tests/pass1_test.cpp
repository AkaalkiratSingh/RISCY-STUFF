#include "assembler/assembler.h"
#include <cassert>
#include <iostream>

void testCleanProgram() {
    risc201::Assembler asmblr;
    std::vector<std::string> src = {
        "start: ADD R1, R2, R3",
        "       BEQ R1, R2, end",
        "       JMP start",
        "end:   HALT",
    };
    bool ok = asmblr.assemblePass1(src);
    assert(ok);
    assert(asmblr.symbolTable().at("start") == 0);
    assert(asmblr.symbolTable().at("end") == 12);
    std::cout << "testCleanProgram passed\n";
}

void testDuplicateLabel() {
    risc201::Assembler asmblr;
    std::vector<std::string> src = { "foo: NOP", "foo: NOP" };
    bool ok = asmblr.assemblePass1(src);
    assert(!ok);
    assert(asmblr.errors().size() == 1);
    assert(asmblr.errors()[0].line_number == 2);
    std::cout << "testDuplicateLabel passed\n";
}

void testUnknownMnemonic() {
    risc201::Assembler asmblr;
    std::vector<std::string> src = { "ADDD R1, R2, R3" };
    bool ok = asmblr.assemblePass1(src);
    assert(!ok);
    assert(asmblr.errors()[0].line_number == 1);
    std::cout << "testUnknownMnemonic passed\n";
}

void testWrongOperandCount() {
    risc201::Assembler asmblr;
    std::vector<std::string> src = { "ADD R1, R2" }; // needs 3
    bool ok = asmblr.assemblePass1(src);
    assert(!ok);
    std::cout << "testWrongOperandCount passed\n";
}

int main() {
    testCleanProgram();
    testDuplicateLabel();
    testUnknownMnemonic();
    testWrongOperandCount();
    std::cout << "All Pass 1 tests passed.\n";
    return 0;
}