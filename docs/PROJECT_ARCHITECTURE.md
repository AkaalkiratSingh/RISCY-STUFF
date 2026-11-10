# Processor Architecture and Simulator

**Technical report for project evaluation**  
**Implementation:** C++17, CMake, custom 32-bit instruction set  

The processor uses a **custom ISA**. Its encodings and calling convention are defined by this project; it is not a binary-compatible implementation of the standard RISC-V ISA.

## Reading guide

1. [Specifications](#1-specifications)
2. [System organization](#2-system-organization)
3. [Instruction encoding and addressing](#3-instruction-encoding-and-addressing)
4. [Instruction set and flags](#4-instruction-set-and-flags)
5. [Assembler and program loading](#5-assembler-and-program-loading)
6. [Control ROM and microcode](#6-control-rom-and-microcode)
7. [Datapath and pipeline execution](#7-datapath-and-pipeline-execution)
8. [Hazards and cycle accounting](#8-hazards-and-cycle-accounting)
9. [ALU implementation](#9-alu-implementation)
10. [Stack, calls, and exceptions](#10-stack-calls-and-exceptions)
11. [Reading the traces](#11-reading-the-traces)
12. [Measured program behavior](#12-measured-program-behavior)
13. [Build and demonstration](#13-build-and-demonstration)
14. [Verification](#14-verification)
15. [Architectural decisions and implementation boundaries](#15-architectural-decisions-and-implementation-boundaries)
16. [Source-code map](#16-source-code-map)

## 1. Specifications

| Property | Implemented specification |
|---|---|
| Data word and register width | 32 bits |
| Instruction width | Fixed 32 bits |
| General-purpose register file | 16 registers: R0–R15 |
| R0 convention | Assembler prohibits writing R0; raw encoded instructions are not protected by a CPU write-discard mechanism |
| Link register | R14, containing the return instruction's word index |
| Stack pointer | R15; mirrored in the CPU's SP field |
| Initial SP | 4096 with the default memory size |
| Flags | Zero, negative, carry, overflow, equal, greater-than |
| Data memory | Default 4096 words, equivalent to 16 KiB of 32-bit storage |
| Instruction storage | Separate vector of encoded 32-bit words |
| Data addressing | Word indices |
| Execution PC addressing | Instruction-word indices |
| Encoded branch displacement | Signed byte displacement relative to the following instruction |
| Four-stage pipeline | IF+ID → OF → EX → MEM+WB |
| Six-stage pipeline | IF → ID → OF → EX → MEM → WB |
| Instruction issue model | In-order pipeline, at most one instruction retired per clock |
| Forwarding | Enabled by default; can be disabled |
| Control memory | 79 physical rows, shared across 28 opcode routines and common control paths |
| Horizontal control word | 42 bits |
| Vertical control word | 16 bits; 22 micro-operation codes |
| Control-memory address fields | 8 bits; encoded 255 represents END |
| Runtime exceptions | Invalid memory access, integer divide-by-zero, illegal instruction |
| Implementation dependencies | C++ standard library; terminal UI additionally uses operating-system terminal facilities |

The CPU begins with cleared registers and flags, except R15/SP, which points one word beyond the top of data memory. R0 is intended as the zero register for assembled programs. The distinction between assembler enforcement and CPU enforcement matters when loading manually constructed machine words.

## 2. System organization

```text
Assembly source
      |
      v
Macro expansion ---- source-line tracking
      |
      v
Assembler pass 1 ---- labels and instruction locations
      |
      v
Assembler pass 2 ---- operand resolution and 32-bit encoding
      |
      v
Program image --------------------------> Disassembler
      |
      v
CPU pipeline <---- controls derived from selected control ROM
      |                               horizontal / vertical
      +---- ALU
      +---- register file
      +---- data memory and access guards
      |
      v
CLI / debugger / terminal UI / execution statistics
```

All major components share the opcode, register, flag, and exception definitions in `isa_spec/isa201.h`. This contract connects the assembler's output to the decoder, control unit, and ALU.

The ROM defines the control operations associated with instructions. The CPU supplies the datapath and schedules those operations into pipeline stages. The debugger observes and advances the CPU through its public execution interface.

Program storage and data storage are separate. Consequently, instruction fetch and a load/store do not compete for a single simulated memory port. The model does not include instruction caches, data caches, virtual memory, or variable memory latency.

## 3. Instruction encoding and addressing

### 3.1 Register and immediate instructions

```text
31          27 26 25       22 21       18 17       14 13          0
+-------------+--+-----------+-----------+-----------+-------------+
| opcode (5)  | I|  rd (4)   | rs1 (4)   | rs2 (4)   | unused      |
+-------------+--+-----------+-----------+-----------+-------------+

31          27 26 25       22 21       18 17                      0
+-------------+--+-----------+-----------+-------------------------+
| opcode (5)  | I|  rd (4)   | rs1 (4)   | signed immediate (18)   |
+-------------+--+-----------+-----------+-------------------------+
```

When `I=0`, the second source is a register. When `I=1`, the low 18 bits are sign-extended for use as an immediate. The signed immediate range is −131072 to 131071. Not every instruction uses all fields: MOV, for example, uses its second source field or immediate as the value to copy.

The assembler reports out-of-range immediates as warnings and truncates them to the encoded field width. Such warnings should be inspected during program preparation.

### 3.2 Jump instructions

```text
31          27 26                                               0
+-------------+--------------------------------------------------+
| opcode (5)  | signed displacement (27)                         |
+-------------+--------------------------------------------------+
```

JMP, BEQ, BGT, and CALL use the 27-bit signed displacement field. The assembler maintains byte locations and resolves a label as:

```text
displacement = target_byte_address − (instruction_byte_address + 4)
```

For aligned targets, the CPU's equivalent instruction-index calculation is:

```text
target_word_index = instruction_word_index + 1 + displacement / 4
```

Thus `JMP 0` in a decoded listing means “jump to the following instruction,” not “jump to program address zero.” In `recursive_sum.asm`, the automatically inserted entry jump is located at word index 0 and targets `.main` at word index 1.

An index printed as `pc=4` corresponds to byte location 16 in the assembler's address calculation. Memory dumps, SP, debugger breakpoints, and CPU instruction PCs use word indices. Byte displacements must not be confused with these indices.

### 3.3 Memory operands

`LOAD R1, 3[R2]` reads data memory at word index `R2 + 3`. `STORE R1, 3[R2]` writes R1 to that address. STORE's encoded `rd` field names the source of the stored data; it does not indicate a register destination.

Assembly-generated branch targets are aligned by construction. The current CPU does not provide a distinct misaligned-branch exception for hand-encoded displacements.

## 4. Instruction set and flags

| Opcodes | Instructions | Function |
|---|---|---|
| 0x00 | NOP | Complete without a data result |
| 0x01 | MOV | Copy register/immediate into a register |
| 0x02–0x06 | ADD, SUB, MUL, DIV, REM | Integer arithmetic |
| 0x07–0x0A | AND, OR, XOR, NOT | Bitwise logic |
| 0x0B–0x0D | SHL, SHR, SAR | Left, logical-right, arithmetic-right shift |
| 0x0E–0x0F | LOAD, STORE | Data-memory access |
| 0x10 | CMP | Subtract for flags without register writeback |
| 0x11–0x13 | JMP, BEQ, BGT | Unconditional and conditional control transfer |
| 0x14–0x15 | CALL, RET | Link-register call and return |
| 0x16–0x19 | FADD, FSUB, FMUL, FDIV | Floating-point arithmetic on register bit patterns |
| 0x1A | FCMP | Floating-point comparison |
| 0x1B | FTOI | Convert a float bit pattern to an integer |
| 0x1C | ITOF | Convert an integer to a float bit pattern |
| 0x1F | HALT | Stop fetching and allow the pipeline to drain |

All listed mnemonics except MOV are exposed by the assembler. PUSH and POP are macros, not additional encoded opcodes.

Integer ALU routines update flags when their ROM row asserts `flag_update`. Zero and negative reflect the result. ADD/SUB compute carry and signed overflow; subtraction's carry reflects the carry out of two's-complement addition. Shift counts use the low five bits of the count operand. CMP updates flags without writing a destination register.

BEQ tests the zero flag. BGT tests the greater-than flag. For integer comparison, the ALU derives greater-than using `!Z && (N == V)`. FCMP supplies equality/greater-than results and mirrors equality into the zero flag. MOV, memory-address calculations, floating-point arithmetic, and FTOI/ITOF conversions preserve flags because their ROM rows omit `flag_update`.

Flag updates occur in EX, making the preceding comparison's result available to the next branch. Register-result writes occur at WB; these are different events.

## 5. Assembler and program loading

### 5.1 Source processing

The preprocessor supports `.macro`/`.endm`, arguments, expansion-local labels prefixed with `@`, and the built-in PUSH/POP macros. Expansion tracks original source locations so diagnostics can identify the source line and macro context. Limits on recursion depth and expansion size prevent unbounded macro expansion.

The assembler requires exactly one `.main:` entry label and prepends `JMP .main`. Pass 1 records labels, validates source structure, and advances byte locations. Pass 2 resolves operands and labels and encodes instructions. The disassembler converts encoded instructions back to readable assembly; optional generated labels support inspection of control flow.

### 5.2 Literal data

A `.literals:` section permits named constants, including floating-point literals. The encoded image contains instruction words followed, when needed, by the marker `0xFFFFFFFF` and literal words. The loader separates code from literals and places literal words into data memory starting at index zero.

A literal reference in a LOAD resolves to an offset based on R0. Programs should therefore preserve the zero-register convention. There is no separate protected literal address space: ordinary stores can modify these memory words.

### 5.3 Input paths

The simulator accepts assembly, hexadecimal images, and binary images. Assembly input is preprocessed and assembled before loading. Encoded images are loaded directly and decoded by the processor. The assembler and disassembler are also available as independent command-line executables.

## 6. Control ROM and microcode

### 6.1 Control ownership

The control memory contains actual signal bundles and sequencing information. It is the source from which the CPU derives instruction controls, including front-end operations.

| Signal or signal group | Datapath operation |
|---|---|
| `ir_load` | Fetch the encoded instruction word into its pipeline latch |
| `pc_inc` | Advance the next-fetch PC by one instruction word |
| `instr_decode` | Populate the decoded instruction fields |
| `opcode_dispatch` | Select the instruction's control routine |
| `operand_read` | Capture register operands; RET also captures R14 |
| `alu_enable`, `alu_op`, `alu_src_imm` | Enable/select ALU operation and operand source |
| `flag_update` | Latch ALU flags |
| `mem_read`, `mem_write`, `addr_check` | Describe guarded data-memory accesses |
| `reg_write`, `mem_to_reg` | Select destination writeback and its source |
| `ra_write` | Write the return address to R14 |
| `pc_write`, PC-source signals | Select control-transfer behavior |
| `halt`, `trap` | Describe stopping and exception paths |

PUSH/POP behavior is produced through ordinary SUB/STORE and LOAD/ADD instructions rather than dedicated stack control signals.

### 6.2 Shared front end and complete instruction flow

The instruction opcode is unknown before fetch and decode. The ROM therefore contains a shared prefix:

```text
84: ir_load pc_inc                    → 85
85: instr_decode opcode_dispatch      → decoded opcode's routine
```

For ADD, the instruction-specific portion is:

```text
1: operand_read                       → 2
2: alu_enable alu_op=ADD flag_update   → 3
3: reg_write                          → END
```

The complete logical flow is `84 → 85 → 1 → 2 → 3 → END`. The shared rows are stored once. The generated instruction listings repeat references to them so that a reader can inspect an instruction's entire flow in one place.

LOAD follows operand read, address calculation, guarded memory read, and memory-to-register writeback. STORE ends after the guarded memory write. BEQ and BGT include a condition row and explicit taken/not-taken successors. A not-taken branch's idle END row retains the sequential PC established by the common fetch control.

RET uses separate OF operand-read and EX redirect rows. Each executed row consumes one clock for that instruction. FTOI and ITOF each use operand-read, ALU, and writeback rows, without flag updates.

### 6.3 Horizontal and vertical encoding

The horizontal word is 42 bits. Individual bits encode control lines, while fields encode ALU selection, branch condition, and successor addresses. The front-end controls occupy bits 38–41. The full bit assignment is recorded in the generated horizontal listing.

The vertical word is 16 bits. Operate words select a micro-operation from a 22-entry decoder and optionally an ALU function. Branch words encode a condition and taken address. Sequential successors are implicit; END terminates the current sequence. The dispatch control provides the dynamic transition from the common prefix to the decoded instruction's routine.

The vertical ROM is generated from the horizontal definitions and decoded back for validation. A row that cannot be represented without losing information is rejected. This makes the two encodings alternative representations of the same control design.

| Encoded storage | Horizontal | Vertical |
|---|---:|---:|
| Physical rows | 86 | 86 |
| Bits per row | 42 | 16 |
| Total encoded bits | 3612 | 1376 |
| Byte-equivalent encoded capacity | 451.5 | 172 |

The vertical representation reduces encoded ROM capacity by approximately 62%. These figures describe the logical packed ROM, not C++ object sizes or total hardware area. The decoder and dispatch structures also require storage/logic. The simulator does not model their electrical delay, so these numbers do not establish a clock-frequency advantage.

### 6.4 How ROM controls reach the pipeline

`control_decode.cpp` classifies each ROM row by stage and also derives `StageControls` bundles used for dependency checks. Conditional microcode paths retain both successors and their branch condition.

Every in-flight instruction owns a microPC. The CPU executes its current row, follows the row's successor, and moves the latch to the slot for the next row. Stages without a row are skipped. Pipeline movement, interlocks, flushing, and clock accounting remain CPU responsibilities.

This is a ROM-row-clocked pipelined controller. An instruction executes at most one row per clock, while different instructions can execute rows in different slots during the same clock.

The complete reference files are [horizontal control ROM](CONTROL_ROM/control_rom_horizontal.txt) and [vertical control ROM](CONTROL_ROM/control_rom_vertical.txt).

## 7. Datapath and pipeline execution

### 7.1 Stage layouts

```text
4-stage: [ IF + ID ] → [ OF ] → [ EX ] → [ MEM + WB ]
6-stage: [ IF ] → [ ID ] → [ OF ] → [ EX ] → [ MEM ] → [ WB ]
```

The four-stage design serializes the IF and ID rows in one `IF+ID` slot, keeps OF and EX in separate slots, and merges MEM with WB. The six-stage design keeps every row in its own slot, so different instructions can occupy them simultaneously.

| Stage | Work |
|---|---|
| IF | Bounds-check instruction fetch, capture encoded word, advance next-fetch PC |
| ID | Decode fields, identify opcode routine, mark invalid instructions, recognize HALT |
| OF | Capture operands authorized by ROM controls |
| EX | Run ALU, update flags, resolve branches and returns; a branch uses a condition row and a successor row |
| MEM | Validate and perform data-memory access |
| WB | Commit register results or the link register; instructions retire at their last ROM row |

Instruction words, decoded fields, operand values, ALU results, loaded data, and fault information are carried in pipeline latches. This distributed state plays the role of internal instruction and operand registers; the simulator does not expose a single global bank of named IR/A/B/MAR/MDR registers shared by every in-flight instruction.

### 7.2 One simulation clock

`Cpu::step()` advances one clock. In-flight instructions execute at most one ROM row each, oldest first, then move toward the slot containing their next row. An older branch or fault can suppress younger work during the same clock. A dependency holds its consumer until the required result is available under the selected forwarding policy.

`stepInstruction()` advances clocks until the next retirement or halt, subject to its internal guard. `run()` advances until fetching has stopped and the pipeline has drained. HALT itself is excluded from the retired-instruction count.

There are two useful PC views: `fetch_pc_` identifies the next word to fetch, while the architectural state PC records the last instruction reaching the normal retirement path. Exception reports preserve a separate faulting PC. These values need not be equal while execution overlaps.

## 8. Hazards and cycle accounting

### 8.1 Data dependencies

Forwarding is enabled by default. Operand reads use ready values from the youngest matching older producer; a dependency stalls while its result is unavailable. With `--no-forward`, consumers wait for older dependencies to complete. ALU results and completed loads can be forwarded to register operands and store data; return-address dependencies are also tracked.

The reported `stalls` count covers these data interlocks. It does not include every clock spent waiting for a busy slot, and the four-stage `IF+ID` slot's two row clocks are normal work rather than stalls.

### 8.2 Control dependencies

The front end fetches sequentially until EX resolves a taken transfer. The CPU then redirects fetch and invalidates younger instructions. This corresponds to a sequential/not-taken fetch policy, without a dynamic branch predictor.

CALL and RET use the link register and resolve in EX. Squashed instructions do not retire. `flush bubbles` now counts discarded younger latches, which depends on their actual occupancy when a transfer resolves. HALT stops subsequent fetching while allowing older work to complete.

### 8.3 Cycle accounting

Cycle counts depend on executed ROM rows, slot contention, interlocks, and redirects. In the four-stage layout, every completed instruction needs IF and ID clocks in the shared `IF+ID` slot; OF and EX have their own slots, and MEM+WB shares the tail slot. For `recursive_sum.asm`, the 78 completed paths including HALT require 78 IF and 78 ID rows: at least 156 `IF+ID`-slot clocks before redirect and fill/drain overhead. The observed forwarding run takes 185 clocks.

## 9. ALU implementation

Integer addition uses eight four-bit carry-lookahead blocks chained into a 32-bit adder. Subtraction uses two's-complement addition. Multiplication uses radix-4 Booth recoding, selecting additions/subtractions of the multiplicand or twice the multiplicand across sixteen recoding groups. The full intermediate product is 64 bits; the architectural register result is the low 32 bits.

Integer division/remainder use a bit-by-bit unsigned magnitude algorithm with sign restoration. Division by zero produces `DIVIDE_BY_ZERO`. The signed minimum-integer divided by −1 case is handled explicitly; it does not raise a separate overflow exception. Arithmetic overflow is represented through flags where computed rather than through an overflow trap.

Floating-point operations manipulate IEEE-754 single-precision bit layouts stored in ordinary integer registers. The implementation includes exponent alignment, normalization, mantissa multiplication, and Newton–Raphson reciprocal approximation for division. It is an educational soft-float implementation: it does not claim complete IEEE-754 rounding-mode, subnormal, NaN, or exception behavior. FDIV does not use the integer divide-by-zero trap path.

All ALU operations occupy one ROM EX row. FTOI and ITOF convert between integer values and single-precision bit patterns without updating flags. Software loops used to compute multiplication or division do not add simulated pipeline clocks. The cycle model describes this simulator's row scheduling, not the physical latency of a synthesized multiplier or divider.

## 10. Stack, calls, and exceptions

### 10.1 Stack and calling convention

The stack is full-descending. SP begins at 4096 and points at the most recently pushed word once nonempty.

```text
PUSH R1 expands to:       POP R1 expands to:
  SUB SP, SP, 1            LOAD R1, 0[SP]
  STORE R1, 0[SP]          ADD SP, SP, 1
```

CALL stores the next instruction's word index in R14 and redirects execution. RET jumps through R14. Neither instruction automatically creates a stack frame. Nested calls save R14 with PUSH and restore it with POP. The register-file copy of R15 and the CPU's SP field are synchronized when R15 writes commit.

### 10.2 Fault handling

| Exception | Trigger | Behavior |
|---|---|---|
| `INVALID_MEMORY_ACCESS` | Instruction fetch outside code or data access outside memory | Suppress invalid access, stop new execution, retain fault context |
| `DIVIDE_BY_ZERO` | Integer DIV/REM with zero divisor | Suppress arithmetic result writeback |
| `ILLEGAL_INSTRUCTION` | Opcode with no supported routine | Follow illegal-instruction handling rather than treating it as normal HALT |

Runtime pipeline faults discard the faulting instruction and younger work while older instructions can drain. The CLI reports the exception, faulting instruction PC, detection slot, and address when available. An out-of-range instruction fetch is detected in the fetch slot after older instructions have had a chance to redirect or stop fetching.

The standalone control sequencers accept fault inputs, suppress a faulting row's controls, and redirect to TRAP. The pipelined CPU enforces its faults directly in EX/MEM and uses the microcode reconstruction to display the corresponding trap path. It does not spend an additional pipeline clock executing the TRAP ROM row.

Memory access checks prevent out-of-bounds reads and writes. They do not roll back earlier instructions in a macro. A rejected PUSH can leave SP at `0xFFFFFFFF` because its SUB committed before the STORE failed. Stack and literal data share the same data-memory array; no automatic stack-versus-global collision detector or call-frame protection is implemented.

A separate `StackGuard` supplies stack-bound checks, and `StackVisualizer` can render stack memory as text. Current ordinary stack macros are protected through the LOAD/STORE memory-bound checks; the standalone visualizer is not wired into the simulator CLI.

## 11. Reading the traces

### 11.1 Three different observation levels

| Interface | What advances or is displayed |
|---|---|
| `--trace` | Pipeline snapshots and hazard events after each simulated clock |
| `-s` / instruction step | Register snapshots associated with instruction-level advancement |
| `--micro-trace` / `--micro-step` | Reconstructed control-ROM path for an instruction at retirement or fault |
| TUI assembled-binary tags | Each in-flight instruction shows its actual next control-ROM row, e.g. `◄[EX 29]` |

The microcode view includes the shared fetch/decode prefix, the selected instruction's routine, branch successors based on captured execution flags, and trap controls when applicable. It prints actual ROM words and signal definitions, but the sequence is reconstructed for explanation. Squashed instructions are omitted.

The TUI does not run a serial global microPC. The assembled-binary pane reads each in-flight instruction's own microPC and shows its next ROM row. If one PC is active in several slots, all annotations are shown. The CONTROL ROM pane inserts a visual blank line after each instruction routine without changing any ROM address. It marks every row currently being executed by an in-flight instruction with a pointer and highlight, and does not auto-follow any single microPC.

### 11.2 Why the first microcode output is labelled cycle 4

In `recursive_sum.asm`, the first encoded instruction is the assembler-generated entry JMP. In the four-stage pipeline it progresses as follows:

| Clock | Action for the first JMP |
|---:|---|
| 1 | Admit the instruction into the `IF+ID` slot |
| 2 | Execute the IF row in `IF+ID` |
| 3 | Execute the ID row in `IF+ID` |
| 4 | Execute the jump row in EX; retire and emit the reconstructed path |

Its reconstructed rows all carry the clock-4 label, including the fetch row. This label is the instruction's completion/fault clock, not the clock at which every displayed signal originally acted. The JMP skips MEM and WB because its routine has no rows there.

For example:

```text
completed/fault cycle=4 pc=0 JMP microPC=84 -> 85 ... signals: ir_load pc_inc
completed/fault cycle=4 pc=0 JMP microPC=85 -> 29 ... signals: instr_decode opcode_dispatch
completed/fault cycle=4 pc=0 JMP microPC=29 -> END ... signals: pc_write
```

`pc` identifies the program instruction. `microPC` identifies a control-memory row. `END` ends the routine, not the program. The label `completed/fault` accommodates either event; its presence alone does not indicate an exception.

Enter in `--micro-step` advances through these reconstructed rows. The displayed cycle is the completion clock for the whole instruction; each row actually executed in its own clock. The `--trace` display supplies pipeline-clock context.

### 11.3 Recursive sum control flow

The program starts with R1=5 and recursively computes the sum of integers down to zero. BEQ selects the base case after ADD sets the zero flag. Each nonzero call saves R14 and the current input on the stack, then restores them before adding the partial result. The completed run has R2=15 and SP=4096.

The repeated microcode addresses in the trace are expected: the same ROM routine implements every execution of a given opcode, even when its program PC or register values differ. BEQ's ROM condition row selects row 33 for not taken or row 34 for taken.

## 12. Measured program behavior

The row-clock executor supports forwarding and conservative interlock modes. Horizontal and vertical ROMs produce the same architectural results. Cycle counts depend on the selected pipeline layout and dependency pattern.

Clock counts alone do not establish hardware execution time because physical clock periods are not modeled.

## 13. Build and demonstration

Run the following commands from the `RISCY_STUFF/` directory. A C++17 compiler and CMake 3.16 or newer are required. The default build enables `-Wall -Wextra` and uses Debug mode unless a build type is supplied.

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

The executables are:

| Executable | Purpose |
|---|---|
| `build/simulator_core/risc201-sim` | Batch simulation and textual traces |
| `build/simulator_core/tui/risc201-tui` | Interactive terminal debugger |
| `build/assembler/risc201-asm` | Assemble source to a machine image |
| `build/assembler/risc201-disasm` | Disassemble a machine image |
| `build/control_unit/risc201-rom` | Export or verify ROM documentation |

### 13.1 Suggested evaluation sequence

First show machine encoding and final behavior:

```bash
./build/simulator_core/risc201-sim examples/recursive_sum.asm -4 -d
```

Expected result: R2=15, 77 retired instructions, 243 clocks, no exception. The decoded listing includes the assembler-generated entry JMP.

Then inspect the pipeline and ROM separately:

```bash
./build/simulator_core/risc201-sim examples/recursive_sum.asm -4 --trace
./build/simulator_core/risc201-sim examples/recursive_sum.asm -4 --micro-step
./build/simulator_core/risc201-sim examples/recursive_sum.asm -6 --vertical --micro-trace
```

Use Enter to advance the microcode view or `q` to stop. Open the generated ROM files to compare the packed word representations and complete instruction paths.

Compare data hazards with forwarding enabled and disabled:

```bash
./build/simulator_core/risc201-sim examples/pointer_walk.asm -4
./build/simulator_core/risc201-sim examples/pointer_walk.asm -4 --no-forward
```

Demonstrate nested calls, literals, and an intentional exception:

```bash
./build/simulator_core/risc201-sim examples/recursive_sum.asm -6
./build/simulator_core/risc201-sim examples/float_math.asm -4 -m 0:12
./build/simulator_core/risc201-sim examples/stack_overflow.asm -4
```

The stack-overflow example is expected to report `INVALID_MEMORY_ACCESS` and return a failing exit status. The wrapped SP described in Section 10 is an expected current limitation, not a successful stack access.

To demonstrate the standalone toolchain:

```bash
./build/assembler/risc201-asm examples/recursive_sum.asm -o /tmp/riscy-sum.hex
./build/assembler/risc201-disasm /tmp/riscy-sum.hex -o /tmp/riscy-sum.asm
./build/simulator_core/risc201-sim /tmp/riscy-sum.hex
```

To regenerate the documentation from the actual ROM:

```bash
cmake --build build --target export_control_rom
./build/control_unit/risc201-rom --check docs/CONTROL_ROM
```

### 13.2 Interactive debugger

```bash
./build/simulator_core/tui/risc201-tui examples/recursive_sum.asm
```

The terminal UI displays assembly, encoded words, registers, memory, statistics, and an optional hazard timeline. It uses the Debugger class for stepping, breakpoint handling, and watchpoint handling. Representative commands are:

| Command | Meaning |
|---|---|
| `:step`, `:clock`, `:run` | Instruction advance, one clock, or continuous execution |
| `:break 3` | Toggle a breakpoint at instruction word index 3 |
| `:break 3 if R1 == 21` | Conditional breakpoint |
| `:watch reg R1` | Stop when the observed register value changes |
| `:watch mem 0` | Stop when the observed memory word changes |
| `:breaks`, `:watches` | Inspect configured stops |
| `:reset` | Clear runtime state and reload the program |
| `:mem 0:16` | Inspect a data-memory range |
| `:config stage 6` | Select the six-stage pipeline and restart |
| `:config rom vert` | Select vertical ROM and restart |
| `:config binary decimal\|hex\|float` | Display register and memory words as signed decimal integers, hexadecimal, or IEEE-754 single-precision values; applies without resetting execution |
| `:trace` | Toggle hazard timeline |

Watchpoints detect value changes, not every access to an address. Breakpoints are fetch barriers: when the next fetch PC matches a breakpoint, the debugger holds fetch, drains the older instructions, and reports the stop only if that PC is still next. No instruction at or after the breakpoint enters the pipeline, and resuming starts from an empty pipeline at that PC.

The source editor requires explicit reassembly. A stale-buffer indication distinguishes edited text from the loaded program. `:save` writes the buffer back to the loaded file or a supplied path, independently of assembly. Command input supports Tab completion for command names and `:load` paths. Failed `:assemble` and `:load` commands list all collected errors with source line numbers and leave the current machine image intact. Both commands also show assembler warnings after success or failure, in yellow below the status or errors. PageUp/PageDown scrolls a diagnostic list that exceeds the feedback pane.

### 13.3 Run limits and exit statuses

Trace modes stop after 4000 pipeline clocks. Some larger examples therefore produce incomplete traces unless run without trace mode. Plain batch `run()` has no execution limit and can run indefinitely on a looping program. The debugger has its own bounded continuation interface.

The simulator returns 0 for normal completion, 1 for an exception/input error, and 2 for an incomplete or interrupted run. `-s` prints successive instruction-level states; interactive row-by-row prompting belongs to `--micro-step`.

## 14. Verification

The suite registers **20 CTest tests**. It covers the following areas:

| Area | Tests |
|---|---|
| Horizontal/vertical controls | `HorizontalControlUnitTest`, `VerticalControlUnitTest`, `ControlDecodeTest` |
| ALU and ROM-to-ALU integration | `AluTest`, `ControlUnitAluIntegration` |
| Assembler and decoder | `Pass1Test`, `Pass2Test`, `EncoderTest`, `DecodeTest`, `DisassemblerTest` |
| Macros and literal loading | `PreprocessorTest`, `LiteralsTest` |
| Memory and complete CPU programs | `MemorySafetyTest`, `CpuIntegrationTest` |
| Pipeline results, hazards, timing | `PipelineTest` |
| Debugger behavior | `DebuggerTest` |
| Microcode reconstruction and CLI | `MicroTraceTest`, `MicroCliTest` |
| TUI assembly diagnostics | `TuiProgramLoaderTest` |
| Generated ROM files | `ControlRomListings` |

Control-collapse tests compare ROM-derived signals with the bundles used for dependency checks. Encoding tests compare decoded vertical rows with horizontal rows, including the shared fetch/decode controls. Pipeline tests check row timing, architecture results, and squash accounting. Microcode tests check branch paths, exceptions, squashed illegal instructions, and preservation of execution results when tracing is enabled.

The vertical-control tests compare both encodings across executable example programs and the pipeline configurations. A source file with assembly errors cannot enter that comparison until corrected.

The measurements in Section 12 reflect the row-clock executor. Passing these tests establishes the exercised behavior; it is not a proof of exhaustive ISA correctness, electrical timing, or complete IEEE-754 compliance. The interactive terminal rendering itself is primarily exercised manually.

## 15. Architectural decisions and implementation boundaries

| Decision | Reason and consequence |
|---|---|
| Custom fixed-width instruction set | Makes encoding, decoding, and control dispatch explicit and manageable; does not provide compatibility with external RISC-V binaries |
| ROM-defined front-end and instruction controls | Keeps fetch, decode, operand access, execution, and writeback visible in the control description |
| Shared fetch/decode prefix | Fetches and decodes before selecting an opcode routine; avoids physically duplicating common ROM rows |
| Per-instruction microPC | Executes one ROM row per instruction per clock while allowing instructions in different slots to overlap |
| Layout-driven four/six-stage CPU | Shares execution, memory, and writeback logic while varying front-end partitioning |
| Separate instruction/data storage | Avoids a single-memory-port structural conflict and simplifies literal loading |
| Sequential speculative fetch | Keeps branch behavior simple and exposes flush costs without a dynamic predictor |
| Link-register CALL/RET | Uses R14 for return addresses; software explicitly saves it for nested calls |
| Stack operations as macros | Exposes stack changes as ordinary instructions; macros are not atomic rollback units |
| Equivalent horizontal/vertical ROMs | Enables an encoded-capacity comparison while preserving functional behavior |
| Reconstruction at retirement/fault | Provides readable ROM paths; the displayed cycle is the completion clock, while the TUI shows live next-row addresses |
| Single modeled EX action for arithmetic | Keeps pipeline timing consistent across ALU operations; does not represent algorithm-dependent hardware latency |

The main implementation boundaries are the reconstructed rather than live microcode trace, lack of automatic stack-frame/collision protection, partial floating-point special-case handling, and the difference between the R0 assembler convention and raw-machine-code execution. These boundaries should be considered when selecting evaluation programs and interpreting results.

## 16. Source-code map

All links are relative to this report in `docs/`.

| Source | Responsibility |
|---|---|
| [isa201.h](../isa_spec/isa201.h) | Shared ISA, register, flag, and exception definitions |
| [assembler.cpp](../assembler/src/assembler.cpp) | Two-pass assembly, encoding, image handling |
| [op_table.h](../assembler/include/assembler/op_table.h) | Mnemonics and operand grammar |
| [preprocessor.cpp](../assembler/src/preprocessor.cpp) | Macros, built-in stack expansion, origin tracking |
| [disassembler.cpp](../assembler/src/disassembler.cpp) | Machine-word-to-assembly conversion |
| [horizontal_control_unit.cpp](../control_unit/src/horizontal_control_unit.cpp) | Signal definitions, ROM construction, routine sequencer, packing |
| [vertical_control_unit.cpp](../control_unit/src/vertical_control_unit.cpp) | Vertical encoder/decoder and equivalent sequencer |
| [export_control_rom.cpp](../control_unit/src/export_control_rom.cpp) | Generated physical and per-instruction ROM references |
| [control_decode.cpp](../simulator_core/src/control_decode.cpp) | Shared front-end extraction and instruction-control collapse |
| [pipeline_types.h](../simulator_core/include/simulator_core/pipeline_types.h) | Stage layouts, latches, hazard records |
| [cpu.cpp](../simulator_core/src/cpu.cpp) | Pipeline scheduling, forwarding, memory operations, retirement, faults |
| [decoder.cpp](../simulator_core/src/decoder.cpp) | Instruction field extraction and sign extension |
| [alu.cpp](../alu/src/alu.cpp) | Integer and floating-point algorithms |
| [stack_guard.cpp](../memory_safety/src/stack_guard.cpp) | Stack bounds helper |
| [visualizer.cpp](../memory_safety/src/visualizer.cpp) | Standalone ASCII stack rendering |
| [debugger.cpp](../simulator_core/src/debugger.cpp) | Breakpoints, watchpoints, stepping, reset |
| [main.cpp](../simulator_core/src/main.cpp) | Simulator CLI and trace formatting |
| [tui.cpp](../simulator_core/tui/src/tui.cpp) | Interactive terminal presentation and commands |
| [tests/CMakeLists.txt](../tests/CMakeLists.txt) | Registered verification targets |

For a focused source walkthrough, start with the shared ISA, read one complete ADD or LOAD flow in the ROM listings, follow its row stages through `control_decode.cpp`, and then locate the corresponding row effects in `cpu.cpp`. The recursive-sum demonstration connects that static design to an observable program result and a row-clock cycle count.
