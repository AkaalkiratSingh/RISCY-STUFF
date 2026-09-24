#include "alu.h"
#include <cassert>
#include <iostream>

using namespace risc201;

static void check(const Alu::Result& r, int32_t value,
                  bool zero, bool negative, bool carry, bool overflow) {
    assert(r.value == value);
    assert(r.flags.zero == zero);
    assert(r.flags.negative == negative);
    assert(r.flags.carry == carry);
    assert(r.flags.overflow == overflow);
}

int main() {
    Alu alu;

    // --- Standard ALU Tests ---
    
    // ADD
    check(alu.execute(Opcode::ADD, 2, 3), 5, false, false, false, false);
    check(alu.execute(Opcode::ADD, 0x7FFFFFFF, 1), 
          static_cast<int32_t>(0x80000000), false, true, false, true);
    check(alu.execute(Opcode::ADD, -1, 1), 0, true, false, true, false);

    // SUB
    check(alu.execute(Opcode::SUB, 5, 3), 2, false, false, true, false);
    check(alu.execute(Opcode::SUB, 3, 5), -2, false, true, false, false);

    // Logic
    check(alu.execute(Opcode::AND, 0xF0F0F0F0, 0x0FF00FF0), 
          static_cast<int32_t>(0x00F000F0), false, false, false, false);
    check(alu.execute(Opcode::XOR, -1, -1), 0, true, false, false, false);
    check(alu.execute(Opcode::NOT, 0), -1, false, true, false, false);

    // Shifts
    check(alu.execute(Opcode::SHL, static_cast<int32_t>(0x80000000), 1), 
          0, true, false, true, false);
    check(alu.execute(Opcode::SAR, static_cast<int32_t>(0x80000000), 1), 
          static_cast<int32_t>(0xC0000000), false, true, false, false);


    // --- CMP and New Flags Tests ---

    // 5 > 3 (Greater Than)
    Alu::Result cmp1 = alu.execute(Opcode::CMP, 5, 3);
    assert(cmp1.flags.greaterThan == true);
    assert(cmp1.flags.equal == false);
    assert(cmp1.flags.zero == false);

    // 3 == 3 (Equal)
    Alu::Result cmp2 = alu.execute(Opcode::CMP, 3, 3);
    assert(cmp2.flags.greaterThan == false);
    assert(cmp2.flags.equal == true);
    assert(cmp2.flags.zero == true);

    // 2 < 5 (Less Than)
    Alu::Result cmp3 = alu.execute(Opcode::CMP, 2, 5);
    assert(cmp3.flags.greaterThan == false);
    assert(cmp3.flags.equal == false);
    assert(cmp3.flags.zero == false);

    // -1 > -5 (Signed comparison test)
    Alu::Result cmp4 = alu.execute(Opcode::CMP, -1, -5);
    assert(cmp4.flags.greaterThan == true);

    // -5 < -1 (Signed comparison test)
    Alu::Result cmp5 = alu.execute(Opcode::CMP, -5, -1);
    assert(cmp5.flags.greaterThan == false);

    std::cout << "All ALU tests passed!\n";
    return 0;
}
