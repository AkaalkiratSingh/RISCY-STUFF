# Vertical Microprogrammed Control Unit

## 1. Overview

The control unit tells the datapath what to do in every cycle. In risc201 it is **microprogrammed**: each machine instruction is a short routine of microinstructions stored in a read-only control memory (ROM).

Two encodings of the same microprogram exist and can be selected at run time (`--horizontal` / `--vertical`, or `:config rom horiz|vert` in the TUI):

- **Horizontal:** a 42-bit word. Each control line has its own ROM bit, and every row carries its own next-address and branch fields.
- **Vertical:** a 12-bit word. A 4-bit *micro-operation code* names a pattern of control lines, and a small 16-entry decoder expands it back into the signals. The next row is implicit (`addr + 1` or `END`).

The vertical ROM is **generated from the horizontal microprogram**. The constructor packs every horizontal row into a 12-bit word, then decodes it again and compares the result with the original. A row that cannot be represented without losing a control line is rejected with an exception. The two units are therefore interchangeable: same interface, same architectural results, same cycle counts (section 9).

| Parameter | Value |
| --- | --- |
| Control memory size | 86 rows (addresses 0–85) |
| Vertical word width | **12 bits** (horizontal: 42 bits) |
| Vertical ROM size | 86 × 12 = **1,032 bits** (129 bytes) |
| Horizontal ROM size | 86 × 42 = 3,612 bits (451.5 bytes) |
| Saving | 2,580 bits, about **71%** smaller |
| Control lines | 18 Boolean lines, plus a 5-bit ALU function (`alu_op`) |
| Micro-operation codes | 16, in a 4-bit field (all 16 used) |
| Microinstruction formats | 2: Operate (bit 11 = 0) and Branch (bit 11 = 1) |
| Branch conditions | 6: `Z`, `!Z`, `N`, `C`, `V`, `GT` |
| Branch target field | 7 bits (addresses 0–126; `0x7F` = END); highest target actually used is 63 |
| Opcode routines | 30 machine instructions, plus the ILLEGAL and TRAP rows and a shared FETCH/DECODE front end |

## 2. Control memory

### 2.1 Organization

The control memory is the vector `vrom_` of 86 `uint16_t` words (each holds one 12-bit word). A second vector, `cm_`, holds the same rows after decoding (`MicroOp` structures). It is used for listings, validation and the control-path utilities. The lookup table `entries_[256]` maps every opcode to the first row of its routine.

```text
 addr   contents                              rows
 ┌────┬───────────────────────────────────┬──────┐
 │  0 │ ILLEGAL  (TRAP, no routine)       │  1   │
 │ 1  │ ADD  SUB  AND  OR  XOR  SHL  SHR  │ 3 ea │  rows  1–21
 │ 22 │ LOAD (4)   STORE (3)              │  7   │  rows 22–28
 │ 29 │ JMP  HALT  NOP                    │  1 ea│  rows 29–31
 │ 32 │ BEQ (3)    MOV (3)                │  6   │  rows 32–37
 │ 38 │ CALL (3)   RET (2)                │  5   │  rows 38–42
 │ 43 │ NOT  SAR  MUL                     │ 3 ea │  rows 43–51
 │ 52 │ CMP (2)                           │  2   │  rows 52–53
 │ 54 │ TRAP     (hardware-fault target)  │  1   │
 │ 55 │ DIV  REM                          │ 3 ea │  rows 55–60
 │ 61 │ BGT (3)                           │  3   │  rows 61–63
 │ 64 │ FADD FSUB FMUL FDIV               │ 3 ea │  rows 64–75
 │ 76 │ FCMP (2)                          │  2   │  rows 76–77
 │ 78 │ FTOI  ITOF                        │ 3 ea │  rows 78–83
 │ 84 │ FETCH   (IF)         ┐ shared     │  1   │
 │ 85 │ DECODE  (ID.DISPATCH)┘ front end  │  1   │
 └────┴───────────────────────────────────┴──────┘
```

