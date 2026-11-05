# Processor stress benchmarks

Five deterministic assembly workloads exercise the implemented pipeline, forwarding paths, control transfers, memory accesses, and call stack. All terminate normally with checkable results. They live separately from the shorter demonstration programs.

## Workloads and expected results

| File | Stress target | Work performed | Expected result |
|---|---|---|---|
| [alu_dependencies.asm](alu_dependencies.asm) | Register dependencies, forwarding, integer ALU | 8192 iterations of dependent ADD/MUL/DIV/XOR/shift/REM operations | R1=8192, R3=8192, R2=392094 |
| [dependent_pointer_chase.asm](dependent_pointer_chase.asm) | Load-use interlock and dependent addresses | Build a 1024-node ring; follow 65536 links and accumulate their indices | R1=0, R2=33521664 |
| [branch_flush_stress.asm](branch_flush_stress.asm) | Conditional branches, jump flushes, wrong-path suppression | 16384 iterations, alternating parity branches plus four taken jumps per iteration | R1=16384, R2=R3=8192, R5=0, R6=777, MEM[0]=777; no exception |
| [memory_ping_pong.asm](memory_ping_pong.asm) | Loads/stores, store data, address dependencies | 32 sweeps over two 1024-word buffers, followed by a checksum | R9=589312; MEM[i]=i+64 and MEM[1024+i]=i+63 for 0≤i<1024 |
| [recursive_stack_stress.asm](recursive_stack_stress.asm) | CALL/RET, R14 preservation, stack traffic | Repeat recursive sum(128) 128 times | R1=128, R2=8256, R9=1056768, SP=4096 |

The arithmetic chain restores its intermediate value to the loop index before taking the remainder, keeping values bounded and avoiding overflow. Its checksum is the sum of `i % 97` for i=1..8192.

The pointer ring stores `(i+37) % 1024` in MEM[i]. Since 37 is coprime to 1024, each circuit visits all nodes. Every LOAD is immediately followed by a dependent ADD. With forwarding enabled, the measured load-use stall count is exactly 65536.

The branch benchmark deliberately places zero-divisor instructions and sentinel-corrupting stores on paths skipped by unconditional jumps. Correct flushing prevents both exceptions and memory corruption. It measures the existing sequential-fetch/flush implementation, not a dynamic branch predictor.

The memory benchmark initializes its first buffer to its indices and writes every word of the second before reading it. Each full sweep increments the first buffer by two. The checksum is therefore `sum(0..1023) + 1024*64 = 589312`.

The stack benchmark saves R14 and its argument at each nonzero recursive level. Its maximum active stack use is 256 words, leaving room within the default 4096-word memory. It stresses valid stack operation rather than intentionally overflowing it.

## Running

Run these commands from `RISCY_STUFF/` after building the project:

```bash
./build/simulator_core/risc201-sim examples/benchmarks/alu_dependencies.asm -4
./build/simulator_core/risc201-sim examples/benchmarks/dependent_pointer_chase.asm -6 --vertical
./build/simulator_core/risc201-sim examples/benchmarks/branch_flush_stress.asm -6 --no-forward
./build/simulator_core/risc201-sim examples/benchmarks/memory_ping_pong.asm -4 -m 0:8
./build/simulator_core/risc201-sim examples/benchmarks/recursive_stack_stress.asm -6
```

Run the complete validation and measurement matrix with Python 3:

```bash
python3 examples/benchmarks/run_benchmarks.py
```

The runner executes all five programs across `{4-stage, 6-stage} × {horizontal, vertical} × {forwarding on, forwarding off}`. It checks normal completion, expected results, SP restoration, the complete register/memory state across configurations, and the cycle-count identity. Each individual run has a 60-second host timeout. Run Python normally, without `-O`, so assertions remain enabled.

On success, it writes [baseline_results.csv](baseline_results.csv), containing all 40 configurations and their retired-instruction, cycle, stall, flush, and flush-bubble counts. Running it again replaces that file with the new results. It uses only the Python standard library; Python is a convenience for benchmarking, not a dependency of the C++ simulator.

## Interpretation

Use plain execution for measurements. All five workloads exceed the CLI's 4000-clock trace limit; `--trace`, `--micro-trace`, and `--micro-step` stop before these benchmarks finish. Console output also distorts host execution-time measurements.

The CSV records simulated processor clocks, not wall-clock time. MUL and DIV each occupy one modeled EX action despite their internal C++ algorithms. There are no simulated caches or variable memory latency, so the memory workloads measure pipeline interactions rather than cache bandwidth or cache misses.

Initialization, loop bookkeeping, checksum computation, and the assembler-inserted entry JMP are included in the counters. The files target particular bottlenecks but are not perfectly isolated measurements of a single instruction. Compare the same file across configurations. Horizontal and vertical ROMs should produce identical results and cycle counts for a fixed pipeline/forwarding setting.

For each normally completed run:

```text
cycles = (stages - 1) + retired + 1 HALT + stalls + flush bubbles
```

## Initial measured results

Collected on 4 November 2026, with forwarding enabled. Both ROM encodings produced the same counters. The full CSV also includes forwarding-disabled runs.

| Benchmark | Stages | Retired | Cycles | Stalls | Flush bubbles |
|---|---:|---:|---:|---:|---:|
| alu_dependencies | 4 | 98307 | 106504 | 0 | 8193 |
| alu_dependencies | 6 | 98307 | 122892 | 0 | 24579 |
| branch_flush_stress | 4 | 188422 | 286731 | 0 | 98305 |
| branch_flush_stress | 6 | 188422 | 483343 | 0 | 294915 |
| dependent_pointer_chase | 4 | 334851 | 466952 | 65536 | 66561 |
| dependent_pointer_chase | 6 | 334851 | 600076 | 65536 | 199683 |
| memory_ping_pong | 4 | 339042 | 440455 | 66560 | 34849 |
| memory_ping_pong | 6 | 339042 | 510155 | 66560 | 104547 |
| recursive_stack_stress | 4 | 230658 | 263943 | 0 | 33281 |
| recursive_stack_stress | 6 | 230658 | 330507 | 0 | 99843 |
