#include <assembler/assembler.h>

#include <iostream>
#include <fstream>
#include <sstream>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: risc201-asm <input.asm> [-o output.bin]\n";
        return 1;
    }
    // TODO(Divyansh): wire up Preprocessor -> Assembler -> output writer.

    //test
    std::ifstream file(argv[1]);
    if (!file) {
        std::cerr << "error: cannot open " << argv[1] << "\n";
        return 1;
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(file, line)) lines.push_back(line);

    risc201::Assembler assembler;
    bool ok = assembler.assemblePass1(lines);

    if (!ok) {
        for (const auto& err : assembler.errors()) {
            std::cerr << argv[1] << ":" << err.line_number << ": error: " << err.message << "\n";
        }
        return 1;
    }

    std::cout << "Pass 1 OK. Symbol table:\n";
    for (const auto& [name, addr] : assembler.symbolTable()) {
        std::cout << "  " << name << " -> 0x" << std::hex << addr << std::dec << "\n";
    }
    return 0;
    //test

    // std::cout << "risc201-asm: not yet implemented\n";
    // return 0;
}