- **Row 0, ILLEGAL:** a TRAP row used when an opcode has no routine entry. It raises the illegal-instruction exception.
- **Row 54, TRAP:** the target for hardware faults (divide-by-zero, invalid memory access). It is a separate copy of the same `0x581` word as row 0.
- **Rows 84–85:** the shared front end, stored once and executed before every instruction. Row 85 has `opcode_dispatch` asserted, which overrides the normal successor and jumps to `routineEntry(opcode)`.

Example flow for `ADD`: `84 (FETCH) → 85 (DECODE) → 1 → 2 → 3 → END`.

### 2.2 Block diagram

```text
                 ┌──────────────────────────────────────────┐
   opcode ──────►│ entries_[256]  (opcode → routine entry)  │
                 └───────────────────┬──────────────────────┘
                                     ▼
 FLAGS ───────┐             ┌──────────────┐   12-bit word   ┌──────────────┐
 fault ─────┐ │   micro-PC  │  Vertical    │────────────────►│ 16-entry     │──► 18 control
            │ │  ┌─────────►│  ROM (86×12) │                 │ micro-op     │    lines
            │ │  │          └──────────────┘                 │ decoder      │
            │ │  │                  │ END / cond / target    └──────┬───────┘
            │ │  │                  ▼                               │ func[4:0]
            │ └──┼────► next-address select ──┐                     ▼
            └────┼───────► fault → TRAP row   │                  alu_op
                 └─────────── micro-PC ◄───────┘
```

### 2.3 Sequencing

`VerticalControlUnit::step(op, flags, fault)` executes one micro-cycle:

1. On the first cycle of an instruction it latches the entry address for `op` into the micro-PC. If that address is the ILLEGAL row, it records an illegal-instruction exception.
2. It reads and decodes the row at the micro-PC and outputs its control word.
3. If a fault is reported and the row's checker is active (`alu_enable` for divide-by-zero, `addr_check` for invalid memory access), the row's signals are cancelled, the exception is recorded and the micro-PC jumps to the TRAP row.
4. Otherwise the successor is the branch target (if the row is a branch and its condition holds), else the implicit next address (`addr + 1`), or END. At END the routine is done and the micro-PC resets for the next instruction.

> The standalone `step()` API starts at `routineEntry(op)` because its caller supplies the opcode. The pipelined CPU instead starts each instruction at `frontendEntry()` (FETCH, then DECODE/dispatch). `controlPath()` includes these front-end rows when displaying a complete flow.

## 3. Control signals

`ControlWord` has **18 Boolean control lines** and one 5-bit field, `alu_op`. Each micro-operation asserts only the lines it needs.

| Signal | Group | Function |
| --- | --- | --- |
| `ir_load` | Front end | Load the fetched instruction word into the instruction latch |
| `pc_inc` | Front end | Advance the next-fetch PC by one instruction |
| `instr_decode` | Front end | Split the instruction into decoded fields |
| `opcode_dispatch` | Front end | Select the instruction's routine (overrides the successor) |
| `operand_read` | Operands | Read source registers |
| `alu_enable` | ALU | Enable the ALU; `alu_op` selects the function |
| `alu_src_imm` | ALU | Use the sign-extended immediate as the second ALU operand |
| `flag_update` | ALU | Latch the ALU result flags into FLAGS |
| `mem_read` | Memory | Read data memory |
| `mem_write` | Memory | Write data memory |
| `addr_check` | Memory | Bounds-check the ALU-computed address before the access; out of range raises a fault |
| `reg_write` | Writeback | Write the result to the destination register |
| `mem_to_reg` | Writeback | Select memory data as the value written back |
| `pc_write` | Control flow | Load the PC with a new value (jump, taken branch, call, return) |
| `pc_src_ra` | Control flow | Take the new PC from the return-address register R14 |
| `ra_write` | Control flow | Write the return address to R14 (CALL) |
| `halt` | System | Stop the processor |
| `trap` | System | Raise the exception path (always together with `halt`) |

