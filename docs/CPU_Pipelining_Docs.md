# RISCY-STUFF (risc201): Pipelining and CPU Execution

*Source: github.com/AkaalkiratSingh/RISCY-STUFF, read from the repository HEAD (5 Nov 2026). Pipeline timings in section 8 were measured by building and running the simulator.*

## 1. Overview

risc201 is a custom 32-bit ISA with a C++17 simulator. Its CPU is not a conventional "one instruction per stage per clock" pipeline. It is a **ROM-row-clocked pipelined controller**:

- Every instruction is a small *microcode routine*: a chain of rows in a control ROM (horizontal 48-bit or vertical 16-bit encoding, selectable at runtime).
- Every in-flight instruction carries its **own micro-PC**.
- Each clock, an instruction executes **at most one ROM row**, then moves to the pipeline slot that owns its *next* row.
- Several instructions overlap, each at a different point in its own routine.

The CPU (`simulator_core/src/cpu.cpp`) supplies the datapath and the scheduling (slots, hazards, flushes). The control ROM (`control_unit/`) decides *what* each row does. `simulator_core/src/control_decode.cpp` is the glue that classifies each ROM row into a pipeline stage.

| Property | Value |
| --- | --- |
| Word / instruction width | 32 bits, fixed |
| Registers | R0–R15 (R14 = link register, R15 = stack pointer) |
| Flags | Z, N, C, V (plus equal / greater-than for compares) |
| Pipeline variants | 4-stage (`FETCH → EX → MEM → WB`) and 6-stage (`IF → ID → OF → EX → MEM → WB`) |
| Issue / retire | In order, at most one retirement per clock |
| Branch policy | Sequential fetch (predict not-taken), flush when EX resolves a taken transfer |
| Data hazards | Interlock plus optional forwarding (default on) |
| Memory | Separate instruction and data storage, word-addressed, 4096 data words by default |

## 2. From instruction to micro-rows

### 2.1 The shared front end

The opcode is unknown until the instruction has been fetched and decoded, so every instruction starts with the same two ROM rows:

```
84: ir_load pc_inc                 -> 85     (fetch word, advance fetch PC)
85: instr_decode opcode_dispatch   -> opcode's routine
```

The dispatch row jumps to the opcode-specific routine. For `ADD` the complete path is:

```
84 -> 85 -> 1: operand_read -> 2: alu_enable alu_op=ADD flag_update -> 3: reg_write -> END
```

`LOAD` follows the same shape with an address-calculation ALU row, a guarded memory-read row and a `mem_to_reg` writeback row. `STORE` ends after the guarded memory write. `BEQ`/`BGT` have a condition row with separate taken / not-taken successors. `RET` reads R14 in an OF row and redirects in a separate EX row. `FTOI` and `ITOF` use operand-read, ALU and writeback rows with no flag update.

### 2.2 Which stage owns a row

`rowStage()` assigns each row to a stage by checking its control signals in this priority order:

| Check (first match wins) | Stage |
| --- | --- |
| `ir_load` | IF |
| `instr_decode` or `opcode_dispatch` | ID |
| `halt` or `trap` | Terminal (stays where it is) |
| `mem_read` or `mem_write` | MEM |
| `reg_write`, `ra_write` or `mem_to_reg` | WB |
| `operand_read` | OF |
| anything else (ALU, flags, PC write) | EX |

`slotForRow()` then maps that stage to a physical slot in the chosen layout. A stage with no row in an instruction's routine is simply skipped, so a `JMP` never occupies MEM or WB.

## 3. The two pipeline layouts

```
4-stage: [ IF + ID + OF ] -> [ EX ] -> [ MEM ] -> [ WB ]
6-stage: [ IF ] -> [ ID ] -> [ OF ] -> [ EX ] -> [ MEM ] -> [ WB ]
```

Both layouts share the same EX / MEM / WB tail. They differ only in the front end:

|  | 4-stage | 6-stage |
| --- | --- | --- |
| Front-end slot(s) | One `FETCH` slot executes the IF, ID and OF rows on **three separate clocks** | Three slots; different instructions can be in IF, ID and OF in the same clock |
| Front-end throughput | At most one instruction enters EX every \~3 clocks | Up to one per clock |
| Data hazards | Rarely visible: the producer has usually left the front end before the consumer reads operands | Frequent: consumer reaches OF while the producer is still in EX/MEM |
| Flush cost | Small (few younger instructions in flight) | Larger (deeper front end holds more younger work) |

The layouts are plain data (`kFourStageLayout`, `kSixStageLayout` in `pipeline_types.h`); each slot is a name plus six flags saying which row types it executes. Adding another variant means adding another table.

## 4. Per-instruction state: the `Latch`

There is no single global IR / A / B / MDR register set. Each in-flight instruction owns a `Latch`:

| Field group | Fields |
| --- | --- |
| Identity | `seq` (age, monotonically increasing), `pc` (word index), `word` (encoded instruction) |
| Decode | `instr` (decoded fields), `decoded` |
| Sequencing | `micro_pc` (its current ROM row), `row_cycle` |
| Progress flags | `alu_done`, `mem_done`, `wb_done`, `ra_done` |
| Data | `rs1_val`, `rs2_val`, `rd_val` (store data), `ra_val` (R14), `alu_result`, `mem_data` |
| Control outcome | `execution_flags`, `redirect`, `next_pc`, `fault`, `fault_code` |

The `*_done` flags are what the hazard logic checks to decide whether a result can be forwarded. `seq` defines "older" and "younger" for ordering, squashing and retirement.

## 5. One clock: `Cpu::tick()`

`Cpu::step()` increments the cycle counter and calls `tick()`. The algorithm:

1. **Order.** Collect all valid latches and sort them **oldest first** by `seq`.
2. **Squash check.** If an older instruction already redirected or faulted this clock (`squash_after`), every younger latch is discarded and counted.
3. **Slot check.** Look up the latch's current ROM row and the slot that owns it. If the latch is not in that slot (it was blocked earlier), it tries to move there without executing; if the destination is occupied by an older instruction, it stays.
4. **Stall check** (section 6.1). If the row cannot execute, the latch stays put and `stalls` increments.
5. **Execute the row.** Row effects are applied in this fixed order, each only if its control signal is set and no error has occurred:
   1. `ir_load`: bounds-check the PC, read the instruction word; `pc_inc` sets `fetch_pc_ = pc + 1`.
   2. `instr_decode`: decode the word into fields.
   3. `operand_read`: read rs1, rs2, rd and R14 from the register file, then apply forwarding.
   4. `alu_enable`: run the ALU; store `alu_result`; if `flag_update`, latch the flags.
   5. `mem_read` / `mem_write`: bounds-checked access to data memory at `alu_result`.
   6. `reg_write`: write `mem_data` or `alu_result` to `rd`; mirror R15 writes into `SP`.
   7. `ra_write`: write `pc + 1` into R14 (CALL).
   8. `trap`: raise `ILLEGAL_INSTRUCTION`.
6. **Pick the successor.** Follow the row's `next`, or `branch_target` if its branch condition holds on the current flags. A dispatch row jumps to the routine entry for the decoded opcode.
7. **Redirect / halt side effects** (section 6.3).
8. **Advance.** If the successor is `END`, the instruction retires (`state.pc = pc`, `retired++`, HALT excluded). Otherwise the latch moves to the slot for its next row, or stays if that slot is blocked.

After all latches are processed, the new pipeline state replaces the old, and if fetch is not stopped, held or blocked, a new instruction is **admitted** into slot 0 at `fetch_pc_` with `micro_pc` at the shared front-end entry. When fetch has stopped and the pipeline is empty, the CPU is `halted`.

### Where architectural state changes

| State | Updated by | Stage |
| --- | --- | --- |
| Flags | ALU row with `flag_update` | EX |
| Data memory | `mem_write` row | MEM |
| Register file (rd) | `reg_write` row | WB |
| R14 (link register) | `ra_write` row | WB |
| Fetch PC | `pc_inc` (sequential) or `pc_write` (redirect) | IF / EX |
| Architectural PC | Last instruction to retire | WB / END |

