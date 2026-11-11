# Stack Safety (Stack Guard)

## 1. Overview

risc201 has a **full-descending stack** in data memory. This document covers two related pieces:

1. **`StackGuard`**, a small class that answers "is this stack pointer, address, push or pop inside the allowed stack range?". It is unit-tested and used by the CPU.
2. **The data-memory bounds check** on `LOAD` and `STORE`, which protects every stack access.

Two checks work together at run time. Any write to `SP` (`R15`) is validated by `StackGuard::checkBounds` before it is committed, so SP can never leave the valid range. Every stack read and write is bounds-checked in its memory row through `addr_check`. A violation of either check raises `INVALID_MEMORY_ACCESS` before the state changes.

## 2. Stack model

| Property | Value |
| --- | --- |
| Direction | Full-descending: the stack grows toward lower addresses |
| Stack pointer | `R15` (`SP`), word-addressed |
| Initial SP | `memory_words` = 4096 by default, one word above the top of data memory |
| Layout | The stack and program data share the same data-memory array |
| `PUSH reg` | Assembler macro: `SUB sp, sp, 1` then `STORE reg, 0[sp]` |
| `POP reg` | Assembler macro: `LOAD reg, 0[sp]` then `ADD sp, sp, 1` |

`PUSH` and `POP` are not instructions. The preprocessor expands them (they are built-in macros), so the control unit only ever sees `SUB`, `STORE`, `LOAD` and `ADD`.

```text
 address
 4096  ─────────────  stack_base (initial SP; not a valid stack address)
 4095  │ first push  │
       │     ...     │   ▼ stack grows downward
       │             │
    0  ─────────────  stack_limit (lowest valid stack address)
```

Valid stack pointer values: `stack_limit ≤ SP ≤ stack_base`. Valid stack addresses: `[stack_limit, stack_base)`.

## 3. How the stack is checked at run time

### 3.1 Stack pointer writes (`StackGuard`)

The CPU owns a `StackGuard` constructed as `StackGuard(memory_words, 0)`: `stack_base` is the initial SP and `stack_limit` is 0. In the writeback row, when the destination register is `R15`, the CPU calls `checkBounds` on the new value before committing it. If the value is above `stack_base` or below `stack_limit`, the write is suppressed, `INVALID_MEMORY_ACCESS` is raised with the rejected value as the fault address, and the CPU halts. SP keeps its last valid value.

This covers every way of changing SP: the `SUB sp, sp, 1` inside `PUSH`, the `ADD sp, sp, 1` inside `POP`, and any direct `SUB`/`ADD`/`MOV` on `R15`.

### 3.2 Stack accesses (`addr_check`)

`LOAD` and `STORE` are microcoded as follows (the vertical and horizontal ROMs behave identically):

| Instruction | Rows | Checked row |
| --- | --- | --- |
| `LOAD` | `OPERANDS`, `ALU.IMM(ADD)`, `MEM.RD`, `WB.MEM` | `MEM.RD` asserts `mem_read` and **`addr_check`** |
| `STORE` | `OPERANDS`, `ALU.IMM(ADD)`, `MEM.WR` | `MEM.WR` asserts `mem_write` and **`addr_check`** |

The `ALU.IMM` row computes the address (`base + imm`). In the memory row, the CPU validates that address against the data memory (`0 ≤ address < memory size`) **before** the access commits. If the address is out of range, the read or write is suppressed, `INVALID_MEMORY_ACCESS` is raised, the faulting instruction and younger work are discarded, and the CPU halts. The simulator reports the exception with the faulting PC, the stage and the address.

The same `addr_check` line is the "checker" that the control units honor when a fault is reported: `faultApplies()` accepts `INVALID_MEMORY_ACCESS` only on a row where `addr_check` is asserted.

## 4. Measured behavior

| Scenario | Result |
| --- | --- |
| `examples/recursive_sum.asm` (balanced `PUSH`/`POP`) | `R2 = 15`, SP returns to 4096, no exception |
| `examples/stack_overflow.asm` (unbounded recursion, 6-stage) | `EXCEPTION: INVALID_MEMORY_ACCESS`, address `4294967295` (that is, −1, the rejected SP value); 12,290 instructions retired; final **`SP = 0`** |
| Same program with `--vertical` | Identical exception and final state |
| `POP` on an empty stack (SP = 4096) | `EXCEPTION: INVALID_MEMORY_ACCESS`, address 4096; SP stays 4096 |

How the overflow plays out: each recursion level executes `PUSH R14` (one word) and `CALL`. After 4096 levels SP reaches 0, so every word of data memory has been used by the stack. The next `PUSH` starts with `SUB sp, sp, 1`, whose result (−1) is rejected by the stack guard before it is committed. The `STORE` never runs, and SP stays at its last valid value, 0.

## 5. The `StackGuard` class

```cpp
StackGuard guard(stack_base, stack_limit);
```

| Method | Behavior |
| --- | --- |
| `checkBounds(sp)` | Returns `INVALID_MEMORY_ACCESS` if `stack_limit > stack_base` (bad configuration), `sp > stack_base`, or `sp < stack_limit`. Otherwise no exception |
| `checkPush(current_sp)` | Rejects `current_sp == 0`, then rejects `current_sp − 1 < stack_limit`. Checks the prospective pointer before SP changes |
| `checkPop(current_sp)` | Rejects `current_sp ≥ stack_base` (nothing to pop); otherwise no exception |
| `isValidAddress(address)` | True only for `stack_limit ≤ address < stack_base` (half-open) |
| `stackBase()`, `stackLimit()` | Return the configured boundaries |

All checks return `std::optional<ExceptionCode>`: empty means the operation is allowed. The CPU uses `checkBounds` on every SP write. Because every `PUSH` and `POP` changes SP through that path, the push and pop rules (`checkPush`, `checkPop`) are enforced as well: a push below `stack_limit` and a pop above `stack_base` both produce an out-of-range SP.

Example with `StackGuard guard(16, 0)`:

- SP values 0 through 16 pass `checkBounds`.
- Addresses 0 and 15 are valid; address 16 is not.
- `PUSH` at SP = 1 is permitted (new SP = 0); `PUSH` at SP = 0 is rejected.
- `POP` at SP = 15 is permitted; `POP` at SP = 16 is rejected.

### Boundary behavior in the simulator

| Situation | Result |
| --- | --- |
| `PUSH` when SP = 0 | `SUB` result (−1) rejected at writeback; SP stays 0; the `STORE` never runs |
| `POP` when SP = 4096 | The `LOAD` from address 4096 is rejected; SP stays 4096 |
| `PUSH` when SP = 1 | Allowed (new SP = 0) |
| `POP` when SP = 4095 | Allowed (new SP = 4096) |

## 6. Tests

`tests/memory_safety_test.cpp` covers valid bounds, address-range endpoints, `PUSH`/`POP` boundary protection and an invalid stack configuration (`stack_limit > stack_base`). It prints `memory_safety_test: all checks passed` when every check passes. The end-to-end behavior is exercised by `examples/stack_overflow.asm`, whose expected result is `EXCEPTION: INVALID_MEMORY_ACCESS`.
