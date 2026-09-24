#include "simulator_core/cpu.h"

namespace risc201 {

Cpu::Cpu(PipelineVariant variant, uint32_t memory_words)
    : variant_(variant),
      memory_(memory_words, 0),
      control_unit_(MicrocodeEncoding::Horizontal) {}

void Cpu::loadProgram(const std::vector<Instruction>& program) {
    program_ = program;
    state_ = CpuState{};
}

void Cpu::step() {
    // TODO(Akaal): fetch -> decode -> (ALU / mem / branch) -> writeback,
    // branching on variant_ for 4-stage vs 6-stage timing.
}

void Cpu::run() {
    while (!state_.halted) {
        step();
    }
}

} // namespace risc201
