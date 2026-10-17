// Control decode: proves each opcode's micro-routine collapses into one
// action-tagged control bundle without losing or inventing a signal. This is the
// contract the pipelined datapath will consume, so it is checked against the
// micro-code itself rather than against a hand-written copy.
//
// No CPU involved and no behaviour change: cpu.cpp is untouched by this phase.

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

    add(c.pc_inc, "pc_inc");
    add(c.alu_enable, "alu_enable");
    if (c.alu_enable) s.insert(std::string("alu_op=") + opcodeName(c.alu_op));
    add(c.alu_src_imm, "alu_src_imm");
    add(c.flag_update, "flag_update");
    add(c.sp_dec, "sp_dec");
    add(c.sp_inc, "sp_inc");
    add(c.mem_read, "mem_read");
    add(c.mem_write, "mem_write");
    add(c.mem_addr_sp, "mem_addr_sp");
    add(c.mem_data_pc, "mem_data_pc");
    add(c.stack_check, "stack_check");
    add(c.reg_write, "reg_write");
    add(c.mem_to_reg, "mem_to_reg");
    add(c.halt, "halt");
    if (c.pc_write) s.insert(c.pc_src_mem ? "pc_from_mem" : "pc_from_target");
    if (row.cond != BranchCond::None) s.insert(std::string("cond=") + condName(row.cond));
    return s;
}

std::set<std::string> controlsSignals(const StageControls& sc) {
    std::set<std::string> s;
    auto add = [&s](bool on, const char* name) { if (on) s.insert(name); };

    add(sc.pc_inc, "pc_inc");
    add(sc.alu_enable, "alu_enable");
    if (sc.alu_enable) s.insert(std::string("alu_op=") + opcodeName(sc.alu_op));
    add(sc.alu_src_imm, "alu_src_imm");
    add(sc.flag_update, "flag_update");
    add(sc.sp_dec, "sp_dec");
    add(sc.sp_inc, "sp_inc");
    add(sc.mem_read, "mem_read");
    add(sc.mem_write, "mem_write");
    add(sc.mem_addr_sp, "mem_addr_sp");
    add(sc.mem_data_pc, "mem_data_pc");
    add(sc.stack_check, "stack_check");
    add(sc.reg_write, "reg_write");
    add(sc.mem_to_reg, "mem_to_reg");
    add(sc.halting, "halt");
    add(sc.pc_from_target, "pc_from_target");
    add(sc.pc_from_mem, "pc_from_mem");
    if (sc.cond != BranchCond::None) s.insert(std::string("cond=") + condName(sc.cond));
    return s;
}

