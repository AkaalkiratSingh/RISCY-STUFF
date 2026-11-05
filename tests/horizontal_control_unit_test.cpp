// tests/horizontal_control_unit_test.cpp - horizontal control unit
#include "control_unit/control_path.h"
#include "control_unit/horizontal_control_unit.h"
#include <iostream>
#include <vector>

// CHECK instead of CHECK(): CHECK() compiles to nothing under NDEBUG (Release builds),
// which would make every test pass silently.
static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::cerr << "  FAIL line " << __LINE__ << ": " #cond "\n"; ++failures; } } while (0)
#define RUN(fn) do { const int before_ = failures; fn(); \
    std::cout << (failures == before_ ? "  ok    " : "  FAIL  ") << #fn << "\n"; } while (0)

using namespace risc201;

namespace {
Flags flagsWithZero(bool z) { Flags f; f.zero = z; return f; }

// Run one macro instruction to completion, collecting every micro-cycle's signals.
std::vector<ControlWord> runAll(Opcode op, const Flags& f = Flags{}) {
    ControlUnit cu; std::vector<ControlWord> out;
    do { out.push_back(cu.step(op, f)); } while (!cu.routineDone());
    return out;
}
}

void testEntryAddresses() {
    ControlUnit cu;
    CHECK(cu.routineEntry(Opcode::ADD)   == 1);
    CHECK(cu.routineEntry(Opcode::SUB)   == 4);
    CHECK(cu.routineEntry(Opcode::LOAD)  == 22);
    CHECK(cu.routineEntry(Opcode::STORE) == 26);
    CHECK(cu.routineEntry(Opcode::JMP)   == 29);
    CHECK(cu.routineEntry(Opcode::BEQ)   == 32);
    CHECK(cu.routineEntry(Opcode::MOV)   == 35);
    CHECK(cu.routineEntry(Opcode::CALL)  == 38);
    CHECK(cu.routineEntry(Opcode::RET)   == 41);
    CHECK(cu.routineEntry(Opcode::NOT)   == 43);
    CHECK(cu.routineEntry(Opcode::SAR)   == 46);
    CHECK(cu.routineEntry(Opcode::MUL)   == 49);
    CHECK(cu.routineEntry(Opcode::DIV)   == 55);
    CHECK(cu.routineEntry(Opcode::REM)   == 58);
    CHECK(cu.routineEntry(Opcode::BGT)   == 61);
    CHECK(cu.trapEntry() == 54);
    CHECK(cu.routineEntry(Opcode::CMP)   == 52);
    CHECK(cu.routineEntry(Opcode::FADD)  == 64);
    CHECK(cu.routineEntry(Opcode::FSUB)  == 67);
    CHECK(cu.routineEntry(Opcode::FMUL)  == 70);
    CHECK(cu.routineEntry(Opcode::FDIV)  == 73);
    CHECK(cu.routineEntry(Opcode::FCMP)  == 76);
    CHECK(cu.routineEntry(Opcode::FTOI)  == 78);
    CHECK(cu.routineEntry(Opcode::ITOF)  == 81);
    CHECK(cu.routineEntry(static_cast<Opcode>(0x55)) == 0);   // unknown -> illegal row
}

void testEveryOpcodeHasARoutine() {
    ControlUnit cu;
    const Opcode all[] = { Opcode::NOP, Opcode::ADD, Opcode::SUB, Opcode::AND, Opcode::OR, Opcode::XOR,
        Opcode::SHL, Opcode::SHR, Opcode::LOAD, Opcode::STORE, Opcode::JMP, Opcode::BEQ, Opcode::CALL,
        Opcode::RET, Opcode::NOT, Opcode::SAR, Opcode::CMP, Opcode::MOV,
        Opcode::MUL, Opcode::DIV, Opcode::REM, Opcode::BGT,
        Opcode::FADD, Opcode::FSUB, Opcode::FMUL, Opcode::FDIV, Opcode::FCMP,
        Opcode::FTOI, Opcode::ITOF,
        Opcode::HALT };
    for (Opcode op : all) CHECK(cu.routineEntry(op) != 0);   // nothing falls to the illegal row
}

