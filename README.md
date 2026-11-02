# RISCY-STUFF (risc201)

A from-scratch 32-bit processor project: a custom instruction set (**risc201**), a two-pass assembler, and a C++17 simulator whose control unit is driven by real microcode. It also includes a hardware-style stack guard that raises exceptions on stack overflow or underflow.

> **Status: work in progress.**  Several pieces are still stubs or planned (see [Status and roadmap](#status-and-roadmap)).

---

## Contents

- [Highlights](#highlights)
- [Quick start](#quick-start)
- [Usage](#usage)
- [The ISA](#the-isa)
- [Architecture](#architecture)
- [Repository layout](#repository-layout)
- [Testing](#testing)
- [Status and roadmap](#status-and-roadmap)
- [Team](#team)

---

## Highlights

- **Custom 32-bit ISA:** 16 general-purpose registers, fixed-width 32-bit instructions.
- **Microcoded control unit:** each instruction runs as a routine of micro-operations stored in a control memory, using a horizontal 44-bit microword.
- **ALU built from gate-level ideas:** a 32-bit carry-lookahead adder (eight chained 4-bit CLA blocks) and a radix-4 Booth multiplier.
- **Stack guard:** a full-descending stack with bounds checking on stack accesses, raising `INVALID_MEMORY_ACCESS` before the access commits.
- **Two-pass assembler** with labels, symbol table, errors and warnings, plus `.bin` / `.hex` output.
- **Simulator CLI** with a decode listing, single-step mode and memory dumps.
- **Interactive TUI (`risc201-tui`):** live source, binary, register, stats and memory panes; a hazard timeline; a source editor with explicit reassembly; and CPU configuration switching.
- **Test suite** covering the ALU, control unit, decoder, assembler passes, encoder, memory safety and full-program CPU execution.

## Quick start

**Requirements:** a C++17 compiler and CMake 3.16 or newer.

```bash
git clone https://github.com/AkaalkiratSingh/RISCY-STUFF.git
cd RISCY-STUFF

# build everything and run all example programs
./demo.sh
```

Or build manually:

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build
```

This produces four executables:

| Binary | Location | Purpose |
|--------|----------|---------|
| `risc201-sim` | `build/simulator_core/` | Assemble (if needed) and run a program |
| `risc201-tui` | `build/simulator_core/tui/` | Interactive terminal debugger and visualization UI |
| `risc201-asm` | `build/assembler/` | Assemble `.asm` into `.bin` or `.hex` |
| `risc201-disasm` | `build/assembler/` | Disassemble `.bin` / `.hex` back into `.asm` |

## Usage

### Run a program

```bash
build/simulator_core/risc201-sim examples/collatz.asm
```

`.asm` input is assembled first. `.hex` and `.bin` input is decoded by the simulator itself. Output is the final register file, PC/SP, flags, cycle and retired-instruction counts, and any exception.

```
usage: risc201-sim <program.asm|program.hex|program.bin> [options]

  -4, --4stage    select the 4-stage pipeline variant (default)
  -6, --6stage    select the 6-stage pipeline variant
  -s, --step      single-step, printing state after each instruction
  -d, --decode    print the decoded instruction listing before running
  -m, --mem S:C   dump C data-memory words starting at word index S
      --trace         print the pipeline per clock, with hazard events
      --no-forward stall on every dependency instead of bypassing
      --horizontal use the horizontal microcode ROM (default)
      --vertical   use the vertical microcode ROM (same behaviour, narrower words)
  -h, --help      show help
```

### Interactive TUI

```bash
build/simulator_core/tui/risc201-tui examples/recursive_sum.asm
```

With no path the TUI starts empty — press `Tab` to focus the assembly pane and `e` to write a program from scratch. It accepts the same pipeline options as the simulator CLI: `-4`/`-6`, `--no-forward`, `--horizontal`/`--vertical` and `--trace`.

The screen has panes for raw assembly, assembled binary, registers, extra stats and memory, plus an optional hazard timeline. Normal-mode keys: `s`/`n`/Enter step, `c` clock, `r` run, `T` toggle the timeline, `Tab` enter inspect mode, `q` quit. Long feedback messages wrap and the feedback pane grows to fit them.

**Inspect mode** (`Tab` to enter, `Esc` to leave) focuses one pane at a time, and `j`/`k` scroll only the focused pane. `g`/`G` jump to the ends. In the memory pane, `f` toggles SP-follow; follow only scrolls when SP leaves the visible window, so the view stays put while the stack moves inside it.

**Editing** — with the assembly pane focused, `e` opens a line editor: type to insert, Enter splits a line, Backspace deletes or joins, arrows/Home/End move, `Esc` leaves. Editing is text only. While the buffer differs from the running program, the assembled-binary pane turns red and reads `ASSEMBLED BINARY [STALE]`; `:assemble` applies the buffer and clears it. A failed assembly reports the error and leaves the running program untouched.

Commands (type `:` in normal or inspect mode):

| Command | Effect |
|---------|--------|
| `:step [n]`, `:clock [n]`, `:run` | Advance execution |
| `:load <path>` | Load `.asm`, `.hex` or `.bin` |
| `:assemble`, `:asm` | Reassemble the editor buffer and reset the run |
| `:reset` | Reset CPU state and re-run the loaded program |
| `:break <pc>`, `:clearbreak` | Toggle or clear breakpoints |
| `:config [stage 4\|6] [rom horiz\|vert] [forward on\|off]` | Change configuration; bare `:config` opens a dialog |
| `:mem S:C` | Dump `C` words from `S` and jump the memory view |
| `:mem_focus <addr>` | Jump the memory view |
| `:trace` | Toggle the hazard timeline |
| `:help`, `:quit` | Show help / quit |

Stage and ROM changes restart the run (a dialog applies both in one reset); forwarding changes apply live.

### Assemble to a file

```bash
build/assembler/risc201-asm examples/recursive_sum.asm -o sum.hex
build/simulator_core/risc201-sim sum.hex
```

### Disassemble

```bash
build/assembler/risc201-disasm sum.hex -o sum.asm   # add -l to synthesise labels
```

### Example program

`examples/recursive_sum.asm` computes `sum(5)` recursively using `CALL`/`RET` and `PUSH`/`POP`. `CALL` writes the return address into `R14`; nested calls save it with `PUSH R14` / `POP R14`:

```asm
MOV R1, 5 ; R1 = 5
CALL sum
HALT

sum:
    ADD R2, R1, R0     ; R2 = n, sets Z when n == 0
    BEQ zero
    PUSH R14           ; save the return address across the recursive call
    PUSH R1            ; save n
    SUB R1, R1, 1
    CALL sum
    POP R1             ; R1 = saved n
    POP R14            ; restore the return address
    ADD R2, R2, R1     ; R2 = sum(n-1) + n
    RET
zero:
    MOV R1, 0          ; R1 = 0
    RET
```

Result: `R2 = 15`, `SP` back at 4096.

The `examples/` folder has 14 programs. Most are real algorithms running from hundreds to tens of thousands of cycles: Collatz, Euclid's gcd, factorial by shift-and-add, a sieve, bubble and insertion sort, CRC-32, naive recursive Fibonacci, an 8x8 matrix multiply, floating-point arithmetic with literals, plus recursion and stack exhaustion. Two are synthetic hazard controls — `branch_storm` (control hazards only) and `pointer_walk` (load-use only) — and `stack_overflow.asm` triggers `INVALID_MEMORY_ACCESS` on purpose.

Every run prints a `pipeline:` line counting stalls, flushes and flush bubbles, and `--trace` prints the pipeline clock by clock. Those explain the cycle count exactly: `cycles = (stages-1) + retired + HALT + stalls + flush bubbles`.

## The ISA

| Property | Value |
|----------|-------|
| Word size | 32 bits |
| Instruction width | 32 bits, fixed |
| Registers | 16 general purpose (`R0` to `R15`); `R14` is the link register for `CALL`/`RET`, `R15` is the stack pointer |
| Data memory | 4096 words by default, word-addressed |
| Program memory | Held separately from data memory |
| Stack | Full-descending, `R15`/`SP` starts at the top of data memory |
| Flags | Zero (`Z`), Negative (`N`), Carry (`C`), Overflow (`V`) |
| Branching | PC-relative |

### Encoding

```
R/I-type:  [31:27 opcode][26 I][25:22 rd][21:18 rs1][17:14 rs2  |  17:0 imm18]
J-type:    [31:27 opcode][26:0 offset]
```

When the `I` bit is set, the last operand is an 18-bit immediate. Otherwise it is a register in `rs2`.

### Instructions

| Category | Mnemonics |
|----------|-----------|
| Arithmetic | `ADD`, `SUB`, `MUL`, `DIV`, `REM` |
| Logic | `AND`, `OR`, `XOR` |
| Shifts | `SHL`, `SHR` |
| Floating point | `FADD`, `FSUB`, `FMUL`, `FDIV` |
| Memory | `LOAD rd, imm[rs1]`, `STORE rd, imm[rs1]` |
| Control flow | `JMP`, `BEQ`, `CALL` (writes `R14`), `RET` (jumps to `R14`) |
| Stack macros | `PUSH`, `POP` (expand to `SUB`/`STORE` and `LOAD`/`ADD` on `SP`) |
| Misc | `NOP`, `HALT` |

CALL/RET do not touch the stack: software that makes nested calls, or needs `R14` for data, saves it with `PUSH R14` / `POP R14`. `NOT`, `SAR`, `CMP`, `MOV`, `BGT` and `FCMP` exist in the ISA and control unit but are not exposed by the assembler yet.

### Exceptions

`INVALID_MEMORY_ACCESS`, `DIVIDE_BY_ZERO`, `ILLEGAL_INSTRUCTION`

An exception halts the CPU and is reported by the simulator.

## Architecture

```
  .asm ──► assembler (pass 1: labels / pass 2: resolve + encode) ──► 32-bit words
                                                                         │
                                                                         ▼
 ┌────────────────────────────── Cpu (simulator_core) ──────────────────────────────┐
 │  fetch ──► decoder ──► control unit (microcode) ──► ALU ──► memory ──► writeback │
 │                              │                                  ▲                │
 │                        control words                       stack guard           │
 └──────────────────────────────────────────────────────────────────────────────────┘
```

Each instruction is expanded into a micro-routine by the control unit. The `Cpu` steps through that routine one control word at a time, driving the ALU, memory, PC and SP, and consulting the stack guard on stack accesses.

All modules share a single header-only contract, `isa_spec/isa201.h`, so the assembler, decoder, ALU and control unit always agree on opcodes, registers, flags and exception codes.

## Repository layout

```
RISCY-STUFF/
├── isa_spec/         # isa201.h: shared opcodes, registers, flags, exceptions
├── alu/              # carry-lookahead adder, Booth multiplier, flag generation
├── control_unit/     # microcoded control unit and control memory
├── memory_safety/    # stack guard and ASCII stack visualizer
├── assembler/        # two-pass assembler, op table, CLI tools
├── simulator_core/   # Cpu, decoder, debugger skeleton, CLI, simulator main
│   └── tui/          # risc201-tui: panes, source editor, config dialog, hazard timeline
├── utils/            # shared helpers
├── tests/            # unit and integration tests (CTest)
├── examples/         # 14 sample .asm programs
├── demo.sh           # build + run every example with decode listing
└── CMakeLists.txt
```

## Testing

```bash
ctest --test-dir build --output-on-failure
```

| Test | Covers |
|------|--------|
| `AluTest` | ALU operations, flags, multiplication |
| `HorizontalControlUnitTest`, `VerticalControlUnitTest` | Microcode routines and control words |
| `ControlDecodeTest` | Collapsing each micro-routine into one action-tagged control bundle |
| `ControlUnitAluIntegration` | ALU and control-unit integration |
| `DecodeTest` | Instruction decoding |
| `Pass1Test`, `Pass2Test` | Assembler label pass and resolve/encode pass |
| `EncoderTest` | Instruction word encoding |
| `DisassemblerTest` | Instruction text formatting |
| `MemorySafetyTest` | Stack guard bounds and exceptions |
| `CpuIntegrationTest` | End-to-end program execution |
| `PipelineTest` | Variant equivalence, cycle accounting and hazard counters |

All CTest targets pass. The TUI is exercised manually and is not covered by CTest.

## Status and roadmap

**Working**
- [x] Shared ISA specification
- [x] ALU with CLA adder and Booth multiplier
- [x] Microcoded control unit (horizontal encoding)
- [x] Stack bounds checks, reported as `INVALID_MEMORY_ACCESS`
- [x] Two-pass assembler with `.bin` / `.hex` output
- [x] Macro preprocessor (`.macro`/`.endm`, `@` local labels, built-in `PUSH`/`POP`) and `.literals:` float constants
- [x] `CALL`/`RET` use `R14` as the link register; software saves it with `PUSH`/`POP` around nested calls
- [x] Scalar IEEE-754 arithmetic (`FADD`/`FSUB`/`FMUL`/`FDIV`) with float literals
- [x] Decoder, CPU execution loop and simulator CLI
- [x] Pipelined datapath: 4-stage (`IF/ID/OF → EX → MEM → WB`) and 6-stage (`IF → ID → OF → EX → MEM → WB`) variants, overlapping instructions clock by clock
- [x] Operand forwarding (bypassing), with `--no-forward` as a stall-everything comparison mode
- [x] Per-cycle pipeline trace and hazard counters (`--trace`), reconciling `cycles = (stages-1) + retired + HALT + stalls + flush bubbles`
- [x] Terminal UI (`risc201-tui`): pane focus, source editor with explicit `:assemble` and a stale-buffer indicator, memory navigation with SP-follow, configuration dialog and wrapped feedback
- [x] Vertical microcode encoding, selectable at runtime alongside the horizontal ROM

**In progress / planned**
- [ ] Debugger class: breakpoints and watchpoints are stubbed in `simulator_core` (`stepInstruction` works; `continueExecution`, `reset` and `printMemory` are TODO). The TUI implements its own breakpoints.
- [ ] Control-unit display: `--trace` shows the pipeline slot by slot, but the microPC, the packed control word and the signal bundle are not shown
- [ ] Wire the stack visualizer into the simulator output
- [ ] Complete the ISA, with end to end implementation
- [ ] Expose `NOT`, `SAR`, `CMP`, `MOV`, `BGT` and `FCMP` in the assembler
- [ ] Full IEEE-754 extension (comparison/branch integration, conversion)

## Team

RISCY-STUFF is a team project.

- **Akaalkirat Singh**: `Cpu` core (integrating the ALU, control unit and stack guard), decoder, simulator runner, and most of the tests
- _Add teammates and their modules here_

## License

_Not yet specified._