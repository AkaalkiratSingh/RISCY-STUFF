# RISC201 Control Unit — Design Document (Sept 30 milestone)

**Module:** `control_unit/` · **Owner:** Control Unit · **Status:** horizontal ROM implemented for 13 opcodes, vertical encoding specified (implementation due Nov 10)

## 1. Purpose and scope

The control unit turns each decoded RISC201 macro-instruction into a sequence of
*micro-operations*. Each micro-operation is one row of the **control memory (CM)** and
drives a bundle of datapath control signals for one micro-cycle. The unit is
**microprogrammed**: sequencing is done by a microPC that walks the CM, not by
hardwired logic.

Sept 30 scope (this document + code):

* microinstruction format — horizontal (implemented) and vertical (specified)
* CM organization and microPC addressing / sequencing rules
* microcode routines for ADD (ALU class), LOAD (memory class), BEQ (branch class),
  plus STORE, JMP, NOP, HALT and the other ALU ops
* interface contract with the simulator core

Out of scope until later milestones: CALL / RET / PUSH / POP routines (Oct 14),
vertical implementation and horizontal-vs-vertical comparison (Nov 10).

## 2. Control signals

| Signal | Meaning when asserted |
|---|---|
| `alu_enable` | ALU performs `alu_op` on its operands this cycle |
| `alu_op` | ALU function (`Opcode` value: ADD, SUB, AND, OR, XOR, SHL, SHR) |
| `alu_src_imm` | ALU operand B = immediate (1) / rs2 (0) |
| `flag_update` | latch Z, N, C, V from the ALU result into FLAGS |
| `mem_read` | memory data <- MEM[address from ALU] |
| `mem_write` | MEM[address from ALU] <- rs data |
| `reg_write` | register file write of `rd` |
| `mem_to_reg` | write-back source: loaded word (1) / ALU result (0) |
| `pc_inc` | PC <- PC + next-sequential step (sequential flow) |
| `pc_write` | PC <- decoded target (taken jump / taken branch) |
| `sp_dec`, `sp_inc` | SP <- SP − 1 / SP + 1 (full-descending stack; used by PUSH/POP/CALL/RET, Oct 14) |
| `halt` | stop the machine |

Mutually exclusive pairs (checked by the unit tests): `mem_read`/`mem_write`,
`pc_inc`/`pc_write`, `sp_dec`/`sp_inc`.

Design rule: **address arithmetic must not disturb FLAGS.** The LOAD/STORE
address-calculation row therefore has `flag_update = 0`; only real ALU
instructions set it.

## 3. Horizontal microinstruction format (implemented)

One bit (or small field) per control signal, no decoding needed. Total **35 bits**.

```
 34        27 26        19 18  16 15    12 11  10   9    8    7      6       5     4     3    2    1    0
+------------+------------+------+--------+----+----+----+----+------+-------+-----+-----+----+----+----+----+
| branch_tgt |    next    | cond | alu_op |halt|flag|sp_i|sp_d|m2reg |srcimm |pcinc|pcwr |regw|memw|memr|aluen|
+------------+------------+------+--------+----+----+----+----+------+-------+-----+-----+----+----+----+----+
   8 bits       8 bits      3 b     4 b
```

| Bits | Field | Notes |
|---|---|---|
| 0 | `alu_enable` | |
| 1 | `mem_read` | |
| 2 | `mem_write` | |
| 3 | `reg_write` | |
| 4 | `pc_write` | |
| 5 | `pc_inc` | |
| 6 | `alu_src_imm` | |
| 7 | `mem_to_reg` | |
| 8 | `sp_dec` | |
| 9 | `sp_inc` | |
| 10 | `flag_update` | |
| 11 | `halt` | |
| 15:12 | `alu_op` | opcode value of the ALU function (0–7 used) |
| 18:16 | `cond` | 0 none, 1 Z, 2 !Z, 3 N, 4 C, 5 V |
| 26:19 | `next` | successor when `cond` is none/false; `0xFF` = end of routine |
| 34:27 | `branch_target` | successor when `cond` is true; `0xFF` = end of routine |

`ControlUnit::packHorizontal()` produces exactly this word; the unit test
`testPackHorizontal` pins the layout.

## 4. Vertical microinstruction format (specified; implemented by Nov 10)

Mutually exclusive signals are grouped into encoded fields and a decoder expands
them back into the same `ControlWord` the datapath already consumes. Total **33 bits**.

| Field | Bits | Encoding |
|---|---|---|
| `ALU` | 3 | 0 = idle, 1..7 = ADD, SUB, AND, OR, XOR, SHL, SHR (folds `alu_enable` + `alu_op`) |
| `SRCB` | 1 | 0 = rs2, 1 = immediate |
| `MEM` | 2 | 00 none, 01 read, 10 write |
| `REG` | 2 | 00 none, 01 write ALU result, 10 write loaded word |
| `PC` | 2 | 00 hold, 01 increment, 10 load target |
| `SP` | 2 | 00 hold, 01 decrement, 10 increment |
| `MISC` | 2 | 00 none, 01 flag_update, 10 halt |
| `cond` | 3 | same as horizontal |
| `next` | 8 | same as horizontal |
| `branch_target` | 8 | same as horizontal |

Honest expectation for the Nov 10 comparison: with only ~12 control signals the CM
shrinks by just 2 bits/word (35 → 33), so the real trade-off is **an extra decode
level (latency/complexity) versus a marginally smaller CM**. The CM row *count* is
identical because both encodings use the same routines and sequencing.