void testAddRoutine() {
    ControlUnit cu; Flags f;
    ControlWord w = cu.step(Opcode::ADD, f);
    CHECK(w.operand_read && !w.alu_enable && !w.reg_write);
    CHECK(!cu.routineDone() && cu.microPC() == 2);
    w = cu.step(Opcode::ADD, f);
    CHECK(w.alu_enable && w.alu_op == Opcode::ADD && w.flag_update && !w.alu_src_imm);
    w = cu.step(Opcode::ADD, f);
    CHECK(w.reg_write && !w.mem_to_reg && cu.routineDone() && cu.microPC() == 0);
}

void testAluOpsCarryTheirOwnFunction() {
    const Opcode ops[] = { Opcode::ADD, Opcode::SUB, Opcode::AND, Opcode::OR, Opcode::XOR,
                           Opcode::SHL, Opcode::SHR, Opcode::NOT, Opcode::SAR, Opcode::MUL, Opcode::DIV, Opcode::REM };
    for (Opcode op : ops) {
        auto r = runAll(op);
        CHECK(r.size() == 3);
        CHECK(r[1].alu_enable && r[1].alu_op == op && r[1].flag_update);
        CHECK(r[2].reg_write && !r[2].mem_to_reg);
    }
}

void testFloatRoutines() {
    for (Opcode op : {Opcode::FADD, Opcode::FSUB, Opcode::FMUL, Opcode::FDIV,
                      Opcode::FTOI, Opcode::ITOF}) {
        auto r = runAll(op);
        CHECK(r.size() == 3 && r[0].operand_read);
        CHECK(r[1].alu_enable && r[1].alu_op == op && !r[1].flag_update);
        CHECK(r[2].reg_write && !r[2].mem_to_reg);
    }
    auto c = runAll(Opcode::FCMP);
    CHECK(c.size() == 2 && c[0].operand_read);
    CHECK(c[1].alu_enable && c[1].alu_op == Opcode::FCMP && c[1].flag_update);
    for (const auto& w : c) CHECK(!w.reg_write && !w.mem_write && !w.mem_read);
}

void testCmpSetsFlagsButWritesNothing() {
    auto r = runAll(Opcode::CMP);
    CHECK(r.size() == 2);
    CHECK(r[0].operand_read);
    CHECK(r[1].alu_enable && r[1].alu_op == Opcode::CMP && r[1].flag_update);
    for (const auto& w : r) CHECK(!w.reg_write && !w.mem_write && !w.mem_read);
}

void testLoadStore() {
    auto l = runAll(Opcode::LOAD);
    CHECK(l.size() == 4 && l[0].operand_read);
    CHECK(l[1].alu_enable && l[1].alu_src_imm && l[1].alu_op == Opcode::ADD && !l[1].flag_update);
    CHECK(l[2].mem_read && !l[2].mem_write);
    CHECK(l[2].addr_check && !l[2].mem_addr_sp);                  // bounds check, address from the ALU
    CHECK(!l[0].addr_check && !l[1].addr_check && !l[3].addr_check);    CHECK(l[3].reg_write && l[3].mem_to_reg);
    auto s = runAll(Opcode::STORE);
    CHECK(s.size() == 3 && s[1].alu_src_imm && !s[1].flag_update);
    CHECK(s[2].mem_write && !s[2].mem_read && !s[2].reg_write);
    CHECK(s[2].addr_check && !s[2].mem_addr_sp);
    CHECK(!s[0].addr_check && !s[1].addr_check);}

void testJmpHaltNop() {
    auto j = runAll(Opcode::JMP);  CHECK(j.size() == 1 && j[0].pc_write && !j[0].pc_inc);
    auto h = runAll(Opcode::HALT); CHECK(h.size() == 1 && h[0].halt);
    auto n = runAll(Opcode::NOP);  CHECK(n.size() == 1 && !n[0].pc_inc && !n[0].halt && !n[0].alu_enable);
}

