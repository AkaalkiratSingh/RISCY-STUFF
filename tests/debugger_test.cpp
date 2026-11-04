// Debugger tests: breakpoints stop before execution, conditions, watchpoints,
// run control, exceptions, cycle limit, reset (including the literal pool) and
// the inspection helpers. Every program runs on both pipeline variants.

#include "assembler/assembler.h"
#include "assembler/preprocessor.h"
#include "simulator_core/debugger.h"

#include <cstdint>
#include <iostream>
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

// Assembles `source` with the implicit "JMP .main" removed, so pc 0 is the first
// line of the program. The literal section, if any, is appended.
std::vector<uint32_t> assembleWords(const std::vector<std::string>& source) {
    std::vector<std::string> lines = source;
    lines.insert(lines.begin(), ".main:");

    Preprocessor preprocessor;
    PreprocessResult expanded = preprocessor.expand(lines);
    for (const auto& err : expanded.errors) {
        std::cerr << "  FAIL: preprocess: " << err.message << "\n";
        ++failures;
    }

    Assembler assembler;
    if (!assembler.assemblePass1(expanded.lines)) {
        for (const auto& e : assembler.errors()) {
            std::cerr << "  FAIL: assemble line " << e.line_number << ": " << e.message << "\n";
        }
        ++failures;
    }

    std::vector<uint32_t> words;
    for (const auto& instr : assembler.assemblePass2()) {
        words.push_back(assembler.encodeInstruction(instr));
    }
    if (!words.empty()) words.erase(words.begin());
    for (uint32_t w : assembler.literalSection()) words.push_back(w);
    return words;
}

// pc: 0 MOV R1,0 | 1 MOV R2,3 | 2 ADD R1,R1,1 | 3 STORE R1,10[R0] | 4 SUB R2,R2,1
//     5 BEQ done | 6 JMP loop | 7 HALT.   The loop runs 3 times; R1 ends at 3 and
//     mem[10] receives 1, 2, 3 in turn.
const std::vector<std::string> kLoop = {
    "MOV R1, 0",
    "MOV R2, 3",
    "loop:",
    "ADD R1, R1, 1",
    "STORE R1, 10[R0]",
    "SUB R2, R2, 1",
    "BEQ done",
    "JMP loop",
    "done:",
    "HALT",
};

constexpr PipelineVariant kVariants[] = {PipelineVariant::FourStage, PipelineVariant::SixStage};

void testStepping(PipelineVariant v) {
    section("Debugger: stepCycle and stepInstruction");
    Cpu cpu(v);
    cpu.loadWords(assembleWords(kLoop));
    Debugger dbg(cpu);

    CHECK_EQ(dbg.nextPc(), 0u);

    StopInfo c = dbg.stepCycle();
    CHECK(c.reason == StopReason::StepDone);
    CHECK_EQ(cpu.cycles(), uint64_t{1});

    StopInfo i = dbg.stepInstruction();
    CHECK(i.reason == StopReason::StepDone);
    CHECK(cpu.retired() >= 1);

    // Step instruction by instruction through the whole program.
    int guard = 0;
    while (!dbg.halted() && guard++ < 200) dbg.stepInstruction();
    CHECK(dbg.halted());
    CHECK_EQ(dbg.registers()[1], 3);
}

void testBreakpointStopsBeforeExecution(PipelineVariant v) {
    section("Debugger: a breakpoint stops before the instruction retires");
    Cpu cpu(v);
    cpu.loadWords(assembleWords(kLoop));
    Debugger dbg(cpu);
    dbg.addBreakpoint(3);   // the STORE

    StopInfo s = dbg.continueExecution();
    CHECK(s.reason == StopReason::Breakpoint);
    CHECK_EQ(s.pc, 3u);
    CHECK_EQ(dbg.nextPc(), 3u);
    CHECK_EQ(cpu.retired(), uint64_t{3});  // MOV, MOV and the first ADD
    CHECK_EQ(dbg.registers()[1], 1);       // the ADD before it has retired
    CHECK_EQ(dbg.registers()[2], 3);       // the SUB behind it has not
    CHECK_EQ(dbg.breakpoints().at(3).hits, uint64_t{1});
    CHECK(cpu.pipelineEmpty());            // the barrier drains everything before pc 3
    CHECK_EQ(cpu.fetchPc(), 3u);
    for (const auto& stage : cpu.stages()) CHECK(!stage.valid);

    // Resuming runs the STORE instead of stopping on it again, then comes back
    // around the loop to the same breakpoint.
    s = dbg.continueExecution();
    CHECK(s.reason == StopReason::Breakpoint);
    CHECK_EQ(s.pc, 3u);
    CHECK_EQ(cpu.retired(), uint64_t{8});  // the first lap's STORE..JMP, then ADD
    CHECK_EQ(dbg.registers()[1], 2);
    CHECK_EQ(dbg.registers()[2], 2);
    CHECK(cpu.memory()[10] >= 1);          // the first lap's store has landed
    CHECK_EQ(dbg.breakpoints().at(3).hits, uint64_t{2});
}

