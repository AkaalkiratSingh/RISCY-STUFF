#pragma once

#include "isa201.h"
#include "alu/alu.h"
#include "control_unit/horizontal_control_unit.h"
#include "memory_safety/stack_guard.h"
#include "simulator_core/control_decode.h"
#include "simulator_core/pipeline_types.h"
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace risc201 {
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
        explicit Cpu(PipelineVariant variant,
                     uint32_t memory_words = kDefaultMemoryWords,
                     MicrocodeEncoding encoding = MicrocodeEncoding::Horizontal);

        void loadWords(const std::vector<uint32_t>& words);

        void step();             // one clock
        void stepInstruction();   // clocks until one instruction retires
        void run();               // until halted and drained

        const CpuState& state() const { return state_; }
        std::vector<int32_t>& memory() { return memory_; }
        const std::vector<int32_t>& memory() const { return memory_; }

        uint64_t cycles() const { return cycles_; }
        uint64_t retired() const { return retired_; }

        PipelineVariant variant() const { return variant_; }
        MicrocodeEncoding encoding() const { return encoding_; }
        void setForwarding(bool on) { forwarding_ = on; }
        bool forwarding() const { return forwarding_; }
        void reconfigure(PipelineVariant variant, MicrocodeEncoding encoding);
        void setTracing(bool on) { tracing_ = on; }
        bool tracing() const { return tracing_; }
        const PipelineTrace& trace() const { return trace_; }
        const std::array<Latch, kMaxPipelineSlots>& stages() const { return regs_; }
        uint32_t fetchPc() const { return fetch_pc_; }
        bool fetching() const { return !stop_fetch_; }
        bool pipelineEmpty() const;
        int stageCount() const { return n_slots_; }
        int exceptionSlot() const { return ex_slot_; }

    private:
        struct Effects {
            bool          taken = false;
            uint32_t      target = 0;
            bool          fault = false;
            ExceptionCode code = ExceptionCode::NONE;
            bool          stop_fetch = false;
        };

        void tick();
        void applySlot(const StageSlot& slot, Latch& out, Effects& fx);
        void retire(const Latch& out);
        Latch fetchStage(bool stall);
        bool wouldStall(uint32_t* pc = nullptr) const;
        bool dependsOn(const Latch& candidate, const Latch& older) const;
        int32_t bypass(Register src, int32_t current) const;
        uint32_t bypassSp() const;
        void resetPipeline();

        uint32_t branchTarget(uint32_t instr_pc, const Instruction& instr) const;
        bool readMemory(int64_t address, int32_t& out) const;
        bool writeMemory(int64_t address, int32_t value);

        PipelineVariant variant_;
        MicrocodeEncoding encoding_;
        CpuState state_;
        std::vector<uint32_t> code_;
        std::vector<int32_t> memory_;
        Alu alu_;
        ControlUnit control_unit_;
        StackGuard stack_guard_;
        StageControlTable controls_;
        std::array<Latch, kMaxPipelineSlots> regs_{};
        PipelineTrace trace_;
        uint32_t fetch_pc_ = 0;
        bool stop_fetch_ = false;
        bool forwarding_ = true;
        bool tracing_ = false;
        int n_slots_ = 0;
        int ex_slot_ = 0;

        uint64_t cycles_ = 0;
        uint64_t retired_ = 0;
    };
}
