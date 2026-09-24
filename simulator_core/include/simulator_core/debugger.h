// Interactive debugger: breakpoints, watchpoints, register/memory
// inspection, step/continue/reset (PRD §5.2).

#pragma once

#include "simulator_core/cpu.h"
#include <cstdint>
#include <set>

namespace risc201 {

class Debugger {
public:
    explicit Debugger(Cpu& cpu) : cpu_(cpu) {}

    void addBreakpoint(uint32_t pc) { breakpoints_.insert(pc); }
    void removeBreakpoint(uint32_t pc) { breakpoints_.erase(pc); }
    void addWatchpoint(uint32_t address) { watchpoints_.insert(address); }

    void stepInstruction();
    void continueExecution();
    void reset();

    void printRegisters() const;
    void printMemory(uint32_t start, uint32_t count) const;

private:
    Cpu& cpu_;
    std::set<uint32_t> breakpoints_;
    std::set<uint32_t> watchpoints_;
    // TODO(Akaal, Oct 14): full breakpoint/watchpoint semantics, pipeline
    // hazard (stall/bubble) display for the 6-stage variant.
    // TODO(Nov): microPC + control-memory-word + control-signal display
    // per cycle once control_unit is integrated.
};

} // namespace risc201