std::set<std::string> routineSignals(const ControlUnit& cu, Opcode op) {
    std::set<std::string> all;
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

        CHECK(four[0].has(Action::IF) &&
              four[0].has(Action::ID) &&
              four[0].has(Action::OF));

        std::cout << "  ok    4-stage: " << stageNames(PipelineVariant::FourStage) << "\n";
        std::cout << "  ok    6-stage: " << stageNames(PipelineVariant::SixStage) << "\n";
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
        // CALL: fetch, make room, write the RETURN ADDRESS (mem_data_pc), then jump.
        const StageControls call = controlFor(cu, Opcode::CALL);
        CHECK(call.pc_inc && call.sp_dec && call.pc_from_target && !call.pc_from_mem);
        CHECK(call.mem_write && call.mem_addr_sp && call.mem_data_pc && call.stack_check);
        CHECK(!call.mem_read && !call.reg_write && !call.mem_to_reg);

        // RET: read first, then the PC comes from memory, then SP is released.
        const StageControls ret = controlFor(cu, Opcode::RET);
        CHECK(ret.mem_read && ret.mem_addr_sp && ret.stack_check && ret.pc_from_mem && ret.sp_inc);
        CHECK(!ret.pc_inc && !ret.pc_from_target && !ret.mem_write && !ret.reg_write);
        CHECK(!ret.alu_enable);

        // PUSH: same stack write as CALL, but the data is a REGISTER, not the PC.
        const StageControls push = controlFor(cu, Opcode::PUSH);
        CHECK(push.pc_inc && push.sp_dec && push.mem_write && push.mem_addr_sp && push.stack_check);
        CHECK(!push.mem_data_pc && !push.pc_from_target && !push.mem_read);

        // POP: read into a register, then release the slot.
        const StageControls pop = controlFor(cu, Opcode::POP);
        CHECK(pop.pc_inc && pop.mem_read && pop.mem_addr_sp && pop.stack_check);
        CHECK(pop.reg_write && pop.mem_to_reg && pop.sp_inc);
        CHECK(!pop.mem_write && !pop.mem_data_pc && !pop.sp_dec);

        // Address and move arithmetic must not latch FLAGS.
        for (Opcode op : {Opcode::LOAD, Opcode::STORE, Opcode::MOV}) {
            const StageControls sc = controlFor(cu, op);
            CHECK(sc.alu_enable && !sc.flag_update);
            CHECK(sc.alu_op == (op == Opcode::MOV ? Opcode::MOV : Opcode::ADD));
        }
        CHECK(controlFor(cu, Opcode::LOAD).mem_read && controlFor(cu, Opcode::LOAD).mem_to_reg);
        CHECK(controlFor(cu, Opcode::STORE).mem_write && !controlFor(cu, Opcode::STORE).reg_write);

        // R-type arithmetic does latch FLAGS.
        for (Opcode op : {Opcode::ADD, Opcode::SUB, Opcode::AND, Opcode::OR,
                          Opcode::XOR, Opcode::SHL, Opcode::SHR}) {
            const StageControls sc = controlFor(cu, op);
            CHECK(sc.alu_enable && sc.flag_update && sc.alu_op == op && sc.reg_write);
        }

        // BEQ is a diamond: both arms must survive the collapse.
        const StageControls beq = controlFor(cu, Opcode::BEQ);
        CHECK(beq.is_branch && beq.cond == BranchCond::Zero);
        CHECK(beq.pc_inc && beq.pc_from_target);
        CHECK_EQ(reachableRows(cu, Opcode::BEQ).size(), size_t{3});

        // JMP has no fetch row in the micro-code (PC is overwritten); the pipeline
        // still fetches, so the bundle carries only the transfer.
        const StageControls jmp = controlFor(cu, Opcode::JMP);
        CHECK(jmp.pc_from_target && !jmp.pc_inc && !jmp.is_branch);

        // NOP advances the PC and does nothing else.
        const StageControls nop = controlFor(cu, Opcode::NOP);
        CHECK(nop.pc_inc && !nop.alu_enable && !nop.mem_read && !nop.mem_write && !nop.reg_write);
    }

    section("Control decode: HALT and the illegal sink stop the machine, nothing else");
    {
        // HALT is a real opcode with its own routine; it just happens to be halt-only too.
        const StageControls halt = controlFor(cu, Opcode::HALT);
        CHECK(halt.halting);
        CHECK(controlsSignals(halt) == std::set<std::string>{"halt"});
        CHECK(cu.routineEntry(Opcode::HALT) != cu.routineEntry(Opcode::MUL));

        // Opcodes with no routine share the illegal sink and collapse identically.
        for (Opcode op : {Opcode::NOT, Opcode::SAR, Opcode::CMP, Opcode::MUL,
                          static_cast<Opcode>(0x55)}) {
            const StageControls sc = controlFor(cu, op);
            CHECK(sc.halting);
            CHECK(controlsSignals(sc) == std::set<std::string>{"halt"});
            CHECK_EQ(cu.routineEntry(op), cu.routineEntry(Opcode::MUL));  // shared sink
        }
    }

    section("Control decode: collapsed control words");
    {
        std::cout << "  " << std::left << std::setw(7) << "op" << "signals\n";
        for (Opcode op : {Opcode::ADD, Opcode::LOAD, Opcode::STORE, Opcode::JMP,
                          Opcode::HALT, Opcode::NOP, Opcode::BEQ, Opcode::MOV,
                          Opcode::CALL, Opcode::RET, Opcode::PUSH, Opcode::POP}) {
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