There are no stack-specific control lines. `PUSH` and `POP` are assembler macros built from `SUB`/`STORE` and `LOAD`/`ADD`.

## 4. Microinstruction format

Two 12-bit formats are distinguished by bit 11.

```text
Operate (bit 11 = 0): drives the datapath
 11    10      7 6        2  1    0
┌─────┬─────────┬──────────┬────┬─────┐
│  0  │ micro-op│ ALU func │rsv │ END │
└─────┴─────────┴──────────┴────┴─────┘

Branch (bit 11 = 1): tests FLAGS
 11    10   8 7          1  0
┌─────┬───────┬────────────┬─────┐
│  1  │ cond  │ taken tgt  │ END │
└─────┴───────┴────────────┴─────┘
```

| Field | Bits | Meaning |
| --- | --- | --- |
| type | [11] | 0 = Operate, 1 = Branch |
| micro-op | [10:7] | Operate only: 4-bit code selecting one of the 16 decoder entries |
| func | [6:2] | Operate only: 5-bit ALU function (the opcode value). Used only by ALU, ALU.NF and ALU.IMM; zero otherwise |
| cond | [10:8] | Branch only: 1 = Z, 2 = !Z, 3 = N, 4 = C, 5 = V, 6 = GT. Values 0 and 7 are invalid |
| target | [7:1] | Branch only: 7-bit address of the taken successor (`0x7F` = END) |
| reserved | [1] (Operate) | Always 0; tests verify canonical encodings |
| END | [0] | Operate: last row of the routine. Branch: the not-taken path ends the routine |

**What changed from horizontal to vertical:**

- Micro-op code: the vertical word uses 4 bits for 16 codes.
- Target field: 8 bits becomes 7, because the ROM has 86 rows and the highest branch target is 63.
- Successor: the explicit next-address field is gone. The next row is `addr + 1`, or END if bit 0 is set. A horizontal row whose successor is anything else cannot be packed. The only exception is DECODE, where `opcode_dispatch` overrides the successor.
- Branch rows only test FLAGS. A branch row cannot also drive the datapath, so a branch routine takes three rows: the test, a not-taken row (`IDLE`, END) and a taken row (`JUMP`, END).
- Undefined encodings (branch `cond` 0 or 7) decode to a HALT row, so a corrupt word fails safe.

### Worked examples

| Row | Word | Decoding |
| --- | --- | --- |
| 2: ALU(ADD) | `0x108` | `0 \| 0010 (ALU) \| 00010 (func ADD) \| 0 \| 0`: asserts `alu_enable`, `flag_update`, `alu_op = ADD` |
| 3: WB, END | `0x381` | `0 \| 0111 (WB) \| 00000 \| 0 \| 1`: asserts `reg_write`; last row |
| 32: BRANCH Z | `0x944` | `1 \| 001 (Z) \| 0100010 (target 34) \| 0`: if Z go to 34, else 33 |
| 84: IF | `0x700` | FETCH: asserts `ir_load`, `pc_inc` |
| 85: ID.DISPATCH, END | `0x781` | Decode and dispatch to the opcode routine |

## 5. Decoder

The decoder is the table `uopTable()` (16 `UopDef` entries). It maps a 4-bit micro-op code to the set of `ControlWord` lines it asserts. If the micro-op takes a function (`takes_func`), the `func` field is copied to `alu_op`. The table is checked at initialization to ensure the entries are in code order; otherwise `std::logic_error` is thrown. All 16 values of the 4-bit field are defined.

