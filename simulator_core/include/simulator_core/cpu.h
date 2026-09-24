// Fetch-decode-execute engine. Supports both pipeline variants (4-stage,
// 6-stage), selectable via flag (PRD §5.2).

#pragma once

#include "isa201.h"
#include "alu/alu.h"
#include "control_unit/control_unit.h"
#include "memory_safety/stack_guard.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace risc201 {

enum class PipelineVariant { FourStage, SixStage };

struct CpuState {
    std::array<int32_t, kNumGPRegisters> registers{};
    uint32_t pc = 0;
    uint32_t sp = 0;
    Flags    flags;
    bool     halted = false;
    std::optional<ExceptionCode> pending_exception;
};

class Cpu {
public:
    explicit Cpu(PipelineVariant variant, uint32_t memory_words = kDefaultMemoryWords);

    void loadProgram(const std::vector<Instruction>& program);

    void step();

    void run();

    const CpuState& state() const { return state_; }
    std::vector<int32_t>& memory() { return memory_; }

private:
    PipelineVariant variant_;
    CpuState state_;
    std::vector<Instruction> program_;
    std::vector<int32_t> memory_;
    Alu alu_;
    ControlUnit control_unit_;
    // TODO(Akaal, Sept 30): minimal 4-stage loop (ALU ops, load/store,
    // unconditional jump). TODO(Oct 14): full ISA + 6-stage + hazard
    // detection. TODO(Nov): wire microPC/control-word display + exception
    // hooks from memory_safety.
};

} // namespace risc201