void testStepOntoBreakpoint(PipelineVariant v) {
    section("Debugger: stepping onto a breakpoint reports it");
    Cpu cpu(v);
    cpu.loadWords(assembleWords(kLoop));
    Debugger dbg(cpu);
    dbg.addBreakpoint(2);

    // Fetching pc 2 is the barrier. The step drains pc 0 and pc 1 first, then stops
    // with an empty pipeline so resuming starts fresh at pc 2.
    StopInfo s = dbg.stepInstruction();
    CHECK(s.reason == StopReason::Breakpoint);
    CHECK_EQ(s.pc, 2u);
    CHECK_EQ(cpu.retired(), uint64_t{2});
    CHECK(cpu.pipelineEmpty());
    CHECK_EQ(cpu.fetchPc(), 2u);

    // Resuming executes pc 2 once and continues to pc 3.
    s = dbg.stepInstruction();
    CHECK(s.reason == StopReason::StepDone);
    CHECK_EQ(s.pc, 3u);
    CHECK_EQ(cpu.retired(), uint64_t{3});
}

void testBreakpointOnSkippedPath(PipelineVariant v) {
    section("Debugger: a branch that skips the breakpoint does not stop");
    Cpu cpu(v);
    cpu.loadWords(assembleWords({"ADD R1, R0, 1", "JMP skip", "NOP", "skip:", "HALT"}));
    Debugger dbg(cpu);
    dbg.addBreakpoint(2);                   // the NOP on the not-taken path

    StopInfo s = dbg.continueExecution();
    CHECK(s.reason == StopReason::Halted);
    CHECK_EQ(dbg.breakpoints().at(2).hits, uint64_t{0});
}

void testConditionalBreakpoint(PipelineVariant v) {
    section("Debugger: conditional breakpoint");
    Cpu cpu(v);
    cpu.loadWords(assembleWords(kLoop));
    Debugger dbg(cpu);
    dbg.addBreakpoint(2, Condition{Register::R1, CompareOp::Eq, 2});

    // pc 2 is reached with R1 = 0, 1, 2; only the third arrival matches.
    StopInfo s = dbg.continueExecution();
    CHECK(s.reason == StopReason::Breakpoint);
    CHECK_EQ(s.pc, 2u);
    CHECK_EQ(dbg.registers()[1], 2);
    CHECK_EQ(dbg.breakpoints().at(2).hits, uint64_t{1});

    s = dbg.continueExecution();
    CHECK(s.reason == StopReason::Halted);
}

void testBreakpointManagement(PipelineVariant v) {
    section("Debugger: toggle, disable, remove, clear");
    Cpu cpu(v);
    cpu.loadWords(assembleWords(kLoop));
    Debugger dbg(cpu);

    CHECK(dbg.toggleBreakpoint(3));
    CHECK(dbg.hasBreakpoint(3));
    CHECK(!dbg.toggleBreakpoint(3));
    CHECK(!dbg.hasBreakpoint(3));

    dbg.addBreakpoint(3);
    CHECK(dbg.enableBreakpoint(3, false));
    CHECK(!dbg.enableBreakpoint(99, false));
    StopInfo s = dbg.continueExecution();
    CHECK(s.reason == StopReason::Halted);          // disabled, so it never stops

    dbg.reset();
    CHECK(dbg.enableBreakpoint(3, true));
    s = dbg.continueExecution();
    CHECK(s.reason == StopReason::Breakpoint);

    CHECK(dbg.removeBreakpoint(3));
    CHECK(!dbg.removeBreakpoint(3));
    dbg.addBreakpoint(1);
    dbg.addBreakpoint(2);
    dbg.clearBreakpoints();
    CHECK(dbg.breakpoints().empty());
}

void testRunUntil(PipelineVariant v) {
    section("Debugger: runUntil");
    Cpu cpu(v);
    cpu.loadWords(assembleWords(kLoop));
    Debugger dbg(cpu);

    StopInfo s = dbg.runUntil(5);
    CHECK(s.reason == StopReason::TargetReached);
    CHECK_EQ(s.pc, 5u);
    CHECK(dbg.breakpoints().empty());               // leaves no breakpoint behind

    // A later continue is not stopped by the old target.
    s = dbg.continueExecution();
    CHECK(s.reason == StopReason::Halted);
}

