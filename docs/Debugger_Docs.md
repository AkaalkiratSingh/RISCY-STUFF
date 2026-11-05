# RISCY201 Debugger — Documentation

This document describes the implementation, behavior, and API of the `Debugger` component (`debugger.h` and `debugger.cpp`), as well as its interaction with the terminal-based User Interface (TUI).

### Table of contents
1. Overview
2. Address Conventions
3. Stop Reasons and Stop Information
4. Breakpoints and Conditions
5. Watchpoints
6. Execution Control
7. Reset Behavior
8. Inspecting Registers and Memory
9. C++ Library API (`Debugger` class)
10. TUI Debugger Integration

---

## 1. Overview
The debugger wraps a `Cpu` instance and provides mechanisms to control execution, evaluate stop conditions (breakpoints and watchpoints), and inspect architectural state (registers and data memory).

```text
                       ┌──────────────────────────┐
                       │       Cpu instance       │
                       │  registers, data memory  │
                       │   fetch PC, state, step  │
                       └────────────┬─────────────┘
                                    │
                             Debugger::clock()
                                    │
            ┌───────────────────────┼───────────────────────┐
            │                       │                       │
      Breakpoints             Watchpoints             Execution Controls
      Stop at a PC            Detect value changes    stepCycle, stepInstruction,
      (optional condition)    (registers & memory)    continueExecution, runUntil
                                    │
                                    ▼
                            StopInfo returned
                    reason, PC, exception, watch hits
```

The debugger operates at the runtime state level:
- Instruction addresses and breakpoint targets are word indices.
- Breakpoints target `Cpu::fetchPc()`.
- Watchpoints monitor register values and data-memory locations.
- Stepping and execution methods advance state via `Cpu::step()`.

---

## 2. Address Conventions

Debugger PCs and watchpoint memory addresses are **word indices**, not byte addresses.

| Value | Unit | Description |
| :--- | :--- | :--- |
| **Debugger Breakpoint PC** | Instruction word index | `1` refers to the second instruction word. |
| **`Cpu::fetchPc()`** | Instruction word index | Advances by 1 per instruction word. |
| **Memory Watchpoint Address** | Data-memory word index | `0` is the first data-memory word. |
| **Assembler Label Address** | Byte address | Divide by 4 to get the corresponding word index. |

Each instruction word is 32 bits (4 bytes). For instance, if an assembler label sits at byte address `12`, its debugger PC (word index) is `3`.

---

## 3. Stop Reasons and Stop Information

When execution halts, execution control methods return a `StopInfo` structure describing why execution stopped and the state at that point.

### 3.1 Stop Reasons (`StopReason`)
The helper function `stopReasonName(StopReason reason)` returns a human-readable string representation of each stop reason:

| Enum Value | String (`stopReasonName`) | Description |
| :--- | :--- | :--- |
| `StopReason::StepDone` | `"step done"` | Step or clock operation completed without hitting another stop condition. |
| `StopReason::Breakpoint` | `"breakpoint"` | Reached an enabled breakpoint whose condition (if present) evaluated to `true`. |
| `StopReason::Watchpoint` | `"watchpoint"` | One or more watched register or memory values changed. |
| `StopReason::TargetReached` | `"target reached"` | Execution reached the PC specified in `runUntil()`. |
| `StopReason::Halted` | `"halted"` | CPU reached a halted state. |
| `StopReason::Exception` | `"exception"` | CPU triggered an exception (e.g., divide by zero, illegal memory access). |
| `StopReason::CycleLimit` | `"cycle limit"` | Continuous execution exceeded `max_cycles`. |

### 3.2 Stop Information Structure (`StopInfo`)
```cpp
struct StopInfo {
    StopReason reason = StopReason::StepDone;
    uint32_t pc = 0;
    std::optional<ExceptionCode> exception;
    std::vector<WatchHit> watch_hits;
};
```
- **`pc`**: Reported via `nextPc()`.
- **`exception`**: Populated if the CPU has a pending exception.
- **`watch_hits`**: Contains details for any watchpoints triggered during the operation.

---

## 4. Breakpoints and Conditions

Breakpoints are managed via a `std::map<uint32_t, Breakpoint>` keyed by word-address PC.