void testMov() {
    auto r = runAll(Opcode::MOV);
    CHECK(r.size() == 3 && r[0].operand_read);
    CHECK(r[1].alu_enable && r[1].alu_op == Opcode::MOV && !r[1].flag_update);
    CHECK(r[2].reg_write && !r[2].mem_to_reg);
}

void testBeq() {
    { ControlUnit cu; Flags z = flagsWithZero(true);
      ControlWord w = cu.step(Opcode::BEQ, z);
      CHECK(!w.pc_write && !w.pc_inc && cu.microPC() == 34);
      w = cu.step(Opcode::BEQ, z); CHECK(w.pc_write && !w.pc_inc && cu.routineDone()); }
    { ControlUnit cu; Flags nz = flagsWithZero(false);
      ControlWord w = cu.step(Opcode::BEQ, nz);
      CHECK(!w.pc_write && !w.pc_inc && cu.microPC() == 33);
      w = cu.step(Opcode::BEQ, nz); CHECK(!w.pc_inc && !w.pc_write && cu.routineDone()); }
}

void testBgt() {
    Flags gt;  gt.greaterThan = true;
    { ControlUnit cu;
      ControlWord w = cu.step(Opcode::BGT, gt);                    // decision row
      CHECK(!w.pc_write && !w.pc_inc && cu.microPC() == 63 && !cu.routineDone());
      w = cu.step(Opcode::BGT, gt);                                // taken row
      CHECK(w.pc_write && !w.pc_inc && cu.routineDone()); }
    { ControlUnit cu; Flags ng;
      ControlWord w = cu.step(Opcode::BGT, ng);
      CHECK(!w.pc_write && !w.pc_inc && cu.microPC() == 62);
      w = cu.step(Opcode::BGT, ng);                                // not-taken row
      CHECK(!w.pc_inc && !w.pc_write && cu.routineDone()); }
    // BGT looks only at greaterThan: Z, N, C, V alone must not take it.
    Flags other; other.zero = other.negative = other.carry = other.overflow = other.equal = true;
    { ControlUnit cu; cu.step(Opcode::BGT, other); CHECK(cu.microPC() == 62); }
    // BEQ is unchanged and ignores greaterThan.
    { ControlUnit cu; cu.step(Opcode::BEQ, gt); CHECK(cu.microPC() == 33); }
}

void testCallRetRows() {
    auto c = runAll(Opcode::CALL);
    CHECK(c.size() == 3 && c[0].operand_read);
    CHECK(c[1].pc_write && !c[1].pc_src_mem && !c[1].pc_src_ra);
    CHECK(c[2].ra_write && !c[2].pc_write);
    auto r = runAll(Opcode::RET);
    CHECK(r.size() == 2);
    CHECK(r[0].operand_read);
    CHECK(r[1].pc_write && r[1].pc_src_ra);
    CHECK(!r[1].mem_read && !r[1].sp_inc);
}

// Toy datapath: applies each ControlWord to PC / SP / one register / stack memory.
// Every source in a cycle is the OLD state (like real registers).
struct ToyMachine {
    static constexpr uint32_t kBase = 64, kLimit = 60;   // empty: SP == kBase; room for 4 words
    uint32_t pc = 0, sp = kBase, mdr = 0, reg = 0, ra = 0, return_pc = 0;
    std::vector<uint32_t> mem = std::vector<uint32_t>(128, 0);
    ExceptionCode fault = ExceptionCode::NONE;

