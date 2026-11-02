#include "assembler/assembler.h"
#include "assembler/preprocessor.h"
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
constexpr uint64_t kMaxTraceCycles = 4'000;     // guard against a flooding trace

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
        << "      --trace         print the pipeline per clock, with hazard events\n"
        << "      --no-forward stall on every dependency instead of bypassing\n"
        << "      --horizontal use the horizontal microcode ROM (default)\n"
        << "      --vertical   use the vertical microcode ROM (same behaviour, narrower words)\n"
        << "  -h, --help      show this message\n";
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

enum class InputKind { Assembly, Hex, Binary };

InputKind kindFromPath(const std::string& path) {
    if (risc201::hasSuffix(path, ".hex")) return InputKind::Hex;
    if (risc201::hasSuffix(path, ".bin")) return InputKind::Binary;
    return InputKind::Assembly;
}

std::string slotText(const risc201::Latch& l) {
    if (!l.valid) return "-";
    const risc201::Instruction instr =
        l.decoded ? l.instr : risc201::decodeInstruction(l.word);
    return std::to_string(l.pc) + " " + risc201::opcodeName(instr.opcode);
}

void printTraceHeader(risc201::PipelineVariant variant) {
    const risc201::StageLayout layout = risc201::layoutFor(variant);
    std::cout << "  cycle |";
    for (size_t i = 0; i < layout.count; ++i) {
        std::cout << " " << std::right << std::setw(12) << layout[i].name << " |";
    }
    std::cout << "\n";
}

void printTraceLine(const risc201::Cpu& cpu, size_t& event_cursor) {
    const risc201::StageLayout layout = risc201::layoutFor(cpu.variant());
    const auto& stages = cpu.stages();

    std::cout << "  " << std::right << std::setw(5) << cpu.cycles() << " |";
    for (size_t i = 0; i < layout.count; ++i) {
        std::string text;
        if (i == 0) {
            text = cpu.fetching() ? "@" + std::to_string(cpu.fetchPc()) : "-";
        } else {
            text = slotText(stages[i - 1]);
        }
        std::cout << " " << std::left << std::setw(12) << text << " |";
    }

    const auto& events = cpu.trace().events;
    for (; event_cursor < events.size(); ++event_cursor) {
        const risc201::HazardEvent& e = events[event_cursor];
        std::cout << " " << risc201::hazardName(e.kind) << " pc=" << e.pc;
        if (e.kind == risc201::HazardKind::Flush) {
            std::cout << " -> " << e.target << " (depth " << e.depth
                      << ", squashed " << e.discarded << ")";
        }
    }
    std::cout << "\n";
}

} // namespace

