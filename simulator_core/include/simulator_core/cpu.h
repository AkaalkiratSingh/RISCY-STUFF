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

    // Retires exactly one macro instruction through the 4-stage datapath
    // (IF -> ID/OF -> EX -> WB). Repeated calls after a halt are no-ops.
    void step();

    // Steps until the machine halts (HALT, illegal opcode, or a fault).
    void run();

    const CpuState& state() const { return state_; }
    std::vector<int32_t>& memory() { return memory_; }
    const std::vector<int32_t>& memory() const { return memory_; }

    uint64_t cycles() const { return cycles_; }
    uint64_t retired() const { return retired_; }

private:
    // Drives the Control Unit's micro-routine for one decoded instruction,
    // applying each control word to the datapath.
    void executeInstruction(const Instruction& instr);

    uint32_t branchTarget(uint32_t instr_pc, const Instruction& instr) const;

    bool readMemory(int64_t address, int32_t& out) const;
    bool writeMemory(int64_t address, int32_t value);

    void raiseException(ExceptionCode code);

    PipelineVariant variant_;
    CpuState state_;
    std::vector<Instruction> program_;
    std::vector<int32_t> memory_;
    Alu alu_;
    ControlUnit control_unit_;
    StackGuard stack_guard_;

    // 4-stage timing model. Every instruction occupies IF/ID/EX/WB; without
    // forwarding a RAW on the immediately preceding instruction costs one
    // bubble, and the pipeline is flushed when a fault or HALT resolves.
    uint64_t cycles_ = 0;
    uint64_t retired_ = 0;
    uint32_t next_fetch_cycle_ = 1;  // cycle the next instruction enters IF
    int      prev_dest_ = -1;        // GP reg written by the previous instr (-1 = none)

    // TODO(Akaal, Oct 14): full ISA + 6-stage + hazard visualization.
    // TODO(Nov): wire microPC/control-word display + exception hooks from
    // memory_safety.
};

} // namespace risc201