    // Returns false once the machine trapped. `w` is what the control unit returned this cycle.
    bool apply(const ControlWord& w, uint32_t target) {
        if (w.trap) return false;                    // TRAP row: stop, fault is in pendingException()
        const uint32_t opc = pc, osp = sp, omdr = mdr;
        if (w.mem_write && w.mem_addr_sp) mem[osp] = w.mem_data_pc ? opc : reg;
        if (w.mem_read  && w.mem_addr_sp) mdr = mem[osp];
        if (w.reg_write && w.mem_to_reg)  reg = omdr;
        if (w.ra_write) ra = return_pc;
        if (w.pc_inc)   pc = opc + 1;
        if (w.pc_write) pc = w.pc_src_ra ? ra : (w.pc_src_mem ? omdr : target);
        if (w.sp_dec)   sp = osp - 1;
        if (w.sp_inc)   sp = osp + 1;
        return true;
    }
    bool run(Opcode op, uint32_t target = 0) {
        ControlUnit cu; Flags f;
        fault = ExceptionCode::NONE;
        return_pc = pc + 1;
        for (int address : frontendRows(cu)) {
            if (!apply(cu.controlMemory()[static_cast<size_t>(address)].ctl, target)) return false;
        }
        for (int guard = 0; guard < 16; ++guard) {
            const ControlWord w = cu.step(op, f);
            if (!apply(w, target)) { fault = cu.pendingException(); return false; }
            if (cu.routineDone()) return true;
        }
        return false;
    }
};

void testCallRetSynchronisation() {
    ToyMachine m; m.pc = 100;
    CHECK(m.run(Opcode::CALL, 200));
    CHECK(m.pc == 200 && m.ra == 101 && m.sp == ToyMachine::kBase);
    CHECK(m.run(Opcode::RET) && m.pc == 101 && m.sp == ToyMachine::kBase);

    m.pc = 10; CHECK(m.run(Opcode::CALL, 300));
    const uint32_t outer = m.ra;
    m.pc = 301; CHECK(m.run(Opcode::CALL, 400));
    CHECK(m.ra == 302);
    CHECK(m.run(Opcode::RET) && m.pc == 302);
    m.ra = outer;
    CHECK(m.run(Opcode::RET) && m.pc == 11);
    CHECK(m.sp == ToyMachine::kBase);
}

void testBackToBackAndIllegal() {
    ControlUnit cu; Flags f;
    cu.step(Opcode::ADD, f); cu.step(Opcode::ADD, f); cu.step(Opcode::ADD, f);
    CHECK(cu.routineDone());
    CHECK(cu.step(Opcode::JMP, f).pc_write && cu.routineDone());
    cu.reset(); CHECK(!cu.routineDone() && cu.microPC() == 0);
    ControlUnit c2; ControlWord w = c2.step(static_cast<Opcode>(0x55), f);
    CHECK(w.halt && c2.routineDone());
}

void testControlMemoryWellFormed() {
    ControlUnit cu; const auto& cm = cu.controlMemory();
    CHECK(cm.size() == 86);
    CHECK(cm.size() <= kMaxControlMemoryRows);
    for (const MicroOp& r : cm) {
        CHECK(r.next == kMicroEnd || (r.next >= 0 && r.next < static_cast<int32_t>(cm.size())));
        if (r.cond != BranchCond::None) CHECK(r.branch_target >= 0 && r.branch_target < static_cast<int32_t>(cm.size()));
        CHECK(!(r.ctl.mem_read && r.ctl.mem_write));
        CHECK(!(r.ctl.pc_inc && r.ctl.pc_write));
        CHECK(!(r.ctl.sp_dec && r.ctl.sp_inc));
        CHECK(!r.ctl.mem_addr_sp || r.ctl.mem_read || r.ctl.mem_write);
        CHECK(!r.ctl.mem_data_pc || r.ctl.mem_write);
        CHECK(!r.ctl.pc_src_mem  || r.ctl.pc_write);
        CHECK(!r.ctl.pc_src_ra   || r.ctl.pc_write);
        CHECK(!(r.ctl.pc_src_mem && r.ctl.pc_src_ra));
        CHECK(!r.ctl.stack_check || r.ctl.mem_addr_sp);
        CHECK((ControlUnit::packHorizontal(r) >> 48) == 0);
        CHECK(!r.ctl.addr_check || ((r.ctl.mem_read || r.ctl.mem_write) && !r.ctl.mem_addr_sp));
        CHECK(!(r.ctl.addr_check && r.ctl.stack_check));          // one guard per access
        CHECK(static_cast<unsigned>(r.cond) <= 7u);               // cond field is 3 bits
        CHECK(!r.ctl.trap || r.ctl.halt);                        // a trap always stops the machine
    }
}

