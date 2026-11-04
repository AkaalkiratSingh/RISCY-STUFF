// Control unit + the real ALU: the microcode rows drive Alu::execute through a tiny datapath,
// so float routines, FLAGS handling and BEQ/BGT after a compare are checked end to end.
// Needs the `alu` library linked in addition to `control_unit` (and `utils`).
#include "control_unit/control_path.h"
#include "control_unit/horizontal_control_unit.h"
#include "alu/alu.h"

#include <cstdint>
#include <cstring>
#include <iostream>

using namespace risc201;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::cerr << "  FAIL line " << __LINE__ << ": " #cond "\n"; ++failures; } } while (0)

namespace {

int32_t bitsOf(float f) { int32_t i; std::memcpy(&i, &f, 4); return i; }
float   floatOf(int32_t i) { float f; std::memcpy(&f, &i, 4); return f; }

struct Cpu {
    int32_t regs[16] = {};
    Flags   flags;
    uint32_t pc = 100;
    Alu     alu;
    bool    branched = false;

    // Runs one macro instruction: every micro-cycle's ControlWord drives the datapath.
    void run(Opcode op, int rd, int rs1, int rs2, uint32_t target = 500) {
        ControlUnit cu;
        Alu::Result last;
        branched = false;
        for (int address : frontendRows(cu)) {
            if (cu.controlMemory()[static_cast<size_t>(address)].ctl.pc_inc) ++pc;
        }
        do {
            const ControlWord w = cu.step(op, flags);
            if (w.alu_enable) last = alu.execute(w.alu_op, regs[rs1], regs[rs2]);
            if (w.flag_update) flags = last.flags;
            if (w.reg_write)   regs[rd] = last.value;
            if (w.pc_inc)      pc += 1;
            if (w.pc_write)  { pc = target; branched = true; }
        } while (!cu.routineDone());
    }
};

}  // namespace

int main() {
    // --- FADD / FSUB / FMUL / FDIV produce the right value and leave FLAGS alone ---
    {
        Cpu c; c.regs[1] = bitsOf(1.5f); c.regs[2] = bitsOf(2.25f);
        c.flags.zero = true; c.flags.carry = true;                 // sentinel flags
        c.run(Opcode::FADD, 3, 1, 2);  CHECK(floatOf(c.regs[3]) == 3.75f);
        c.run(Opcode::FSUB, 4, 2, 1);  CHECK(floatOf(c.regs[4]) == 0.75f);
        c.regs[5] = bitsOf(2.0f); c.regs[6] = bitsOf(3.0f);
        c.run(Opcode::FMUL, 7, 5, 6);  CHECK(floatOf(c.regs[7]) == 6.0f);
        c.run(Opcode::FDIV, 8, 7, 5);  CHECK(floatOf(c.regs[8]) == 3.0f);
        CHECK(c.flags.zero && c.flags.carry && !c.flags.negative);  // untouched by float arithmetic
        CHECK(c.pc == 104);                                         // each instruction advanced PC once
    }

    // --- FCMP sets flags, writes no register, and BGT / BEQ follow it correctly ---
    {
        Cpu c; c.regs[1] = bitsOf(5.5f); c.regs[2] = bitsOf(2.5f); c.regs[9] = 1234;
        c.run(Opcode::FCMP, 9, 1, 2);
        CHECK(c.flags.greaterThan && !c.flags.equal && !c.flags.zero);
        CHECK(c.regs[9] == 1234);                                   // no writeback
        c.run(Opcode::BGT, 0, 0, 0);  CHECK(c.branched && c.pc == 500);

        c.run(Opcode::FCMP, 9, 2, 1);                               // 2.5 vs 5.5
        CHECK(!c.flags.greaterThan);
        c.pc = 100; c.run(Opcode::BGT, 0, 0, 0);  CHECK(!c.branched && c.pc == 101);

        c.run(Opcode::FCMP, 9, 1, 1);                               // equal
        CHECK(c.flags.zero && c.flags.equal && !c.flags.greaterThan);
        c.run(Opcode::BEQ, 0, 0, 0);  CHECK(c.branched);            // BEQ (tests zero) taken on equal floats

        c.regs[3] = bitsOf(-1.0f); c.regs[4] = bitsOf(-2.0f);       // negatives: -1 > -2
        c.run(Opcode::FCMP, 9, 3, 4);  CHECK(c.flags.greaterThan);
    }

    // --- Integer CMP + BGT (signed greater-than) ---
    {
        Cpu c; c.regs[1] = 5; c.regs[2] = 3;
        c.run(Opcode::CMP, 0, 1, 2);  c.run(Opcode::BGT, 0, 0, 0);  CHECK(c.branched);
        c.run(Opcode::CMP, 0, 2, 1);  c.run(Opcode::BGT, 0, 0, 0);  CHECK(!c.branched);
        c.regs[1] = -4; c.regs[2] = 3;                                // signed: -4 > 3 is false
        c.run(Opcode::CMP, 0, 1, 2);  c.run(Opcode::BGT, 0, 0, 0);  CHECK(!c.branched);
        c.run(Opcode::CMP, 0, 2, 1);  c.run(Opcode::BGT, 0, 0, 0);  CHECK(c.branched);
    }

    // --- The ALU reports a zero divisor as a fault; the control unit and pipeline route it ---
    {
        Alu alu;
        Alu::Result q = alu.execute(Opcode::DIV, 7, 0);
        Alu::Result f = alu.execute(Opcode::FDIV, bitsOf(1.0f), bitsOf(0.0f));
        CHECK(q.fault == ExceptionCode::DIVIDE_BY_ZERO);
        CHECK(q.value == 0);
        CHECK(f.fault == ExceptionCode::NONE);                       // IEEE divide by zero is +infinity, not a fault
        CHECK(static_cast<uint32_t>(f.value) == 0x7F800000u);
    }

    if (failures == 0) { std::cout << "\ncontrol_unit_alu_integration_test: all checks passed\n"; return 0; }
    std::cerr << "\ncontrol_unit_alu_integration_test: " << failures << " check(s) failed\n";
    return 1;
}