void testMemoryWatchpoint(PipelineVariant v) {
    section("Debugger: memory watchpoint");
    Cpu cpu(v);
    cpu.loadWords(assembleWords(kLoop));
    Debugger dbg(cpu);

    const int id = dbg.addMemoryWatchpoint(10);
    CHECK(id > 0);
    CHECK_EQ(dbg.addMemoryWatchpoint(1u << 30), -1);

    for (int expected = 1; expected <= 3; ++expected) {
        StopInfo s = dbg.continueExecution();
        CHECK(s.reason == StopReason::Watchpoint);
        CHECK_EQ(s.watch_hits.size(), size_t{1});
        if (s.watch_hits.size() == 1) {
            CHECK_EQ(s.watch_hits[0].id, id);
            CHECK_EQ(s.watch_hits[0].old_value, expected - 1);
            CHECK_EQ(s.watch_hits[0].new_value, expected);
        }
        CHECK_EQ(cpu.memory()[10], expected);
    }
    CHECK_EQ(dbg.watchpoints().at(id).hits, uint64_t{3});

    StopInfo s = dbg.continueExecution();
    CHECK(s.reason == StopReason::Halted);
}

void testRegisterWatchpointAndOutsideEdits(PipelineVariant v) {
    section("Debugger: register watchpoint ignores edits made between steps");
    Cpu cpu(v);
    cpu.loadWords(assembleWords(kLoop));
    Debugger dbg(cpu);

    const int id = dbg.addRegisterWatchpoint(Register::R2);
    cpu.memory()[10] = 99;                                   // an outside edit
    const int mem_id = dbg.addMemoryWatchpoint(10);
    cpu.memory()[10] = 42;                                   // another one

    StopInfo s = dbg.continueExecution();
    CHECK(s.reason == StopReason::Watchpoint);
    bool saw_r2 = false;
    for (const auto& h : s.watch_hits) {
        CHECK(h.id != mem_id);        // the outside edit was not reported as a hit
        if (h.id == id) saw_r2 = true;
    }
    CHECK(saw_r2);

    CHECK(dbg.enableWatchpoint(id, false));
    CHECK(dbg.removeWatchpoint(mem_id));
    CHECK(!dbg.removeWatchpoint(mem_id));
    dbg.clearWatchpoints();
    CHECK(dbg.watchpoints().empty());
}

void testBreakpointAndWatchpointSameStop(PipelineVariant v) {
    section("Debugger: watch hits are reported even when a breakpoint wins");
    Cpu cpu(v);
    cpu.loadWords(assembleWords(kLoop));
    Debugger dbg(cpu);
    // R1 changes when pc 2 (the ADD) retires, which is exactly when pc 3 becomes
    // next in line, so both fire on the same clock.
    dbg.addRegisterWatchpoint(Register::R1);
    dbg.addBreakpoint(3);

    // First stops: R1 0 -> 1 as ADD retires, with the breakpoint at pc 3 next.
    StopInfo s = dbg.continueExecution();
    CHECK(s.reason == StopReason::Breakpoint);
    CHECK_EQ(s.pc, 3u);
    CHECK_EQ(s.watch_hits.size(), size_t{1});
}

void testHaltAndRepeatedCalls(PipelineVariant v) {
    section("Debugger: halting, and run calls after halt");
    Cpu cpu(v);
    cpu.loadWords(assembleWords(kLoop));
    Debugger dbg(cpu);

    StopInfo s = dbg.continueExecution();
    CHECK(s.reason == StopReason::Halted);
    CHECK(!s.exception.has_value());
    CHECK(dbg.halted());
    CHECK_EQ(dbg.registers()[1], 3);
    CHECK_EQ(cpu.memory()[10], 3);

    const uint64_t cycles = cpu.cycles();
    CHECK(dbg.continueExecution().reason == StopReason::Halted);
    CHECK(dbg.stepInstruction().reason == StopReason::Halted);
    CHECK(dbg.stepCycle().reason == StopReason::Halted);
    CHECK_EQ(cpu.cycles(), cycles);                 // nothing advanced
}

void testException(PipelineVariant v) {
    section("Debugger: an exception is reported with its code");
    Cpu cpu(v);
    cpu.loadWords(assembleWords({"MOV R1, 5", "LOAD R2, 5000[R0]", "HALT"}));
    Debugger dbg(cpu);

    StopInfo s = dbg.continueExecution();
    CHECK(s.reason == StopReason::Exception);
    CHECK(s.exception.has_value());
    CHECK(s.exception.has_value() && *s.exception == ExceptionCode::INVALID_MEMORY_ACCESS);
}

void testCycleLimit(PipelineVariant v) {
    section("Debugger: cycle limit stops an infinite loop");
    Cpu cpu(v);
    cpu.loadWords(assembleWords({"spin:", "JMP spin"}));
    Debugger dbg(cpu);

    StopInfo s = dbg.continueExecution(50);
    CHECK(s.reason == StopReason::CycleLimit);
    CHECK_EQ(cpu.cycles(), uint64_t{50});
    CHECK(!dbg.halted());

    s = dbg.continueExecution(0);                   // always runs at least a clock
    CHECK(s.reason == StopReason::CycleLimit);
    CHECK_EQ(cpu.cycles(), uint64_t{51});
}

