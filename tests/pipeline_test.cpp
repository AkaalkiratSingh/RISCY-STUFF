#include "assembler/assembler.h"
#include "simulator_core/cpu.h"

#include <cstdint>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

int failures = 0;

#define CHECK(cond) do {                                                   \
    if (!(cond)) {                                                         \
        std::cerr << "  FAIL line " << __LINE__ << ": " #cond << "\n";     \
        ++failures;                                                        \
    }                                                                      \
} while (0)

#define CHECK_EQ(actual, expected) do {                                    \
    const auto got_  = (actual);                                           \
    const auto want_ = (expected);                                         \
    if (!(got_ == want_)) {                                                \
        std::cerr << "  FAIL line " << __LINE__ << ": " #actual            \
                  << " == " #expected << "\n    got  " << got_             \
                  << "\n    want " << want_ << "\n";                       \
        ++failures;                                                        \
    }                                                                      \
} while (0)

void section(const char* name) { std::cout << name << "\n"; }

using namespace risc201;

std::vector<uint32_t> assembleWords(const std::vector<std::string>& source) {
    Assembler asmblr;
    if (!asmblr.assemblePass1(source)) {
        std::cerr << "  FAIL: assembly (pass 1) failed\n";
        ++failures;
    }
    std::vector<uint32_t> words;
    for (const auto& instr : asmblr.assemblePass2()) {
        words.push_back(asmblr.encodeInstruction(instr));
    }
    return words;
}

Cpu runOn(PipelineVariant variant,
          const std::vector<std::string>& source,
          const std::map<uint32_t, int32_t>& memory = {},
          uint32_t words = kDefaultMemoryWords,
          bool forwarding = true) {
    Cpu cpu(variant, words);
    cpu.setForwarding(forwarding);
    for (const auto& [addr, value] : memory) cpu.memory()[addr] = value;
    cpu.loadWords(assembleWords(source));
    cpu.run();
    return cpu;
}

std::string snapshot(const Cpu& cpu) {
    std::ostringstream os;
    for (int i = 0; i < kNumGPRegisters; ++i) {
        os << 'R' << i << '=' << cpu.state().registers[static_cast<size_t>(i)] << ' ';
    }
    os << "PC=" << cpu.state().pc << " SP=" << cpu.state().sp
       << " halted=" << cpu.state().halted
       << " exc=" << static_cast<int>(cpu.state().pending_exception.value_or(ExceptionCode::NONE))
       << " retired=" << cpu.retired();
    return os.str();
}

struct Case {
    const char* name;
    std::vector<std::string> source;
    std::map<uint32_t, int32_t> memory;
    uint32_t words = kDefaultMemoryWords;
};

Case mk(const char* name, std::vector<std::string> source,
        std::map<uint32_t, int32_t> memory = {},
        uint32_t words = kDefaultMemoryWords) {
    return Case{name, std::move(source), std::move(memory), words};
}

} // namespace

