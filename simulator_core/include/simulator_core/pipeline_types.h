#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace risc201 {
    enum class PipelineVariant { FourStage, SixStage };

    enum class Action : uint8_t { IF = 0, ID, OF, EX, MEM, WB, Count };

    inline const char* actionName(Action a) {
        switch (a) {
        case Action::IF:    return "IF";
        case Action::ID:    return "ID";
        case Action::OF:    return "OF";
        case Action::EX:    return "EX";
        case Action::MEM:   return "MEM";
        case Action::WB:    return "WB";
        case Action::Count: break;
        }
        return "?";
    }

    struct StageSlot {
        const char* name;
        bool IF, ID, OF, EX, MEM, WB;

        bool has(Action a) const {
            switch (a) {
            case Action::IF:    return IF;
            case Action::ID:    return ID;
            case Action::OF:    return OF;
            case Action::EX:    return EX;
            case Action::MEM:   return MEM;
            case Action::WB:    return WB;
            case Action::Count: break;
            }
            return false;
        }
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