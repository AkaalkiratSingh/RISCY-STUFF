// Control unit in isolation: asserts each macro instruction's micro-sequence,
// that microPC walks the routine and wraps, and that unsupported opcodes fall
// into the illegal sink instead of executing garbage.
//
// No CPU involved yet — this is the proof Milan inherits a working sequencer.

#include "control_unit/horizontal_control_unit.h"

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

// One char per control bit, ordered left-to-right as the datapath flows
// IF -> EX -> MEM -> WB, so a whole routine reads as a pipeline trace:
// "f........" == fetch, "..A......" == ALU with rs2, "......R.." == writeback.
//   f=pc_inc J=pc_write | A=alu_enable I=alu_src_imm |
//   r=mem_read w=mem_write | R=reg_write M=mem_to_reg | H=halt
std::string bits(const risc201::ControlWord& w) {
    std::string s;
    s += w.pc_inc      ? 'f' : '.';
    s += w.pc_write    ? 'J' : '.';
    s += w.alu_enable  ? 'A' : '.';
    s += w.alu_src_imm ? 'I' : '.';
    s += w.mem_read    ? 'r' : '.';
    s += w.mem_write   ? 'w' : '.';
    s += w.reg_write   ? 'R' : '.';
    s += w.mem_to_reg  ? 'M' : '.';
    s += w.halt        ? 'H' : '.';
    return s;
}

void section(const char* name) {
    std::cout << name << "\n";
}

void checkTrace(const char* label,
                const std::vector<std::string>& got,
                const std::vector<std::string>& want) {
    if (got == want) {
        std::cout << "  ok    " << label << "\n";
        return;
    }
    ++failures;
    std::cerr << "  FAIL  " << label << "\n    got:  ";
    for (const auto& s : got)  std::cerr << '[' << s << "] ";
    std::cerr << "\n    want: ";
    for (const auto& s : want) std::cerr << '[' << s << "] ";
    std::cerr << "\n";
}

// Drives one macro instruction to completion and records the emitted words.
// The bound turns a broken `next` chain into a failed assertion rather than a
// hang — every routine here is far shorter than 32 micro-cycles.
std::vector<std::string> runRoutine(risc201::ControlUnit& cu,
                                    risc201::Opcode op,
                                    risc201::Flags flags = {}) {
    std::vector<std::string> trace;
    cu.reset();
    for (int i = 0; i < 32; ++i) {
        trace.push_back(bits(cu.step(op, flags)));
        if (cu.routineDone()) return trace;
    }
    trace.push_back("<routine never terminated>");
    return trace;
}

} // namespace

