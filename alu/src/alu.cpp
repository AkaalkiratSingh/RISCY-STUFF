#include "alu/alu.h"

namespace risc201 {

namespace {
constexpr uint32_t SIGN_BIT = 0x80000000u;
}

// 4-bit Carry-Lookahead Adder block.
//   Generate  G = A & B
//   Propagate P = A ^ B
uint32_t Alu::cla4(uint32_t a, uint32_t b, bool carry_in, bool& carry_out) {
    uint32_t g = (a & b) & 0xFu;
    uint32_t p = (a ^ b) & 0xFu;

    bool c0 = carry_in;
    bool c1 = ((g >> 0) & 1) || (((p >> 0) & 1) && c0);
    bool c2 = ((g >> 1) & 1) || (((p >> 1) & 1) && ((g >> 0) & 1))
                              || (((p >> 1) & 1) && ((p >> 0) & 1) && c0);
    bool c3 = ((g >> 2) & 1) || (((p >> 2) & 1) && ((g >> 1) & 1))
                              || (((p >> 2) & 1) && ((p >> 1) & 1) && ((g >> 0) & 1))
                              || (((p >> 2) & 1) && ((p >> 1) & 1) && ((p >> 0) & 1) && c0);
    carry_out = ((g >> 3) & 1) || (((p >> 3) & 1) && ((g >> 2) & 1))
                                || (((p >> 3) & 1) && ((p >> 2) & 1) && ((g >> 1) & 1))
                                || (((p >> 3) & 1) && ((p >> 2) & 1) && ((p >> 1) & 1) && ((g >> 0) & 1))
                                || (((p >> 3) & 1) && ((p >> 2) & 1) && ((p >> 1) & 1) && ((p >> 0) & 1) && c0);

    uint32_t sum = 0;
    if ( ((p >> 0) & 1) ^ c0 ) sum |= 0x1;
    if ( ((p >> 1) & 1) ^ c1 ) sum |= 0x2;
    if ( ((p >> 2) & 1) ^ c2 ) sum |= 0x4;
    if ( ((p >> 3) & 1) ^ c3 ) sum |= 0x8;

    return sum;
}

// 32-bit CLA — chains eight 4-bit blocks.
uint32_t Alu::carryLookaheadAdder(uint32_t a, uint32_t b,
                                  bool carry_in, bool& carry_out) {
    uint32_t sum = 0;
    bool carry = carry_in;
    for (int i = 0; i < 32; i += 4) {
        bool c_out;
        uint32_t s = cla4((a >> i) & 0xF, (b >> i) & 0xF, carry, c_out);
        sum |= (s << i);
        carry = c_out;
    }
    carry_out = carry;
    return sum;
}

Alu::Result Alu::execute(Opcode op, int32_t lhs, int32_t rhs) {
    Result r{};

    uint32_t a = static_cast<uint32_t>(lhs);
    uint32_t b = static_cast<uint32_t>(rhs);
    uint32_t res = 0;
    bool carry_out = false;

    switch (op) {
        case Opcode::ADD: {
            res = carryLookaheadAdder(a, b, false, carry_out);
            r.flags.carry    = carry_out;
            r.flags.overflow = ((a ^ res) & (b ^ res) & SIGN_BIT) != 0;
            break;
        }

        case Opcode::SUB: {
            // A - B = A + (~B) + 1
            res = carryLookaheadAdder(a, ~b, true, carry_out);
            r.flags.carry    = carry_out;
            r.flags.overflow = ((a ^ b) & (a ^ res) & SIGN_BIT) != 0;
            break;
        }

        case Opcode::CMP: {
            res = carryLookaheadAdder(a, ~b, true, carry_out);
            r.flags.carry    = carry_out;
            r.flags.overflow = ((a ^ b) & (a ^ res) & SIGN_BIT) != 0;
            break;
        }

        case Opcode::AND: res = a & b; break;
        case Opcode::OR:  res = a | b; break;
        case Opcode::XOR: res = a ^ b; break;
        case Opcode::NOT: res = ~a;    break;

        case Opcode::SHL: {
            uint32_t sh = b & 0x1Fu;
            if (sh == 0) { res = a; }
            else {
                res = a << sh;
                r.flags.carry = ((a >> (32u - sh)) & 1u) != 0;
            }
            break;
        }

        case Opcode::SHR: {
            uint32_t sh = b & 0x1Fu;
            if (sh == 0) { res = a; }
            else {
                res = a >> sh;
                r.flags.carry = ((a >> (sh - 1u)) & 1u) != 0;
            }
            break;
        }

        case Opcode::SAR: {
            uint32_t sh = b & 0x1Fu;
            if (sh == 0) { res = a; }
            else {
                if ((a & SIGN_BIT) != 0) {
                    res = (a >> sh) | (~0u << (32u - sh));
                } else {
                    res = a >> sh;
                }
                r.flags.carry = ((a >> (sh - 1u)) & 1u) != 0;
            }
            break;
        }

        default:
            res = 0;
            break;
    }

    r.value = static_cast<int32_t>(res);
    r.flags.zero     = (res == 0);
    r.flags.negative = (res & SIGN_BIT) != 0;
    r.flags.equal       = r.flags.zero;
    r.flags.greaterThan = (!r.flags.zero) && (r.flags.negative == r.flags.overflow);

    return r;
}

} 