void testPackHorizontal() {
    ControlUnit cu; const auto& cm = cu.controlMemory();
    // ADD execute row (2): alu_enable(0) | flag_update(10) | alu_op=2<<16 | next=3<<24 | taken=END(0xFF)<<32
    CHECK(ControlUnit::packHorizontal(cm[2]) == ((1ull<<0)|(1ull<<10)|(2ull<<16)|(3ull<<24)|(0xFFull<<32)));
    // BEQ test row (32): cond=Zero(1)<<21 | next=33<<24 | taken=34<<32
    CHECK(ControlUnit::packHorizontal(cm[32]) == ((1ull<<21)|(33ull<<24)|(34ull<<32)));
    // MOV execute row (36): alu_op = MOV in bits 20:16
    CHECK(((ControlUnit::packHorizontal(cm[36]) >> 16) & 0x1F) == static_cast<uint64_t>(Opcode::MOV));
    // CALL jump row (39): pc_write(4) | next=40 | END
    CHECK(ControlUnit::packHorizontal(cm[39]) ==
           ((1ull<<4)|(40ull<<24)|(0xFFull<<32)));
    // CALL RA row (40): ra_write(42) | next=END | END
    CHECK(ControlUnit::packHorizontal(cm[40]) ==
           ((1ull<<42)|(0xFFull<<24)|(0xFFull<<32)));
    // LOAD access row (24): mem_read(1) | addr_check(41) | next=25 (writeback)
    CHECK(ControlUnit::packHorizontal(cm[24]) == ((1ull<<1)|(1ull<<41)|(25ull<<24)|(0xFFull<<32)));
    // BGT test row (61): cond=GreaterThan(6)<<21 | next=62<<24 | taken=63<<32
    CHECK(ControlUnit::packHorizontal(cm[61]) == ((6ull<<21)|(62ull<<24)|(63ull<<32)));
    // TRAP row (54): trap(40) | halt(11), next = END
    CHECK(ControlUnit::packHorizontal(cm[54]) == ((1ull<<40)|(1ull<<11)|(0xFFull<<24)|(0xFFull<<32)));
}

// ---------------------------------------------------------------------------------------
// ALU fault handling (divide by zero). DIV is not in the ISA yet, so these tests inject the
// fault into an ordinary ALU instruction exactly as the ALU's fault output would arrive.
// ---------------------------------------------------------------------------------------
constexpr ExceptionCode kDivZero = ExceptionCode::DIVIDE_BY_ZERO;

void testAluFaultAbortsRoutineWithoutCommitting() {
    ControlUnit cu; Flags f;
    ControlWord w = cu.step(Opcode::DIV, f, kDivZero);          // fetch row: ALU idle, fault not honoured
    CHECK(w.operand_read && cu.pendingException() == ExceptionCode::NONE);

    w = cu.step(Opcode::DIV, f, kDivZero);                      // execute row: fault honoured
    // every control signal of the faulting cycle is deleted
    CHECK(!w.alu_enable && !w.flag_update && !w.reg_write && !w.mem_write && !w.mem_read);
    CHECK(!w.pc_write && !w.pc_inc && !w.sp_dec && !w.sp_inc && !w.halt && !w.trap);
    CHECK(w.alu_op == Opcode::NOP && !w.alu_src_imm && !w.mem_to_reg);
    CHECK(!w.mem_addr_sp && !w.mem_data_pc && !w.pc_src_mem && !w.stack_check);
    CHECK(!cu.routineDone());                                  // TRAP row still to run
    CHECK(cu.microPC() == static_cast<uint32_t>(cu.trapEntry()));
    CHECK(cu.pendingException() == kDivZero);

    w = cu.step(Opcode::DIV, f);                                // TRAP row
    CHECK(w.trap && w.halt && !w.reg_write);
    CHECK(cu.routineDone());
    CHECK(cu.pendingException() == kDivZero);
}