### 4.1 Breakpoint Operations
| Operation | Behavior |
| :--- | :--- |
| `addBreakpoint(pc, condition)` | Adds or replaces a breakpoint at `pc` with an optional condition. |
| `removeBreakpoint(pc)` | Removes the breakpoint at `pc`; returns `true` if it existed. |
| `toggleBreakpoint(pc)` | Removes the breakpoint if present, otherwise adds one. |
| `enableBreakpoint(pc, enabled)` | Enables or disables an existing breakpoint. |
| `setBreakpointCondition(pc, condition)` | Sets or clears (`std::nullopt`) a breakpoint's condition. |
| `clearBreakpoints()` | Clears all breakpoints and resets temporary breakpoint skips. |

### 4.2 Breakpoint Conditions (`Condition`)
A breakpoint can have a condition comparing a register's signed integer value against a target value.

```cpp
enum class CompareOp { Eq, Ne, Lt, Le, Gt, Ge };

struct Condition {
    Register reg = Register::R0;
    CompareOp op = CompareOp::Eq;
    int32_t value = 0;
};
```

| Operator (`CompareOp`) | Comparison |
| :--- | :--- |
| `CompareOp::Eq` | `==` |
| `CompareOp::Ne` | `!=` |
| `CompareOp::Lt` | `<` |
| `CompareOp::Le` | `<=` |
| `CompareOp::Gt` | `>` |
| `CompareOp::Ge` | `>=` |

A breakpoint hit count (`Breakpoint::hits`) increments only when enabled and its condition holds true.

### 4.3 Breakpoint Execution Barrier
When `Cpu::fetchPc()` hits an armed breakpoint or `runUntil` target:
1. Fetching is held (`cpu_.holdFetch(true)`).
2. The CPU is stepped until the pipeline drains.
3. Watchpoints are checked after each step during the drain.
4. Fetching is unheld.
5. If the target PC remains after draining, `StopReason::Breakpoint` or `StopReason::TargetReached` is reported. If execution redirected away, execution continues.

---

## 5. Watchpoints

Watchpoints observe value changes in registers or memory locations.

### 5.1 Watchpoint Types and Management
```cpp
enum class WatchKind { Memory, Register };

struct Watchpoint {
    int id = 0;
    WatchKind kind = WatchKind::Memory;
    uint32_t target = 0; // Data memory address or register number
    bool enabled = true;
    int32_t last = 0;
    uint64_t hits = 0;
};
```

| Operation | Behavior |
| :--- | :--- |
| `addMemoryWatchpoint(address)` | Adds a watchpoint for a data memory index. Returns `-1` if address is invalid. Reuses ID if already watched. |
| `addRegisterWatchpoint(reg)` | Adds a watchpoint for a register (`Register::R0` to `R15`). Reuses ID if already watched. |
| `removeWatchpoint(id)` | Removes the watchpoint with matching `id`. |
| `enableWatchpoint(id, enabled)` | Enables or disables a watchpoint. |
| `clearWatchpoints()` | Removes all watchpoints and resets ID counter to 1. |

### 5.2 Watchpoint Hits (`WatchHit`)
Watchpoints are evaluated after CPU ticks. A hit is logged when `enabled == true` and the current value differs from `last`:

```cpp
struct WatchHit {
    int id = 0;
    WatchKind kind = WatchKind::Memory;
    uint32_t target = 0;
    int32_t old_value = 0;
    int32_t new_value = 0;
};
```
- Writing the same value does not trigger a hit.
- Memory and register reads do not trigger a hit.

---

## 6. Execution Control

Execution methods advance the underlying CPU state and monitor for stop conditions.

| Method | Description |
| :--- | :--- |
| `stepCycle()` | Snapshots watchpoints and performs a single clock execution check (`clock()`). |
| `stepInstruction()` | Steps until the CPU's retired instruction count increases or a stop reason triggers. Features a 64-step guard (`kStepInstructionClockGuard`) to prevent infinite loops. |
| `continueExecution(max_cycles)` | Runs continuously until a stop condition triggers or `max_cycles` (default `1'000'000`) is reached. |
| `runUntil(pc, max_cycles)` | Sets a target PC and runs until `pc` is reached, a breakpoint/watchpoint hits, or `max_cycles` expires. |

