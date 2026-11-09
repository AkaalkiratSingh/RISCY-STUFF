#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
build_dir="$repo_dir/build"

if ! command -v cmake >/dev/null 2>&1; then
    echo "CMake 3.16 or newer is required. Install CMake and a C++17 compiler, then rerun this script." >&2
    exit 1
fi

cmake -S "$repo_dir" -B "$build_dir"
cmake --build "$build_dir" --parallel
(
    cd "$build_dir"
    ctest --output-on-failure
)

echo "Setup complete. Run: \"$build_dir/simulator_core/risc201-sim\" \"$repo_dir/examples/recursive_sum.asm\" -4"
