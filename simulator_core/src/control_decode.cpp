#include "simulator_core/control_decode.h"

#include <algorithm>

namespace risc201 {

    namespace {

        // Folds one micro-row's signals into the flattened bundle.
        void mergeRow(StageControls& sc, const MicroOp& row) {
            const ControlWord& c = row.ctl;

            sc.ir_load |= c.ir_load;
            sc.instr_decode |= c.instr_decode;
            sc.operand_read |= c.operand_read;
            sc.opcode_dispatch |= c.opcode_dispatch;
            sc.pc_inc |= c.pc_inc;

            if (c.alu_enable) {
                sc.alu_enable = true;
                sc.alu_op = c.alu_op;
                sc.alu_src_imm |= c.alu_src_imm;
                sc.flag_update |= c.flag_update;
            }

            sc.sp_dec |= c.sp_dec;
            sc.sp_inc |= c.sp_inc;
            sc.mem_read |= c.mem_read;
            sc.mem_write |= c.mem_write;
            sc.mem_addr_sp |= c.mem_addr_sp;
            sc.mem_data_pc |= c.mem_data_pc;
            sc.stack_check |= c.stack_check;
            sc.reg_write |= c.reg_write;
            sc.mem_to_reg |= c.mem_to_reg;
            sc.halting |= c.halt;
            sc.ra_write |= c.ra_write;

            if (c.pc_write) {
                if (c.pc_src_mem)      sc.pc_from_mem = true;
                else if (c.pc_src_ra) sc.pc_from_ra = true;
                else                  sc.pc_from_target = true;
            }

            if (row.cond != BranchCond::None) {
                sc.is_branch = true;
                sc.cond = row.cond;
            }
        }

    }

    namespace {

        // shared by the horizontal and the vertical control unit.
        template <class Unit>
        std::vector<int> reachableRowsOf(const Unit& cu, Opcode op) {
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

        template <class Unit>
        StageControls frontendOf(const Unit& cu) {
            StageControls sc;
            if (cu.controlMemory().empty()) return sc;
            for (int addr = cu.frontendEntry(); addr != kMicroEnd;) {
                const auto& row = cu.controlMemory().at(static_cast<size_t>(addr));
                mergeRow(sc, row);
                addr = row.next;
            }
            return sc;
        }

        template <class Unit>
        StageControls controlForUnit(const Unit& cu, Opcode op) {
            StageControls sc = frontendOf(cu);

            if (cu.controlMemory().empty()) {
                sc.halting = true;
                return sc;
            }

            for (int addr : reachableRowsOf(cu, op)) {
                mergeRow(sc, cu.controlMemory()[static_cast<size_t>(addr)]);
            }
            return sc;
        }

        template <class Unit>
        StageControlTable buildTableOf(const Unit& cu) {
            StageControlTable table{};
            for (int i = 0; i < 256; ++i) {
                table[static_cast<size_t>(i)] = controlForUnit(cu, static_cast<Opcode>(i));
            }
            return table;
        }

    }

    StageControls frontendControls(const ControlUnit& cu) { return frontendOf(cu); }
    StageControls frontendControls(const VerticalControlUnit& cu) { return frontendOf(cu); }

    std::vector<int> reachableRows(const ControlUnit& cu, Opcode op) {
        return reachableRowsOf(cu, op);
    }

    StageControls controlFor(const ControlUnit& cu, Opcode op) {
        return controlForUnit(cu, op);
    }

    StageControlTable buildStageControls(const ControlUnit& cu) {
        return buildTableOf(cu);
    }

    StageControls controlFor(const VerticalControlUnit& cu, Opcode op) {
        return controlForUnit(cu, op);
    }

    StageControlTable buildStageControls(const VerticalControlUnit& cu) {
        return buildTableOf(cu);
    }

    std::string controlProblem(const ControlUnit& cu, Opcode op) {
        const auto& cm = cu.controlMemory();
        if (cm.empty()) return "no control memory to collapse";

        const std::vector<int> rows = reachableRows(cu, op);

        bool saw_alu = false;
        Opcode alu_fn = Opcode::NOP;
        bool pc_from_target = false, pc_from_mem = false, pc_from_ra = false;
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
                if (c.pc_src_mem)      pc_from_mem = true;
                else if (c.pc_src_ra) pc_from_ra = true;
                else                  pc_from_target = true;
            }
            mem_read |= c.mem_read;
            mem_write |= c.mem_write;
            sp_dec |= c.sp_dec;
            sp_inc |= c.sp_inc;
        }

        if (rows.empty())                 return "routine reached no control-memory row";
        const int pc_sources = (pc_from_target ? 1 : 0) + (pc_from_mem ? 1 : 0) + (pc_from_ra ? 1 : 0);
        if (pc_sources > 1)               return "PC written from more than one source";
        if (mem_read && mem_write)         return "routine both reads and writes memory";
        if (sp_dec && sp_inc)              return "routine both decrements and increments SP";
        return std::string();
    }
}
