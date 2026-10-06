// risc201-sim: load a program, run it on the processor, and report the
// resulting machine state. Bare-bones CLI — see printUsage() for flags.
//
// Input can be either assembly source (.asm, assembled here) or the
// assembler's encoded output (.hex / .bin), which the CPU decodes itself.

#include "assembler/assembler.h"
#include "simulator_core/cpu.h"
#include "simulator_core/decoder.h"
#include "utils/utils.h"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr uint64_t kMaxStepSteps = 10'000'000;  // guard against runaway loops

void printUsage(const char* prog) {
    std::cout
        << "usage: " << prog << " <program.asm|program.hex|program.bin> [options]\n"
        << "\n"
        << "Loads a program, runs it on the processor, and prints the final\n"
        << "register file. .asm input is assembled first; .hex/.bin input is\n"
        << "decoded by the processor itself.\n"
        << "\n"
        << "options:\n"
        << "  -4, --4stage    select the 4-stage pipeline (default)\n"
        << "  -6, --6stage    select the 6-stage pipeline\n"
        << "  -s, --step      single-step, printing state after each instruction\n"
        << "  -d, --decode    print the decoded instruction listing before running\n"
        << "  -m, --mem S:C   dump C data-memory words starting at word index S\n"
        << "  -h, --help      show this message\n";
}

const char* exceptionName(risc201::ExceptionCode code) {
    using risc201::ExceptionCode;
    switch (code) {
        case ExceptionCode::NONE:                  return "NONE";
        case ExceptionCode::STACK_OVERFLOW:        return "STACK_OVERFLOW";
        case ExceptionCode::STACK_UNDERFLOW:       return "STACK_UNDERFLOW";
        case ExceptionCode::INVALID_MEMORY_ACCESS: return "INVALID_MEMORY_ACCESS";
        case ExceptionCode::DIVIDE_BY_ZERO:        return "DIVIDE_BY_ZERO";
        case ExceptionCode::ILLEGAL_INSTRUCTION:   return "ILLEGAL_INSTRUCTION";
    }
    return "UNKNOWN";
}

void printRegisters(const risc201::CpuState& st) {
    for (int i = 0; i < risc201::kNumGPRegisters; ++i) {
        std::cout << "R" << std::setw(2) << i << " = " << std::setw(12)
                  << st.registers[static_cast<size_t>(i)];
        if ((i + 1) % 4 == 0) std::cout << "\n";
        else                  std::cout << "   ";
    }
    std::cout << "PC = " << st.pc << "   SP = " << st.sp << "\n"
              << "FLAGS: Z=" << st.flags.zero << " N=" << st.flags.negative
              << " C=" << st.flags.carry << " V=" << st.flags.overflow << "\n";
}

bool parseRange(const std::string& text, uint32_t& start, uint32_t& count) {
    const size_t colon = text.find(':');
    if (colon == std::string::npos) return false;
    try {
        start = static_cast<uint32_t>(std::stoul(text.substr(0, colon)));
        count = static_cast<uint32_t>(std::stoul(text.substr(colon + 1)));
    } catch (...) {
        return false;
    }
    return true;
}

std::string regName(int r) { return "R" + std::to_string(r); }

std::string formatInstruction(const risc201::Instruction& in) {
    using risc201::Opcode;
    const std::string name = risc201::opcodeName(in.opcode);
    const int rd  = static_cast<int>(in.rd);
    const int rs1 = static_cast<int>(in.rs1);
    const int rs2 = static_cast<int>(in.rs2);

    switch (in.opcode) {
        case Opcode::NOP:
        case Opcode::RET:
        case Opcode::HALT:
            return name;
        case Opcode::JMP:
        case Opcode::BEQ:
        case Opcode::CALL:
            return name + " " + std::to_string(in.imm);
        case Opcode::PUSH:
        case Opcode::POP:
            return name + " " + regName(rd);
        case Opcode::LOAD:
        case Opcode::STORE:
            return name + " " + regName(rd) + ", " + std::to_string(in.imm) +
                   "[" + regName(rs1) + "]";
        default: {
            const std::string third = in.I ? std::to_string(in.imm) : regName(rs2);
            return name + " " + regName(rd) + ", " + regName(rs1) + ", " + third;
        }
    }
}

enum class InputKind { Assembly, Hex, Binary };

bool hasSuffix(const std::string& path, const std::string& suffix) {
    return path.size() >= suffix.size() &&
           path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0;
}

InputKind kindFromPath(const std::string& path) {
    if (hasSuffix(path, ".hex")) return InputKind::Hex;
    if (hasSuffix(path, ".bin")) return InputKind::Binary;
    return InputKind::Assembly;
}

std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// One 8-digit hex word per line (Assembler::writeHexFile format); "0x" tolerated.
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

