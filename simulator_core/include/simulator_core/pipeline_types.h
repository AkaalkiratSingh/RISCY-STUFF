#pragma once

#include "isa201.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace risc201 {
    enum class PipelineVariant { FourStage, SixStage };

    inline constexpr size_t kMaxPipelineSlots = 6;

    struct Latch {
        bool     valid = false;
        uint32_t pc = 0;
        uint32_t word = 0;
        Instruction instr{};
        bool     decoded = false;

        int32_t  rs1_val = 0;
        int32_t  rs2_val = 0;
        int32_t  rd_val = 0;
        int32_t  alu_result = 0;
        int32_t  mem_data = 0;

        uint32_t mem_sp_addr = 0;
        uint32_t sp_commit = 0;
        bool     writes_sp = false;

        bool     redirect = false;
        uint32_t next_pc = 0;
        bool     fault = false;
        ExceptionCode fault_code = ExceptionCode::NONE;
    };

    enum class HazardKind : uint8_t { Stall = 0, Flush, Fault };

    inline const char* hazardName(HazardKind k) {
        switch (k) {
        case HazardKind::Stall: return "stall";
        case HazardKind::Flush: return "flush";
        case HazardKind::Fault: return "fault";
        }
        return "?";
    }

    struct HazardEvent {
        uint64_t   cycle = 0;
        HazardKind kind = HazardKind::Stall;
        uint32_t   pc = 0;
        int        slot = -1;
        uint32_t   target = 0;
        int        depth = 0;
        int        discarded = 0;
    };

    struct PipelineTrace {
        uint64_t stalls = 0;
        uint64_t flushes = 0;
        uint64_t flush_bubbles = 0;
        uint64_t squashed = 0;
        uint64_t faults = 0;
        std::vector<HazardEvent> events;

        void clear() { *this = PipelineTrace{}; }
    };

    struct StageSlot {
        const char* name;
        bool IF, ID, OF, EX, MEM, WB;
    };

    inline constexpr StageSlot kFourStageLayout[] = {
        { "IF/ID/OF", true,  true,  true,  false, false, false },
        { "EX",       false, false, false, true,  false, false },
        { "MEM",      false, false, false, false, true,  false },
        { "WB",       false, false, false, false, false, true },
    };

    inline constexpr StageSlot kSixStageLayout[] = {
        { "IF",  true,  false, false, false, false, false },
        { "ID",  false, true,  false, false, false, false },
        { "OF",  false, false, true,  false, false, false },
        { "EX",  false, false, false, true,  false, false },
        { "MEM", false, false, false, false, true,  false },
        { "WB",  false, false, false, false, false, true },
    };

    struct StageLayout {
        const StageSlot* slots = nullptr;
        size_t count = 0;

        const StageSlot& operator[](size_t i) const { return slots[i]; }
        const StageSlot* begin() const { return slots; }
        const StageSlot* end() const { return slots + count; }
    };

    inline StageLayout layoutFor(PipelineVariant v) {
        return v == PipelineVariant::SixStage
            ? StageLayout{ kSixStageLayout, sizeof(kSixStageLayout) / sizeof(kSixStageLayout[0]) }
        : StageLayout{ kFourStageLayout,  sizeof(kFourStageLayout) / sizeof(kFourStageLayout[0]) };
    }

    inline size_t stageCount(PipelineVariant v) { return layoutFor(v).count; }

    inline std::string variantName(PipelineVariant v) {
        return v == PipelineVariant::SixStage ? "6-stage" : "4-stage";
    }

    inline std::string stageNames(PipelineVariant v) {
        std::string s;
        for (const StageSlot& slot : layoutFor(v)) {
            if (!s.empty()) s += " -> ";
            s += slot.name;
        }
        return s;
    }

}