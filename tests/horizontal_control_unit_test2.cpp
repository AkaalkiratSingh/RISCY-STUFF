// tests/control_unit_test.cpp — horizontal control unit (Sept 30 milestone)
#include "control_unit/control_unit.h"
#include <cassert>
#include <iostream>
#include <vector>

using namespace risc201;

namespace {
    Flags flagsWithZero(bool z) {
        Flags f;
        f.zero = z;
        return f;
    }
}

void testEntryAddresses() {
    ControlUnit cu;
    assert(cu.routineEntry(Opcode::ADD)   == 1);
    assert(cu.routineEntry(Opcode::SUB)   == 4);
    assert(cu.routineEntry(Opcode::LOAD)  == 22);
    assert(cu.routineEntry(Opcode::STORE) == 26);
    assert(cu.routineEntry(Opcode::JMP)   == 29);
    assert(cu.routineEntry(Opcode::BEQ)   == 32);
    assert(cu.routineEntry(Opcode::MOV)   == 35);
    assert(cu.routineEntry(Opcode::CALL)  == 38);
    assert(cu.routineEntry(Opcode::RET)   == 42);
    // Opcodes with no routine yet fall back to the illegal-instruction (halt) row.
    assert(cu.routineEntry(Opcode::PUSH) == 0);
    assert(cu.routineEntry(static_cast<Opcode>(0x55)) == 0);
    std::cout << "testEntryAddresses passed\n";
}

void testAddRoutine() {
    ControlUnit cu;
    Flags f;

    ControlWord w = cu.step(Opcode::ADD, f);            // μ1: fetch
    assert(w.pc_inc && !w.alu_enable && !w.reg_write);
    assert(!cu.routineDone());
    assert(cu.microPC() == 2);

    w = cu.step(Opcode::ADD, f);                        // μ2: execute
    assert(w.alu_enable && w.alu_op == Opcode::ADD);
    assert(w.flag_update && !w.alu_src_imm);
    assert(!cu.routineDone());

    w = cu.step(Opcode::ADD, f);                        // μ3: writeback
    assert(w.reg_write && !w.mem_to_reg);
    assert(cu.routineDone());
    assert(cu.microPC() == 0);
    std::cout << "testAddRoutine passed\n";
}

void testEveryAluOpCarriesItsOwnFunction() {
    const Opcode ops[] = { Opcode::ADD, Opcode::SUB, Opcode::AND, Opcode::OR,
                           Opcode::XOR, Opcode::SHL, Opcode::SHR };
    for (Opcode op : ops) {
        ControlUnit cu;
        Flags f;
        cu.step(op, f);
        ControlWord ex = cu.step(op, f);
        assert(ex.alu_enable && ex.alu_op == op && ex.flag_update);
    }
    std::cout << "testEveryAluOpCarriesItsOwnFunction passed\n";
}

void testLoadRoutine() {
    ControlUnit cu;
    Flags f;

    ControlWord w = cu.step(Opcode::LOAD, f);           // fetch
    assert(w.pc_inc);
    w = cu.step(Opcode::LOAD, f);                       // address = rs1 + imm
    assert(w.alu_enable && w.alu_src_imm && w.alu_op == Opcode::ADD);
    assert(!w.flag_update);                             // address math must not touch flags
    w = cu.step(Opcode::LOAD, f);                       // memory read
    assert(w.mem_read && !w.mem_write);
    assert(!cu.routineDone());
    w = cu.step(Opcode::LOAD, f);                       // writeback from memory
    assert(w.reg_write && w.mem_to_reg);
    assert(cu.routineDone());
    std::cout << "testLoadRoutine passed\n";
}

void testStoreRoutine() {
    ControlUnit cu;
    Flags f;

    cu.step(Opcode::STORE, f);
    ControlWord w = cu.step(Opcode::STORE, f);
    assert(w.alu_enable && w.alu_src_imm && !w.flag_update);
    w = cu.step(Opcode::STORE, f);
    assert(w.mem_write && !w.mem_read && !w.reg_write);
    assert(cu.routineDone());
    std::cout << "testStoreRoutine passed\n";
}

