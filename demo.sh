#!/usr/bin/env bash
# Build the simulator, then run every example program in examples/.
set -u

cd "$(dirname "$0")"

# colors
RED=$'\033[0;31m'
GREEN=$'\033[0;32m'
YELLOW=$'\033[0;33m'
CYAN=$'\033[0;36m'
BRIGHT_GREEN=$'\033[38;2;57;255;20m'
BOLD=$'\033[1m'
RESET=$'\033[0m'

echo "${YELLOW}${BOLD}Building...${RESET}"
cmake -S . -B build || {
	echo "${RED}cmake configure failed${RESET}"
	exit 1
}
cmake --build build -j || {
	echo "${RED}build failed${RESET}"
	exit 1
}
echo "${GREEN}${BOLD}Build Successful${RESET}"

SIM=build/simulator_core/risc201-sim

for example in examples/*.asm; do
	echo ""
	echo "=================================================================================="
	echo "${CYAN}${BOLD}${example}${RESET}"
	echo "=================================================================================="
	echo "${BRIGHT_GREEN}"
	"$SIM" "$example" "-d"
	echo "${RESET}"
done