int main() {
    const std::vector<Case> cases = {
        mk("alu chain",              {"ADD R1, R0, 5", "ADD R2, R1, 5", "SUB R3, R2, R1", "HALT"}),
        mk("logic ops",              {"ADD R1, R0, 12", "AND R2, R1, 10", "XOR R3, R1, R2", "HALT"}),
        mk("load/store",             {"LOAD R1, 0[R0]", "STORE R1, 5[R0]", "LOAD R2, 5[R0]", "HALT"}, {{0, 42}}),
        mk("branch taken",           {"SUB R1, R1, R1", "BEQ skip", "ADD R2, R0, 99", "skip:", "HALT"}),
        mk("branch not taken",       {"LOAD R1, 0[R0]", "BEQ skip", "ADD R2, R0, 99", "skip:", "HALT"}, {{0, 9}}),
        mk("jump skips fallthrough", {"LOAD R1, 0[R0]", "JMP done", "LOAD R2, 1[R0]", "done:", "HALT"}, {{0, 1}, {1, 2}}),
        mk("push/pop round-trip",    {"ADD R1, R0, 42", "PUSH R1", "POP R2", "HALT"}),
        mk("push three, pop three",  {"ADD R1, R0, 10", "ADD R2, R0, 20", "ADD R3, R0, 30",
                                      "PUSH R1", "PUSH R2", "PUSH R3",
                                      "POP R4", "POP R5", "POP R6", "HALT"}),
        mk("call then ret",          {"ADD R1, R0, 7", "CALL sub", "ADD R2, R0, 1", "HALT",
                                      "sub:", "ADD R3, R0, 5", "RET"}),
        mk("recursion sum(5)",       {"ADD R1, R0, 5", "CALL sum", "HALT",
                                      "sum:", "SUB R2, R1, R0", "BEQ zero", "PUSH R1", "SUB R1, R1, 1",
                                      "CALL sum", "POP R2", "ADD R1, R1, R2", "RET",
                                      "zero:", "ADD R1, R0, 0", "RET"}),
        mk("bare ret underflows",    {"RET", "HALT"}),
        mk("pop from empty",         {"POP R1", "HALT"}),
        mk("stack overflow",         {"PUSH R1", "PUSH R1", "HALT"}, {}, 1),
        mk("runs off the end",       {"ADD R1, R0, R0"}),
        mk("bad data address",       {"LOAD R1, 99999[R0]", "HALT"}),
    };

    section("Pipeline: both variants produce the same architectural result");
    for (const Case& c : cases) {
        const uint32_t words = c.words == 0 ? kDefaultMemoryWords : c.words;
        const Cpu four = runOn(PipelineVariant::FourStage, c.source, c.memory, words);
        const Cpu six  = runOn(PipelineVariant::SixStage,  c.source, c.memory, words);

        const std::string a = snapshot(four);
        const std::string b = snapshot(six);

        if (a != b) {
            ++failures;
            std::cerr << "  FAIL " << c.name << "\n"
                      << "    4-stage: " << a << "\n"
                      << "    6-stage: " << b << "\n";
            continue;
        }
        std::cout << "  ok    " << c.name << "\n"
                  << "          " << a << "\n"
                  << "          cycles 4-stage=" << four.cycles()
                  << "  6-stage=" << six.cycles() << "\n";
    }

    section("Pipeline: identical results for the two hazard shapes");
    for (PipelineVariant v : {PipelineVariant::FourStage, PipelineVariant::SixStage}) {
        const std::vector<std::string> taken = {
            "ADD R1, R0, 1", "SUB R1, R1, R1", "BEQ skip", "ADD R2, R0, 99", "skip:", "HALT"};
        const std::vector<std::string> not_taken = {
            "ADD R1, R0, 1", "ADD R1, R1, R1", "BEQ skip", "ADD R2, R0, 99", "skip:", "HALT"};

        const Cpu t = runOn(v, taken);
        const Cpu n = runOn(v, not_taken);

        CHECK_EQ(t.state().registers[static_cast<size_t>(Register::R2)], 0);
        CHECK_EQ(n.state().registers[static_cast<size_t>(Register::R2)], 99);
        CHECK(t.state().halted && n.state().halted);
    }

    section("Pipeline: forwarding never changes an architectural result");
    for (PipelineVariant v : {PipelineVariant::FourStage, PipelineVariant::SixStage}) {
        int mismatches = 0;
        for (const Case& c : cases) {
            const uint32_t words = c.words == 0 ? kDefaultMemoryWords : c.words;
            const Cpu on  = runOn(v, c.source, c.memory, words, true);
            const Cpu off = runOn(v, c.source, c.memory, words, false);
            if (snapshot(on) != snapshot(off)) {
                ++mismatches;
                ++failures;
                std::cerr << "  FAIL " << c.name << "\n"
                          << "    bypassing: " << snapshot(on) << "\n"
                          << "    stalling:  " << snapshot(off) << "\n";
            }
        }
        if (mismatches == 0) {
            std::cout << "  ok    " << variantName(v)
                      << ": every program matches with and without forwarding\n";
        }
    }

    section("Pipeline: forwarding removes the stall on an ALU chain");
    for (PipelineVariant v : {PipelineVariant::FourStage, PipelineVariant::SixStage}) {
        const uint64_t ideal = (v == PipelineVariant::FourStage ? 4u : 6u) + 3u - 1u;

        const Cpu on  = runOn(v, {"ADD R1, R0, 5", "ADD R2, R1, 5", "HALT"}, {}, kDefaultMemoryWords, true);
        const Cpu off = runOn(v, {"ADD R1, R0, 5", "ADD R2, R1, 5", "HALT"}, {}, kDefaultMemoryWords, false);

        CHECK_EQ(on.cycles(), ideal);
        CHECK(off.cycles() > ideal);
        CHECK_EQ(on.state().registers[static_cast<size_t>(Register::R2)], 10);
        CHECK_EQ(off.state().registers[static_cast<size_t>(Register::R2)], 10);
    }

    section("Pipeline: a load-use dependency costs one bubble even with forwarding");
    for (PipelineVariant v : {PipelineVariant::FourStage, PipelineVariant::SixStage}) {
        const uint64_t ideal = (v == PipelineVariant::FourStage ? 4u : 6u) + 3u - 1u;

        const Cpu on  = runOn(v, {"LOAD R1, 0[R0]", "ADD R2, R1, R0", "HALT"}, {{0, 9}}, kDefaultMemoryWords, true);
        const Cpu off = runOn(v, {"LOAD R1, 0[R0]", "ADD R2, R1, R0", "HALT"}, {{0, 9}}, kDefaultMemoryWords, false);

        CHECK_EQ(on.state().registers[static_cast<size_t>(Register::R2)], 9);
        CHECK_EQ(off.state().registers[static_cast<size_t>(Register::R2)], 9);
        CHECK(on.cycles() > ideal);             // the value is not ready until MEM completes
        CHECK(on.cycles() <= off.cycles());
    }

    section("Pipeline: a store's data source is bypassed, not stalled");
    for (PipelineVariant v : {PipelineVariant::FourStage, PipelineVariant::SixStage}) {
        const uint64_t ideal = (v == PipelineVariant::FourStage ? 4u : 6u) + 4u - 1u;

        const std::vector<std::string> program = {
            "ADD R2, R0, 7", "STORE R2, 4[R0]", "LOAD R3, 4[R0]", "HALT"};

        const Cpu on  = runOn(v, program, {}, kDefaultMemoryWords, true);
        const Cpu off = runOn(v, program, {}, kDefaultMemoryWords, false);

        CHECK_EQ(on.memory()[4], 7);            // the freshly computed R2 reached memory
        CHECK_EQ(off.memory()[4], 7);
        CHECK_EQ(on.state().registers[static_cast<size_t>(Register::R3)], 7);
        CHECK_EQ(on.cycles(), ideal);           // bypassed, so nothing stalls
        CHECK(off.cycles() > ideal);            // without bypassing the store waits
    }

    if (failures == 0) {
        std::cout << "\npipeline_test: all checks passed\n";
        return 0;
    }
    std::cerr << "\npipeline_test: " << failures << " check(s) failed\n";
    return 1;
}