| Code | Micro-op | Control lines asserted | Function |
| --- | --- | --- | --- |
| 0 | `IDLE` | none | No operation; used by NOP and the branch not-taken path |
| 1 | `OPERANDS` | `operand_read` | Read source registers |
| 2 | `ALU` * | `alu_enable`, `flag_update` | Execute ALU function and update FLAGS |
| 3 | `ALU.NF` * | `alu_enable` | Execute ALU function, flags unchanged |
| 4 | `ALU.IMM` * | `alu_enable`, `alu_src_imm` | ALU with immediate (address calculation) |
| 5 | `MEM.RD` | `mem_read`, `addr_check` | Bounds-checked data memory read |
| 6 | `MEM.WR` | `mem_write`, `addr_check` | Bounds-checked data memory write |
| 7 | `WB` | `reg_write` | Write ALU result to the destination register |
| 8 | `WB.MEM` | `reg_write`, `mem_to_reg` | Write memory data to the destination register |
| 9 | `JUMP` | `pc_write` | Load PC with the jump/branch target |
| 10 | `HALT` | `halt` | Stop the processor |
| 11 | `TRAP` | `trap`, `halt` | Exception: stop and flag a trap |
| 12 | `RA.WR` | `ra_write` | Write return address to R14 |
| 13 | `JUMP.RA` | `pc_write`, `pc_src_ra` | Return: PC from R14 |
| 14 | `IF` | `ir_load`, `pc_inc` | Instruction fetch (shared front end) |
| 15 | `ID.DISPATCH` | `instr_decode`, `opcode_dispatch` | Decode and dispatch to the instruction routine |

\* takes the 5-bit `func` field. ALU function codes are the opcode values: MOV 0x01, ADD 0x02, SUB 0x03, MUL 0x04, DIV 0x05, REM 0x06, AND 0x07, OR 0x08, XOR 0x09, NOT 0x0A, SHL 0x0B, SHR 0x0C, SAR 0x0D, CMP 0x10, FADD 0x16, FSUB 0x17, FMUL 0x18, FDIV 0x19, FCMP 0x1A, FTOI 0x1B, ITOF 0x1C.

## 6. The microprogram

Every machine instruction runs FETCH (row 84, `0x700`) and DECODE (row 85, `0x781`) first, then its own routine. END is set on the last row of each routine (words ending in 1 below).

