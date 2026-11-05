#include "simulator_core/control_decode.h"

#include "control_unit/control_path.h"

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

        template <class Unit>
        StageControls frontendOf(const Unit& cu) {
            StageControls sc;
            if (cu.controlMemory().empty()) return sc;
            for (int address : frontendRows(cu)) {
                mergeRow(sc, cu.controlMemory()[static_cast<size_t>(address)]);
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

            for (int address : routineRows(cu, op)) {
                mergeRow(sc, cu.controlMemory()[static_cast<size_t>(address)]);
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

        template <class Unit>
        std::vector<PathStep> traceOf(const Unit& cu, Opcode op,
                                      const Flags& flags, ExceptionCode fault) {
            std::vector<PathStep> steps;
            for (const ControlStep& step : controlPath(cu, op, flags, fault)) {
                steps.push_back({step.micro_pc, step.signals, step.condition,
                                 step.next, stageMask(step.signals)});
            }
            return steps;
        }

        template <class Unit>
        const std::vector<PathStep>& layoutOf(const Unit& cu, Opcode op) {
            static const std::vector<PathStep> empty;
            if (cu.controlMemory().empty()) return empty;

            static std::array<std::vector<PathStep>, 256> cache;
            static bool built = false;
            if (!built) {
                for (int i = 0; i < 256; ++i) {
                    std::vector<PathStep>& rows = cache[static_cast<size_t>(i)];
                    for (int address : frontendRows(cu)) {
                        const MicroOp& row = cu.controlMemory()[static_cast<size_t>(address)];
                        rows.push_back({address, row.ctl, row.cond, row.next,
                                        stageMask(row.ctl)});
                    }
                    for (int address : routineRows(cu, static_cast<Opcode>(i))) {
                        const MicroOp& row = cu.controlMemory()[static_cast<size_t>(address)];
                        rows.push_back({address, row.ctl, row.cond, row.next,
                                        stageMask(row.ctl)});
                    }
                }
                built = true;
            }
            return cache[static_cast<size_t>(op)];
        }

    }

    StageControls frontendControls(const ControlUnit& cu) { return frontendOf(cu); }
    StageControls frontendControls(const VerticalControlUnit& cu) { return frontendOf(cu); }

    std::vector<int> reachableRows(const ControlUnit& cu, Opcode op) {
        return routineRows(cu, op);
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

    StageMask stageMask(const ControlWord& c) {
        StageMask mask = 0;
        const auto add = [&mask](PipelineStage stage, bool on) {
            if (on) mask = static_cast<StageMask>(mask | stageBit(stage));
        };
        add(PipelineStage::IF,  c.ir_load);
        add(PipelineStage::ID,  c.instr_decode || c.opcode_dispatch);
        add(PipelineStage::OF,  c.operand_read);
        add(PipelineStage::EX,  c.alu_enable || c.pc_write || c.flag_update ||
                                c.sp_dec || c.sp_inc);
        add(PipelineStage::MEM, c.mem_read || c.mem_write || c.addr_check ||
                                c.stack_check);
        add(PipelineStage::WB,  c.reg_write || c.ra_write || c.mem_to_reg);
        return mask;
    }

    std::vector<PathStep> controlTrace(const ControlUnit& cu, Opcode op,
                                       const Flags& flags, ExceptionCode fault) {
        return traceOf(cu, op, flags, fault);
    }

    std::vector<PathStep> controlTrace(const VerticalControlUnit& cu, Opcode op,
                                       const Flags& flags, ExceptionCode fault) {
        return traceOf(cu, op, flags, fault);
    }

    const std::vector<PathStep>& opcodeLayout(const ControlUnit& cu, Opcode op) {
        return layoutOf(cu, op);
    }

    const std::vector<PathStep>& opcodeLayout(const VerticalControlUnit& cu, Opcode op) {
        return layoutOf(cu, op);
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
        if (mem_read && mem_write)        return "routine both reads and writes memory";
        if (sp_dec && sp_inc)             return "routine both decrements and increments SP";
        return std::string();
    }
} // namespace risc201
