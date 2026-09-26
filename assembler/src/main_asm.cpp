#include <assembler/assembler.h>

#include <iostream>
#include <fstream>
#include <sstream>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: risc201-asm <input.asm> [-o output.bin]\n";
        return 1;
    }

    std::ifstream file(argv[1]);
    if (!file) {
        std::cerr << "error: cannot open " << argv[1] << "\n";
        return 1;
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(file, line)) lines.push_back(line);

    risc201::Assembler assembler;

    bool pass1_ok = assembler.assemblePass1(lines);
    if (!pass1_ok) {
        for (const auto& err : assembler.errors()) {
            std::cerr << argv[1] << ":" << err.line_number << ": error: " << err.message << "\n";
        }
        return 1;
    }

    std::vector<risc201::Instruction> program = assembler.assemblePass2();

    for (const auto& warn : assembler.warnings()) {
        std::cerr << argv[1] << ":" << warn.line_number << ": warning: " << warn.message << "\n";
    }

    if (!assembler.errors().empty()) {
        for (const auto& err : assembler.errors()) {
            std::cerr << argv[1] << ":" << err.line_number << ": error: " << err.message << "\n";
        }
        return 1;
    }

    std::cout << "Assembled " << program.size() << " instruction(s).\n";
    // TODO(Divyansh): encode `program` to bytes and write to output file
    
    return 0;
}