void testJmpHaltNop() {
    Flags f;
    {
        ControlUnit cu;
        ControlWord w = cu.step(Opcode::JMP, f);
        assert(w.pc_write && !w.pc_inc);
        assert(cu.routineDone());
    }
    {
        ControlUnit cu;
        ControlWord w = cu.step(Opcode::HALT, f);
        assert(w.halt);
        assert(cu.routineDone());
    }
    {
        ControlUnit cu;
        ControlWord w = cu.step(Opcode::NOP, f);
        assert(!w.halt && !w.pc_write && !w.alu_enable);
        assert(w.pc_inc);                               // NOP still advances the PC
        assert(cu.routineDone());
    }
    std::cout << "testJmpHaltNop passed\n";
}

void testMovRoutine() {
    ControlUnit cu;
    Flags f;

    ControlWord w = cu.step(Opcode::MOV, f);            // fetch
    assert(w.pc_inc && !w.alu_enable);
    w = cu.step(Opcode::MOV, f);                        // ALU passes the source operand
    assert(w.alu_enable && w.alu_op == Opcode::MOV);
    assert(!w.flag_update);                             // MOV never changes FLAGS
    assert(!w.mem_read && !w.mem_write);
    assert(!cu.routineDone());
    w = cu.step(Opcode::MOV, f);                        // write rd
    assert(w.reg_write && !w.mem_to_reg);
    assert(cu.routineDone());
    std::cout << "testMovRoutine passed\n";
}

void testCallRoutine() {
    ControlUnit cu;
    Flags f;

    ControlWord w = cu.step(Opcode::CALL, f);           // 1: PC <- return address
    assert(w.pc_inc && !w.sp_dec && !w.mem_write);

    w = cu.step(Opcode::CALL, f);                       // 2: make room on the stack
    assert(w.sp_dec && !w.sp_inc && !w.mem_write && !w.pc_write);

    w = cu.step(Opcode::CALL, f);                       // 3: store return address, guarded
    assert(w.mem_write && w.mem_addr_sp && w.mem_data_pc && w.stack_check);
    assert(!w.mem_read && !w.pc_write && !w.sp_dec);    // PC has NOT jumped yet
    assert(!cu.routineDone());

    w = cu.step(Opcode::CALL, f);                       // 4: jump to target
    assert(w.pc_write && !w.pc_src_mem && !w.mem_write);
    assert(cu.routineDone());
    std::cout << "testCallRoutine passed\n";
}

void testRetRoutine() {
    ControlUnit cu;
    Flags f;

    ControlWord w = cu.step(Opcode::RET, f);            // 1: read return address, guarded
    assert(w.mem_read && w.mem_addr_sp && w.stack_check);
    assert(!w.pc_write && !w.sp_inc);                   // SP not released before the read succeeds

    w = cu.step(Opcode::RET, f);                        // 2: PC <- popped word
    assert(w.pc_write && w.pc_src_mem && !w.sp_inc);

    w = cu.step(Opcode::RET, f);                        // 3: release the slot
    assert(w.sp_inc && !w.sp_dec && !w.mem_read);
    assert(cu.routineDone());
    std::cout << "testRetRoutine passed\n";
}

// ---------------------------------------------------------------------------------------
// Tiny stand-in for the datapath: applies each micro-cycle's ControlWord to PC / SP / stack
// memory so we can prove CALL and RET leave the stack and PC consistent (and fault safely).
// Inside one cycle every source is read from the OLD state, like real registers.
// ---------------------------------------------------------------------------------------
struct ToyMachine {
    static constexpr uint32_t kBase = 64;    // empty stack: SP == kBase (full-descending)
    static constexpr uint32_t kLimit = 60;   // SP below this = overflow (room for 4 return addresses)
    uint32_t pc = 0, sp = kBase, mdr = 0;
    std::vector<uint32_t> mem = std::vector<uint32_t>(128, 0);
    ExceptionCode fault = ExceptionCode::NONE;

