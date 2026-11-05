#include "simulator_core/cpu.h"
#include "tui/program_loader.h"
#include "tui/tui.h"

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
        << "      --horizontal use the horizontal microcode ROM (default)\n"
        << "      --vertical   use the vertical microcode ROM (same behaviour, narrower words)\n"
        << "      --trace      open the hazard timeline panel at startup\n"
        << "  -h, --help      show this message\n";
}

} // namespace

int main(int argc, char** argv) {
    std::string input_path;
    risc201::PipelineVariant variant = risc201::PipelineVariant::FourStage;
    bool forwarding = true;
    risc201::MicrocodeEncoding encoding = risc201::MicrocodeEncoding::Horizontal;
    bool trace_view = false;

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
        } else if (arg == "--vertical") {
            encoding = risc201::MicrocodeEncoding::Vertical;
        } else if (arg == "--horizontal") {
            encoding = risc201::MicrocodeEncoding::Horizontal;
        } else if (arg == "--trace") {
            trace_view = true;
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
        risc201::ProgramFile file = risc201::readProgramFile(input_path);
        if (!file.error.empty()) {
            std::cerr << "error: " << file.error << "\n";
            return 1;
        }
        raw_lines = std::move(file.lines);
        if (file.kind == risc201::SourceKind::Assembly) {
            risc201::AssemblyResult assembled = risc201::assembleLines(raw_lines);
            if (!assembled.errors.empty()) {
                for (const auto& err : assembled.errors) {
                    std::cerr << input_path << ":" << err.line_number
                              << ": error: " << err.message << "\n";
                }
                return 1;
            }
            words = std::move(assembled.words);
        } else {
            words = std::move(file.words);
        }
    }

    risc201::Cpu cpu(variant, risc201::kDefaultMemoryWords, encoding);
    cpu.setForwarding(forwarding);
    if (!words.empty()) {
        cpu.loadWords(words);
    }

    risc201::Tui tui(cpu, raw_lines, words, input_path, trace_view);
    tui.run();

    return 0;
}
