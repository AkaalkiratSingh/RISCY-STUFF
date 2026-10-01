# RISC201 example programs

Runnable assembly for the current simulator. Each file states its expected
result in a header comment; all of them were verified against
`risc201-sim` from this repo.

## Running

Build first, then point the simulator at a file:

```sh
cmake -S . -B build && cmake --build build -j
./build/simulator_core/risc201-sim examples/03_memory_roundtrip.asm --mem 0:8
```

Useful flags: `-s/--step` (print state after each instruction),
`-m/--mem S:C` (dump `C` data-memory words from word index `S`),
`-4/-6` (pipeline variant, 6-stage not modelled yet), `-h` (help).

Run them all:

```sh
for f in examples/*.asm; do ./build/simulator_core/risc201-sim "$f"; done
```

## The programs

| File | What it shows | Key result |
|---|---|---|
| `01_add_immediates.asm` | immediate + register-register ALU | `R3 = 12` |
| `02_arithmetic_logic.asm` | SUB/AND/OR/XOR/SHL/SHR | `R3..R8 = 2,8,14,6,48,24` |
| `03_memory_roundtrip.asm` | STORE then LOAD through data memory | `MEM[4] = 42`, `R2 = 42` |
| `04_branch_skip.asm` | JMP over a block | `R2 = 0`, `PC = 4` |
| `05_flags_overflow.asm` | signed overflow flag | `V=1 N=1`, `R2 = -2147483648` |
| `06_flags_zero_negative.asm` | zero / borrow flags | `Z=1`, `R3 = -9` |
| `07_nop_advances_pc.asm` | NOP fetches and retires | `R1 = 1`, `retired = 3` |

## What the assembler accepts but the CPU cannot run yet

The assembler understands `BEQ`, `CALL`, `RET`, `PUSH`, and `POP`, but the
control unit has no micro-routines for them yet, so they fall into the illegal
sink and halt immediately. Stick to
`ADD SUB AND OR XOR SHL SHR LOAD STORE JMP NOP HALT` for now.

That also means there are no conditional branches yet, so avoid counting down
in a loop — a backward `JMP` with no exit condition will spin forever.
