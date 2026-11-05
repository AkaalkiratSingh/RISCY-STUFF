# RISCY-STUFF (risc201)

**Project evaluation report:** [Specifications, architecture, operation, and measured results](docs/PROJECT_ARCHITECTURE.md).


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
- **Microcoded control unit:** each instruction runs as a routine of micro-operations stored in a control memory, using a horizontal 48-bit microword.
- **Optimized ALU:** A 32-bit Kogge-Stone parallel-prefix adder (O(logn) latency), a Radix-4 Booth multiplier, and a complete Soft-Float IEEE 754 FPU (FADD, FSUB, FMUL, FDIV using Newton-Raphson, FCMP, FTOI, ITOF) that reuses the integer datapath.

2. Update the "Repository layout" Section
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
      --no-forward select forwarding-off mode (currently the same conservative interlock)
      --horizontal use the horizontal microcode ROM (default)
      --vertical   use the vertical microcode ROM (same behaviour, narrower words)
  -h, --help      show help
```

### Interactive TUI

```bash
build/simulator_core/tui/risc201-tui examples/recursive_sum.asm
```

With no path the TUI starts empty — press `Tab` to focus the assembly pane and `e` to write a program from scratch. It accepts the same pipeline options as the simulator CLI: `-4`/`-6`, `--no-forward`, `--horizontal`/`--vertical` and `--trace`.

The screen has panes for raw assembly, assembled binary, registers, extra stats, a control-ROM listing and memory, plus an optional hazard timeline. Long source and control-ROM rows wrap inside their panes. Each assembled-binary row with an active control row ends with its next ROM address, read from that instruction's microPC, for example `◄[EX 29]`. If an instruction waits to move slots, the tag shows its pending next row. Normal-mode keys: `s`/`n`/Enter step, `c` clock, `r` run, `T` toggle the timeline, `Tab` enter inspect mode, `q` quit. Feedback messages wrap and the pane grows to fit available space.

**Inspect mode** (`Tab` to enter, `Esc` to leave) focuses one pane at a time, and `j`/`k` scroll only the focused pane. `g`/`G` jump to the ends. The CONTROL ROM pane is a scrollable listing with blank separators between instruction routines (addresses are unchanged); `Tab` focuses it, `j`/`k` scroll the ROM, and `g`/`G` jump to its ends. In the memory pane, `f` toggles SP-follow; follow only scrolls when SP leaves the visible window, so the view stays put while the stack moves inside it.

**Editing** — with the assembly pane focused, `e` opens a line editor: type to insert, Enter splits a line, Backspace deletes or joins, arrows/Home/End move, `Esc` leaves. Editing is text only. While the buffer differs from the running program, the assembled-binary pane turns red and reads `ASSEMBLED BINARY [STALE]`. `:assemble` applies the buffer and clears the stale state; `:save` (`:w`) writes the buffer back to the loaded file (or a path given to `:save`). Saving and assembling are independent. A failed assembly lists all errors with source line numbers and leaves the running program untouched. `:assemble` and `:load` also report assembler warnings, including successful assemblies; warnings appear in yellow below the status or errors. Use PageUp/PageDown to read a diagnostic list longer than the feedback pane.

Commands (type `:` in normal or inspect mode). While typing, `Tab` completes command names and `:load` paths, with the candidates listed in the feedback pane; `Tab` again cycles them.

| Command | Effect |
|---------|--------|
| `:step [n]`, `:clock [n]`, `:run` | Advance execution |
| `:load <path>` | Load `.asm`, `.hex` or `.bin` |
| `:assemble`, `:asm` | Reassemble the editor buffer and reset the run |
| `:save [path]`, `:w` | Write the editor buffer to the loaded file (or `path`) |
| `:reset` | Reset CPU state and re-run the loaded program |
| `:break <pc>`, `:clearbreak` | Toggle or clear breakpoints |
| `:config [stage 4\|6] [rom horiz\|vert] [forward on\|off]` | Change configuration; bare `:config` opens a dialog |
| `:mem S:C` | Dump `C` words from `S` and jump the memory view |
| `:mem_focus <addr>` | Jump the memory view |
| `:trace` | Toggle the hazard timeline |
| `:help`, `:quit` | Show help / quit |

Stage and ROM changes restart the run (a dialog applies both in one reset); forwarding changes apply live.

Breakpoints are fetch barriers. When one fires, fetching stops before that PC is admitted, the older instructions drain and retire, and the debugger reports the stop with an empty pipeline at the breakpoint PC. Resuming therefore starts a fresh pipeline exactly at that instruction.

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

The `examples/` folder has 14 assembly sources. The runnable examples include Collatz, Euclid's gcd, factorial by shift-and-add, a sieve, bubble and insertion sort, CRC-32, naive recursive Fibonacci, an 8x8 matrix multiply, floating-point arithmetic with literals, recursion, and stack exhaustion. Two are synthetic hazard controls — `branch_storm` (control hazards only) and `pointer_walk` (load-use only) — and `stack_overflow.asm` triggers `INVALID_MEMORY_ACCESS` on purpose.

Every instruction executes at most one control-ROM row per clock. In the 4-stage variant, the FETCH slot serializes the IF, ID, and operand-read rows; row-less stages are skipped. The `pipeline:` line reports data stalls, redirects, and squashed instructions. The old stage-count cycle formula no longer applies.

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
| Floating point | `FADD`, `FSUB`, `FMUL`, `FDIV`, `FCMP`, `FTOI`, `ITOF` |
| Memory | `LOAD rd, imm[rs1]`, `STORE rd, imm[rs1]` |
| Control flow | `JMP`, `BEQ`, `CALL` (writes `R14`), `RET` (jumps to `R14`) |
| Stack macros | `PUSH`, `POP` (expand to `SUB`/`STORE` and `LOAD`/`ADD` on `SP`) |
| Misc | `NOP`, `HALT` |

CALL/RET do not touch the stack: software that makes nested calls, or needs `R14` for data, saves it with `PUSH R14` / `POP R14`. `FTOI rd, rs` converts a float bit pattern to an integer; `ITOF rd, rs|imm` converts an integer to a float bit pattern. Both preserve flags. `MOV` has an ISA and ROM entry but no assembler mnemonic entry yet.

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

The control ROM includes a shared front end: `ir_load pc_inc` fetches a word and
advances the word-index PC, then `instr_decode opcode_dispatch` decodes it and
selects its opcode routine. Instruction bodies use `operand_read` when operands
are needed, followed by ALU, memory, branch and writeback signals. RET reads R14
in an OF row and redirects from it in a separate EX row.

The CPU gives every in-flight instruction its own microPC. The four-stage pipeline
executes fetch, decode and operand read as separate clocks in one FETCH slot; the
six-stage pipeline keeps them in separate slots. A row-less stage is skipped.

Generated ROM references show the physical ROM and each instruction's complete
flow, repeating the shared prefix by address before its opcode-specific rows:

- [Horizontal ROM](docs/control_rom_horizontal.txt): 48-bit packed words.
- [Vertical ROM](docs/control_rom_vertical.txt): 16-bit words, 22 micro-op codes.

Regenerate both files from the actual ROM definitions with:

```bash
cmake --build build --target export_control_rom
```

`ControlRomListings` in CTest fails if either checked-in listing differs from the
current ROM. The `risc201-rom` exporter also accepts `--check <directory>`.

The CPU retains a per-opcode control bundle for dependency detection and advances
each in-flight instruction through its own ROM rows. Several instructions overlap.

`--micro-trace` reconstructs each retired or faulting instruction's microcode path
using the selected sequencer and the flags captured when that instruction executes.
The shared fetch/decode rows are included before each opcode routine.
Each row shows its instruction PC, microPC, successor (or END), packed ROM word,
output signals, branch condition, and exception. The cycle label identifies the
instruction completion/fault clock; each executed row consumed its own clock.
Squashed instructions are omitted. Faulting rows show suppressed signals followed
by the TRAP row; an invalid instruction fetch has no microcode routine to display.

```bash
build/simulator_core/risc201-sim examples/recursive_sum.asm --micro-trace --vertical
build/simulator_core/risc201-sim examples/recursive_sum.asm --micro-step
```

`--micro-step` pauses between reconstructed rows: Enter advances the display and
`q` (or EOF) stops. It does not pause or execute datapath micro-operations individually.
Combine `--micro-trace` with `--trace` for pipeline context or `-s` for register
snapshots. Trace modes stop after 4000 pipeline clocks. Exit status is 0 for normal
completion, 1 for an exception/input error, and 2 for an incomplete run.
Exceptions include the faulting instruction PC, detection stage, and memory address
when applicable. Undefined opcodes raise `ILLEGAL_INSTRUCTION`.

All modules share a single header-only contract, `isa_spec/isa201.h`, so the assembler, decoder, ALU and control unit always agree on opcodes, registers, flags and exception codes.

## Repository layout

```
RISCY-STUFF/
├── isa_spec/         # isa201.h: shared opcodes, registers, flags, exceptions
├── alu/              # Kogge-Stone adder, Booth multiplier, Soft-Float FPU, flag generation
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