| Instr. | Opcode | Rows | Microinstructions (word) | Function |
| --- | --- | --- | --- | --- |
| NOP | 00 | 31 | `IDLE 001` | Do nothing |
| MOV | 01 | 35–37 | `OPERANDS 080`, `ALU.NF(MOV) 184`, `WB 381` | rd ← source operand; flags unchanged |
| ADD | 02 | 1–3 | `OPERANDS 080`, `ALU(ADD) 108`, `WB 381` | rd ← rs1 + rs2; updates flags |
| SUB | 03 | 4–6 | `OPERANDS 080`, `ALU(SUB) 10C`, `WB 381` | rd ← rs1 − rs2; updates flags |
| MUL | 04 | 49–51 | `OPERANDS 080`, `ALU(MUL) 110`, `WB 381` | rd ← rs1 × rs2 |
| DIV | 05 | 55–57 | `OPERANDS 080`, `ALU(DIV) 114`, `WB 381` | rd ← rs1 ÷ rs2; divide by zero traps |
| REM | 06 | 58–60 | `OPERANDS 080`, `ALU(REM) 118`, `WB 381` | rd ← remainder of rs1 ÷ rs2 |
| AND | 07 | 7–9 | `OPERANDS 080`, `ALU(AND) 11C`, `WB 381` | Bitwise AND |
| OR | 08 | 10–12 | `OPERANDS 080`, `ALU(OR) 120`, `WB 381` | Bitwise OR |
| XOR | 09 | 13–15 | `OPERANDS 080`, `ALU(XOR) 124`, `WB 381` | Bitwise XOR |
| NOT | 0A | 43–45 | `OPERANDS 080`, `ALU(NOT) 128`, `WB 381` | Bitwise complement |
| SHL | 0B | 16–18 | `OPERANDS 080`, `ALU(SHL) 12C`, `WB 381` | Logical shift left |
| SHR | 0C | 19–21 | `OPERANDS 080`, `ALU(SHR) 130`, `WB 381` | Logical shift right |
| SAR | 0D | 46–48 | `OPERANDS 080`, `ALU(SAR) 134`, `WB 381` | Arithmetic shift right |
| LOAD | 0E | 22–25 | `OPERANDS 080`, `ALU.IMM(ADD) 208`, `MEM.RD 280`, `WB.MEM 401` | rd ← memory[base + imm]; bounds checked |
| STORE | 0F | 26–28 | `OPERANDS 080`, `ALU.IMM(ADD) 208`, `MEM.WR 301` | memory[base + imm] ← rd; bounds checked |
| CMP | 10 | 52–53 | `OPERANDS 080`, `ALU(CMP) 141` | Subtract to set flags; no register write |
| JMP | 11 | 29 | `JUMP 481` | PC ← target |
| BEQ | 12 | 32–34 | `BRANCH Z 944`, `IDLE 001`, `JUMP 481` | If Z, PC ← target; else continue |
| BGT | 13 | 61–63 | `BRANCH GT E7E`, `IDLE 001`, `JUMP 481` | If GT, PC ← target; else continue |
| CALL | 14 | 38–40 | `OPERANDS 080`, `JUMP 480`, `RA.WR 601` | Jump to target and save instruction PC + 1 in R14 |
| RET | 15 | 41–42 | `OPERANDS 080`, `JUMP.RA 681` | PC ← R14 |
| FADD | 16 | 64–66 | `OPERANDS 080`, `ALU.NF(FADD) 1D8`, `WB 381` | Single-precision add; flags unchanged |
| FSUB | 17 | 67–69 | `OPERANDS 080`, `ALU.NF(FSUB) 1DC`, `WB 381` | Single-precision subtract |
| FMUL | 18 | 70–72 | `OPERANDS 080`, `ALU.NF(FMUL) 1E0`, `WB 381` | Single-precision multiply |
| FDIV | 19 | 73–75 | `OPERANDS 080`, `ALU.NF(FDIV) 1E4`, `WB 381` | Single-precision divide |
| FCMP | 1A | 76–77 | `OPERANDS 080`, `ALU(FCMP) 169` | Float compare; sets flags, no register write |
| FTOI | 1B | 78–80 | `OPERANDS 080`, `ALU.NF(FTOI) 1EC`, `WB 381` | Convert float to integer |
| ITOF | 1C | 81–83 | `OPERANDS 080`, `ALU.NF(ITOF) 1F0`, `WB 381` | Convert integer to float |
| HALT | 1F | 30 | `HALT 501` | Stop the processor |
| ILLEGAL | any other | 0 | `TRAP 581` | Undefined opcode: illegal-instruction exception |
| TRAP | (fault) | 54 | `TRAP 581` | Target of divide-by-zero and invalid-memory faults |

Words are 12-bit hex values. Rows 0 and 54 are two separate copies of the same TRAP word. MOV, the floating-point arithmetic and the conversions use `ALU.NF`, so the architectural flags are left untouched. CMP and FCMP have no writeback row, so a branch can follow them. The full generated listing is in [`CONTROL_ROM/control_rom_vertical.txt`](CONTROL_ROM/control_rom_vertical.txt).

## 7. Software implementation

