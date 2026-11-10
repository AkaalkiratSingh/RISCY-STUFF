// Private experiment: enumerate the ten four-stage partitions of the six logical
// row stages (choose two of the five internal boundaries to merge) and compare
// them on the stress workloads. This target is only built with
// -DRISC201_PIPELINE_SWEEP=ON and is never linked into a user-facing binary.

#include "assembler/assembler.h"
#include "assembler/preprocessor.h"
#include "simulator_core/cpu.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using namespace risc201;

namespace {

constexpr int kStageCount = 6;
constexpr int kGroupCount = 4;
constexpr int kBoundaryCount = kStageCount - 1;
constexpr int kTimingRuns = 3;

const char* groupName(uint8_t mask) {
    switch (mask) {
    case 0x01: return "IF";
    case 0x02: return "ID";
    case 0x04: return "OF";
    case 0x08: return "EX";
    case 0x10: return "MEM";
    case 0x20: return "WB";
    case 0x03: return "IF+ID";
    case 0x06: return "ID+OF";
    case 0x0C: return "OF+EX";
    case 0x18: return "EX+MEM";
    case 0x30: return "MEM+WB";
    case 0x07: return "IF+ID+OF";
    case 0x0E: return "ID+OF+EX";
    case 0x1C: return "OF+EX+MEM";
    case 0x38: return "EX+MEM+WB";
    default:  return "?";
    }
}

StageSlot makeSlot(uint8_t mask) {
    return StageSlot{groupName(mask),
                     (mask & 0x01) != 0, (mask & 0x02) != 0, (mask & 0x04) != 0,
                     (mask & 0x08) != 0, (mask & 0x10) != 0, (mask & 0x20) != 0};
}

struct BuiltLayout {
    std::array<StageSlot, kGroupCount> slots{};
    StageLayout view{};
    uint8_t merges = 0;
    std::string label;
};

BuiltLayout build(uint8_t merges) {
    BuiltLayout out;
    out.merges = merges;

    int index = 0;
    uint8_t mask = 0x01;
    for (int stage = 1; stage < kStageCount; ++stage) {
        if (merges & (1u << (stage - 1))) {
            mask |= static_cast<uint8_t>(1u << stage);
        } else {
            out.slots[static_cast<size_t>(index++)] = makeSlot(mask);
            mask = static_cast<uint8_t>(1u << stage);
        }
    }
    out.slots[static_cast<size_t>(index++)] = makeSlot(mask);
    if (index != kGroupCount) {
        std::cerr << "internal error: mask 0x" << std::hex << static_cast<int>(merges)
                  << " produced " << std::dec << index << " groups\n";
        std::exit(2);
    }

    for (int i = 0; i < kGroupCount; ++i) {
        if (i) out.label += " -> ";
        out.label += out.slots[static_cast<size_t>(i)].name;
    }
    return out;
}

const std::vector<BuiltLayout>& layouts() {
    static const std::vector<BuiltLayout> all = [] {
        std::vector<BuiltLayout> built;
        built.reserve(kBoundaryCount * (kBoundaryCount - 1) / 2);
        for (int a = 0; a < kBoundaryCount; ++a) {
            for (int b = a + 1; b < kBoundaryCount; ++b) {
                built.push_back(build(static_cast<uint8_t>((1u << a) | (1u << b))));
            }
        }
        for (BuiltLayout& layout : built) {
            layout.view = StageLayout{layout.slots.data(), layout.slots.size()};
        }
        return built;
    }();
    return all;
}

int defaultIndex() {
    const std::vector<BuiltLayout>& all = layouts();
    for (size_t i = 0; i < all.size(); ++i) {
        if (all[i].merges == 0x11) return static_cast<int>(i);
    }
    std::cerr << "internal error: default layout missing\n";
    std::exit(2);
}

bool assemble(const fs::path& path, std::vector<uint32_t>& words) {
    std::ifstream file(path);
    if (!file) {
        std::cerr << "cannot open " << path << "\n";
        return false;
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(file, line)) lines.push_back(line);

    Preprocessor preprocessor;
    const PreprocessResult expanded = preprocessor.expand(lines);
    if (!expanded.errors.empty()) {
        std::cerr << path << ": " << expanded.errors.front().message << "\n";
        return false;
    }

    Assembler assembler;
    assembler.assemblePass1(expanded.lines);
    const std::vector<Instruction> program = assembler.assemblePass2();
    if (!assembler.errors().empty()) {
        std::cerr << path << ": " << assembler.errors().front().message << "\n";
        return false;
    }

    words.clear();
    for (const Instruction& instr : program) words.push_back(assembler.encodeInstruction(instr));
    for (uint32_t word : assembler.literalSection()) words.push_back(word);
    return true;
}

struct Snapshot {
    CpuState state;
    std::vector<int32_t> memory;
    PipelineTrace trace;
    uint64_t retired = 0;
    uint64_t cycles = 0;
};

Snapshot run(const std::vector<uint32_t>& words, const StageLayout& layout, bool forwarding) {
    Cpu cpu(layout);
    cpu.loadWords(words);
    cpu.setForwarding(forwarding);
    cpu.run();

    Snapshot snap;
    snap.state = cpu.state();
    snap.memory = cpu.memory();
    snap.trace = cpu.trace();
    snap.retired = cpu.retired();
    snap.cycles = cpu.cycles();
    return snap;
}

bool sameFlags(const Flags& a, const Flags& b) {
    return a.zero == b.zero && a.negative == b.negative && a.carry == b.carry &&
           a.overflow == b.overflow && a.equal == b.equal &&
           a.greaterThan == b.greaterThan;
}

bool sameArchitecture(const Snapshot& a, const Snapshot& b) {
    return a.state.registers == b.state.registers &&
           a.state.pc == b.state.pc &&
           a.state.sp == b.state.sp &&
           a.state.halted == b.state.halted &&
           sameFlags(a.state.flags, b.state.flags) &&
           a.state.pending_exception == b.state.pending_exception &&
           a.retired == b.retired &&
           a.memory == b.memory;
}

struct Row {
    std::string program;
    std::string layout;
    uint8_t merges = 0;
    bool forwarding = true;
    Snapshot snapshot;
    double milliseconds = 0.0;
    bool identical = false;
};

struct Timed {
    Snapshot snapshot;
    double milliseconds = 0.0;
    bool stable = true;
};

Timed timedRun(const std::vector<uint32_t>& words, const StageLayout& layout,
               bool forwarding, int runs) {
    Timed result;
    double best = 1e18;
    for (int i = 0; i < runs; ++i) {
        const auto started = std::chrono::steady_clock::now();
        Snapshot snap = run(words, layout, forwarding);
        const auto finished = std::chrono::steady_clock::now();
        const double ms =
            std::chrono::duration<double, std::milli>(finished - started).count();
        best = std::min(best, ms);
        if (i == 0) {
            result.snapshot = std::move(snap);
        } else if (!sameArchitecture(result.snapshot, snap)) {
            result.stable = false;
        }
    }
    result.milliseconds = best;
    return result;
}

std::vector<fs::path> workloads(const fs::path& directory) {
    std::vector<fs::path> programs;
    for (const fs::directory_entry& entry : fs::directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".asm") {
            programs.push_back(entry.path());
        }
    }
    std::sort(programs.begin(), programs.end());
    return programs;
}

