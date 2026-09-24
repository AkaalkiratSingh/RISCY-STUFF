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

    // ADD
    check(alu.execute(Opcode::ADD, 2, 3), 5, false, false, false, false);
    check(alu.execute(Opcode::ADD, 0x7FFFFFFF, 1), 
          static_cast<int32_t>(0x80000000), false, true, false, true);
    check(alu.execute(Opcode::ADD, -1, 1), 0, true, false, true, false);

    // SUB
    check(alu.execute(Opcode::SUB, 5, 3), 2, false, false, true, false);
    check(alu.execute(Opcode::SUB, 3, 5), -2, false, true, false, false);
    check(alu.execute(Opcode::SUB, static_cast<int32_t>(0x80000000), 1), 
          0x7FFFFFFF, false, false, true, true);

    // Logic
    check(alu.execute(Opcode::AND, 0xF0F0F0F0, 0x0FF00FF0), 
          static_cast<int32_t>(0x00F000F0), false, false, false, false);
    check(alu.execute(Opcode::XOR, -1, -1), 0, true, false, false, false);
    check(alu.execute(Opcode::NOT, 0), -1, false, true, false, false);

    std::cout << "All ALU tests passed!\n";
    return 0;
}
