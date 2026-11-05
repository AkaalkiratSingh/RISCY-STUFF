#pragma once

#include "simulator_core/cpu.h"

#include <array>
#include <cstdint>
#include <iosfwd>
#include <map>
#include <optional>
#include <vector>

namespace risc201 {

enum class CompareOp { Eq, Ne, Lt, Le, Gt, Ge };

struct Condition {
    Register reg = Register::R0;
    CompareOp op = CompareOp::Eq;
    int32_t value = 0;
};

struct Breakpoint {
    uint32_t pc = 0;
    bool enabled = true;
    std::optional<Condition> condition;
    uint64_t hits = 0;
};

enum class WatchKind { Memory, Register };

struct Watchpoint {
    int id = 0;
    WatchKind kind = WatchKind::Memory;
    uint32_t target = 0;
    bool enabled = true;
    int32_t last = 0;
    uint64_t hits = 0;
};

struct WatchHit {
    int id = 0;
    WatchKind kind = WatchKind::Memory;
    uint32_t target = 0;
    int32_t old_value = 0;
    int32_t new_value = 0;
};

enum class StopReason {
    StepDone,
    Breakpoint,
    Watchpoint,
    TargetReached,
    Halted,
    Exception,
    CycleLimit,
};

const char* stopReasonName(StopReason reason);

struct StopInfo {
    StopReason reason = StopReason::StepDone;
    uint32_t pc = 0;
    std::optional<ExceptionCode> exception;
    std::vector<WatchHit> watch_hits;
};

class Debugger {
public:
    static constexpr uint64_t kDefaultMaxCycles = 1'000'000;

    explicit Debugger(Cpu& cpu) : cpu_(cpu) {}

    void addBreakpoint(uint32_t pc, std::optional<Condition> condition = std::nullopt);
    bool removeBreakpoint(uint32_t pc);
    bool toggleBreakpoint(uint32_t pc);
    bool enableBreakpoint(uint32_t pc, bool enabled);
    bool setBreakpointCondition(uint32_t pc, std::optional<Condition> condition);
    void clearBreakpoints();
    bool hasBreakpoint(uint32_t pc) const { return breakpoints_.count(pc) != 0; }
    const std::map<uint32_t, Breakpoint>& breakpoints() const { return breakpoints_; }

    int  addMemoryWatchpoint(uint32_t address);
    int  addRegisterWatchpoint(Register reg);
    bool removeWatchpoint(int id);
    bool enableWatchpoint(int id, bool enabled);
    void clearWatchpoints();
    const std::map<int, Watchpoint>& watchpoints() const { return watchpoints_; }

    StopInfo stepCycle();
    StopInfo stepInstruction();
    StopInfo continueExecution(uint64_t max_cycles = kDefaultMaxCycles);
    StopInfo runUntil(uint32_t pc, uint64_t max_cycles = kDefaultMaxCycles);
    void reset();

    uint32_t nextPc() const;
    bool halted() const { return cpu_.state().halted; }
    std::array<int32_t, kNumGPRegisters> registers() const { return cpu_.state().registers; }
    std::vector<int32_t> readMemory(uint32_t start, uint32_t count) const;
    void printRegisters() const;
    void printRegisters(std::ostream& os) const;
    void printMemory(uint32_t start, uint32_t count) const;
    void printMemory(uint32_t start, uint32_t count, std::ostream& os) const;
    const Cpu& cpu() const { return cpu_; }

private:
    StopInfo clock();
    StopInfo terminalInfo() const;
    StopInfo runFetchBarrier(uint32_t pc);
    bool breakpointArmedAt(uint32_t pc) const;
    bool breakpointHit(uint32_t pc);
    bool conditionHolds(const Condition& c) const;
    int32_t watchValue(const Watchpoint& w) const;
    void snapshotWatchpoints();
    std::vector<WatchHit> checkWatchpoints();

    Cpu& cpu_;
    std::map<uint32_t, Breakpoint> breakpoints_;
    std::map<int, Watchpoint> watchpoints_;
    int next_watch_id_ = 1;
    std::optional<uint32_t> run_target_;
    std::optional<uint32_t> skip_break_once_;
};

} // namespace risc201