std::string csvField(const std::string& text) {
    if (text.find_first_of(",\"") == std::string::npos) return text;
    std::string quoted = "\"";
    for (char c : text) {
        if (c == '"') quoted += '"';
        quoted += c;
    }
    quoted += '"';
    return quoted;
}

} // namespace

int main(int argc, char** argv) {
    fs::path directory = "processor_benchmark";
    std::string csv_path;
    int timing_runs = kTimingRuns;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--csv" && i + 1 < argc) {
            csv_path = argv[++i];
        } else if (arg == "--check") {
            csv_path.clear();
            timing_runs = 1;
        } else if (!arg.empty() && arg[0] != '-') {
            directory = arg;
        } else {
            std::cerr << "usage: " << argv[0]
                      << " [benchmark-dir] [--csv path] [--check]\n";
            return 2;
        }
    }

    const std::vector<fs::path> programs = workloads(directory);
    if (programs.empty()) {
        std::cerr << "no .asm workloads in " << directory << "\n";
        return 2;
    }

    const std::vector<BuiltLayout>& all = layouts();
    const int reference = defaultIndex();

    std::vector<Row> rows;
    bool ok = true;

    for (const fs::path& program : programs) {
        std::vector<uint32_t> words;
        if (!assemble(program, words)) return 1;

        for (bool forwarding : {true, false}) {
            const Timed baseline =
                timedRun(words, all[static_cast<size_t>(reference)].view, forwarding, timing_runs);
            if (!baseline.stable) {
                std::cerr << program << ": default layout is not deterministic\n";
                ok = false;
            }

            for (size_t i = 0; i < all.size(); ++i) {
                const Timed timed = i == static_cast<size_t>(reference)
                    ? baseline
                    : timedRun(words, all[i].view, forwarding, timing_runs);

                Row row;
                row.program = program.stem().string();
                row.layout = all[i].label;
                row.merges = all[i].merges;
                row.forwarding = forwarding;
                row.snapshot = timed.snapshot;
                row.milliseconds = timed.milliseconds;
                row.identical = timed.stable &&
                                sameArchitecture(baseline.snapshot, timed.snapshot);
                if (!row.identical) ok = false;
                rows.push_back(std::move(row));
            }

            std::cout << std::left << std::setw(24) << program.stem().string()
                      << (forwarding ? "forward" : "no-forward") << "\n";
            for (size_t i = 0; i < all.size(); ++i) {
                const Row& row = rows[rows.size() - all.size() + i];
                std::cout << "  " << std::setw(36) << row.layout
                          << " cycles " << std::setw(9) << row.snapshot.cycles
                          << " stalls " << std::setw(8) << row.snapshot.trace.stalls
                          << " flushes " << std::setw(7) << row.snapshot.trace.flushes
                          << " ms " << std::fixed << std::setprecision(2)
                          << std::setw(8) << row.milliseconds
                          << (row.identical ? "" : "  STATE MISMATCH") << "\n";
            }
        }
    }

    if (!csv_path.empty()) {
        std::ofstream csv(csv_path);
        if (!csv) {
            std::cerr << "cannot write " << csv_path << "\n";
            return 2;
        }
        csv << "program,layout,merges,forwarding,retired,cycles,stalls,flushes,"
               "flush_bubbles,squashed,faults,rows,identical,milliseconds\n";
        for (const Row& row : rows) {
            csv << csvField(row.program) << ',' << csvField(row.layout) << ','
                << "0x" << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<int>(row.merges) << std::dec << std::setfill(' ')
                << ',' << (row.forwarding ? "on" : "off")
                << ',' << row.snapshot.retired
                << ',' << row.snapshot.cycles
                << ',' << row.snapshot.trace.stalls
                << ',' << row.snapshot.trace.flushes
                << ',' << row.snapshot.trace.flush_bubbles
                << ',' << row.snapshot.trace.squashed
                << ',' << row.snapshot.trace.faults
                << ',' << row.snapshot.trace.rows
                << ',' << (row.identical ? "yes" : "no")
                << ',' << std::fixed << std::setprecision(3) << row.milliseconds << '\n';
        }
    }

    if (!ok) {
        std::cerr << "pipeline sweep: architectural mismatch between layouts\n";
        return 1;
    }
    std::cout << "\nall " << all.size() << " four-stage layouts agree on architectural state\n";
    return 0;
}
