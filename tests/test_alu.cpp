#include "alu/alu.h"
#include <cassert>
#include <iostream>
#include <cstring> // Required for memcpy
#include <cmath>   // Required for fabs

using namespace risc201;

// Helpers to convert float to uint32_t bits and back
uint32_t floatToBits(float f) {
    uint32_t b;
    std::memcpy(&b, &f, sizeof(b));
    return b;
}
float bitsToFloat(uint32_t b) {
    float f;
    std::memcpy(&f, &b, sizeof(f));
    return f;
}

// Helper to check if two floats are close enough (handles tiny rounding errors)
bool isClose(float a, float b, float epsilon = 0.0001f) {
    return std::fabs(a - b) < epsilon;
}

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

    // ==========================================
    // 1. INTEGER TESTS
    // ==========================================
    check(alu.execute(Opcode::ADD, 2, 3), 5, false, false, false, false);
    check(alu.execute(Opcode::SUB, 5, 3), 2, false, false, true, false);
    check(alu.execute(Opcode::CMP, 3, 3), 0, true, false, true, false); // 3-3=0
    check(alu.execute(Opcode::MUL, 7, 5), 35, false, false, false, false);
    
    // Integer Division
    assert(alu.execute(Opcode::DIV, 10, 3).value == 3);
    assert(alu.execute(Opcode::DIV, -10, 3).value == -3);
    assert(alu.execute(Opcode::REM, 10, 3).value == 1);
    assert(alu.execute(Opcode::REM, -10, 3).value == -1); // Remainder takes sign of dividend

    // ==========================================
    // 2. FLOATING POINT TESTS
    // ==========================================

    // FADD: 2.5 + 1.25 = 3.75
    float f_add = bitsToFloat(alu.execute(Opcode::FADD, floatToBits(2.5f), floatToBits(1.25f)).value);
    assert(isClose(f_add, 3.75f));

    // FSUB: 5.0 - 3.25 = 1.75
    float f_sub = bitsToFloat(alu.execute(Opcode::FSUB, floatToBits(5.0f), floatToBits(3.25f)).value);
    assert(isClose(f_sub, 1.75f));

    // FMUL: 3.0 * 4.0 = 12.0
    float f_mul = bitsToFloat(alu.execute(Opcode::FMUL, floatToBits(3.0f), floatToBits(4.0f)).value);
    assert(isClose(f_mul, 12.0f));

    // FDIV (Newton-Raphson): 10.0 / 4.0 = 2.5
    float f_div1 = bitsToFloat(alu.execute(Opcode::FDIV, floatToBits(10.0f), floatToBits(4.0f)).value);
    assert(isClose(f_div1, 2.5f));

    // FDIV (Newton-Raphson): 1.0 / 3.0 = 0.333333...
    float f_div2 = bitsToFloat(alu.execute(Opcode::FDIV, floatToBits(1.0f), floatToBits(3.0f)).value);
    assert(isClose(f_div2, 0.333333f, 0.00001f));

    // Negative FDIV: -15.0 / 3.0 = -5.0
    float f_div3 = bitsToFloat(alu.execute(Opcode::FDIV, floatToBits(-15.0f), floatToBits(3.0f)).value);
    assert(isClose(f_div3, -5.0f));

    std::cout << "All tests passed successfully!\n";
    std::cout << " - Integer ALU (ADD, SUB, MUL, DIV, REM) OK\n";
    std::cout << " - Float ALU (FADD, FSUB, FMUL, FDIV via Newton-Raphson) OK\n";
    
    return 0;
}