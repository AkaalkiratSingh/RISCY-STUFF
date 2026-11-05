// Control decode: proves each opcode's micro-routine collapses into one
// action-tagged control bundle without losing or inventing a signal. This is the
// contract the pipelined datapath will consume, so it is checked against the
// micro-code itself rather than against a hand-written copy.
//
// No CPU involved and no behaviour change: cpu.cpp is untouched by this phase.

#include "control_unit/control_path.h"
#include "simulator_core/control_decode.h"
#include "utils/utils.h"

#include <iomanip>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace {

int failures = 0;

#define CHECK(cond) do {                                                   \
    if (!(cond)) {                                                         \
        std::cerr << "  FAIL line " << __LINE__ << ": " #cond << "\n";     \
        ++failures;                                                        \
    }                                                                      \
} while (0)

#define CHECK_EQ(actual, expected) do {                                    \
    const auto got_  = (actual);                                           \
    const auto want_ = (expected);                                         \
    if (!(got_ == want_)) {                                                \
        std::cerr << "  FAIL line " << __LINE__ << ": " #actual            \
                  << " == " #expected << "\n    got  " << got_             \
                  << "\n    want " << want_ << "\n";                       \
        ++failures;                                                        \
    }                                                                      \
} while (0)

void section(const char* name) { std::cout << name << "\n"; }

using namespace risc201;

const char* condName(BranchCond c) {
    switch (c) {
        case BranchCond::None:     return "-";
        case BranchCond::Zero:     return "Z";
        case BranchCond::NotZero:  return "!Z";
        case BranchCond::Negative: return "N";
        case BranchCond::Carry:    return "C";
        case BranchCond::Overflow: return "V";
        case BranchCond::GreaterThan: return "GT";
    }
    return "?";
}

std::string join(const std::set<std::string>& names) {
    std::string out;
    for (const auto& n : names) {
        if (!out.empty()) out += ' ';
        out += n;
    }
    return out.empty() ? "(none)" : out;
}

// --- independent re-derivation --------------------------------------------------
// These two name the signals on each side using one shared vocabulary. They are
// written straight against MicroOp/StageControls, so they share no code with the
// merge under test — a dropped or invented signal shows up as a name-set mismatch.
std::set<std::string> rowSignals(const MicroOp& row) {
    const ControlWord& c = row.ctl;
    std::set<std::string> s;
    auto add = [&s](bool on, const char* name) { if (on) s.insert(name); };

    add(c.ir_load, "ir_load");
    add(c.instr_decode, "instr_decode");
    add(c.operand_read, "operand_read");
    add(c.opcode_dispatch, "opcode_dispatch");
    add(c.pc_inc, "pc_inc");
    add(c.alu_enable, "alu_enable");
    if (c.alu_enable) s.insert(std::string("alu_op=") + opcodeName(c.alu_op));
    add(c.alu_src_imm, "alu_src_imm");
    add(c.flag_update, "flag_update");
    add(c.mem_read, "mem_read");
    add(c.mem_write, "mem_write");
    add(c.reg_write, "reg_write");
    add(c.mem_to_reg, "mem_to_reg");
    add(c.halt, "halt");
    add(c.ra_write, "ra_write");
    if (c.pc_write) s.insert(c.pc_src_ra ? "pc_from_ra" : "pc_from_target");
    if (row.cond != BranchCond::None) s.insert(std::string("cond=") + condName(row.cond));
    return s;
}

std::set<std::string> controlsSignals(const StageControls& sc) {
    std::set<std::string> s;
    auto add = [&s](bool on, const char* name) { if (on) s.insert(name); };

    add(sc.ir_load, "ir_load");
    add(sc.instr_decode, "instr_decode");
    add(sc.operand_read, "operand_read");
    add(sc.opcode_dispatch, "opcode_dispatch");
    add(sc.pc_inc, "pc_inc");
    add(sc.alu_enable, "alu_enable");
    if (sc.alu_enable) s.insert(std::string("alu_op=") + opcodeName(sc.alu_op));
    add(sc.alu_src_imm, "alu_src_imm");
    add(sc.flag_update, "flag_update");
    add(sc.mem_read, "mem_read");
    add(sc.mem_write, "mem_write");
    add(sc.reg_write, "reg_write");
    add(sc.mem_to_reg, "mem_to_reg");
    add(sc.halting, "halt");
    add(sc.ra_write, "ra_write");
    add(sc.pc_from_target, "pc_from_target");
    add(sc.pc_from_ra, "pc_from_ra");
    if (sc.cond != BranchCond::None) s.insert(std::string("cond=") + condName(sc.cond));
    return s;
}