    // Returns false if the guard stopped the machine this cycle.
    bool apply(const ControlWord& w, uint32_t target) {
        if (w.stack_check) {                         // guard runs BEFORE the access commits
            if (w.mem_write && sp < kLimit)  { fault = ExceptionCode::STACK_OVERFLOW;  return false; }
            if (w.mem_read  && sp >= kBase)  { fault = ExceptionCode::STACK_UNDERFLOW; return false; }
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
    bool run(Opcode op, uint32_t target = 0) {
        ControlUnit cu;
        Flags f;
        do {
            if (!apply(cu.step(op, f), target)) return false;
        } while (!cu.routineDone());
        return true;
    }
};

void testCallRetKeepStackAndPcInSync() {
    ToyMachine m;
    m.pc = 100;

    assert(m.run(Opcode::CALL, 200));                   // CALL 200 at address 100
    assert(m.pc == 200);
    assert(m.sp == ToyMachine::kBase - 1);              // one slot pushed
    assert(m.mem[m.sp] == 101);                         // return address = next instruction

    assert(m.run(Opcode::RET));
    assert(m.pc == 101);                                // back after the call site
    assert(m.sp == ToyMachine::kBase);                  // stack empty again

    // Nested calls unwind in reverse order.
    m.pc = 10;  assert(m.run(Opcode::CALL, 300));       // return address 11
    m.pc = 301; assert(m.run(Opcode::CALL, 400));       // return address 302
    m.pc = 401; assert(m.run(Opcode::CALL, 500));       // return address 402
    assert(m.sp == ToyMachine::kBase - 3);
    assert(m.run(Opcode::RET)); assert(m.pc == 402);
    assert(m.run(Opcode::RET)); assert(m.pc == 302);
    assert(m.run(Opcode::RET)); assert(m.pc == 11);
    assert(m.sp == ToyMachine::kBase);
    std::cout << "testCallRetKeepStackAndPcInSync passed\n";
}

void testCallOverflowFaultsBeforeJumping() {
    ToyMachine m;
    for (int i = 0; i < 4; ++i) { m.pc = 1000 + i; assert(m.run(Opcode::CALL, 2000)); }
    assert(m.sp == ToyMachine::kLimit);                 // stack exactly full

    m.pc = 7;
    assert(!m.run(Opcode::CALL, 5000));                 // 5th call must fault
    assert(m.fault == ExceptionCode::STACK_OVERFLOW);
    assert(m.pc != 5000);                               // never jumped to the target
    assert(m.mem[m.sp] == 0);                           // and nothing was written out of bounds
    std::cout << "testCallOverflowFaultsBeforeJumping passed\n";
}

void testRetUnderflowFaultsBeforeJumping() {
    ToyMachine m;                                       // empty stack
    m.pc = 55;
    assert(!m.run(Opcode::RET));
    assert(m.fault == ExceptionCode::STACK_UNDERFLOW);
    assert(m.pc == 55);                                 // PC untouched
    assert(m.sp == ToyMachine::kBase);                  // SP untouched (no sp_inc past the base)
    std::cout << "testRetUnderflowFaultsBeforeJumping passed\n";
}

void testBeqTaken() {
    ControlUnit cu;
    Flags z = flagsWithZero(true);

    ControlWord w = cu.step(Opcode::BEQ, z);            // decision row
    assert(!w.pc_write && !w.pc_inc);                   // no datapath action yet
    assert(!cu.routineDone());
    assert(cu.microPC() == 34);                         // branched to the taken row

    w = cu.step(Opcode::BEQ, z);                        // taken row
    assert(w.pc_write && !w.pc_inc);
    assert(cu.routineDone());
    std::cout << "testBeqTaken passed\n";
}

void testBeqNotTaken() {
    ControlUnit cu;
    Flags nz = flagsWithZero(false);

    ControlWord w = cu.step(Opcode::BEQ, nz);
    assert(!w.pc_write && !w.pc_inc);
    assert(cu.microPC() == 33);                         // fell through to the not-taken row

    w = cu.step(Opcode::BEQ, nz);
    assert(w.pc_inc && !w.pc_write);
    assert(cu.routineDone());
    std::cout << "testBeqNotTaken passed\n";
}

void testBackToBackInstructions() {
    ControlUnit cu;
    Flags f;
    // Finish an ADD, then ask for a JMP without an explicit reset().
    cu.step(Opcode::ADD, f);
    cu.step(Opcode::ADD, f);
    cu.step(Opcode::ADD, f);
    assert(cu.routineDone());

    ControlWord w = cu.step(Opcode::JMP, f);
    assert(w.pc_write);
    assert(cu.routineDone());

    cu.reset();
    assert(!cu.routineDone() && cu.microPC() == 0);
    std::cout << "testBackToBackInstructions passed\n";
}

void testIllegalOpcodeHalts() {
    ControlUnit cu;
    Flags f;
    ControlWord w = cu.step(static_cast<Opcode>(0x55), f);
    assert(w.halt);
    assert(cu.routineDone());
    std::cout << "testIllegalOpcodeHalts passed\n";
}

void testControlMemoryIsWellFormed() {
    ControlUnit cu;
    const auto& cm = cu.controlMemory();
    assert(cm.size() == 45);
    assert(cm.size() <= kMaxControlMemoryRows);         // must fit the 8-bit microPC
    for (const MicroOp& r : cm) {
        assert(r.next == kMicroEnd || (r.next >= 0 && r.next < static_cast<int32_t>(cm.size())));
        if (r.cond != BranchCond::None) {
            assert(r.branch_target >= 0 && r.branch_target < static_cast<int32_t>(cm.size()));
        }
        // Mutually exclusive signal pairs.
        assert(!(r.ctl.mem_read && r.ctl.mem_write));
        assert(!(r.ctl.pc_inc && r.ctl.pc_write));
        assert(!(r.ctl.sp_dec && r.ctl.sp_inc));
        // Stack-synchronisation signals only make sense together with what they qualify.
        assert(!r.ctl.mem_addr_sp || r.ctl.mem_read || r.ctl.mem_write);
        assert(!r.ctl.mem_data_pc || r.ctl.mem_write);
        assert(!r.ctl.pc_src_mem  || r.ctl.pc_write);
        assert(!r.ctl.stack_check || r.ctl.mem_addr_sp);
    }
    std::cout << "testControlMemoryIsWellFormed passed\n";
}

void testPackHorizontal() {
    ControlUnit cu;
    const auto& cm = cu.controlMemory();

    // MOV execute row (addr 36): alu_op=MOV(0x13) needs the 5th alu_op bit (bit 20).
    assert(((ControlUnit::packHorizontal(cm[36]) >> 16) & 0x1F) == static_cast<uint64_t>(Opcode::MOV));

    // ADD execute row (addr 2): alu_enable(0) | flag_update(10) | alu_op=ADD(1)<<16 | next=3<<24 | taken=END(0xFF)<<32
    const uint64_t expect_add_exec =
        (1ull << 0) | (1ull << 10) | (1ull << 16) | (3ull << 24) | (0xFFull << 32);
    assert(ControlUnit::packHorizontal(cm[2]) == expect_add_exec);

    // BEQ decision row (addr 32): cond=Zero(1)<<21 | next=33<<24 | taken=34<<32
    const uint64_t expect_beq_test =
        (1ull << 21) | (33ull << 24) | (34ull << 32);   // no datapath signals
    assert(ControlUnit::packHorizontal(cm[32]) == expect_beq_test);

    // CALL write row (addr 40): mem_write(2) | mem_addr_sp(12) | mem_data_pc(13) | stack_check(15) | next=41<<24
    const uint64_t expect_call_write =
        (1ull << 2) | (1ull << 12) | (1ull << 13) | (1ull << 15) | (41ull << 24) | (0xFFull << 32);
    assert(ControlUnit::packHorizontal(cm[40]) == expect_call_write);

    // No packed word may use more than 40 bits.
    for (const MicroOp& r : cm) {
        assert((ControlUnit::packHorizontal(r) >> 40) == 0);
    }
    std::cout << "testPackHorizontal passed\n";
}

void testVerticalIsPlaceholder() {
    // Vertical encoding is a Nov 10 deliverable; until then it must fail safe (halt), not crash.
    ControlUnit cu(MicrocodeEncoding::Vertical);
    Flags f;
    ControlWord w = cu.step(Opcode::ADD, f);
    assert(w.halt);
    std::cout << "testVerticalIsPlaceholder passed\n";
}

void printControlMemoryDemo() {
    ControlUnit cu;
    std::cout << "\n--- control memory (horizontal) ---\n"
              << cu.dumpControlMemory()
              << "-----------------------------------\n\n";
}

int main() {
    testEntryAddresses();
    testAddRoutine();
    testEveryAluOpCarriesItsOwnFunction();
    testLoadRoutine();
    testStoreRoutine();
    testJmpHaltNop();
    testMovRoutine();
    testCallRoutine();
    testRetRoutine();
    testCallRetKeepStackAndPcInSync();
    testCallOverflowFaultsBeforeJumping();
    testRetUnderflowFaultsBeforeJumping();
    testBeqTaken();
    testBeqNotTaken();
    testBackToBackInstructions();
    testIllegalOpcodeHalts();
    testControlMemoryIsWellFormed();
    testPackHorizontal();
    testVerticalIsPlaceholder();
    printControlMemoryDemo();
    std::cout << "All control unit tests passed.\n";
    return 0;
}