`MicrocodeEncoding::Vertical` currently builds an empty CM and fails safe (`halt`).

## 5. Control memory organization and microPC addressing

* **CM** = `std::vector<MicroOp>`; rows addressed by an **8-bit microPC** (max 255
  rows; `0xFF` is reserved as the end marker). Current usage: **35 rows**.
* **Routines are contiguous blocks.** Each opcode has one routine; `routineEntry(op)`
  is the **dispatch table** (opcode → first row).
* **Row 0 is the illegal-instruction routine** (`halt`). Any opcode without a routine
  dispatches there, so unknown/unimplemented opcodes stop the machine safely.
* **Sequencing rule** — for the row at microPC:
  1. `cond == None`  → successor = `next`
  2. `cond` holds in FLAGS → successor = `branch_target`, otherwise `next`
  3. successor `END` → routine finished (`routineDone()`), microPC returns to 0
* **Dispatch:** on the first `step()` of a macro-instruction the microPC is loaded
  from `routineEntry(opcode)` (this is the microprogrammed "decode → start address"
  step). `reset()` clears it; calling `step()` after a finished routine starts a fresh one.

## 6. Microcode routines

Rows are shown in execution order. "Stage" is the mapping onto the 4-stage
pipeline IF → ID/OF → EX → WB (MEM folded into EX/WB); in the 6-stage variant
`mem_*` rows move to a separate MEM stage.

### 6.1 ADD (ALU class) — entry 1; SUB…SHR identical except `alu_op`

| μ-addr | Signals | Stage | RTL |
|---|---|---|---|
| 1 | `pc_inc` | IF / sequencing | PC ← PC + step |
| 2 | `alu_enable`, `alu_op=ADD`, `flag_update` | EX | result ← rs1 + operand B; Z,N,C,V ← flags(result) |
| 3 | `reg_write` | WB | rd ← result |

### 6.2 LOAD (memory class) — entry 22

| μ-addr | Signals | Stage | RTL |
|---|---|---|---|
| 22 | `pc_inc` | IF | PC ← PC + step |
| 23 | `alu_enable`, `alu_op=ADD`, `alu_src_imm` | EX | addr ← rs1 + imm (flags untouched) |
| 24 | `mem_read` | EX/MEM | data ← MEM[addr] |
| 25 | `reg_write`, `mem_to_reg` | WB | rd ← data |

STORE (entry 26) is the same address calculation followed by a single `mem_write` row and no write-back.

### 6.3 BEQ (branch class) — entry 32, uses the micro-branch

| μ-addr | cond | next | branch_target | Signals | RTL |
|---|---|---|---|---|---|
| 32 | Z | 33 | 34 | none (decision row) | test FLAGS.Z |
| 33 | – | END | – | `pc_inc` | Z clear: PC ← PC + step (not taken) |
| 34 | – | END | – | `pc_write` | Z set: PC ← target (taken) |

BEQ has no operands other than the label; it tests the Z flag left by the most
recent flag-setting ALU instruction. Both paths take two micro-cycles.

### 6.4 Other implemented routines

JMP (entry 29): one row, `pc_write`. NOP (31): one idle row. HALT (30): one row, `halt`.
Full listing: `docs/cm_table.md`.

## 7. Interface contract with the simulator core

```cpp
ControlUnit cu(MicrocodeEncoding::Horizontal);
ControlWord w = cu.step(instr.opcode, cpu_flags);   // call once per micro-cycle
if (cu.routineDone()) { /* instruction finished; next instruction */ }
cu.microPC();                                        // for the CLI microPC display
cu.controlMemory()[cu.microPC()];                    // control-memory word for the display
ControlUnit::packHorizontal(row);                    // raw microword for the display
cu.dumpControlMemory();                              // whole CM listing
```

Notes for Core Engine / ALU: `step()` only needs the opcode and current FLAGS.

## 8. Assumptions and open items (need a team decision)

1. **Immediate operand on ALU ops.** ALU routines assert `alu_src_imm = 0`. The `I`
   bit lives in the instruction, not the opcode, so the datapath should use
   `alu_src_imm || instr.I` for operand B. (LOAD/STORE always assert it.)
2. **PC step.** The assembler computes addresses in bytes (+4 per instruction) while the
   PRD says word-addressable memory. `pc_inc` is defined as "next sequential
   instruction"; the simulator decides whether that is +1 or +4. Needs one agreed answer.
3. **HALT opcode.** `HALT = 0xFF` in `isa201.h`, but the assembler encodes only 5 opcode
   bits, so HALT is emitted as `0x1F`. The control unit dispatches on the `Opcode`
   value it receives; the ISA/assembler owners should fix this in a team sync
   (the header is frozen after Milestone 0).
4. **Illegal opcodes** currently halt via row 0. The `ILLEGAL_INSTRUCTION` exception code
   in `isa201.h` could be raised by the simulator when it sees dispatch to row 0 —
   to be agreed with the memory-safety module.

## 9. Roadmap

* **Oct 14:** routines for CALL / RET / PUSH / POP; horizontal unit driving the simulator; ALU/core integration.
* **Nov 10–11:** vertical unit (runtime-selectable), horizontal-vs-vertical comparison (CM size, decode latency, ease of modification), per-cycle microPC exposure in the CLI.
