#pragma once

#include "control_unit/horizontal_control_unit.h"
#include "control_unit/vertical_control_unit.h"
#include "simulator_core/pipeline_types.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace risc201 {
    struct StageControls {
        bool ir_load = false;
        bool instr_decode = false;
        bool operand_read = false;
        bool opcode_dispatch = false;

        bool       halting = false;
        bool       is_branch = false;
        BranchCond cond = BranchCond::None;

        bool       pc_inc = false;

        bool       alu_enable = false;
        Opcode     alu_op = Opcode::NOP;
        bool       alu_src_imm = false;
        bool       flag_update = false;
        bool       pc_from_target = false;
        bool       pc_from_ra = false;
        bool       ra_write = false;

        bool       mem_read = false;
        bool       mem_write = false;

        bool       reg_write = false;
        bool       mem_to_reg = false;
    };
    StageControls frontendControls(const ControlUnit& cu);
    StageControls frontendControls(const VerticalControlUnit& cu);
    StageControls controlFor(const ControlUnit& cu, Opcode op);

    using StageControlTable = std::array<StageControls, 256>;
    StageControlTable buildStageControls(const ControlUnit& cu);
    StageControls controlFor(const VerticalControlUnit& cu, Opcode op);
    StageControlTable buildStageControls(const VerticalControlUnit& cu);

    std::vector<int> reachableRows(const ControlUnit& cu, Opcode op);
    std::string controlProblem(const ControlUnit& cu, Opcode op);

    enum class RowStage : uint8_t { IF, ID, OF, EX, MEM, WB, Terminal };
    RowStage rowStage(const MicroOp& row);
    const char* rowStageName(RowStage stage);
    int slotForRow(PipelineVariant variant, RowStage stage);

    enum class PipelineStage : uint8_t { IF = 0, ID, OF, EX, MEM, WB };
    inline constexpr size_t kPipelineStageCount = 6;
    using StageMask = uint8_t;

    inline StageMask stageBit(PipelineStage stage) {
        return static_cast<StageMask>(1u << static_cast<uint8_t>(stage));
    }

    inline const char* pipelineStageName(PipelineStage stage) {
        switch (stage) {
            case PipelineStage::IF:  return "IF";
            case PipelineStage::ID:  return "ID";
            case PipelineStage::OF:  return "OF";
            case PipelineStage::EX:  return "EX";
            case PipelineStage::MEM: return "MEM";
            case PipelineStage::WB:  return "WB";
        }
        return "?";
    }

    // A ROM row can drive more than one stage (RET reads operands in OF and redirects in EX).
    StageMask stageMask(const ControlWord& word);

    struct PathStep {
        int         micro_pc = 0;
        ControlWord signals;
        BranchCond  condition = BranchCond::None;
        int         next = kMicroEnd;
        StageMask   stages = 0;
    };

    std::vector<PathStep> controlTrace(const ControlUnit& cu, Opcode op,
                                       const Flags& flags,
                                       ExceptionCode fault = ExceptionCode::NONE);
    std::vector<PathStep> controlTrace(const VerticalControlUnit& cu, Opcode op,
                                       const Flags& flags,
                                       ExceptionCode fault = ExceptionCode::NONE);

    // Both branch arms, no flags: the static stage layout of an opcode.
    const std::vector<PathStep>& opcodeLayout(const ControlUnit& cu, Opcode op);
    const std::vector<PathStep>& opcodeLayout(const VerticalControlUnit& cu, Opcode op);

} // namespace risc201
