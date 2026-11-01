#include <assembler/disassembler.h>

#include <iostream>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    std::string input_path;
    std::string output_path;
    bool use_labels = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "-o" && i + 1 < argc) {
            output_path = argv[++i];
        } else if (arg == "-l") {
            use_labels = true;
        } else if (input_path.empty()) {
            input_path = arg;
        }
    }

    if (input_path.empty()) {
        std::cerr << "usage: risc201-disasm <input.bin/input.hex> [-o output.asm] [-l]\n";
        return 1;
    }
    if (output_path.empty()) output_path = "a.asm";

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
        std::cerr << "warning: word " << index << " (0x" << std::hex << word << std::dec << ") has an unrecognized opcode, emitted as '; unknown opcode'\n";
    }

    std::vector<std::string> lines;
    if (use_labels) lines = disassembler.disassembleWithLabels(program);
    else lines = disassembler.disassemble(program);

    for (const auto& lit_line : disassembler.formatLiterals()) lines.push_back(lit_line);

    std::ofstream out(output_path);
    if (!out) {
        std::cerr << "error: cannot open " << output_path << "\n";
        return 1;
    }
    for (const auto& line : lines) {
        out << line << "\n";
    }
    std::cout << "Disassembled " << program.size() << " instruction(s) -> " << output_path << "\n";

    return 0;
}