std::set<std::string> routineSignals(const ControlUnit& cu, Opcode op) {
    std::set<std::string> all;
    for (int addr : frontendRows(cu)) {
        for (const auto& name : rowSignals(cu.controlMemory()[static_cast<size_t>(addr)])) {
            all.insert(name);
        }
    }
    for (int addr : reachableRows(cu, op)) {
        for (const auto& name : rowSignals(cu.controlMemory()[static_cast<size_t>(addr)])) {
            all.insert(name);
        }
    }
    return all;
}

} // namespace

int main() {
    ControlUnit cu;
    const StageControlTable table = buildStageControls(cu);

    section("Control decode: shared front end and operand gates");
    {
        const auto front = frontendControls(cu);
        const std::vector<int> front_rows = frontendRows(cu);
        CHECK(front.ir_load && front.pc_inc && front.instr_decode && front.opcode_dispatch);
        CHECK(!front.operand_read && !front.alu_enable && !front.reg_write);
        CHECK(front_rows.size() >= 2);
        CHECK(cu.controlMemory()[static_cast<size_t>(front_rows[0])].ctl.ir_load);
        const auto& decode = cu.controlMemory()[static_cast<size_t>(front_rows[1])];
        CHECK(decode.ctl.instr_decode && decode.ctl.opcode_dispatch);
        CHECK(decode.next == kMicroEnd);
        for (int i = 0; i < 256; ++i) {
            const auto& c = table[static_cast<size_t>(i)];
            CHECK(c.ir_load && c.instr_decode && c.pc_inc && c.opcode_dispatch);
            if (c.alu_enable || c.mem_write || c.pc_from_ra) CHECK(c.operand_read);
        }
        for (auto op : {Opcode::JMP, Opcode::BEQ, Opcode::BGT, Opcode::HALT, Opcode::NOP}) {
            CHECK(!controlFor(cu, op).operand_read);
        }
        for (auto op : {Opcode::ADD, Opcode::LOAD, Opcode::STORE, Opcode::RET,
                        Opcode::FTOI, Opcode::ITOF}) {
            CHECK(controlFor(cu, op).operand_read);
        }
    }

    section("Control decode: the layout tables describe both variants");
    {
        const StageLayout four = layoutFor(PipelineVariant::FourStage);
        const StageLayout six  = layoutFor(PipelineVariant::SixStage);

        CHECK_EQ(four.count, size_t{4});
        CHECK_EQ(six.count,  size_t{6});
        CHECK_EQ(stageCount(PipelineVariant::FourStage), size_t{4});
        CHECK_EQ(stageCount(PipelineVariant::SixStage),  size_t{6});

        // Both variants must agree on the EX/MEM/WB tail; only the front-end differs.
        CHECK_EQ(std::string(four[1].name), std::string("EX"));
        CHECK_EQ(std::string(four[2].name), std::string("MEM"));
        CHECK_EQ(std::string(four[3].name), std::string("WB"));
        CHECK_EQ(std::string(six[3].name),  std::string("EX"));
        CHECK_EQ(std::string(six[4].name),  std::string("MEM"));
        CHECK_EQ(std::string(six[5].name),  std::string("WB"));

        CHECK(four[0].IF && four[0].ID && four[0].OF);
        for (int stage = 0; stage < 6; ++stage) {
            const RowStage row_stage = static_cast<RowStage>(stage);
            CHECK_EQ(slotForRow(PipelineVariant::SixStage, row_stage), stage);
            CHECK_EQ(slotForRow(PipelineVariant::FourStage, row_stage),
                     stage < 3 ? 0 : stage - 2);
        }
        CHECK_EQ(slotForRow(PipelineVariant::FourStage, RowStage::Terminal), -1);

        std::cout << "  ok    4-stage: " << stageNames(PipelineVariant::FourStage) << "\n";
        std::cout << "  ok    6-stage: " << stageNames(PipelineVariant::SixStage) << "\n";
    }

    section("Control decode: ROM rows belong to one monotonic stage");
    {
        const auto& rom = cu.controlMemory();
        for (const MicroOp& row : rom) {
            const RowStage stage = rowStage(row);
            const StageMask mask = stageMask(row.ctl);
            if (stage == RowStage::Terminal) {
                CHECK(mask == 0);
            } else {
                CHECK(mask == stageBit(static_cast<PipelineStage>(stage)) ||
                      (stage == RowStage::EX && mask == 0));
            }
            CHECK(std::string(rowStageName(stage)) != "?");
        }
        for (int i = 0; i < 256; ++i) {
            for (bool taken : {false, true}) {
                Flags flags;
                flags.zero = flags.greaterThan = taken;
                RowStage previous = RowStage::IF;
                for (const PathStep& step : controlTrace(cu, static_cast<Opcode>(i), flags)) {
                    const RowStage stage = rowStage(rom[static_cast<size_t>(step.micro_pc)]);
                    if (stage == RowStage::Terminal) continue;
                    CHECK(static_cast<int>(stage) >= static_cast<int>(previous));
                    previous = stage;
                }
            }
        }
    }

    section("Control decode: every opcode collapses without losing or inventing a signal");
    {
        int mismatches = 0;
        for (int i = 0; i < 256; ++i) {
            const Opcode op = static_cast<Opcode>(i);
            const auto want = routineSignals(cu, op);
            const auto got  = controlsSignals(controlFor(cu, op));
            if (want != got) {
                ++mismatches;
                ++failures;
                std::cerr << "  FAIL opcode 0x" << std::hex << i << std::dec
                          << " (" << opcodeName(op) << ")\n"
                          << "    routine:  " << join(want) << "\n"
                          << "    controls: " << join(got) << "\n";
            }
            // The table builder must agree with the per-opcode entry point.
            if (controlsSignals(table[static_cast<size_t>(i)]) != got) {
                ++failures;
                std::cerr << "  FAIL buildStageControls disagrees for opcode 0x"
                          << std::hex << i << std::dec << "\n";
            }
        }
        if (mismatches == 0) std::cout << "  ok    all 256 opcode values match their routine\n";
    }

    section("Control decode: every routine is expressible as one bundle");
    {
        int problems = 0;
        for (int i = 0; i < 256; ++i) {
            const std::string why = controlProblem(cu, static_cast<Opcode>(i));
            if (!why.empty()) {
                ++problems;
                ++failures;
                std::cerr << "  FAIL opcode 0x" << std::hex << i << std::dec
                          << ": " << why << "\n";
            }
        }
        if (problems == 0) std::cout << "  ok    no opcode needs more than one bundle\n";
    }

    section("Control decode: stack and control-flow opcodes keep their meaning");
    {
        // CALL: fetch, write the RETURN ADDRESS into R14, then jump.
        const StageControls call = controlFor(cu, Opcode::CALL);
        CHECK(call.pc_inc && call.ra_write && call.pc_from_target && !call.pc_from_ra);
        CHECK(!call.mem_write && !call.mem_read && !call.reg_write);

        // RET: jump through R14.
        const StageControls ret = controlFor(cu, Opcode::RET);
        CHECK(ret.pc_from_ra && !ret.pc_from_target);
        CHECK(ret.pc_inc && !ret.mem_write && !ret.mem_read && !ret.reg_write);
        CHECK(!ret.alu_enable);

        // // PUSH: same stack write as CALL, but the data is a REGISTER, not the PC.
        // const StageControls push = controlFor(cu, Opcode::PUSH);
        // CHECK(push.pc_inc && push.sp_dec && push.mem_write && push.mem_addr_sp && push.stack_check);
        // CHECK(!push.mem_data_pc && !push.pc_from_target && !push.mem_read);

        // // POP: read into a register, then release the slot.
        // const StageControls pop = controlFor(cu, Opcode::POP);
        // CHECK(pop.pc_inc && pop.mem_read && pop.mem_addr_sp && pop.stack_check);
        // CHECK(pop.reg_write && pop.mem_to_reg && pop.sp_inc);
        // CHECK(!pop.mem_write && !pop.mem_data_pc && !pop.sp_dec);

        // Address and move arithmetic must not latch FLAGS.
        for (Opcode op : {Opcode::LOAD, Opcode::STORE, Opcode::MOV}) {
            const StageControls sc = controlFor(cu, op);
            CHECK(sc.alu_enable && !sc.flag_update);
            CHECK(sc.alu_op == (op == Opcode::MOV ? Opcode::MOV : Opcode::ADD));
        }
        CHECK(controlFor(cu, Opcode::LOAD).mem_read && controlFor(cu, Opcode::LOAD).mem_to_reg);
        for (Opcode op : {Opcode::FTOI, Opcode::ITOF}) {
            const StageControls sc = controlFor(cu, op);
            CHECK(sc.operand_read && sc.alu_enable && sc.alu_op == op);
            CHECK(sc.reg_write && !sc.flag_update);
        }
        CHECK(controlFor(cu, Opcode::STORE).mem_write && !controlFor(cu, Opcode::STORE).reg_write);

        // R-type arithmetic does latch FLAGS.
        for (Opcode op : {Opcode::ADD, Opcode::SUB, Opcode::AND, Opcode::OR,
                          Opcode::XOR, Opcode::SHL, Opcode::SHR, Opcode::MUL,
                          Opcode::DIV, Opcode::REM}) {
            const StageControls sc = controlFor(cu, op);
            CHECK(sc.alu_enable && sc.flag_update && sc.alu_op == op && sc.reg_write);
        }

        // BEQ is a diamond: both arms must survive the collapse.
        const StageControls beq = controlFor(cu, Opcode::BEQ);
        CHECK(beq.is_branch && beq.cond == BranchCond::Zero);
        CHECK(beq.pc_inc && beq.pc_from_target);
        CHECK_EQ(reachableRows(cu, Opcode::BEQ).size(), size_t{3});

        const StageControls jmp = controlFor(cu, Opcode::JMP);
        CHECK(jmp.pc_from_target && jmp.pc_inc && !jmp.is_branch);

        // NOP advances the PC and does nothing else.
        const StageControls nop = controlFor(cu, Opcode::NOP);
        CHECK(nop.pc_inc && !nop.alu_enable && !nop.mem_read && !nop.mem_write && !nop.reg_write);
    }

    section("Control decode: HALT and the illegal sink stop the machine, nothing else");
    {
        // HALT is a real opcode with its own routine; it just happens to be halt-only too.
        const StageControls halt = controlFor(cu, Opcode::HALT);
        CHECK(halt.halting);
        CHECK((controlsSignals(halt) == std::set<std::string>{"halt", "ir_load", "instr_decode", "opcode_dispatch", "pc_inc"}));
        CHECK(cu.routineEntry(Opcode::HALT) != cu.routineEntry(static_cast<Opcode>(0x55)));

        // An opcode with no routine collapses to the illegal sink: halt, and nothing else.
        const StageControls sink = controlFor(cu, static_cast<Opcode>(0x55));
        CHECK(sink.halting);
        CHECK((controlsSignals(sink) == std::set<std::string>{"halt", "ir_load", "instr_decode", "opcode_dispatch", "pc_inc"}));
    }

    section("Control decode: collapsed control words");
    {
        std::cout << "  " << std::left << std::setw(7) << "op" << "signals\n";
        for (Opcode op : {Opcode::ADD, Opcode::MUL, Opcode::DIV, Opcode::REM,
                          Opcode::LOAD, Opcode::STORE, Opcode::JMP, Opcode::HALT,
                          Opcode::NOP, Opcode::BEQ, Opcode::MOV, Opcode::CALL,
                          Opcode::RET}) {
            std::cout << "  " << std::left << std::setw(7) << opcodeName(op)
                      << join(controlsSignals(controlFor(cu, op))) << "\n";
        }
    }

    if (failures == 0) {
        std::cout << "\ncontrol_decode_test: all checks passed\n";
        return 0;
    }
    std::cerr << "\ncontrol_decode_test: " << failures << " check(s) failed\n";
    return 1;
}
