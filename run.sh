#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: ./run.sh <mode> [arguments...]

Modes (choose one as the first argument):
  --asm, asm        Run the assembler
  --disasm, disasm  Run the disassembler
  --sim, sim        Run the simulator
  --tui, tui        Run the terminal debugger
  -h, --help       Show this help

All arguments after the mode are passed directly to the selected binary.
Input and output paths are relative to your current directory.

Examples:
  ./run.sh --asm examples/recursive_sum.asm -o program.bin
  ./run.sh --disasm program.bin -o program.asm -l
  ./run.sh --sim examples/recursive_sum.asm -6 --no-forward
  ./run.sh --tui examples/recursive_sum.asm -4
EOF
}

if [[ $# -eq 0 ]]; then
    usage >&2
    exit 1
fi

case "$1" in
    --asm|asm) binary="assembler/risc201-asm" ;;
    --disasm|disasm) binary="assembler/risc201-disasm" ;;
    --sim|sim) binary="simulator_core/risc201-sim" ;;
    --tui|tui) binary="simulator_core/tui/risc201-tui" ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown mode: $1" >&2; usage >&2; exit 1 ;;
esac
shift

repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
binary_path="$repo_dir/build/$binary"

if [[ ! -x "$binary_path" ]]; then
    echo "Executable not found: $binary_path" >&2
    echo "Run \"$repo_dir/setup.sh\" to build the project first." >&2
    exit 1
fi

exec "$binary_path" "$@"
