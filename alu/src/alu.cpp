#include "alu.h"

namespace risc201 {

namespace {
constexpr uint32_t SIGN_BIT = 0x80000000u;
}

Alu::Result Alu::execute(Opcode op, int32_t lhs, int32_t rhs) {
    Result r{};
    
    uint32_t a = static_cast<uint32_t>(lhs);
    uint32_t b = static_cast<uint32_t>(rhs);
    uint32_t res = 0;

    switch (op) {
        case Opcode::ADD: {
            uint64_t sum = static_cast<uint64_t>(a) + static_cast<uint64_t>(b);
            res = static_cast<uint32_t>(sum);
            r.flags.carry = sum > 0xFFFFFFFFu;
            r.flags.overflow = ((a ^ res) & (b ^ res) & SIGN_BIT) != 0;
            break;
        }
        case Opcode::SUB: {
            res = a - b;
            r.flags.carry = a >= b; // no borrow
            r.flags.overflow = ((a ^ b) & (a ^ res) & SIGN_BIT) != 0;
            break;
        }
        case Opcode::CMP: {
            // CMP is a subtraction that discards the result.
            // Only the flags matter for branching.
            res = a - b;
            r.flags.carry = a >= b; // no borrow
            r.flags.overflow = ((a ^ b) & (a ^ res) & SIGN_BIT) != 0;
            break;
        }
        case Opcode::AND:
            res = a & b;
            break;
        case Opcode::OR:
            res = a | b;
            break;
        case Opcode::XOR:
            res = a ^ b;
            break;
        case Opcode::NOT:
            res = ~a;
            break;
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

    // Set the result value
    r.value = static_cast<int32_t>(res);

    // --- Set the Standard Flags ---
    r.flags.zero = (res == 0);
    r.flags.negative = (res & SIGN_BIT) != 0;

    // --- Set the New Comparison Flags ---
    r.flags.equal = r.flags.zero;
    
    // For signed numbers, A > B if (A - B) > 0. 
    // This means Z=0 AND N=V (Negative equals Overflow).
    r.flags.greaterThan = (!r.flags.zero) && (r.flags.negative == r.flags.overflow);

    return r;
}

} // namespace risc201
