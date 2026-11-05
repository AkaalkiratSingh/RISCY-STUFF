import csv, itertools, re, subprocess, time
from pathlib import Path

bench = Path(__file__).resolve().parent
root = bench.parent
results = []
for p in sorted(bench.glob("*.asm")):
    reference = None
    for stages, rom, forward in itertools.product(
        [4, 6], ["horizontal", "vertical"], [True, False]
    ):
        cmd = [
            str(root / "build/simulator_core/risc201-sim"),
            str(p),
            f"-{stages}",
            f"--{rom}",
            "-m",  # type: ignore
            "0:4096",
        ]
        if not forward:
            cmd.append("--no-forward")
        elapsed = []
        first_run = None
        for _ in range(5):
            started = time.perf_counter()
            run = subprocess.run(cmd, text=True, capture_output=True, timeout=60)
            elapsed.append(time.perf_counter() - started)
            assert run.returncode == 0, (p.name, cmd, run.stderr, run.stdout[-2000:])
            assert "warning:" not in run.stderr and "EXCEPTION" not in run.stdout
            if first_run is None:
                first_run = run
            else:
                assert (run.stdout, run.stderr) == (first_run.stdout, first_run.stderr), (
                    p.name, stages, rom, forward, "run mismatch"
                )
        r = first_run
        regs = {
            int(i): int(v) for i, v in re.findall(r"R\s*(\d+) =\s*(-?\d+)", r.stdout) # type: ignore
        }
        memory = {
            int(i): int(v)
            for i, v in re.findall(r"MEM\[\s*(\d+)\] =\s*(-?\d+)", r.stdout) # type: ignore
        }
        assert len(regs) == 16 and len(memory) == 4096
        state = (
            regs,
            memory,
            re.search(r"FLAGS:.*", r.stdout)[0],  # type: ignore
            re.search(r"PC =.*", r.stdout)[0],    # type: ignore
        )
        if reference is None:
            reference = state
        assert state == reference, (p.name, stages, rom, forward, "state mismatch")
        assert regs[15] == 4096
        if p.stem == "alu_dependencies":
            assert (
                regs[1] == 8192
                and regs[3] == 8192
                and regs[2] == sum(i % 97 for i in range(1, 8193))
            )
        elif p.stem == "dependent_pointer_chase":
            assert regs[1] == 0 and regs[2] == 33521664
            assert all(memory[i] == (i + 37) % 1024 for i in range(1024))
        elif p.stem == "branch_flush_stress":
            assert (
                regs[1] == 16384
                and regs[2] == regs[3] == 8192
                and regs[5] == 0
                and regs[6] == memory[0] == 777
            )
        elif p.stem == "memory_ping_pong":
            assert regs[9] == 589312
            assert all(
                memory[i] == i + 64 and memory[i + 1024] == i + 63 for i in range(1024)
            )
        elif p.stem == "recursive_stack_stress":
            assert regs[1] == 128 and regs[2] == 8256 and regs[9] == 1056768
        retired, cycles = map(
            int,
            re.search(r"retired (\d+) instruction\(s\)   in (\d+)", r.stdout).groups(),  # type: ignore
        )
        stalls, flushes, bubbles = map(
            int, re.search(r"stalls (\d+)   flushes (\d+) \((\d+)", r.stdout).groups()   # type: ignore
        )
        results.append(
            dict(
                program=p.stem,
                stages=stages,
                rom=rom,
                forwarding=forward,
                retired=retired,
                cycles=cycles,
                stalls=stalls,
                flushes=flushes,
                flush_bubbles=bubbles,
                runTime=f"{sum(elapsed) / len(elapsed):.6f}",
            )
        )
    print(p.name, "passed all 8 configurations", flush=True)
with (bench / "baseline_results.csv").open("w") as f:
    writer = csv.DictWriter(f, fieldnames=results[0].keys())
    writer.writeheader()
    writer.writerows(results)
print(
    "All 40 configurations passed across 5 runs each; full register/memory state verified.",
    flush=True,
)
