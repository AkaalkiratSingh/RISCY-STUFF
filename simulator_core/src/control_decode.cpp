#include "simulator_core/control_decode.h"

#include <algorithm>

namespace risc201 {

    namespace {

        // Folds one micro-row's signals into the flattened bundle.
        void mergeRow(StageControls& sc, const MicroOp& row) {
            const ControlWord& c = row.ctl;

            sc.pc_inc = c.pc_inc;

            if (c.alu_enable) {
                sc.alu_enable = true;
                sc.alu_op = c.alu_op;
                sc.alu_src_imm = c.alu_src_imm;
                sc.flag_update = c.flag_update;
            }

            sc.sp_dec = c.sp_dec;
            sc.sp_inc = c.sp_inc;
            sc.mem_read = c.mem_read;
            sc.mem_write = c.mem_write;
            sc.mem_addr_sp = c.mem_addr_sp;
            sc.mem_data_pc = c.mem_data_pc;
            sc.stack_check = c.stack_check;
            sc.reg_write = c.reg_write;
            sc.mem_to_reg = c.mem_to_reg;
            sc.halting = c.halt;

            if (c.pc_write) {
                if (c.pc_src_mem) sc.pc_from_mem = true;
                else              sc.pc_from_target = true;
            }

            if (row.cond != BranchCond::None) {
                sc.is_branch = true;
                sc.cond = row.cond;
            }
        }

    }

    std::vector<int> reachableRows(const ControlUnit& cu, Opcode op) {
        std::vector<int> out;

        const auto& cm = cu.controlMemory();
        if (cm.empty()) return out;

        std::vector<bool> seen(cm.size(), false);
        std::vector<int> pending{ cu.routineEntry(op) };

        while (!pending.empty()) {
            const int addr = pending.back();
            pending.pop_back();

            if (addr < 0 || static_cast<size_t>(addr) >= cm.size()) continue;
            if (seen[static_cast<size_t>(addr)]) continue;
            seen[static_cast<size_t>(addr)] = true;
            out.push_back(addr);

            const MicroOp& row = cm[static_cast<size_t>(addr)];
            if (row.next != kMicroEnd) pending.push_back(row.next);

            if (row.cond != BranchCond::None && row.branch_target != kMicroEnd) {
                pending.push_back(row.branch_target);
            }
        }

        std::sort(out.begin(), out.end());
        return out;
    }

    StageControls controlFor(const ControlUnit& cu, Opcode op) {
        StageControls sc;

        if (cu.controlMemory().empty()) {
            sc.halting = true;
            return sc;
        }

        for (int addr : reachableRows(cu, op)) {
            mergeRow(sc, cu.controlMemory()[static_cast<size_t>(addr)]);
        }
        return sc;
    }

    StageControlTable buildStageControls(const ControlUnit& cu) {
        StageControlTable table{};
        for (int i = 0; i < 256; ++i) {
            table[static_cast<size_t>(i)] = controlFor(cu, static_cast<Opcode>(i));
        }
        return table;
    }

    std::string controlProblem(const ControlUnit& cu, Opcode op) {
        const auto& cm = cu.controlMemory();
        if (cm.empty()) return "no control memory to collapse";

        const std::vector<int> rows = reachableRows(cu, op);

        bool saw_alu = false;
        Opcode alu_fn = Opcode::NOP;
        bool pc_from_target = false, pc_from_mem = false;
        bool mem_read = false, mem_write = false;
        bool sp_dec = false, sp_inc = false;

        for (int addr : rows) {
            const MicroOp& row = cm[static_cast<size_t>(addr)];
            const ControlWord& c = row.ctl;

            if (c.alu_enable) {
                if (saw_alu && c.alu_op != alu_fn) {
                    return "two ALU rows select different functions";
                }
                saw_alu = true;
                alu_fn = c.alu_op;
            }
            if (c.flag_update && !c.alu_enable) {
                return "flag_update on a row that does not enable the ALU";
            }
            if (c.pc_write) {
                if (c.pc_src_mem) pc_from_mem = true;
                else              pc_from_target = true;
            }
            mem_read |= c.mem_read;
            mem_write |= c.mem_write;
            sp_dec |= c.sp_dec;
            sp_inc |= c.sp_inc;
        }

        if (rows.empty())                 return "routine reached no control-memory row";
        if (pc_from_target && pc_from_mem) return "PC written from both a target and memory";
        if (mem_read && mem_write)         return "routine both reads and writes memory";
        if (sp_dec && sp_inc)              return "routine both decrements and increments SP";
        return std::string();
    }
}