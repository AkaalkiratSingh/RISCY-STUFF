// risc201-sim: assemble a source file, run it on the processor, and report
// the resulting machine state. Bare-bones CLI — see printUsage() for flags.

#include "assembler/assembler.h"
#include "simulator_core/cpu.h"

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
        << "usage: " << prog << " <program.asm> [options]\n"
        << "\n"
        << "Assemble the program, run it on the processor, and print the final\n"
        << "register file.\n"
        << "\n"
        << "options:\n"
        << "  -4, --4stage    select the 4-stage pipeline (default)\n"
        << "  -6, --6stage    select the 6-stage pipeline\n"
        << "  -s, --step      single-step, printing state after each instruction\n"
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

} // namespace

int main(int argc, char** argv) {
    std::string input_path;
    risc201::PipelineVariant variant = risc201::PipelineVariant::FourStage;
    bool step_mode = false;
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

    std::ifstream file(input_path);
    if (!file) {
        std::cerr << "error: cannot open " << input_path << "\n";
        return 1;
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(file, line)) lines.push_back(line);

    // --- assemble ---------------------------------------------------------
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

    // --- run --------------------------------------------------------------
    risc201::Cpu cpu(variant);
    cpu.loadProgram(program);

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