void testReset(PipelineVariant v) {
    section("Debugger: reset reloads the program and the literal pool");
    Cpu cpu(v);
    cpu.loadWords(assembleWords({
        "LOAD R1, pi",
        "STORE R1, 10[R0]",
        "HALT",
        ".literals:",
        "pi: 3.14f",
    }));
    Debugger dbg(cpu);
    dbg.addBreakpoint(1);
    const int wid = dbg.addMemoryWatchpoint(10);

    StopInfo s = dbg.continueExecution();
    CHECK(s.reason == StopReason::Breakpoint);
    s = dbg.continueExecution();
    CHECK(s.reason == StopReason::Watchpoint);
    s = dbg.continueExecution();
    CHECK(s.reason == StopReason::Halted);
    CHECK_EQ(static_cast<uint32_t>(dbg.registers()[1]), 0x4048F5C3u);
    CHECK_EQ(static_cast<uint32_t>(cpu.memory()[10]), 0x4048F5C3u);

    dbg.reset();
    CHECK(!dbg.halted());
    CHECK_EQ(dbg.registers()[1], 0);
    CHECK_EQ(cpu.memory()[10], 0);                              // data cleared
    CHECK_EQ(static_cast<uint32_t>(cpu.memory()[0]), 0x4048F5C3u);   // pool restored
    CHECK_EQ(cpu.cycles(), uint64_t{0});
    CHECK_EQ(cpu.retired(), uint64_t{0});
    CHECK_EQ(dbg.nextPc(), 0u);
    CHECK(dbg.hasBreakpoint(1));                                // kept
    CHECK_EQ(dbg.breakpoints().at(1).hits, uint64_t{0});        // but zeroed
    CHECK_EQ(dbg.watchpoints().at(wid).hits, uint64_t{0});

    // The second run behaves like the first.
    s = dbg.continueExecution();
    CHECK(s.reason == StopReason::Breakpoint);
    s = dbg.continueExecution();
    CHECK(s.reason == StopReason::Watchpoint);
    s = dbg.continueExecution();
    CHECK(s.reason == StopReason::Halted);
    CHECK_EQ(static_cast<uint32_t>(dbg.registers()[1]), 0x4048F5C3u);
}

void testInspection() {
    section("Debugger: readMemory and print helpers");
    Cpu cpu(PipelineVariant::FourStage);
    cpu.loadWords(assembleWords({"HALT"}));
    Debugger dbg(cpu);
    cpu.memory()[4] = 7;
    cpu.memory()[5] = -1;

    std::vector<int32_t> words = dbg.readMemory(4, 3);
    CHECK_EQ(words.size(), size_t{3});
    CHECK(words.size() == 3 && words[0] == 7 && words[1] == -1 && words[2] == 0);

    const uint32_t size = static_cast<uint32_t>(cpu.memory().size());
    CHECK_EQ(dbg.readMemory(size - 2, 10).size(), size_t{2});   // clamped
    CHECK(dbg.readMemory(size, 4).empty());
    CHECK(dbg.readMemory(size + 100, 4).empty());

    std::ostringstream mem;
    dbg.printMemory(4, 2, mem);
    CHECK(mem.str().find("0xFFFFFFFF") != std::string::npos);
    CHECK(mem.str().find("[   4]") != std::string::npos);

    std::ostringstream regs;
    dbg.printRegisters(regs);
    CHECK(regs.str().find("R0 = 0") != std::string::npos);
    CHECK(regs.str().find("R15 = ") != std::string::npos);

    CHECK(std::string(stopReasonName(StopReason::Breakpoint)) == "breakpoint");
}

}  // namespace

int main() {
    for (PipelineVariant v : kVariants) {
        std::cout << "== " << variantName(v) << " ==\n";
        testStepping(v);
        testBreakpointStopsBeforeExecution(v);
        testStepOntoBreakpoint(v);
        testBreakpointOnSkippedPath(v);
        testConditionalBreakpoint(v);
        testBreakpointManagement(v);
        testRunUntil(v);
        testMemoryWatchpoint(v);
        testRegisterWatchpointAndOutsideEdits(v);
        testBreakpointAndWatchpointSameStop(v);
        testHaltAndRepeatedCalls(v);
        testException(v);
        testCycleLimit(v);
        testReset(v);
    }
    testInspection();

    if (failures == 0) {
        std::cout << "\ndebugger_test: all checks passed\n";
        return 0;
    }
    std::cerr << "\ndebugger_test: " << failures << " check(s) failed\n";
    return 1;
}