// Big-endian 4-byte words (Assembler::writeBinaryFile format).
bool readBinaryWords(const std::string& path, std::vector<uint32_t>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;

    unsigned char bytes[4];
    while (true) {
        in.read(reinterpret_cast<char*>(bytes), 4);
        const std::streamsize got = in.gcount();
        if (got == 0) break;
        if (got != 4) return false;  // trailing partial word
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
    bool step_mode = false;
    bool decode_listing = false;
    bool dump_mem = false;
    uint32_t mem_start = 0;
    uint32_t mem_count = 0;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "-4" || arg == "--4stage") {
            variant = risc201::PipelineVariant::FourStage;
        } else if (arg == "-6" || arg == "--6stage") {
            variant = risc201::PipelineVariant::SixStage;
        } else if (arg == "-s" || arg == "--step") {
            step_mode = true;
        } else if (arg == "-d" || arg == "--decode") {
            decode_listing = true;
        } else if (arg == "-m" || arg == "--mem") {
            if (i + 1 >= argc || !parseRange(argv[++i], mem_start, mem_count)) {
                std::cerr << "error: --mem expects START:COUNT (e.g. 0:16)\n";
                return 1;
            }
            dump_mem = true;
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

    if (input_path.empty()) {
        printUsage(argv[0]);
        return 1;
    }

    const InputKind kind = kindFromPath(input_path);
    std::vector<uint32_t> words;
    risc201::Cpu cpu(variant);

    // --- load -------------------------------------------------------------
    if (kind == InputKind::Assembly) {
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
        const std::vector<risc201::Instruction> program = assembler.assemblePass2();

        for (const auto& warn : assembler.warnings()) {
            std::cerr << input_path << ":" << warn.line_number
                      << ": warning: " << warn.message << "\n";
        }
        if (!assembler.errors().empty()) {
            for (const auto& err : assembler.errors()) {
                std::cerr << input_path << ":" << err.line_number
                          << ": error: " << err.message << "\n";
            }
            return 1;
        }

        std::cout << "Assembled " << program.size() << " instruction(s) from "
                  << input_path << "\n";

        for (const auto& instr : program) {
            words.push_back(assembler.encodeInstruction(instr));
        }
        cpu.loadWords(words);
    } else {
        const bool ok = (kind == InputKind::Hex)
            ? readHexWords(input_path, words)
            : readBinaryWords(input_path, words);
        if (!ok) {
            std::cerr << "error: failed to parse encoded image " << input_path << "\n";
            return 1;
        }
        std::cout << "Loaded " << words.size() << " encoded word(s) from "
                  << input_path << "\n";

        // The processor decodes the raw words on its own.
        cpu.loadWords(words);
    }

    if (decode_listing) {
        std::cout << "\nDecoded program (" << words.size() << " instruction(s)):\n";
        for (size_t i = 0; i < words.size(); ++i) {
            const risc201::Instruction instr = risc201::decodeInstruction(words[i]);
            std::cout << "  [" << std::setw(4) << i << "]  0x" << std::hex
                      << std::uppercase << std::setfill('0') << std::setw(8) << words[i]
                      << std::dec << std::setfill(' ')
                      << "  " << formatInstruction(instr) << "\n";
        }
    }

    // --- run --------------------------------------------------------------

    if (variant == risc201::PipelineVariant::SixStage) {
        std::cerr << "note: 6-stage pipeline is not modelled yet; running 4-stage\n";
    }

    if (step_mode) {
        uint64_t steps = 0;
        while (!cpu.state().halted && steps < kMaxStepSteps) {
            std::cout << "--- step @ PC=" << cpu.state().pc << " ---\n";
            cpu.step();
            printRegisters(cpu.state());
            ++steps;
        }
    } else {
        cpu.run();
    }

    // --- report -----------------------------------------------------------
    std::cout << "\nHalted: " << (cpu.state().halted ? "yes" : "no")
              << "   retired " << cpu.retired() << " instruction(s)"
              << "   in " << cpu.cycles() << " cycle(s)\n";

    if (cpu.state().pending_exception) {
        std::cout << "EXCEPTION: "
                  << exceptionName(*cpu.state().pending_exception) << "\n";
    }

    std::cout << "\nFinal register file:\n";
    printRegisters(cpu.state());

    if (dump_mem) {
        std::cout << "\nData memory [" << mem_start << ".."
                  << (mem_start + mem_count) << "):\n";
        for (uint32_t a = mem_start; a < mem_start + mem_count; ++a) {
            if (a >= cpu.memory().size()) break;
            std::cout << "MEM[" << std::setw(5) << a << "] = "
                      << std::setw(12) << cpu.memory()[a] << "\n";
        }
    }

    return 0;
}
