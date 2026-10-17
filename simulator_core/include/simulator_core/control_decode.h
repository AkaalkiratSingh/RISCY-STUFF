#pragma once

#include "control_unit/control_unit.h"
#include "simulator_core/pipeline_types.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace risc201 {
    struct StageControls {
        bool       halting = false;
        bool       is_branch = false;
        BranchCond cond = BranchCond::None;

        bool       pc_inc = false;

        bool       alu_enable = false;
        Opcode     alu_op = Opcode::NOP;
        bool       alu_src_imm = false;
        bool       flag_update = false;
        bool       sp_dec = false;
        bool       sp_inc = false;
        bool       pc_from_target = false;

        bool       mem_read = false;
        bool       mem_write = false;
        bool       mem_addr_sp = false;
        bool       mem_data_pc = false;
        bool       stack_check = false;
        bool       pc_from_mem = false;

        bool       reg_write = false;
        bool       mem_to_reg = false;
    };
    StageControls controlFor(const ControlUnit& cu, Opcode op);

    using StageControlTable = std::array<StageControls, 256>;
    StageControlTable buildStageControls(const ControlUnit& cu);


    std::vector<int> reachableRows(const ControlUnit& cu, Opcode op);
    std::string controlProblem(const ControlUnit& cu, Opcode op);

}