void testFaultIgnoredWhileAluIsIdle() {
    ControlUnit cu; Flags f;
    ControlWord w = cu.step(Opcode::ADD, f, kDivZero);          // row 1 = fetch (no ALU)
    CHECK(w.operand_read && !w.trap && cu.pendingException() == ExceptionCode::NONE);
    CHECK(cu.microPC() == 2);                                  // routine continues normally
}

void testFaultOnLastRowOfRoutine() {
    ControlUnit cu; Flags f;                                    // CMP's ALU row is its LAST row
    cu.step(Opcode::CMP, f);
    ControlWord w = cu.step(Opcode::CMP, f, kDivZero);
    CHECK(!w.alu_enable && !w.flag_update);
    CHECK(!cu.routineDone());                                  // would have ended; TRAP row runs instead
    CHECK(cu.step(Opcode::CMP, f).trap && cu.routineDone());
}

void testFaultLifecycle() {
    ControlUnit cu; Flags f;
    cu.step(Opcode::ADD, f); cu.step(Opcode::ADD, f, kDivZero); cu.step(Opcode::ADD, f);
    CHECK(cu.pendingException() == kDivZero);

    // The automatic restart between instructions must not lose the exception.
    ControlWord w = cu.step(Opcode::JMP, f);
    CHECK(w.pc_write && cu.pendingException() == kDivZero);

    cu.clearException();
    CHECK(cu.pendingException() == ExceptionCode::NONE);

    cu.step(Opcode::ADD, f); cu.step(Opcode::ADD, f, kDivZero);
    cu.reset();
    CHECK(cu.pendingException() == ExceptionCode::NONE && cu.microPC() == 0 && !cu.routineDone());

    // After a reset the next instruction runs normally.
    auto r = runAll(Opcode::ADD);
    CHECK(r.size() == 3 && r[2].reg_write);
}

void testNormalExecutionIsUnaffected() {
    ControlUnit cu; Flags f;
    cu.step(Opcode::SUB, f); cu.step(Opcode::SUB, f); cu.step(Opcode::SUB, f);
    CHECK(cu.pendingException() == ExceptionCode::NONE && cu.routineDone());
    int traps = 0;
    for (const MicroOp& r : cu.controlMemory()) traps += r.ctl.trap ? 1 : 0;
    CHECK(traps == 2);
}

// Toy datapath: one destination register, FLAGS, a halted bit.
void testFaultLeavesMachineStateUntouched() {
    struct Toy {
        uint32_t rd = 1234; Flags flags; bool halted = false; bool trapped = false;
        void apply(const ControlWord& w) {
            if (w.flag_update) flags.zero = true;               // pretend the ALU result latched Z
            if (w.reg_write)   rd = 0;                          // pretend the result was written back
            if (w.trap)        trapped = true;
            if (w.halt)        halted = true;
        }
    };

    Toy ok;                                                     // no fault: state changes
    { ControlUnit cu; do ok.apply(cu.step(Opcode::DIV, ok.flags)); while (!cu.routineDone()); }
    CHECK(ok.rd == 0 && ok.flags.zero && !ok.halted);

    Toy bad;                                                    // fault in the ALU cycle
    { ControlUnit cu; int cyc = 0;
      do { bad.apply(cu.step(Opcode::DIV, bad.flags, cyc == 1 ? kDivZero : ExceptionCode::NONE)); ++cyc; }
      while (!cu.routineDone());
      CHECK(cu.pendingException() == kDivZero); }
    CHECK(bad.rd == 1234);                                     // destination register untouched
    CHECK(!bad.flags.zero);                                    // flags untouched
    CHECK(bad.trapped && bad.halted);                          // machine stopped with a trap
}


// ---------------------------------------------------------------------------------------
// Memory-bounds fault on LOAD/STORE. The memory guard's wire is INVALID_MEMORY_ACCESS; it is
// honoured only on the row that carries addr_check, i.e. the row that would touch memory.
// ---------------------------------------------------------------------------------------
constexpr ExceptionCode kBadAddr = ExceptionCode::INVALID_MEMORY_ACCESS;

