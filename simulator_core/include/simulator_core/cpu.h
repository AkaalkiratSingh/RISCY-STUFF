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
    struct MicroTraceRow {
        uint64_t cycle = 0;
        uint32_t pc = 0;
        Opcode opcode = Opcode::NOP;
        uint32_t micro_pc = 0;
        int32_t next = kMicroEnd;
        uint64_t packed = 0;
        ControlWord signals;
        BranchCond condition = BranchCond::None;
        ExceptionCode exception = ExceptionCode::NONE;
    };

    struct FaultInfo {
        uint32_t pc = 0;
        int slot = -1;
        std::optional<uint32_t> address;
    };

    struct MicroPcView {
        uint32_t    pc = 0;
        const char* stage = "";
        int         micro_pc = 0;
        ControlWord signals;
    };

    struct CpuState {
        std::array<int32_t, kNumGPRegisters> registers{};
        uint32_t pc = 0;
        uint32_t sp = 0;
        Flags    flags;
        bool     halted = false;
        std::optional<ExceptionCode> pending_exception;
        std::optional<FaultInfo> fault_info;
    };

    class Cpu {
    public:
        explicit Cpu(PipelineVariant variant,
                     uint32_t memory_words = kDefaultMemoryWords,
                     MicrocodeEncoding encoding = MicrocodeEncoding::Horizontal);

        Cpu(const StageLayout& layout,
            uint32_t memory_words = kDefaultMemoryWords,
            MicrocodeEncoding encoding = MicrocodeEncoding::Horizontal);

        void loadWords(const std::vector<uint32_t>& words);

        void step();             // one clock
        void stepInstruction();   // clocks until one instruction retires
        void run();               // until halted and drained

        const CpuState& state() const { return state_; }
        std::vector<int32_t>& memory() { return memory_; }
        const std::vector<int32_t>& memory() const { return memory_; }
        const std::vector<uint32_t>& codeWords() const { return code_; }
        const std::vector<uint32_t>& sourceWords() const { return source_words_; }

        uint64_t cycles() const { return cycles_; }
        uint64_t retired() const { return retired_; }

        PipelineVariant variant() const { return variant_; }
        const StageLayout& layout() const { return layout_; }
        MicrocodeEncoding encoding() const { return encoding_; }
        void setForwarding(bool on) { forwarding_ = on; }
        bool forwarding() const { return forwarding_; }
        void reconfigure(PipelineVariant variant, MicrocodeEncoding encoding);
        void setTracing(bool on) { tracing_ = on; }
        bool tracing() const { return tracing_; }
        const PipelineTrace& trace() const { return trace_; }
        void setMicroTracing(bool on) { micro_tracing_ = on; micro_trace_.clear(); }
        const std::vector<MicroTraceRow>& microTrace() const { return micro_trace_; }
        const std::array<Latch, kMaxPipelineSlots>& stages() const { return regs_; }
        uint32_t fetchPc() const { return fetch_pc_; }
        bool fetching() const { return !stop_fetch_; }

        // Debugger barrier: stop admitting new instructions without halting the CPU.
        void holdFetch(bool hold);
        bool fetchHeld() const { return fetch_held_; }

        // Per-stage control-ROW cursors for the active pipeline state.
        std::vector<MicroPcView> microSnapshot() const;
        const std::vector<MicroOp>& controlMemory() const;
        const std::vector<int>& routineEnds() const;
        bool pipelineEmpty() const;
        int stageCount() const { return n_slots_; }
        int exceptionSlot() const { return ex_slot_; }

    private:
        void recordMicroTrace(const Latch& instruction, ExceptionCode fault = ExceptionCode::NONE);
        void tick();
        bool dependsOn(const Latch& candidate, const Latch& older) const;
        void resetPipeline();

        uint32_t branchTarget(uint32_t instr_pc, const Instruction& instr) const;
        bool readMemory(int64_t address, int32_t& out) const;
        bool writeMemory(int64_t address, int32_t value);

        PipelineVariant variant_;
        MicrocodeEncoding encoding_;
        StageLayout layout_;
        CpuState state_;
        std::vector<uint32_t> source_words_;
        std::vector<uint32_t> code_;
        std::vector<int32_t> memory_;
        Alu alu_;
        ControlUnit control_unit_;
        VerticalControlUnit vertical_unit_;
        StackGuard stack_guard_;
        StageControlTable controls_;
        std::array<Latch, kMaxPipelineSlots> regs_{};
        PipelineTrace trace_;
        std::vector<MicroTraceRow> micro_trace_;
        mutable std::vector<int> routine_ends_;
        mutable bool routine_ends_valid_ = false;
        bool micro_tracing_ = false;
        uint32_t fetch_pc_ = 0;
        bool stop_fetch_ = false;
        bool fetch_held_ = false;
        bool forwarding_ = true;
        bool tracing_ = false;
        int n_slots_ = 0;
        int ex_slot_ = 0;

        uint64_t cycles_ = 0;
        uint64_t retired_ = 0;
        uint64_t seq_counter_ = 0;
    };
}