Note that flags are updated in EX while registers are updated in WB, so a compare's result reaches the next branch earlier than a register result reaches its consumer.

### Driving the CPU

| Call | Effect |
| --- | --- |
| `step()` | Exactly one clock |
| `stepInstruction()` | Clocks until the next retirement (guarded to 64 clocks) |
| `run()` | Clocks until halted and drained (no limit) |
| `holdFetch(true)` | Debugger barrier: stop admitting new instructions without halting |

## 6. Hazards

### 6.1 Stalls

A latch is held in place (and the `stalls` counter increments) in three situations:

| Situation | Condition | Purpose |
| --- | --- | --- |
| Data dependency | An `operand_read` row where an older in-flight instruction writes a register this instruction reads, and its result is not yet available (or forwarding is off) | Read-after-write hazard |
| Fetch past end of code | An `ir_load` row with `pc >= code size` waits until every older instruction has finished | A pending redirect or HALT might make the fetch irrelevant; otherwise it faults |
| In-order retirement | A routine's final row (`next == END`) waits until all older instructions have retired | Keeps retirement in program order |

### 6.2 Data hazards and forwarding

`dependsOn()` detects the hazards. A consumer depends on an older producer when:

- the producer writes a register (`reg_write`) that the consumer reads as `rs1`, or as `rs2` when the second operand is not an immediate;
- the consumer is a `STORE` and the producer writes the register being stored (`rd`);
- the producer is a `CALL` (`ra_write`) and the consumer touches R14, including `RET`.

What happens next depends on the forwarding switch (`--no-forward`, `setForwarding`):

| Mode | Behaviour |
| --- | --- |
| **Forwarding on** (default) | The consumer proceeds as soon as the producer's result *exists* in its latch: `alu_done` for ALU results, `mem_done` for loads, `ra_done` for CALL. The newest older producer's `alu_result` / `mem_data` / `pc + 1` overrides the stale register-file value |
| **Forwarding off** | The consumer stalls for as long as the producer is still in flight, i.e. until the producer has written back |

A **load-use** dependency still costs cycles with forwarding on, because the loaded value only exists after the MEM row (`mem_done`). There is no separate bypass network to model: forwarding is a read from the producer's latch.

### 6.3 Control hazards

The front end fetches sequentially. A row with `pc_write` (JMP, CALL, RET, and the taken arm of BEQ/BGT) resolves in EX:

- the target is `branchTarget(pc, instr) = (4*pc + imm + 4) / 4` for relative transfers, or `ra_val` (R14) for `RET`;
- `fetch_pc_` is set to the target, fetch is re-enabled, and `squash_after = seq` of the branch;
- every younger latch is discarded in the same clock and counted as one **flush** with its `squashed` / `flush_bubbles` totals.

There is no branch predictor, so every taken transfer pays the flush. A not-taken branch costs nothing beyond its own rows. The assembler also prepends `JMP .main`, so **every program starts with one flush**.

Because displacement is relative to the *next* instruction, `JMP 0` means "fall through", not "jump to address 0".

### 6.4 HALT and faults

