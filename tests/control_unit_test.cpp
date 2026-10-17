// Control unit in isolation: asserts each macro instruction's micro-sequence,
// that microPC walks the routine and wraps, that unsupported opcodes fall into
// the illegal sink, and that CALL/RET/PUSH/POP drive the stack signals in a safe
// order — with a toy datapath proving the stack and PC stay consistent.
//
// No CPU involved yet — this is the proof Milan inherits a working sequencer.

#include "control_unit/control_unit.h"

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
// The stack signals (sp_dec/sp_inc/mem_addr_sp/mem_data_pc/stack_check) are
// asserted per row in the "stack signals" sections, not through this trace.
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

// ---------------------------------------------------------------------------
// Tiny stand-in for the datapath: applies each micro-cycle's ControlWord to
// PC / SP / stack memory so we can prove CALL and RET leave the stack and PC
// consistent (and fault safely). Inside one cycle every source is read from the
// OLD state, like real registers.
// ---------------------------------------------------------------------------
struct ToyMachine {
    static constexpr uint32_t kBase = 64;    // empty stack: SP == kBase (full-descending)
    static constexpr uint32_t kLimit = 60;   // SP below this = overflow (room for 4 return addresses)
    uint32_t pc = 0, sp = kBase, mdr = 0;
    std::vector<uint32_t> mem = std::vector<uint32_t>(128, 0);
    risc201::ExceptionCode fault = risc201::ExceptionCode::NONE;

    // Returns false if the guard stopped the machine this cycle.
    bool apply(const risc201::ControlWord& w, uint32_t target) {
        if (w.stack_check) {                         // guard runs BEFORE the access commits
            if (w.mem_write && sp < kLimit) { fault = risc201::ExceptionCode::STACK_OVERFLOW;  return false; }
            if (w.mem_read  && sp >= kBase) { fault = risc201::ExceptionCode::STACK_UNDERFLOW; return false; }
        }
        const uint32_t old_pc = pc, old_sp = sp, old_mdr = mdr;
        if (w.mem_write && w.mem_addr_sp && w.mem_data_pc) mem[old_sp] = old_pc;
        if (w.mem_read  && w.mem_addr_sp)                  mdr = mem[old_sp];
        if (w.pc_inc)   pc = old_pc + 1;
        if (w.pc_write) pc = w.pc_src_mem ? old_mdr : target;
        if (w.sp_dec)   sp = old_sp - 1;
        if (w.sp_inc)   sp = old_sp + 1;
        return true;
    }