---

## 7. Reset Behavior

`Debugger::reset()` resets the processor while keeping configured breakpoints and watchpoints:
1. Reloads the original source words into the CPU.
2. Clears data memory and re-initializes memory state.
3. Resets run targets and skip flags.
4. Resets hit counts (`hits = 0`) for all breakpoints and watchpoints.
5. Re-snapshots initial baseline values for all watchpoints.

---

## 8. Inspecting Registers and Memory

The debugger provides inspection methods to read and format processor state:

- **`readMemory(start, count)`**: Returns a `std::vector<int32_t>` containing up to `count` words starting at `start`. Bounds-checked against data memory.
- **`printRegisters()` / `printRegisters(stream)`**: Outputs values of all 16 general-purpose registers (`R0`–`R15`).
- **`printMemory(start, count)` / `printMemory(start, count, stream)`**: Prints data-memory words in decimal and formatted 8-digit uppercase hex (`0xXXXXXXXX`).
- **`nextPc()`**: Returns the PC of the highest-index active pipeline instruction, or `fetchPc()` if the pipeline is empty.
- **`registers()`**: Returns a `std::array<int32_t, 16>` of current register values.

---

## 9. C++ Library API (`Debugger` class)

Complete class interface declared in `simulator_core/debugger.h`:

```cpp
namespace risc201 {

class Debugger {
public:
    static constexpr uint64_t kDefaultMaxCycles = 1'000'000;

    explicit Debugger(Cpu& cpu);

    // Breakpoint Management
    void addBreakpoint(uint32_t pc, std::optional<Condition> condition = std::nullopt);
    bool removeBreakpoint(uint32_t pc);
    bool toggleBreakpoint(uint32_t pc);
    bool enableBreakpoint(uint32_t pc, bool enabled);
    bool setBreakpointCondition(uint32_t pc, std::optional<Condition> condition);
    void clearBreakpoints();
    bool hasBreakpoint(uint32_t pc) const;
    const std::map<uint32_t, Breakpoint>& breakpoints() const;

    // Watchpoint Management
    int  addMemoryWatchpoint(uint32_t address);
    int  addRegisterWatchpoint(Register reg);
    bool removeWatchpoint(int id);
    bool enableWatchpoint(int id, bool enabled);
    void clearWatchpoints();
    const std::map<int, Watchpoint>& watchpoints() const;

    // Execution Controls
    StopInfo stepCycle();
    StopInfo stepInstruction();
    StopInfo continueExecution(uint64_t max_cycles = kDefaultMaxCycles);
    StopInfo runUntil(uint32_t pc, uint64_t max_cycles = kDefaultMaxCycles);
    void reset();

    // Inspection & Utilities
    uint32_t nextPc() const;
    bool halted() const;
    std::array<int32_t, kNumGPRegisters> registers() const;
    std::vector<int32_t> readMemory(uint32_t start, uint32_t count) const;
    void printRegisters() const;
    void printRegisters(std::ostream& os) const;
    void printMemory(uint32_t start, uint32_t count) const;
    void printMemory(uint32_t start, uint32_t count, std::ostream& os) const;
    const Cpu& cpu() const;
};

} // namespace risc201
```

---

## 10. TUI Debugger Integration

The terminal user interface (`tui.cpp`) interfaces with the `Debugger` class via CLI commands and hotkeys:

### 10.1 Debugger Commands in TUI
| Command | Usage | Debugger API Called |
| :--- | :--- | :--- |
| `:step [n]` / `:s` | Step `n` instructions | `Debugger::stepInstruction()` |
| `:clock [n]` / `:c` | Step `n` clock cycles | `Debugger::stepCycle()` |
| `:run` / `:r` | Continuous execution | `Debugger::stepCycle()` inside loop |
| `:break <pc>` / `:b` | Toggle breakpoint at `pc` | `Debugger::toggleBreakpoint(pc)` |
| `:break <pc> if ...` | Conditional breakpoint | `Debugger::addBreakpoint(pc, cond)` |
| `:cond <pc> ...` | Set/clear condition | `Debugger::setBreakpointCondition()` |
| `:clearbreak` / `:cb` | Clear all breakpoints | `Debugger::clearBreakpoints()` |
|