int main(int argc, char** argv) {
    std::string input_path;
    risc201::PipelineVariant variant = risc201::PipelineVariant::FourStage;
    bool step_mode = false;
    bool decode_listing = false;
    bool dump_mem = false;
    bool forwarding = true;
    bool trace_mode = false;
    risc201::MicrocodeEncoding encoding = risc201::MicrocodeEncoding::Horizontal;
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
        } else if (arg == "--no-forward") {
            forwarding = false;
        } else if (arg == "--horizontal") {
            encoding = risc201::MicrocodeEncoding::Horizontal;
        } else if (arg == "--vertical") {
            encoding = risc201::MicrocodeEncoding::Vertical;
        } else if (arg == "--trace") {
            trace_mode = true;
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
    risc201::Cpu cpu(variant, risc201::kDefaultMemoryWords, encoding);
    cpu.setForwarding(forwarding);
    cpu.setTracing(trace_mode);

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

        risc201::Preprocessor preprocessor;
        risc201::PreprocessResult expanded = preprocessor.expand(lines);

        auto originalLine = [&](int n) {
            if (n >= 1 && n <= static_cast<int>(expanded.origin_line.size())) {
                return expanded.origin_line[static_cast<size_t>(n - 1)];
            }
            return n;
        };
        auto contextOf = [&](int n) -> std::string {
            if (n >= 1 && n <= static_cast<int>(expanded.origin_context.size()) &&
                !expanded.origin_context[static_cast<size_t>(n - 1)].empty()) {
                return " (" + expanded.origin_context[static_cast<size_t>(n - 1)] + ")";
            }
            return "";
        };

        for (const auto& err : expanded.errors) {
            std::cerr << input_path << ":" << err.line_number
                      << ": error: " << err.message << "\n";
        }
        if (!expanded.errors.empty()) return 1;

        risc201::Assembler assembler;
        assembler.assemblePass1(expanded.lines);
        const std::vector<risc201::Instruction> program = assembler.assemblePass2();

        for (const auto& warn : assembler.warnings()) {
            std::cerr << input_path << ":" << originalLine(warn.line_number)
                      << ": warning: " << warn.message << contextOf(warn.line_number) << "\n";
        }
        if (!assembler.errors().empty()) {
            for (const auto& err : assembler.errors()) {
                std::cerr << input_path << ":" << originalLine(err.line_number)
                          << ": error: " << err.message << contextOf(err.line_number) << "\n";
            }
            return 1;
        }

        std::cout << "Assembled " << program.size() << " instruction(s) from "
                  << input_path << "\n";

        for (const auto& instr : program) {
            words.push_back(assembler.encodeInstruction(instr));
        }
        for (uint32_t word : assembler.literalSection()) {
            words.push_back(word);
        }
        cpu.loadWords(words);
    } else {
        const bool ok = (kind == InputKind::Hex)
            ? risc201::readHexWords(input_path, words)
            : risc201::readBinaryWords(input_path, words);
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
        const auto& code = cpu.codeWords();
        std::cout << "\nDecoded program (" << code.size() << " instruction(s)):\n";
        for (size_t i = 0; i < code.size(); ++i) {
            const risc201::Instruction instr = risc201::decodeInstruction(code[i]);
            std::cout << "  [" << std::setw(4) << i << "]  0x" << std::hex
                      << std::uppercase << std::setfill('0') << std::setw(8) << code[i]
                      << std::dec << std::setfill(' ')
                      << "  " << risc201::formatInstruction(instr) << "\n";
        }
    }

    // --- run --------------------------------------------------------------
    if (trace_mode) {
        std::cout << "\n--- pipeline trace: " << risc201::variantName(variant)
                  << (forwarding ? " (forwarding)" : " (no forwarding)") << " ---\n";
        printTraceHeader(variant);
        size_t event_cursor = 0;
        uint64_t clock = 0;
        while (!cpu.state().halted && clock < kMaxTraceCycles) {
            cpu.step();
            ++clock;
            printTraceLine(cpu, event_cursor);
        }
        if (!cpu.state().halted) {
            std::cout << "  (trace stopped after " << kMaxTraceCycles << " cycles)\n";
        }
    } else if (step_mode) {
        uint64_t steps = 0;
        while (!cpu.state().halted && steps < kMaxStepSteps) {
            std::cout << "--- step @ PC=" << cpu.state().pc << " ---\n";
            cpu.stepInstruction();
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

    const risc201::PipelineTrace& hazards = cpu.trace();
    std::cout << "  pipeline: " << risc201::variantName(cpu.variant())
              << (cpu.forwarding() ? ", forwarding" : ", no forwarding")
              << "   stalls " << hazards.stalls
              << "   flushes " << hazards.flushes
              << " (" << hazards.flush_bubbles << " bubble(s), "
              << hazards.squashed << " squashed)"
              << "   faults " << hazards.faults << "\n";
    
    std::cout << "  control:  "
              << (cpu.encoding() == risc201::MicrocodeEncoding::Vertical ? "vertical" : "horizontal")
              << " microcode\n";

    if (cpu.state().pending_exception) {
        std::cout << "EXCEPTION: "
                  << risc201::exceptionName(*cpu.state().pending_exception) << "\n";
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