    // Run one whole macro instruction; stops early on a fault.
    bool run(risc201::Opcode op, uint32_t target = 0) {
        risc201::ControlUnit cu;
        risc201::Flags f;
        do {
            if (!apply(cu.step(op, f), target)) return false;
        } while (!cu.routineDone());
        return true;
    }
};

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
    checkTrace("PUSH  fetch/dec/write", runRoutine(cu, Opcode::PUSH),
               {"f........", ".........", ".....w..."});
    checkTrace("POP   fetch/read/wb/inc", runRoutine(cu, Opcode::POP),
               {"f........", "....r....", "......RM.", "........."});
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
    CHECK_EQ(cu.routineEntry(Opcode::PUSH),  45);
    CHECK_EQ(cu.routineEntry(Opcode::POP),   48);
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

    section("ControlUnit: PUSH addresses through SP and guards the write");
    {
        ControlUnit c;
        Flags f;
        ControlWord w = c.step(Opcode::PUSH, f);        // 1: fetch
        CHECK(w.pc_inc && !w.sp_dec && !w.mem_write);

        w = c.step(Opcode::PUSH, f);                    // 2: make room on the stack
        CHECK(w.sp_dec && !w.sp_inc && !w.mem_write);

        w = c.step(Opcode::PUSH, f);                    // 3: write the register, guarded
        CHECK(w.mem_write && w.mem_addr_sp && w.stack_check && !w.mem_data_pc);
        CHECK(!w.mem_read && !w.sp_dec);
        CHECK(c.routineDone());
    }

    section("ControlUnit: POP reads, writes back, then releases the slot");
    {
        ControlUnit c;
        Flags f;
        ControlWord w = c.step(Opcode::POP, f);         // 1: fetch
        CHECK(w.pc_inc && !w.mem_read);

        w = c.step(Opcode::POP, f);                     // 2: read the stack word, guarded
        CHECK(w.mem_read && w.mem_addr_sp && w.stack_check);
        CHECK(!w.sp_inc && !w.reg_write);

        w = c.step(Opcode::POP, f);                     // 3: write it back to the register
        CHECK(w.reg_write && w.mem_to_reg && !w.sp_inc);

        w = c.step(Opcode::POP, f);                     // 4: release the slot
        CHECK(w.sp_inc && !w.sp_dec && !w.mem_read);
        CHECK(c.routineDone());
    }

    section("ControlUnit: BEQ branches on the zero flag");
    checkTrace("BEQ  Z=0 -> not taken (sequential fetch)",
               runRoutine(cu, Opcode::BEQ, Flags{}), {".........", "f........"});
    Flags zSet{};
    zSet.zero = true;
    checkTrace("BEQ  Z=1 -> taken (PC <- target)",
               runRoutine(cu, Opcode::BEQ, zSet), {".........", ".J......."});

    section("ControlUnit: unsupported opcodes hit the illegal sink");
    const int sink = cu.routineEntry(Opcode::MUL);
    for (Opcode op : {Opcode::NOT, Opcode::SAR, Opcode::CMP, Opcode::MUL}) {
        CHECK_EQ(cu.routineEntry(op), sink);
    }
    checkTrace("MUL -> halt (no routine yet)",
               runRoutine(cu, Opcode::MUL), {"........H"});

    section("ControlUnit: Control Memory is well formed");
    const auto& cm = cu.controlMemory();
    // illegal(1) + 7 R-type x3 + LOAD(4) + STORE(3) + JMP + HALT + NOP + BEQ(3) + MOV(3) + CALL(4) + RET(3) + PUSH(3) + POP(4)
    CHECK_EQ(cm.size(), static_cast<size_t>(52));
    CHECK(cm.size() <= kMaxControlMemoryRows);          // must fit the 8-bit microPC
    for (const auto& row : cm) {
        if (row.next != kMicroEnd) {
            CHECK(row.next >= 0);
            CHECK(static_cast<size_t>(row.next) < cm.size());
        }
        if (row.cond != BranchCond::None) {
            CHECK(row.branch_target >= 0);
            CHECK(static_cast<size_t>(row.branch_target) < cm.size());
        }
        // Mutually exclusive signal pairs.
        CHECK(!(row.ctl.mem_read && row.ctl.mem_write));
        CHECK(!(row.ctl.pc_inc && row.ctl.pc_write));
        CHECK(!(row.ctl.sp_dec && row.ctl.sp_inc));
        // Stack-synchronisation signals only make sense together with what they qualify.
        CHECK(!row.ctl.mem_addr_sp || row.ctl.mem_read || row.ctl.mem_write);
        CHECK(!row.ctl.mem_data_pc || row.ctl.mem_write);
        CHECK(!row.ctl.pc_src_mem || row.ctl.pc_write);
        CHECK(!row.ctl.stack_check || row.ctl.mem_addr_sp);
    }
    std::set<int> entries;
    for (Opcode op : {Opcode::ADD, Opcode::SUB, Opcode::AND, Opcode::OR,
                      Opcode::XOR, Opcode::SHL, Opcode::SHR, Opcode::LOAD,
                      Opcode::STORE, Opcode::JMP, Opcode::HALT, Opcode::NOP,
                      Opcode::BEQ, Opcode::MOV, Opcode::CALL, Opcode::RET,
                      Opcode::PUSH, Opcode::POP}) {
        const int e = cu.routineEntry(op);
        CHECK(e >= 0);
        CHECK(static_cast<size_t>(e) < cm.size());
        CHECK(entries.insert(e).second);  // no two opcodes share an entry
    }

    section("ControlUnit: ALU function select is per micro-op, not per opcode");
    const auto row = [&](Opcode op, int offset) -> const MicroOp& {
        return cm[static_cast<size_t>(cu.routineEntry(op) + offset)];
    };
    for (Opcode op : {Opcode::ADD, Opcode::SUB, Opcode::AND, Opcode::OR,
                      Opcode::XOR, Opcode::SHL, Opcode::SHR}) {
        CHECK(row(op, 1).ctl.alu_enable && row(op, 1).ctl.alu_op == op);
        CHECK(row(op, 1).ctl.flag_update);              // R-type math latches FLAGS
    }
    // LOAD/STORE form base+offset with an ADD whatever the macro opcode is,
    // and the address math must not disturb the condition flags.
    CHECK(row(Opcode::LOAD,  1).ctl.alu_op == Opcode::ADD);
    CHECK(row(Opcode::STORE, 1).ctl.alu_op == Opcode::ADD);
    CHECK(!row(Opcode::LOAD,  1).ctl.flag_update);
    CHECK(!row(Opcode::STORE, 1).ctl.flag_update);
    CHECK(!row(Opcode::MOV,   1).ctl.flag_update);      // MOV never changes FLAGS
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

    section("ControlUnit: packed horizontal microword");
    {
        const size_t add_ex = static_cast<size_t>(cu.routineEntry(Opcode::ADD) + 1);
        const uint64_t expect_add_exec =
            (1ull << 0) | (1ull << 10) |                     // alu_enable, flag_update
            (static_cast<uint64_t>(Opcode::ADD) << 16) |
            (static_cast<uint64_t>(add_ex + 1) << 24) | (0xFFull << 32);
        CHECK_EQ(ControlUnit::packHorizontal(cm[add_ex]), expect_add_exec);

        const size_t mov_ex = static_cast<size_t>(cu.routineEntry(Opcode::MOV) + 1);
        CHECK_EQ((ControlUnit::packHorizontal(cm[mov_ex]) >> 16) & 0x1Fu,
                 static_cast<uint64_t>(Opcode::MOV));

        const size_t beq_test = static_cast<size_t>(cu.routineEntry(Opcode::BEQ));
        const uint64_t expect_beq_test =
            (static_cast<uint64_t>(BranchCond::Zero) << 21) |   // no datapath signals
            (static_cast<uint64_t>(beq_test + 1) << 24) | (static_cast<uint64_t>(beq_test + 2) << 32);
        CHECK_EQ(ControlUnit::packHorizontal(cm[beq_test]), expect_beq_test);

        const size_t call_write = static_cast<size_t>(cu.routineEntry(Opcode::CALL) + 2);
        const uint64_t expect_call_write =
            (1ull << 2) | (1ull << 12) | (1ull << 13) | (1ull << 15) |  // mem_write/addr_sp/data_pc/check
            (static_cast<uint64_t>(call_write + 1) << 24) | (0xFFull << 32);
        CHECK_EQ(ControlUnit::packHorizontal(cm[call_write]), expect_call_write);

        // No packed word may use more than 40 bits.
        for (const MicroOp& r : cm) {
            CHECK_EQ(ControlUnit::packHorizontal(r) >> 40, 0ull);
        }
    }

    section("ControlUnit: CALL/RET keep the stack and PC in sync (toy datapath)");
    {
        ToyMachine m;
        m.pc = 100;

        CHECK(m.run(Opcode::CALL, 200));                // CALL 200 at address 100
        CHECK_EQ(m.pc, 200u);
        CHECK_EQ(m.sp, ToyMachine::kBase - 1);          // one slot pushed
        CHECK_EQ(m.mem[m.sp], 101u);                    // return address = next instruction

        CHECK(m.run(Opcode::RET));
        CHECK_EQ(m.pc, 101u);                           // back after the call site
        CHECK_EQ(m.sp, ToyMachine::kBase);              // stack empty again

        // Nested calls unwind in reverse order.
        m.pc = 10;  CHECK(m.run(Opcode::CALL, 300));    // return address 11
        m.pc = 301; CHECK(m.run(Opcode::CALL, 400));    // return address 302
        m.pc = 401; CHECK(m.run(Opcode::CALL, 500));    // return address 402
        CHECK_EQ(m.sp, ToyMachine::kBase - 3);
        CHECK(m.run(Opcode::RET)); CHECK_EQ(m.pc, 402u);
        CHECK(m.run(Opcode::RET)); CHECK_EQ(m.pc, 302u);
        CHECK(m.run(Opcode::RET)); CHECK_EQ(m.pc, 11u);
        CHECK_EQ(m.sp, ToyMachine::kBase);
    }

    section("ControlUnit: a full stack faults before the PC jumps");
    {
        ToyMachine m;
        for (int i = 0; i < 4; ++i) { m.pc = 1000 + i; CHECK(m.run(Opcode::CALL, 2000)); }
        CHECK_EQ(m.sp, ToyMachine::kLimit);             // stack exactly full

        m.pc = 7;
        CHECK(!m.run(Opcode::CALL, 5000));              // 5th call must fault
        CHECK(m.fault == ExceptionCode::STACK_OVERFLOW);
        CHECK(m.pc != 5000u);                           // never jumped to the target
        CHECK_EQ(m.mem[m.sp], 0u);                      // and nothing was written out of bounds
    }

    section("ControlUnit: RET on an empty stack faults before jumping");
    {
        ToyMachine m;                                   // empty stack
        m.pc = 55;
        CHECK(!m.run(Opcode::RET));
        CHECK(m.fault == ExceptionCode::STACK_UNDERFLOW);
        CHECK_EQ(m.pc, 55u);                            // PC untouched
        CHECK_EQ(m.sp, ToyMachine::kBase);              // SP untouched (no sp_inc past the base)
    }

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