void testLoadOutOfBoundsTraps() {
    ControlUnit cu; Flags f;
    cu.step(Opcode::LOAD, f);                                      // fetch
    cu.step(Opcode::LOAD, f);                                      // address = rs1 + imm
    ControlWord w = cu.step(Opcode::LOAD, f, kBadAddr);            // access row, address out of bounds
    CHECK(!w.mem_read && !w.mem_write && !w.reg_write && !w.addr_check);   // the read never happens
    CHECK(cu.pendingException() == kBadAddr);
    CHECK(cu.microPC() == static_cast<uint32_t>(cu.trapEntry()) && !cu.routineDone());
    w = cu.step(Opcode::LOAD, f);                                  // writeback row was skipped: TRAP runs
    CHECK(w.trap && w.halt && !w.reg_write && cu.routineDone());
}

void testStoreOutOfBoundsTraps() {
    ControlUnit cu; Flags f;
    cu.step(Opcode::STORE, f); cu.step(Opcode::STORE, f);
    ControlWord w = cu.step(Opcode::STORE, f, kBadAddr);           // access row is also the LAST row
    CHECK(!w.mem_write && !w.mem_read);                            // the write never happens
    CHECK(cu.pendingException() == kBadAddr && !cu.routineDone());
    CHECK(cu.step(Opcode::STORE, f).trap && cu.routineDone());
}

void testInBoundsAccessIsUnaffected() {
    for (Opcode op : { Opcode::LOAD, Opcode::STORE }) {
        ControlUnit cu; Flags f;
        do { cu.step(op, f, ExceptionCode::NONE); } while (!cu.routineDone());
        CHECK(cu.pendingException() == ExceptionCode::NONE);
    }
}

// Each fault wire only counts in a cycle where its own checker is active.
void testFaultsOnlyCountWhereTheirCheckerIsActive() {
    Flags f;
    { ControlUnit cu; cu.step(Opcode::LOAD, f);                    // fetch row
      ControlWord w = cu.step(Opcode::LOAD, f, kBadAddr);          // ALU row: a stale address wire is ignored
      CHECK(w.alu_enable && cu.pendingException() == ExceptionCode::NONE && cu.microPC() == 24); }
    { ControlUnit cu; cu.step(Opcode::LOAD, f); cu.step(Opcode::LOAD, f);
      ControlWord w = cu.step(Opcode::LOAD, f, kDivZero);          // divide-by-zero wire on a memory row
      CHECK(w.mem_read && cu.pendingException() == ExceptionCode::NONE); }
    { ControlUnit cu; cu.step(Opcode::CALL, f);
      ControlWord w = cu.step(Opcode::CALL, f, ExceptionCode::INVALID_MEMORY_ACCESS);   // jump row: no checker
      CHECK(w.pc_write && cu.pendingException() == ExceptionCode::NONE); }
    { ControlUnit cu; cu.step(Opcode::ADD, f);
      ControlWord w = cu.step(Opcode::ADD, f, ExceptionCode::INVALID_MEMORY_ACCESS);   // ALU row, no checker
      CHECK(w.alu_enable && cu.pendingException() == ExceptionCode::NONE); }
    { ControlUnit cu; cu.step(Opcode::ADD, f);
      ControlWord w = cu.step(Opcode::ADD, f, ExceptionCode::ILLEGAL_INSTRUCTION);
      CHECK(w.alu_enable && cu.pendingException() == ExceptionCode::NONE); }
}

// Toy LOAD/STORE datapath with a memory guard: the guard wire is evaluated from the latched
// address EVERY cycle, exactly as a hardware wire would be, and the datapath only obeys the
// control word it receives.
struct ToyLoadStore {
    static constexpr uint32_t kLimit = 100;                       // valid addresses: [0, kLimit)
    uint32_t reg = 1234, mdr = 0, addr = 0;
    std::vector<uint32_t> mem = std::vector<uint32_t>(kLimit, 0);
    bool wild = false, trapped = false, halted = false;
    ExceptionCode raised = ExceptionCode::NONE;

