#pragma once

#include "isa201.h"

#include <cstddef>
#include <cstdint>
#include <string>

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