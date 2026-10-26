#include "assembler/assembler.h"
#include "simulator_core/cpu.h"
#include "simulator_core/decoder.h"
#include "tui/tui.h"
#include "utils/utils.h"

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

void printUsage(const char* prog) {
    std::cout
        << "usage: " << prog << " <program.asm|program.hex|program.bin> [options]\n"
        << "\n"
        << "Interactive terminal debugger and visualization UI for risc201 processor.\n"
        << "\n"
        << "options:\n"
        << "  -4, --4stage    select the 4-stage pipeline (default)\n"
        << "  -6, --6stage    select the 6-stage pipeline\n"
        << "      --no-forward stall on every dependency instead of bypassing\n"
        << "  -h, --help      show this message\n";
}

bool hasSuffix(const std::string& path, const std::string& suffix) {
    return path.size() >= suffix.size() &&
           path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool readHexWords(const std::string& path, std::vector<uint32_t>& out) {
    std::ifstream in(path);
    if (!in) return false;

    std::string line;
    while (std::getline(in, line)) {
        std::string token = trim(line);
        if (token.empty()) continue;
        if (token.rfind("0x", 0) == 0 || token.rfind("0X", 0) == 0) {
            token = token.substr(2);
        }
        try {
            size_t consumed = 0;
            const unsigned long value = std::stoul(token, &consumed, 16);
            if (consumed != token.size() || value > 0xFFFFFFFFul) return false;
            out.push_back(static_cast<uint32_t>(value));
        } catch (...) {
            return false;
        }
    }
    return true;
}

bool readBinaryWords(const std::string& path, std::vector<uint32_t>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;

    unsigned char bytes[4];
    while (true) {
        in.read(reinterpret_cast<char*>(bytes), 4);
        const std::streamsize got = in.gcount();
        if (got == 0) break;
        if (got != 4) return false;
        out.push_back((static_cast<uint32_t>(bytes[0]) << 24) |
                      (static_cast<uint32_t>(bytes[1]) << 16) |
                      (static_cast<uint32_t>(bytes[2]) << 8) |
                      static_cast<uint32_t>(bytes[3]));
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    std::string input_path;
    risc201::PipelineVariant variant = risc201::PipelineVariant::FourStage;
    bool forwarding = true;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "-4" || arg == "--4stage") {
            variant = risc201::PipelineVariant::FourStage;
        } else if (arg == "-6" || arg == "--6stage") {
            variant = risc201::PipelineVariant::SixStage;
        } else if (arg == "--no-forward") {
            forwarding = false;
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "error: unknown option '" << arg << "'\n";
            printUsage(argv[0]);
            return 1;
        } else if (input_path.empty()) {
            input_path = arg;
        } else {
            std::cerr << "error: unexpected extra argument '" << arg << "'\n";
            return 1;
        }
    }

    // Optional input_path: if omitted, starts empty with prompt to :load

    std::vector<std::string> raw_lines;
    std::vector<uint32_t> words;

    if (!input_path.empty()) {
        if (hasSuffix(input_path, ".hex")) {
            if (!readHexWords(input_path, words)) {
                std::cerr << "error: failed to read hex file " << input_path << "\n";
                return 1;
            }
            for (size_t i = 0; i < words.size(); ++i) {
                risc201::Instruction instr = risc201::decodeInstruction(words[i]);
                raw_lines.push_back(risc201::opcodeName(instr.opcode));
            }
        } else if (hasSuffix(input_path, ".bin")) {
            if (!readBinaryWords(input_path, words)) {
                std::cerr << "error: failed to read binary file " << input_path << "\n";
                return 1;
            }
            for (size_t i = 0; i < words.size(); ++i) {
                risc201::Instruction instr = risc201::decodeInstruction(words[i]);
                raw_lines.push_back(risc201::opcodeName(instr.opcode));
            }
        } else {
            std::ifstream file(input_path);
            if (!file) {
                std::cerr << "error: cannot open " << input_path << "\n";
                return 1;
            }
            std::string line;
            while (std::getline(file, line)) {
                raw_lines.push_back(line);
            }

            risc201::Assembler assembler;
            assembler.assemblePass1(raw_lines);
            const std::vector<risc201::Instruction> program = assembler.assemblePass2();

            if (!assembler.errors().empty()) {
                for (const auto& err : assembler.errors()) {
                    std::cerr << input_path << ":" << err.line_number
                              << ": error: " << err.message << "\n";
                }
                return 1;
            }

            for (const auto& instr : program) {
                words.push_back(assembler.encodeInstruction(instr));
            }
        }
    }

    risc201::Cpu cpu(variant);
    cpu.setForwarding(forwarding);
    if (!words.empty()) {
        cpu.loadWords(words);
    }

    risc201::Tui tui(cpu, raw_lines, words, input_path);
    tui.run();

    return 0;
}
