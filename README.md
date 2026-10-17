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
- **Microcoded control unit:** each instruction runs as a routine of micro-operations stored in a control memory, using a horizontal 40-bit microword.
- **ALU built from gate-level ideas:** a 32-bit carry-lookahead adder (eight chained 4-bit CLA blocks) and a radix-4 Booth multiplier.
- **Stack guard:** a full-descending stack with bounds checking on `PUSH`, `POP`, `CALL` and `RET`, raising `STACK_OVERFLOW` / `STACK_UNDERFLOW` before the access commits.
- **Two-pass assembler** with labels, symbol table, errors and warnings, plus `.bin` / `.hex` output.
- **Simulator CLI** with a decode listing, single-step mode and memory dumps.
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

This produces three executables:

| Binary | Location | Purpose |
|--------|----------|---------|
| `risc201-sim` | `build/simulator_core/` | Assemble (if needed) and run a program |
| `risc201-asm` | `build/assembler/` | Assemble `.asm` into `.bin` or `.hex` |
| `risc201-disasm` | `build/assembler/` | Disassembler (placeholder, see roadmap) |

## Usage

### Run a program

```bash
build/simulator_core/risc201-sim examples/11_fibonacci.asm
```

`.asm` input is assembled first. `.hex` and `.bin` input is decoded by the simulator itself. Output is the final register file, PC/SP, flags, cycle and retired-instruction counts, and any exception.

```
usage: risc201-sim <program.asm|program.hex|program.bin> [options]

  -4, --4stage    select the 4-stage pipeline variant (default)
  -6, --6stage    select the 6-stage pipeline variant
  -s, --step      single-step, printing state after each instruction
  -d, --decode    print the decoded instruction listing before running
  -m, --mem S:C   dump C data-memory words starting at word index S
  -h, --help      show help
```

### Assemble to a file

```bash
build/assembler/risc201-asm examples/17_recursive_sum.asm -o sum.hex
build/simulator_core/risc201-sim sum.hex
```

### Example program

`examples/17_recursive_sum.asm` computes `sum(5)` recursively using `CALL`/`RET` and `PUSH`/`POP`:

```asm
ADD R1, R0, 5 ; R1 = 5
CALL sum
HALT

sum:
    ADD R2, R1, R0     ; R2 = n, sets Z when n == 0
    BEQ zero
    PUSH R1            ; save n across the recursive call
    SUB R1, R1, 1
    CALL sum
    POP R1             ; R1 = saved n
    ADD R2, R2, R1     ; R2 = sum(n-1) + n
    RET
zero:
    ADD R1, R0, 0      ; R1 = 0
    RET
```

Result: `R2 = 15`, `SP` back at 4096.

The `examples/` folder has 17 programs: arithmetic and logic, flags and overflow, memory round-trips, branches and loops, multiplication by repeated addition, Fibonacci, bit masks and shifts, and stack usage. `16_stack_overflow.asm` triggers `STACK_OVERFLOW` on purpose.

## The ISA

| Property | Value |
|----------|-------|
| Word size | 32 bits |
| Instruction width | 32 bits, fixed |
| Registers | 16 general purpose (`R0` to `R15`) |
| Data memory | 4096 words by default, word-addressed |
| Program memory | Held separately from data memory |
| Stack | Full-descending, `SP` starts at the top of data memory |
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
| Arithmetic | `ADD`, `SUB` |
| Logic | `AND`, `OR`, `XOR` |
| Shifts | `SHL`, `SHR` |
| Memory | `LOAD rd, imm[rs1]`, `STORE rd, imm[rs1]` |
| Control flow | `JMP`, `BEQ`, `CALL`, `RET` |
| Stack | `PUSH`, `POP` |
| Misc | `NOP`, `HALT` |

`NOT`, `SAR`, `CMP`, `MOV` and `MUL` are reserved in the ISA spec. The ALU already implements `MUL`, but the assembler and control unit do not expose these yet.

### Exceptions

`STACK_OVERFLOW`, `STACK_UNDERFLOW`, `INVALID_MEMORY_ACCESS`, `DIVIDE_BY_ZERO`, `ILLEGAL_INSTRUCTION`

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
├── utils/            # shared helpers
├── tests/            # unit and integration tests (CTest)
├── examples/         # 17 sample .asm programs
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
| `ControlUnitTest` | Microcode routines and control words |
| `DecodeTest` | Instruction decoding |
| `Pass1Test`, `Pass2Test` | Assembler label pass and resolve/encode pass |
| `EncoderTest` | Instruction word encoding |
| `MemorySafetyTest` | Stack guard bounds and exceptions |
| `CpuIntegrationTest` | End-to-end program execution |

## Status and roadmap

**Working**
- [x] Shared ISA specification
- [x] ALU with CLA adder and Booth multiplier
- [x] Microcoded control unit (horizontal encoding)
- [x] Stack guard with overflow/underflow detection
- [x] Two-pass assembler with `.bin` / `.hex` output
- [x] Decoder, CPU execution loop and simulator CLI

**In progress / planned**
- [ ] Pipeline modelling: the 4-stage and 6-stage variants can be selected, but execution is currently one instruction at a time
- [ ] Debugger: breakpoints and watchpoints are stubbed, not yet wired up
- [ ] Per-cycle microPC and control-word display
- [ ] Wire the stack visualizer into the simulator output
- [ ] Complete the ISA, with end to end implementation
- [ ] Vertical microcode encoding 
- [ ] Disassembler and assembler preprocessor/macros
- [ ] Floating-point (IEEE 754) extension

## Team

RISCY-STUFF is a team project.

- **Akaalkirat Singh**: `Cpu` core (integrating the ALU, control unit and stack guard), decoder, simulator runner, and most of the tests
- _Add teammates and their modules here_

## License

_Not yet specified._