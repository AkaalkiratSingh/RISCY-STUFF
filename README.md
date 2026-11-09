# RISCY-STUFF (risc201)

**Project evaluation report:** [Specifications, architecture, operation, and measured results](docs/PROJECT_ARCHITECTURE.md)

A from-scratch 32-bit processor: a custom instruction set, a two-pass assembler, and a simulator whose control unit is driven by microcode.

---

## Contents

- [Highlights](#highlights)
- [Quick start](#quick-start)
- [Usage](#usage)
- [The ISA](#the-isa)
- [Architecture](#architecture)
- [Repository layout](#repository-layout)
- [Team](#team)

---

## Highlights

- **Custom 32-bit ISA:** fixed-width 32-bit instructions
- **Microcoded control unit:** each instruction runs as a routine of micro-operations stored in a control memory, using a horizontal 48-bit microword or a vertical 16-bit microword.
- **Optimized ALU:** A 32-bit Kogge-Stone parallel-prefix adder (O(logn) latency), a Radix-4 Booth multiplier, and a complete Soft-Float IEEE 754 FPU (FADD, FSUB, FMUL, FDIV using Newton-Raphson, FCMP, FTOI, ITOF) that reuses the integer datapath.
- **Stack guard:** a full-descending stack with bounds checking on stack accesses, raising `INVALID_MEMORY_ACCESS` before the access commits.
- **Assembler** with macro preprocessing, errors and warnings, plus `.bin` / `.hex` output and a equivalent `disassembler`
- **Simulator CLI** with a decode listing, single-step mode and memory dumps.
- **Interactive TUI (`risc201-tui`):** live source, binary, register, stats and memory panes; a hazard timeline; a source editor; and configurable CPU.

## Quick start

**Requirements:** a C++17 compiler and CMake 3.16 or newer.

```bash
git clone https://github.com/AkaalkiratSingh/RISCY-STUFF.git
cd RISCY-STUFF

./setup.sh
```

The setup script requires CMake 3.16 or newer, a C++17 compiler, and a build tool such as Make. It creates `build/`, compiles the project, and runs the tests.

This produces four executables:

| Binary | Location | Purpose |
|--------|----------|---------|
| `risc201-asm` | `build/assembler/` | Assemble `.asm` into `.bin` or `.hex` |
| `risc201-disasm` | `build/assembler/` | Disassemble `.bin` / `.hex` back into `.asm` |
| `risc201-sim` | `build/simulator_core/` | Assemble (if needed) and run a program |
| `risc201-tui` | `build/simulator_core/tui/` | Interactive terminal debugger and visualization UI |

## Usage
Use `run.sh` with a mode flag to select a binary and pass its arguments through:

```bash
./run.sh --asm examples/recursive_sum.asm -o program.bin
./run.sh --disasm program.bin -o program.asm -l
./run.sh --sim examples/recursive_sum.asm -6 --no-forward
./run.sh --tui examples/recursive_sum.asm -4
```

The mode must be the first argument. Bare names (`asm`, `disasm`, `sim`, `tui`) also work. Run `./run.sh --help` for help. File paths are relative to your currently open directory; run `./setup.sh` first to build the binaries.

### Run a program

```bash
build/simulator_core/risc201-sim examples/collatz.asm
```
OR
```bash
./run.sh --sim examples/collatz.asm
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
      --no-forward disable operand forwarding and stall on dependencies
      --horizontal use the horizontal microcode ROM (default)
      --vertical   use the vertical microcode ROM (same behaviour, narrower words)
  -h, --help      show help
```

### Interactive TUI

```bash
./build/simulator_core/tui/risc201-tui ./examples/recursive_sum.asm
```
OR
```bash
./run.sh --tui ./examples/recursive_sum.asm
```

With no path the TUI starts empty — press `Tab` to focus the assembly pane and `e` to write a program from scratch. It accepts the same pipeline options as the simulator CLI: `-4`/`-6`, `--no-forward`, `--horizontal`/`--vertical` and `--trace`.

The screen has panes for raw assembly, assembled binary, registers, extra stats, a control-ROM listing and memory, plus an optional hazard timeline. Each assembled-binary row with an active control row ends with its next ROM address, read from that instruction's microPC, for example `◄[EX 29]`. If an instruction waits to move slots, the tag shows its pending next row. Normal-mode keys: `s`/`n`/Enter step, `c` clock, `r` run, `T` toggle the timeline, `Tab` enter inspect mode, `q` quit.

**Inspect mode** (`Tab` to enter, `Esc` to leave) focuses one pane at a time, and `j`/`k` scroll only the focused pane. `g`/`G` jump to the ends. The CONTROL ROM pane is a scrollable listing with blank separators between instruction routines (addresses are unchanged); `Tab` focuses it, `j`/`k` scroll the ROM, and `g`/`G` jump to its ends. In the memory pane, `f` toggles SP-follow; follow only scrolls when SP leaves the visible window, so the view stays put while the stack moves inside it.

**Editing** — with the raw assembly pane focused, `e` opens a line editor: type to insert, Enter splits a line, Backspace deletes or joins, arrows/Home/End move, `Esc` leaves. Editing is text only. Whwn the buffer differs from the running program, the assembled-binary pane turns red and reads `ASSEMBLED BINARY [STALE]`. `:assemble` applies the buffer and clears the stale state; `:save` (`:w`) writes the buffer back to the loaded file (or a path given to `:save`). Saving and assembling are independent.

Commands (type `:` in normal or inspect mode). While typing, `Tab` completes command names and `:load` paths, with the candidates listed in the feedback pane; `Tab` again cycles them.

| Command | Effect |
|---------|--------|
| `:step [n]`, `:clock [n]`, `:run` | Advance execution |
| `:load <path>` | Load `.asm`, `.hex` or `.bin` |
| `:assemble`, `:asm` | Reassemble the editor buffer and reset the run |
| `:save [path]`, `:w` | Write the editor buffer to the loaded file (or `path`) |
| `:reset` | Reset CPU state and re-run the loaded program |
| `:break <pc>`, `:clearbreak` | Toggle or clear breakpoints |
| `:config [stage 4\|6] [rom horiz\|vert] [forward on\|off] [binary decimal\|hex\|float]` | Change configuration; bare `:config` opens a dialog |
| `:mem S:C` | Dump `C` words from `S` and jump the memory view |
| `:mem_focus <addr>` | Jump the memory view |
| `:trace` | Toggle the hazard timeline |
| `:help`, `:quit` | Show help / quit |

Stage and ROM changes restart the run (a dialog applies both in one reset); forwarding and binary interpretation changes apply live. Binary interpretation controls register and memory values: Decimal Integer (default), Hexadecimal (eight digits), or Floating Point Number (the same 32 bits interpreted as an IEEE-754 single-precision value). Select it in the dialog or use `:config binary decimal`, `:config binary hex`, or `:config binary float`.

Breakpoints are `fetch barriers`. When one fires, fetching stops before that PC is admitted, the older instructions drain and retire, and the debugger reports the stop with an empty pipeline at the breakpoint PC. Resuming therefore starts a fresh pipeline exactly at that instruction.

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
    mov R2, R1     ; R2 = n, sets Z when n == 0
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

Every instruction executes at most one control-ROM row per clock. In the 4-stage variant, the FETCH slot serializes the IF, ID, and operand-read rows; row-less stages are skipped. The `pipeline:` line reports data stalls, redirects, and squashed instructions. The old stage-count cycle formula no longer applies.

## The ISA

| Property | Value |
|----------|-------|
| Word size | 32 bits |
| Instruction width | 32 bits, fixed |
| Registers | 16 general purpose (`R0` to `R15`); `R14` is the link register for `CALL`/`RET`, `R15` is the stack pointer, `R0` is foribly kept as **0** to facilitate in processor calculations |
| Data memory | 4096 words by default, word-addressed |
| Program memory | Held separately from data memory |
| Stack | Full-descending, `R15`/`SP` starts at the top of data memory |
| Flags | Zero (`Z`), Negative (`N`), Carry (`C`), Overflow (`V`) |
| Branching | PC-relative |

### Encoding

ALL instructions are encoded as per 2 formats: 
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
| Memory | `LOAD`, `STORE` |
| Control flow | `JMP`, `BEQ`, `CALL` (writes `R14`), `RET` (jumps to `R14`) |
| Misc | `NOP`, `HALT` |
| Predefined Stack macros | `PUSH`, `POP` (expand to `SUB`/`STORE` and `LOAD`/`ADD` on `SP`) |

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

- [Horizontal ROM](docs/CONTROL_ROM/control_rom_horizontal.txt): 42-bit packed words.
- [Vertical ROM](docs/CONTROL_ROM/control_rom_vertical.txt): 16-bit words, 22 micro-op codes.

The CPU retains a per-opcode control bundle for dependency detection and advances
each in-flight instruction through its own ROM rows. Several instructions overlap.

All modules share a single header-only contract, `isa_spec/isa201.h`, so the assembler, decoder, ALU and control unit always agree on opcodes, registers, flags and exception codes.

## Repository layout

```
RISCY-STUFF/
├── isa_spec/         # isa201.h: shared opcodes, registers, flags, exceptions
├── alu/              # Kogge-Stone adder, Booth multiplier, Soft-Float FPU, flag generation
├── control_unit/     # microcoded control unit and control memory
├── memory_safety/    # stack guard
├── assembler/        # two-pass assembler, op table, CLI tools
├── simulator_core/   # Cpu, decoder, debugger skeleton, CLI, simulator main
│   └── tui/          # risc201-tui: panes, source editor, config dialog, hazard timeline
├── utils/            # shared helpers
├── tests/            # unit and integration tests (CTest)
├── examples/         # 14 sample .asm programs
└── CMakeLists.txt
```

## Team
- **Akaalkirat Singh**: `Cpu` core (integrating the ALU, control unit and stack guard), decoder, simulator runner
- **Divyansh Singh**: Assembler, Disassembler, Debugger
- **Vaibhav Vatsal**: Vertical Control unit, Memory Safety and Stack Visualiser
- **Milan Mohane**: Horizontal Control Unit
- **Arjun Phaneesh**: ALU implementation, ALU Benchmarking
