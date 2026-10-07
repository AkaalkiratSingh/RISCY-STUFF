#!/usr/bin/env bash
# Build the simulator, then run every example program in examples/.
set -u

cd "$(dirname "$0")"

echo "Building..."
cmake -S . -B build > /dev/null || { echo "cmake configure failed"; exit 1; }
cmake --build build -j > /dev/null || { echo "build failed"; exit 1; }

SIM=build/simulator_core/risc201-sim

for example in examples/*.asm; do
    echo ""
    echo "=================================================================================="
    echo "$example"
    echo "=================================================================================="
    "$SIM" "$example" "-d"
done