## Stress benchmarks

Five longer, deterministic workloads are available in [processor_benchmark](processor_benchmark/README.md), with expected results, a runner for all eight CPU configurations, simulated counters, and average host runtime from five runs per configuration. They target ALU dependencies, pointer chasing, branch flushing, memory traffic, and recursive stack use.

The standalone ALU algorithm benchmark writes per-operation timings to [alu_benchmark/alu_benchmark_results.csv](alu_benchmark/alu_benchmark_results.csv). From the project root, run `./build/risc201-alu-bench`; pass a CSV path as its first argument to choose another output file. Its columns are the algorithm name and average nanoseconds per operation for 2-, 4-, 8-, 16-, 32-, and 64-bit input cases, plus 128-bit operands when the compiler supports `__uint128_t`. The 2- and 4-bit cases mask inputs stored in `uint8_t`, so the algorithms still execute their 8-bit loops. No wider integer type is available to the unchanged algorithms; they therefore stop at 64 bits on standard C++17 compilers or 128 bits on this GCC build.

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
- [x]ALU with Kogge-Stone parallel-prefix adder, Radix-4 Booth multiplier, and full Soft-Float IEEE 754 FPU (FADD, FSUB, FMUL, FDIV via Newton-Raphson, FCMP, FTOI, ITOF)
- [x] Microcoded control unit (horizontal encoding)
- [x] Stack bounds checks, reported as `INVALID_MEMORY_ACCESS`
- [x] Two-pass assembler with `.bin` / `.hex` output
- [x] Macro preprocessor (`.macro`/`.endm`, `@` local labels, built-in `PUSH`/`POP`) and `.literals:` float constants
- [x] `CALL`/`RET` use `R14` as the link register; software saves it with `PUSH`/`POP` around nested calls
- [x] Scalar floating-point arithmetic and `FTOI`/`ITOF` conversions with float literals
- [x] Decoder, CPU execution loop and simulator CLI
- [x] Pipelined datapath: 4-stage (`FETCH → EX → MEM → WB`) and 6-stage (`IF → ID → OF → EX → MEM → WB`) variants, overlapping instructions clock by clock
- [ ] Readiness-based operand forwarding; both forwarding settings currently use a conservative interlock
- [x] Per-cycle pipeline trace and hazard counters (`--trace`); ROM rows now determine instruction latency
- [x] Terminal UI (`risc201-tui`): pane focus, source editor with explicit `:assemble` and a stale-buffer indicator, memory navigation with SP-follow, configuration dialog and wrapped feedback
- [x] Vertical microcode encoding, selectable at runtime alongside the horizontal ROM

**In progress / planned**
- [x] Debugger breakpoints, watchpoints, reset, and stepping
- [x] Reconstructed microcode display: `--micro-trace` and `--micro-step` show microPC transitions, packed ROM words, signals and traps without changing pipeline timing
- [ ] Wire the stack visualizer into the simulator output
- [ ] Complete the ISA, with end to end implementation
- [ ] Expose `MOV` in the assembler
- [ ] Complete IEEE-754 special-case and rounding behavior

## Team

RISCY-STUFF is a team project.

- **Akaalkirat Singh**: `Cpu` core (integrating the ALU, control unit and stack guard), decoder, simulator runner
- **Divyansh Singh**: Assembler, Disassembler, Debugger
- **Vaibhav Vatsal**: Vertical Control unit, Memory Safety and Stack Visualiser
- **Milan Mohane**: Horizontal Control Unit
- **Arjun Phaneesh**: ALU implementation, ALU Benchmarking