    void run(Opcode op, uint32_t effective_address) {
        ControlUnit cu; Flags f;
        for (int guard = 0; guard < 16; ++guard) {
            const ExceptionCode wire = addr >= kLimit ? kBadAddr : ExceptionCode::NONE;
            const ControlWord w = cu.step(op, f, wire);
            const uint32_t a = addr;                              // address bus = latched ALU result
            if (w.mem_read)  { if (a >= kLimit) wild = true; else mdr = mem[a]; }
            if (w.mem_write) { if (a >= kLimit) wild = true; else mem[a] = reg; }
            if (w.reg_write && w.mem_to_reg) reg = mdr;
            if (w.alu_enable && w.alu_src_imm) addr = effective_address;   // rs1 + imm, latched
            if (w.trap) { trapped = true; raised = cu.pendingException(); }
            if (w.halt) halted = true;
            if (cu.routineDone()) return;
        }
    }
};

void testBoundsGuardKeepsMemoryAndRegistersSafe() {
    ToyLoadStore m;
    m.mem[10] = 77;
    m.run(Opcode::LOAD, 10);                                      // in bounds: works
    CHECK(m.reg == 77 && !m.trapped);
    m.reg = 5; m.run(Opcode::STORE, 20);
    CHECK(m.mem[20] == 5 && !m.trapped && !m.wild);

    m.reg = 1234; m.run(Opcode::LOAD, 500);                       // out of bounds LOAD
    CHECK(m.trapped && m.halted && m.raised == kBadAddr);
    CHECK(m.reg == 1234 && !m.wild);                              // register untouched, no wild access

    ToyLoadStore s; s.reg = 9;
    s.run(Opcode::STORE, 100);                                    // first address past the end
    CHECK(s.trapped && s.raised == kBadAddr && !s.wild);

    // Boundary: last valid address still works, and a stale out-of-range latch does not
    // trap the next, valid instruction (the wire is ignored on its ALU row).
    ToyLoadStore b; b.reg = 3;
    b.run(Opcode::STORE, 99);
    CHECK(b.mem[99] == 3 && !b.trapped);
    ToyLoadStore c; c.run(Opcode::LOAD, 1000);  CHECK(c.trapped);
    c.trapped = false; c.halted = false; c.raised = ExceptionCode::NONE;   // fresh instruction, stale addr
    c.mem[4] = 8; c.run(Opcode::LOAD, 4);
    CHECK(!c.trapped && c.reg == 8);
}

void testVerticalIsPlaceholder() {
    ControlUnit cu(MicrocodeEncoding::Vertical); Flags f;
    CHECK(cu.step(Opcode::ADD, f).halt);
}

int main() {
    RUN(testEntryAddresses); RUN(testEveryOpcodeHasARoutine); RUN(testAddRoutine);
    RUN(testAluOpsCarryTheirOwnFunction); RUN(testFloatRoutines); RUN(testCmpSetsFlagsButWritesNothing); RUN(testLoadStore);
    RUN(testJmpHaltNop); RUN(testMov); RUN(testBeq); RUN(testBgt); RUN(testCallRetRows);
    RUN(testCallRetSynchronisation);
    RUN(testBackToBackAndIllegal);
    RUN(testAluFaultAbortsRoutineWithoutCommitting); RUN(testFaultIgnoredWhileAluIsIdle);
    RUN(testFaultOnLastRowOfRoutine); RUN(testFaultLifecycle); RUN(testNormalExecutionIsUnaffected);
    RUN(testFaultLeavesMachineStateUntouched);
    RUN(testLoadOutOfBoundsTraps); RUN(testStoreOutOfBoundsTraps); RUN(testInBoundsAccessIsUnaffected);
    RUN(testFaultsOnlyCountWhereTheirCheckerIsActive);
    RUN(testBoundsGuardKeepsMemoryAndRegistersSafe);
    RUN(testControlMemoryWellFormed); RUN(testPackHorizontal); RUN(testVerticalIsPlaceholder);
    if (failures == 0) { std::cout << "\nhorizontal_control_unit_test: all checks passed\n"; return 0; }
    std::cerr << "\nhorizontal_control_unit_test: " << failures << " check(s) failed\n";
    return 1;
}
