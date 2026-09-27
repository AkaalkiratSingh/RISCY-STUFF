#include <assembler/assembler.h>

#include <iostream>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 2 && argc != 4) {
        std::cerr << "usage: risc201-asm <input.asm> [-o output.bin/output.hex]\n";
        return 1;
    }

    std::string input_path = argv[1];
    std::string output_path;

    if (argc == 2) output_path = "a.bin";
    else if (argc == 4 && std::string(argv[2]) == "-o") output_path = argv[3];
    else {
        std::cerr << "usage: risc201-asm <input.asm> [-o output.bin/output.hex]\n";
        return 1;
    }

    std::ifstream file(input_path);
    if (!file) {
        std::cerr << "error: cannot open " << input_path << "\n";
        return 1;
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(file, line)) lines.push_back(line);

    risc201::Assembler assembler;

    assembler.assemblePass1(lines);

    std::vector<risc201::Instruction> program = assembler.assemblePass2();

    for (const auto& warn : assembler.warnings()) {
        std::cerr << input_path << ":" << warn.line_number << ": warning: " << warn.message << "\n";
    }

    if (!assembler.errors().empty()) {
        for (const auto& err : assembler.errors()) {
            std::cerr << input_path << ":" << err.line_number << ": error: " << err.message << "\n";
        }
        return 1;
    }

    std::vector<uint32_t> encoded;

    for (const auto& instr : program) {
        encoded.push_back(assembler.encodeInstruction(instr));
    }

    bool is_hex = output_path.size() >= 4 && (output_path.compare(output_path.size() - 4, 4, ".hex") == 0);

    bool write_ok;
    if (is_hex) write_ok = assembler.writeHexFile(output_path, encoded);
    else write_ok = assembler.writeBinaryFile(output_path, encoded);

    if (!write_ok) {
        std::cerr << "error: failed to write output file " << output_path << "\n";
        return 1;
    }

    std::cout << "Assembled " << program.size() << " instruction(s) -> " << output_path << "\n";
    return 0;
}