| Function / type | Purpose |
| --- | --- |
| `MicroOpCode` (enum) | The 16 micro-op codes used by the decoder; `microOpName(code)` returns the mnemonic |
| `kHorizontalWordBits`, `kVerticalWordBits`, `kNumControlLines` | 42, 12 and 18 |
| `uopTable()` | Static decoder: `UopDef { code, name, takes_func, control lines }` for every micro-op; verifies table order |
| `lines({...})` | Builds a `ControlWord` with the listed lines asserted |
| `lineBits(ControlWord)` | Reduces a control word to a bit pattern of its lines, ignoring `alu_op` and sequencing |
| `packVertical(row, addr)` | Encoder: checks the successor, builds the operate/branch word, verifies func and target widths, then round-trips the word. Throws if the row cannot be encoded without loss |
| `unpackVertical(word, addr)` | Decoder: rebuilds a `MicroOp` from a word. Undefined encodings yield a HALT row |
| `canPackVertical(row, addr)` | Returns whether a row can be encoded without loss |
| `VerticalControlUnit()` | Builds the vertical ROM from the horizontal microprogram, decodes it back and checks that every successor is inside the ROM |
| `step(op, flags, fault)` | Executes one micro-cycle, handles faults and restarts after END |
| `readRow(addr)`, `reset()` | ROM read and decode; sequencer reset |
| `routineEntry(op)`, `frontendEntry()`, `trapEntry()` | Entry addresses of instruction routines, the shared front end and TRAP |
| `controlMemory()`, `verticalRom()` | Decoded rows and the packed 12-bit ROM |
| `pendingException()`, `clearException()`, `routineDone()`, `microPC()`, `encoding()` | State accessors; the interface matches the horizontal `ControlUnit` |
| `dumpControlMemory()` | Human-readable ROM listing with the size comparison |

Supporting files:

- `control_path.cpp`: `frontendRows`, `routineRows`, `routineEnds` and `controlPath` walk the ROM for either unit to show an instruction's complete flow.
- `export_control_rom.cpp`: generates `docs/CONTROL_ROM/control_rom_vertical.txt` and the horizontal listing; `--check` detects stale listings.
- `tests/vertical_control_unit_test.cpp`: checks ROM sizes, exact microwords, canonical round-trips, horizontal/vertical equivalence, stage controls, lockstep execution and fault behavior.

## 8. Horizontal vs. vertical

| | Horizontal | Vertical |
| --- | --- | --- |
| Word width | 42 bits | 12 bits |
| Total ROM (86 rows) | 3,612 bits (451.5 bytes) | 1,032 bits (129 bytes) |
| Driving a control line | 1 ROM bit per line | 4-bit micro-op code, decoded |
| Next-row address | 8-bit field in every row | Implicit: `addr + 1`, or END |
| Branch information | 3-bit cond + 8-bit target | 3-bit cond + 7-bit target |
| Branch plus datapath in one row | Allowed | Not allowed (branch rows only test FLAGS) |
| Extra hardware | None | 16-entry micro-op decoder |

## 9. Benchmark: space versus time

**Space.** The vertical ROM is about 71% smaller: 3,612 bits down to 1,032 bits, a saving of 2,580 bits.

**Time.** The cycle counts are identical. The vertical unit decodes the same rows and produces the same control words, so it needs the same number of clock cycles (every program in `baseline_results.csv` shows equal cycle counts for both ROMs). The cost is the extra decoder in the control path, which can lengthen the clock period.

Cycle counts are from the 6-stage pipeline with forwarding on, running `dependent_pointer_chase.asm` and `recursive_stack_stress.asm`.

| Design | Clock | Pointer chase cycles | Pointer chase time | Recursive stack cycles | Recursive stack time | Total time | Slower |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Horizontal | 10 ns | 735,242 | 7.35 ms | 429,192 | 4.29 ms | 11.64 ms | baseline |
| Vertical +0.5 ns | 10.5 ns | 735,242 | 7.72 ms | 429,192 | 4.51 ms | 12.23 ms | +5.0% |
| Vertical +1.0 ns | 11 ns | 735,242 | 8.09 ms | 429,192 | 4.72 ms | 12.81 ms | +10.0% |
| Vertical +1.5 ns | 11.5 ns | 735,242 | 8.46 ms | 429,192 | 4.94 ms | 13.39 ms | +15.0% |

| | Winner | Why |
| --- | --- | --- |
| Space | Vertical | The ROM shrinks by 71%, saving 2,580 bits |
| Time | Horizontal | Run time grows only by the decoder delay: 5%, 10% or 15% in the scenarios above |

**Summary:** vertical microprogramming trades a small, bounded slowdown (the decoder delay on the clock period) for a ROM that is roughly a quarter of the size. The functional behavior is unchanged.
