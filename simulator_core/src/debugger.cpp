#include "simulator_core/debugger.h"

#include <algorithm>
#include <iomanip>
#include <iostream>

namespace risc201 {

namespace {

constexpr int kStepInstructionClockGuard = 64;

}  // namespace

const char* stopReasonName(StopReason reason) {
    switch (reason) {
    case StopReason::StepDone: return "step done";
    case StopReason::Breakpoint: return "breakpoint";
    case StopReason::Watchpoint: return "watchpoint";
    case StopReason::TargetReached: return "target reached";
    case StopReason::Halted: return "halted";
    case StopReason::Exception: return "exception";
    case StopReason::CycleLimit: return "cycle limit";
    }
    return "?";
}

void Debugger::addBreakpoint(uint32_t pc, std::optional<Condition> condition) {
    Breakpoint bp;
    bp.pc = pc;
    bp.condition = condition;
    breakpoints_[pc] = bp;
}

bool Debugger::removeBreakpoint(uint32_t pc) {
    return breakpoints_.erase(pc) != 0;
}

bool Debugger::toggleBreakpoint(uint32_t pc) {
    if (removeBreakpoint(pc)) return false;
    addBreakpoint(pc);
    return true;
}

bool Debugger::enableBreakpoint(uint32_t pc, bool enabled) {
    auto it = breakpoints_.find(pc);
    if (it == breakpoints_.end()) return false;
    it->second.enabled = enabled;
    return true;
}

bool Debugger::setBreakpointCondition(uint32_t pc, std::optional<Condition> condition) {
    auto it = breakpoints_.find(pc);
    if (it == breakpoints_.end()) return false;
    it->second.condition = condition;
    return true;
}

void Debugger::clearBreakpoints() {
    breakpoints_.clear();
}

bool Debugger::conditionHolds(const Condition& c) const {
    const int32_t v = cpu_.state().registers[static_cast<size_t>(c.reg)];
    switch (c.op) {
    case CompareOp::Eq: return v == c.value;
    case CompareOp::Ne: return v != c.value;
    case CompareOp::Lt: return v < c.value;
    case CompareOp::Le: return v <= c.value;
    case CompareOp::Gt: return v > c.value;
    case CompareOp::Ge: return v >= c.value;
    }
    return false;
}

bool Debugger::breakpointHit(uint32_t pc) {
    auto it = breakpoints_.find(pc);
    if (it == breakpoints_.end() || !it->second.enabled) return false;
    if (it->second.condition && !conditionHolds(*it->second.condition)) return false;
    ++it->second.hits;
    return true;
}

int32_t Debugger::watchValue(const Watchpoint& w) const {
    if (w.kind == WatchKind::Register) {
        return cpu_.state().registers[w.target];
    }
    return w.target < cpu_.memory().size() ? cpu_.memory()[w.target] : 0;
}

int Debugger::addMemoryWatchpoint(uint32_t address) {
    if (address >= cpu_.memory().size()) return -1;
    Watchpoint w;
    w.id = next_watch_id_++;
    w.kind = WatchKind::Memory;
    w.target = address;
    w.last = watchValue(w);
    watchpoints_[w.id] = w;
    return w.id;
}

int Debugger::addRegisterWatchpoint(Register reg) {
    Watchpoint w;
    w.id = next_watch_id_++;
    w.kind = WatchKind::Register;
    w.target = static_cast<uint32_t>(reg);
    w.last = watchValue(w);
    watchpoints_[w.id] = w;
    return w.id;
}

bool Debugger::removeWatchpoint(int id) {
    return watchpoints_.erase(id) != 0;
}

bool Debugger::enableWatchpoint(int id, bool enabled) {
    auto it = watchpoints_.find(id);
    if (it == watchpoints_.end()) return false;
    it->second.enabled = enabled;
    return true;
}

void Debugger::clearWatchpoints() {
    watchpoints_.clear();
    next_watch_id_ = 1;
}

void Debugger::snapshotWatchpoints() {
    for (auto& entry : watchpoints_) entry.second.last = watchValue(entry.second);
}

std::vector<WatchHit> Debugger::checkWatchpoints() {
    std::vector<WatchHit> hits;
    for (auto& entry : watchpoints_) {
        Watchpoint& w = entry.second;
        const int32_t now = watchValue(w);
        if (w.enabled && now != w.last) {
            ++w.hits;
            hits.push_back(WatchHit{w.id, w.kind, w.target, w.last, now});
        }
        w.last = now;
    }
    return hits;
}

uint32_t Debugger::nextPc() const {
    const auto& stages = cpu_.stages();
    for (int i = cpu_.stageCount() - 1; i >= 0; --i) {
        if (stages[static_cast<size_t>(i)].valid) return stages[static_cast<size_t>(i)].pc;
    }
    return cpu_.fetchPc();
}

StopInfo Debugger::terminalInfo() const {
    StopInfo info;
    info.pc = nextPc();
    if (cpu_.state().pending_exception) {
        info.reason = StopReason::Exception;
        info.exception = cpu_.state().pending_exception;
    }
    else {
        info.reason = StopReason::Halted;
    }
    return info;
}

StopInfo Debugger::clock() {
    const uint64_t retired_before = cpu_.retired();
    cpu_.step();

    std::vector<WatchHit> hits = checkWatchpoints();

    StopInfo info;
    if (cpu_.state().halted) {
        info = terminalInfo();
    }
    else {
        info.pc = nextPc();
        const bool retired = cpu_.retired() != retired_before;
        if (retired && breakpointHit(info.pc)) {
            info.reason = StopReason::Breakpoint;
        }
        else if (retired && run_target_ && *run_target_ == info.pc) {
            info.reason = StopReason::TargetReached;
        }
        else if (!hits.empty()) {
            info.reason = StopReason::Watchpoint;
        }
    }
    info.watch_hits = std::move(hits);
    return info;
}

StopInfo Debugger::stepCycle() {
    if (cpu_.state().halted) return terminalInfo();
    snapshotWatchpoints();
    return clock();
}

StopInfo Debugger::stepInstruction() {
    if (cpu_.state().halted) return terminalInfo();
    snapshotWatchpoints();

    const uint64_t before = cpu_.retired();
    StopInfo info;
    for (int guard = 0; guard < kStepInstructionClockGuard; ++guard) {
        info = clock();
        if (info.reason != StopReason::StepDone) return info;
        if (cpu_.retired() != before) return info;
    }
    return info;
}

StopInfo Debugger::continueExecution(uint64_t max_cycles) {
    if (cpu_.state().halted) return terminalInfo();
    snapshotWatchpoints();

    const uint64_t start = cpu_.cycles();
    for (;;) {
        StopInfo info = clock();
        if (info.reason != StopReason::StepDone) return info;
        if (cpu_.cycles() - start >= max_cycles) {
            info.reason = StopReason::CycleLimit;
            return info;
        }
    }
}

StopInfo Debugger::runUntil(uint32_t pc, uint64_t max_cycles) {
    run_target_ = pc;
    StopInfo info = continueExecution(max_cycles);
    run_target_.reset();
    return info;
}

void Debugger::reset() {
    const std::vector<uint32_t> image = cpu_.sourceWords();
    std::fill(cpu_.memory().begin(), cpu_.memory().end(), 0);
    cpu_.loadWords(image);

    run_target_.reset();
    for (auto& entry : breakpoints_) entry.second.hits = 0;
    for (auto& entry : watchpoints_) entry.second.hits = 0;
    snapshotWatchpoints();
}

std::vector<int32_t> Debugger::readMemory(uint32_t start, uint32_t count) const {
    const auto& mem = cpu_.memory();
    if (start >= mem.size()) return {};
    const size_t n = std::min<size_t>(count, mem.size() - start);
    return std::vector<int32_t>(mem.begin() + start, mem.begin() + start + n);
}

void Debugger::printRegisters() const { printRegisters(std::cout); }

void Debugger::printRegisters(std::ostream& os) const {
    for (int i = 0; i < kNumGPRegisters; ++i) {
        os << "R" << i << " = " << cpu_.state().registers[static_cast<size_t>(i)] << "\n";
    }
}

void Debugger::printMemory(uint32_t start, uint32_t count) const {
    printMemory(start, count, std::cout);
}

void Debugger::printMemory(uint32_t start, uint32_t count, std::ostream& os) const {
    const std::vector<int32_t> words = readMemory(start, count);
    for (size_t i = 0; i < words.size(); ++i) {
        os << "[" << std::setw(4) << (start + i) << "] " << std::setw(11) << words[i]
           << "  0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(8)
           << static_cast<uint32_t>(words[i]) << std::dec << std::setfill(' ') << "\n";
    }
}

} // namespace risc201