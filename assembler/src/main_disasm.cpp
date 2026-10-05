#include <assembler/disassembler.h>

#include <iostream>
#include <fstream>
#include <string>
#include<vector>

int main(int argc, char** argv) {
    if (argc != 2 && argc != 4) {
        std::cerr << "usage: risc201-disasm <input.bin/input.hex> [-o output.asm]\n";
        return 1;
    }

    std::string input_path = argv[1];
    std::string output_path;

    if (argc == 2) output_path = "a.asm";
    else if (argc == 4 && std::string(argv[2]) == "-o") output_path = argv[3];
    else {
        std::cerr << "usage: risc201-asm <input.asm> [-o output.bin/output.hex]\n";
        return 1;
    }
    
    bool is_hex = input_path.size() >= 4 && (input_path.compare(input_path.size() - 4, 4, ".hex") == 0);

    risc201::Disassembler disassembler;
    std::vector<uint32_t> words;

    bool read_ok;
    if (is_hex) read_ok = disassembler.readHexFile(input_path, words);
    else read_ok = disassembler.readBinaryFile(input_path, words);

    if (!read_ok) {
        std::cerr << "error: cannot read or parse " << input_path << "\n";
        return 1;
    }

    std::vector<risc201::Instruction> program = disassembler.decodeProgram(words);

    for (const auto& [index, word] : disassembler.decodeErrors()) {
        std::cerr << "warning: word " << index << " (0x" << std::hex << word << std::dec << ") has an unrecognized opcode, skipped\n";
    }

    std::vector<std::string> lines = disassembler.disassemble(program);

    std::ofstream out(output_path);
    if (!out) {
        std::cerr << "error: cannot open " << output_path << "\n";
        return 1;
    }
    for (const auto& line : lines) {
        out << line << "\n";
    }
    std::cout << "Disassembled " << lines.size() << " instruction(s) -> " << output_path << "\n";

    return 0;
}