- **HALT**: when the dispatch row selects HALT (or an unsupported opcode's entry), fetch stops and younger work is squashed. Older instructions drain. HALT is not counted in `retired`.
- **Faults**: an error in any row (divide by zero from the ALU, out-of-range data access, fetch outside code, a `trap` row) records `pending_exception` and `FaultInfo{pc, slot, address}`, stops fetch, and squashes the faulting instruction and everything younger. Older instructions still drain, so the reported fault is precise.

| Exception | Trigger |
| --- | --- |
| `INVALID_MEMORY_ACCESS` | Instruction fetch outside code, or load/store outside data memory |
| `DIVIDE_BY_ZERO` | Integer `DIV`/`REM` by zero |
| `ILLEGAL_INSTRUCTION` | Opcode with no routine (`trap` row) |

Memory checks do not roll back earlier instructions of a macro: a rejected `PUSH` can leave SP wrapped because its `SUB` already committed.

## 7. Worked example

Program:

```
.main:
    MOV R1, 5
    ADD R2, R1, 3     ; depends on R1
    ADD R3, R2, R1    ; depends on R2 (and R1)
    HALT
```

`risc201-sim t.asm -6 --trace` (6-stage, forwarding on):

```
cycle |   IF |       ID |       OF |       EX |      MEM | WB
    1 |   @0 | 0 NOP    | -        | -        | -        | -
    2 |   @1 | 1 NOP    | 0 JMP    | -        | -        | -
    3 |   @2 | 2 NOP    | 1 MOV    | -        | 0 JMP    | -
    4 |   @1 | 1 NOP    | -        | -        | -        | -    flush pc=0 -> 1 (squashed 2)
    5 |   @2 | 2 NOP    | 1 MOV    | -        | -        | -
    6 |   @3 | 3 NOP    | 2 ADD    | 1 MOV    | -        | -
    7 |   @4 | 4 NOP    | 3 ADD    | 2 ADD    | 1 MOV    | -
    8 |   @5 | 4 HALT   | 3 ADD    | 2 ADD    | -        | -    stall pc=2
    9 |   @5 | 5 NOP    | 4 HALT   | 3 ADD    | 2 ADD    | -
   10 |    - |        - | 4 HALT   | 3 ADD    | -        | -    stall pc=3
  ...
   14 |    - |        - | -        | -        | -        | -
Halted: yes   retired 4   in 14 cycle(s)   stalls 5   flushes 1
```

How to read it:

- The first column shows the **next fetch PC** (`@N`), not a latch. Instructions display as `NOP` until their ID row has decoded them.
- **Cycle 4**: the assembler-inserted `JMP .main` resolves in EX. It squashes the two younger instructions that were fetched sequentially (the flush).
- **Cycles 5–7**: `MOV`, then `ADD`, then `ADD` enter and advance one slot per clock. The second `ADD` reads R1 and R2 at OF in cycle 7 while their producers are still in EX and MEM, which only works because of forwarding.
- **Stalls at cycles 8 and later** are mostly the in-order retirement rule: a final ROM row waits until everything older has retired.
- Final state: R1 = 5, R2 = 8, R3 = 13. `HALT` is excluded from the retired count (JMP, MOV, ADD, ADD = 4).

The same program in the 4-stage layout takes 16 cycles with 0 stalls. Each instruction spends three clocks in the FETCH slot (IF, ID and OF rows), so dependencies are already resolved by the time the consumer reads operands.

## 8. Measured results

Built from HEAD and run with `risc201-sim <prog> -4|-6 [--no-forward]`:

| Program | Stages | Forwarding | Retired | Cycles | Stalls | Flushes | Squashed |
| --- | --: | --- | --: | --: | --: | --: | --: |
| recursive\_sum | 4 | on / off | 77 | 243 / 243 | 0 / 0 | 14 | 14 |
| recursive\_sum | 6 | on | 77 | 154 | 29 | 14 | 37 |
| recursive\_sum | 6 | off | 77 | 174 | 49 | 14 | 37 |
| pointer\_walk | 4 | on | 1793 | 5129 | 0 | 257 | 257 |
| pointer\_walk | 4 | off | 1793 | 5385 | 256 | 257 | 257 |
| pointer\_walk | 6 | on | 1793 | 3848 | 1025 | 257 | 514 |
| pointer\_walk | 6 | off | 1793 | 4617 | 1794 | 257 | 514 |
| branch\_storm | 4 | on / off | 1201 | 3509 / 3509 | 0 / 0 | 901 | 901 |
| branch\_storm | 6 | on | 1201 | 3309 | 101 | 901 | 1802 |
| branch\_storm | 6 | off | 1201 | 3409 | 201 | 901 | 1802 |

What the numbers show:

- **6-stage beats 4-stage on cycle count** (recursive\_sum: 154 vs 243), because the 4-stage FETCH slot serialises three rows per instruction.
- **Forwarding helps the 6-stage a lot** (recursive\_sum: 174 → 154; pointer\_walk: 4617 → 3848) and the 4-stage only on load-use patterns (pointer\_walk: 5385 → 5129).
- **Deeper pipeline, bigger flush**: `branch_storm` squashes 901 instructions in the 4-stage and 1802 in the 6-stage for the same 901 flushes.
- `pointer_walk` is the load-use control and `branch_storm` the control-hazard control. Both are synthetic.
- Cycle counts are simulator row clocks. They say nothing about physical clock frequency; every ALU operation, including multiply, divide and floating point, is modelled as a single EX row.

## 9. Observing execution

| Tool | What it shows |
| --- | --- |
| `risc201-sim prog.asm` | Final registers, PC/SP, flags, cycles, retired count, pipeline counters, exception |
| `--trace` | Per-clock pipeline snapshot with stall / flush / fault events (stops at 4000 clocks) |
| `-s` | Register snapshot after each instruction |
| `--micro-trace`, `--micro-step` | Reconstructed ROM path for each retired or faulting instruction, with packed word, signals, branch condition |
| `-4` / `-6`, `--no-forward`, `--horizontal` / `--vertical` | Select the pipeline layout, forwarding mode and ROM encoding (8 configurations in total) |
| `risc201-tui` | Live panes for source, binary, registers, memory, stats, control ROM and a hazard timeline; each in-flight instruction is tagged with its next ROM row, e.g. `◄[EX 29]` |
| TUI `:break`, `:watch`, `:config` | Fetch-barrier breakpoints, register/memory watchpoints, runtime reconfiguration |

Two details matter when interpreting output:

- **Micro-trace cycle labels** are the instruction's *completion* clock. The rows are reconstructed at retirement, even though each row actually ran on its own clock.
- **Breakpoints are fetch barriers**: fetch stops before the breakpoint PC, older instructions drain, and the debugger reports the stop with an empty pipeline.

## 10. Limitations and notes

- **Stale wording in the repo docs.** The README roadmap and `docs/PROJECT_ARCHITECTURE.md` describe readiness-based forwarding as planned and say forwarding on/off behave identically. At HEAD the code in `Cpu::tick()` does implement forwarding, and the measurements above show the two modes differing. The docs' measurement table (e.g. recursive\_sum 6-stage = 174 cycles) matches the *forwarding-off* run. Updating those passages would make the documentation consistent.
- **The `stalls` counter** covers all three stall situations in section 6.1, including in-order-retirement waits, not just data interlocks.
- **No branch prediction**, no caches, no variable memory latency, no out-of-order execution.
- **Arithmetic latency is not modelled**: ALU operations of any complexity occupy one EX row.
- **Faulting `TRAP` rows** are reconstructed for display but do not consume an extra pipeline clock.
- **R0** is only protected by the assembler; raw machine code can write it.
- The TUI is exercised manually rather than by CTest; CPU behaviour is covered by `PipelineTest`, `CpuIntegrationTest`, `ControlDecodeTest` and the horizontal / vertical control-unit tests.

## 11. Source map

| File | Responsibility |
| --- | --- |
| `isa_spec/isa201.h` | Opcodes, registers, flags, exception codes shared by every module |
| `control_unit/src/horizontal_control_unit.cpp`, `vertical_control_unit.cpp` | Control ROM definitions and sequencers |
| `simulator_core/include/simulator_core/pipeline_types.h` | Stage layouts, `Latch`, hazard records, `PipelineTrace` |
| `simulator_core/src/control_decode.cpp` | Row-to-stage mapping (`rowStage`, `slotForRow`) and per-opcode control bundles |
| `simulator_core/src/cpu.cpp` | `tick()`, hazard detection, forwarding, flushes, faults, retirement |
| `simulator_core/src/debugger.cpp` | Stepping, breakpoints, watchpoints |
| `simulator_core/src/main.cpp` | CLI and trace formatting |
| `simulator_core/tui/` | Terminal UI |
| `tests/pipeline_test.cpp` | Variant equivalence, cycle accounting, hazard counters |
| `processor_benchmark/` | Five stress workloads and a runner for all eight configurations |