int main() {
    using namespace risc201;
    ControlUnit cu;
    const Flags flags{};

    section("ControlUnit: encoding selection");
    CHECK(cu.encoding() == MicrocodeEncoding::Horizontal);
    CHECK(!cu.controlMemory().empty());

    section("ControlUnit: micro-sequences");
    checkTrace("ADD   fetch/exe/wb", runRoutine(cu, Opcode::ADD),
               {"f........", "..A......", "......R.."});
    checkTrace("SUB   same routine shape", runRoutine(cu, Opcode::SUB),
               {"f........", "..A......", "......R.."});
    checkTrace("AND/OR/XOR/SHL/SHR all R-type", runRoutine(cu, Opcode::XOR),
               {"f........", "..A......", "......R.."});
    checkTrace("LOAD  fetch/addr/read/wb", runRoutine(cu, Opcode::LOAD),
               {"f........", "..AI.....", "....r....", "......RM."});
    checkTrace("STORE fetch/addr/write", runRoutine(cu, Opcode::STORE),
               {"f........", "..AI.....", ".....w..."});
    checkTrace("JMP   taken transfer, no fetch row", runRoutine(cu, Opcode::JMP),
               {".J......."});
    checkTrace("HALT  single row", runRoutine(cu, Opcode::HALT),
               {"........H"});
    checkTrace("NOP   fetch only (advances PC)", runRoutine(cu, Opcode::NOP),
               {"f........"});
    // checkTrace("PUSH  fetch/dec/write", runRoutine(cu, Opcode::PUSH),
    //            {"f........", ".........", ".....w..."});
    // checkTrace("POP   fetch/read/wb/inc", runRoutine(cu, Opcode::POP),
    //            {"f........", "....r....", "......RM.", "........."});
    checkTrace("MOV   fetch/exe/wb", runRoutine(cu, Opcode::MOV),
               {"f........", "..A......", "......R.."});
    checkTrace("CALL  fetch/sp/write/jump", runRoutine(cu, Opcode::CALL),
               {"f........", ".........", ".....w...", ".J......."});
    checkTrace("RET   pop/pc/sp", runRoutine(cu, Opcode::RET),
               {"....r....", ".J.......", "........."});

    section("ControlUnit: routine entry addresses pin the CM layout");
    CHECK_EQ(cu.routineEntry(Opcode::ADD),   1);
    CHECK_EQ(cu.routineEntry(Opcode::SUB),   4);
    CHECK_EQ(cu.routineEntry(Opcode::LOAD),  22);
    CHECK_EQ(cu.routineEntry(Opcode::STORE), 26);
    CHECK_EQ(cu.routineEntry(Opcode::JMP),   29);
    CHECK_EQ(cu.routineEntry(Opcode::BEQ),   32);
    CHECK_EQ(cu.routineEntry(Opcode::MOV),   35);
    CHECK_EQ(cu.routineEntry(Opcode::CALL),  38);
    CHECK_EQ(cu.routineEntry(Opcode::RET),   42);
    // CHECK_EQ(cu.routineEntry(Opcode::PUSH),  45);
    // CHECK_EQ(cu.routineEntry(Opcode::POP),   48);
    // An opcode with no routine falls back to the illegal-instruction (halt) row.
    CHECK_EQ(cu.routineEntry(static_cast<Opcode>(0x55)), 0);

    section("ControlUnit: CALL stores the return address before it jumps");
    {
        ControlUnit c;
        Flags f;
        ControlWord w = c.step(Opcode::CALL, f);        // 1: PC <- return address
        CHECK(w.pc_inc && !w.sp_dec && !w.mem_write);

        w = c.step(Opcode::CALL, f);                    // 2: make room on the stack
        CHECK(w.sp_dec && !w.sp_inc && !w.mem_write && !w.pc_write);

        w = c.step(Opcode::CALL, f);                    // 3: store return address, guarded
        CHECK(w.mem_write && w.mem_addr_sp && w.mem_data_pc && w.stack_check);
        CHECK(!w.mem_read && !w.pc_write && !w.sp_dec); // PC has NOT jumped yet
        CHECK(!c.routineDone());

        w = c.step(Opcode::CALL, f);                    // 4: jump to target
        CHECK(w.pc_write && !w.pc_src_mem && !w.mem_write);
        CHECK(c.routineDone());
    }

    section("ControlUnit: RET reads the slot before releasing it");
    {
        ControlUnit c;
        Flags f;
        ControlWord w = c.step(Opcode::RET, f);         // 1: read return address, guarded
        CHECK(w.mem_read && w.mem_addr_sp && w.stack_check);
        CHECK(!w.pc_write && !w.sp_inc);                // SP not released before the read succeeds

        w = c.step(Opcode::RET, f);                     // 2: PC <- popped word
        CHECK(w.pc_write && w.pc_src_mem && !w.sp_inc);

        w = c.step(Opcode::RET, f);                     // 3: release the slot
        CHECK(w.sp_inc && !w.sp_dec && !w.mem_read);
        CHECK(c.routineDone());
    }

    // section("ControlUnit: PUSH addresses through SP and guards the write");
    // {
    //     ControlUnit c;
    //     Flags f;
    //     ControlWord w = c.step(Opcode::PUSH, f);        // 1: fetch
    //     CHECK(w.pc_inc && !w.sp_dec && !w.mem_write);

    //     w = c.step(Opcode::PUSH, f);                    // 2: make room on the stack
    //     CHECK(w.sp_dec && !w.sp_inc && !w.mem_write);

    //     w = c.step(Opcode::PUSH, f);                    // 3: write the register, guarded
    //     CHECK(w.mem_write && w.mem_addr_sp && w.stack_check && !w.mem_data_pc);
    //     CHECK(!w.mem_read && !w.sp_dec);
    //     CHECK(c.routineDone());
    // }

    // section("ControlUnit: POP reads, writes back, then releases the slot");
    // {
    //     ControlUnit c;
    //     Flags f;
    //     ControlWord w = c.step(Opcode::POP, f);         // 1: fetch
    //     CHECK(w.pc_inc && !w.mem_read);

    //     w = c.step(Opcode::POP, f);                     // 2: read the stack word, guarded
    //     CHECK(w.mem_read && w.mem_addr_sp && w.stack_check);
    //     CHECK(!w.sp_inc && !w.reg_write);

    //     w = c.step(Opcode::POP, f);                     // 3: write it back to the register
    //     CHECK(w.reg_write && w.mem_to_reg && !w.sp_inc);

    //     w = c.step(Opcode::POP, f);                     // 4: release the slot
    //     CHECK(w.sp_inc && !w.sp_dec && !w.mem_read);
    //     CHECK(c.routineDone());
    // }

    section("ControlUnit: BEQ branches on the zero flag");
    checkTrace("BEQ  Z=0 -> not taken (sequential fetch)",
               runRoutine(cu, Opcode::BEQ, Flags{}), {".........", "f........"});
    Flags zSet{};
    zSet.zero = true;
    checkTrace("BEQ  Z=1 -> taken (PC <- target)",
               runRoutine(cu, Opcode::BEQ, zSet), {".........", ".J......."});

    section("ControlUnit: unsupported opcodes hit the illegal sink");
    const int sink = cu.routineEntry(Opcode::CALL);
    for (Opcode op : {Opcode::CALL, Opcode::RET}) {
        CHECK_EQ(cu.routineEntry(op), sink);
    }
    checkTrace("CALL -> halt (no routine yet)",
               runRoutine(cu, Opcode::CALL), {"........H"});

    section("ControlUnit: Control Memory is well formed");
    const auto& cm = cu.controlMemory();
    // illegal(1) + 7 R-type x3 + LOAD(4) + STORE(3) + JMP + HALT + NOP + BEQ(3) + PUSH(2) + POP(2)
    CHECK_EQ(cm.size(), static_cast<size_t>(39));
    for (const auto& row : cm) {
        if (row.next != kMicroEnd) {
            CHECK(row.next >= 0);
            CHECK(static_cast<size_t>(row.next) < cm.size());
        }
    }
    std::set<int> entries;
    for (Opcode op : {Opcode::ADD, Opcode::SUB, Opcode::AND, Opcode::OR,
                      Opcode::XOR, Opcode::SHL, Opcode::SHR, Opcode::LOAD,
                      Opcode::STORE, Opcode::JMP, Opcode::HALT, Opcode::NOP,
                      Opcode::BEQ, Opcode::MOV, Opcode::CALL, Opcode::RET/*,
                      Opcode::PUSH, Opcode::POP*/}) {
        const int e = cu.routineEntry(op);
        CHECK(e >= 0);
        CHECK(static_cast<size_t>(e) < cm.size());
        CHECK(entries.insert(e).second);  // no two opcodes share an entry
    }

    section("ControlUnit: ALU function select is per micro-op, not per opcode");
    const auto row = [&](Opcode op, int offset) -> const MicroOp& {
        return cm[static_cast<size_t>(cu.routineEntry(op) + offset)];
    };
    CHECK(row(Opcode::ADD, 1).ctl.alu_op == Opcode::ADD);
    CHECK(row(Opcode::SUB, 1).ctl.alu_op == Opcode::SUB);
    CHECK(row(Opcode::XOR, 1).ctl.alu_op == Opcode::XOR);
    // LOAD/STORE form base+offset with an ADD whatever the macro opcode is.
    CHECK(row(Opcode::LOAD,  1).ctl.alu_op == Opcode::ADD);
    CHECK(row(Opcode::STORE, 1).ctl.alu_op == Opcode::ADD);
    // Rows that do not enable the ALU must not select a function.
    CHECK(row(Opcode::ADD, 0).ctl.alu_op == Opcode::NOP);
    CHECK(!row(Opcode::ADD, 0).ctl.alu_enable);

    section("ControlUnit: microPC sequencing");
    cu.reset();
    const int load_entry = cu.routineEntry(Opcode::LOAD);
    CHECK_EQ(static_cast<int>(cu.microPC()), 0);
    CHECK(!cu.routineDone());
    cu.step(Opcode::LOAD, flags);
    CHECK_EQ(static_cast<int>(cu.microPC()), load_entry + 1);
    cu.step(Opcode::LOAD, flags);
    CHECK_EQ(static_cast<int>(cu.microPC()), load_entry + 2);
    cu.step(Opcode::LOAD, flags);
    CHECK_EQ(static_cast<int>(cu.microPC()), load_entry + 3);
    CHECK(!cu.routineDone());
    cu.step(Opcode::LOAD, flags);           // terminal row
    CHECK(cu.routineDone());
    CHECK_EQ(static_cast<int>(cu.microPC()), 0);  // wraps per PRD 5.4

    section("ControlUnit: reset() mid-routine restarts it");
    cu.reset();
    cu.step(Opcode::ADD, flags);            // consume the fetch row
    cu.reset();
    CHECK(!cu.routineDone());
    CHECK_EQ(bits(cu.step(Opcode::ADD, flags)), std::string("f........"));

    section("ControlUnit: stepping past done re-latches from the opcode");
    runRoutine(cu, Opcode::ADD);            // leaves done_ == true
    checkTrace("ADD replayed without an explicit reset",
               runRoutine(cu, Opcode::ADD),
               {"f........", "..A......", "......R.."});
    CHECK_EQ(bits(cu.step(Opcode::JMP, flags)), std::string(".J......."));

    section("ControlUnit: vertical mode is not silently horizontal");
    ControlUnit vcu(MicrocodeEncoding::Vertical);
    CHECK(vcu.encoding() == MicrocodeEncoding::Vertical);
    CHECK(vcu.controlMemory().empty());     // no vertical ROM yet
    vcu.reset();
    const ControlWord vw = vcu.step(Opcode::ADD, flags);
    CHECK(vw.halt);                         // loud, per the ctor TODO(Milan)
    CHECK(vcu.routineDone());
    CHECK_EQ(vcu.microPC(), 0u);

    if (failures == 0) {
        std::cout << "\ncontrol_unit_test: all checks passed\n";
        return 0;
    }
    std::cerr << "\ncontrol_unit_test: " << failures << " check(s) failed\n";
    return 1;
}
