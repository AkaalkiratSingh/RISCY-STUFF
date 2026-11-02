#include "alu/alu.h"
#include <cstdint>
#include <climits>

namespace risc201 {

namespace {
constexpr uint32_t SIGN_BIT = 0x80000000u;
}

// ============================================================
//  ARITHMETIC CIRCUITS
// ============================================================

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

uint32_t Alu::carryLookaheadAdder(uint32_t a, uint32_t b, bool carry_in, bool& carry_out) {
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

int64_t Alu::multiply(int32_t a, int32_t b) {
    int64_t product = 0;
    int64_t mcand = static_cast<int64_t>(a);
    uint32_t multiplier = static_cast<uint32_t>(b);
    int q_prev = 0;

    for (int i = 0; i < 16; ++i) {
        int y_i        = multiplier & 1;
        int y_i_plus_1 = (multiplier >> 1) & 1;
        int y_i_minus_1 = q_prev;

        int code = (y_i_plus_1 << 2) | (y_i << 1) | y_i_minus_1;
        int64_t shifted_mcand = mcand << (2 * i);

        switch (code) {
            case 1: case 2: product += shifted_mcand; break;
            case 3:         product += (shifted_mcand << 1); break;
            case 4:         product -= (shifted_mcand << 1); break;
            case 5: case 6: product -= shifted_mcand; break;
            default:        break;
        }

        q_prev = y_i_plus_1;
        multiplier >>= 2;
    }
    return product;
}

// ============================================================
//  SOFT-FLOAT (IEEE 754 Single Precision)
// ============================================================

uint32_t Alu::packFloat(uint32_t sign, int32_t exp, uint32_t mantissa) {
    if (exp >= 255) return (sign << 31) | 0x7F800000; // Infinity
    if (exp <= 0)   return (sign << 31);              // Flush to zero
    uint32_t frac = mantissa & 0x007FFFFF;
    return (sign << 31) | ((static_cast<uint32_t>(exp) & 0xFF) << 23) | frac;
}

uint32_t Alu::floatAdd(uint32_t a, uint32_t b) {
    uint32_t sign_a = (a >> 31) & 1, sign_b = (b >> 31) & 1;
    int32_t  exp_a  = (a >> 23) & 0xFF, exp_b  = (b >> 23) & 0xFF;
    uint32_t mant_a = (a & 0x007FFFFF) | 0x00800000;
    uint32_t mant_b = (b & 0x007FFFFF) | 0x00800000;

    if (exp_a == 0) return b;
    if (exp_b == 0) return a;

    if (exp_a > exp_b) {
        mant_b >>= (exp_a - exp_b);
        exp_b = exp_a;
    } else if (exp_b > exp_a) {
        mant_a >>= (exp_b - exp_a);
        exp_a = exp_b;
    }

    uint32_t result_mant, result_sign;
    bool carry_out;

    if (sign_a == sign_b) {
        result_mant = carryLookaheadAdder(mant_a, mant_b, false, carry_out);
        result_sign = sign_a;
        if (carry_out) {
            result_mant >>= 1;
            exp_a++;
        }
    } else {
        if (mant_a > mant_b) {
            result_mant = carryLookaheadAdder(mant_a, ~mant_b, true, carry_out);
            result_sign = sign_a;
        } else if (mant_b > mant_a) {
            result_mant = carryLookaheadAdder(mant_b, ~mant_a, true, carry_out);
            result_sign = sign_b;
        } else {
            return 0;
        }
    }

    if (result_mant == 0) return 0;
    while ((result_mant & 0x00800000) == 0 && exp_a > 0) {
        result_mant <<= 1;
        exp_a--;
    }
    return packFloat(result_sign, exp_a, result_mant);
}

uint32_t Alu::floatMul(uint32_t a, uint32_t b) {
    uint32_t sign_a = (a >> 31) & 1, sign_b = (b >> 31) & 1;
    int32_t  exp_a  = (a >> 23) & 0xFF, exp_b  = (b >> 23) & 0xFF;
    uint32_t mant_a = (a & 0x007FFFFF) | 0x00800000;
    uint32_t mant_b = (b & 0x007FFFFF) | 0x00800000;

    if (exp_a == 0 || exp_b == 0) return 0;

    uint32_t result_sign = sign_a ^ sign_b;
    int32_t  result_exp  = exp_a + exp_b - 127;

    int64_t prod = multiply(mant_a, mant_b);
    uint64_t u_prod = static_cast<uint64_t>(prod);

    if (u_prod & 0x0000800000000000) {
        u_prod >>= 24;
        result_exp++;
    } else {
        u_prod >>= 23;
    }
    return packFloat(result_sign, result_exp, static_cast<uint32_t>(u_prod));
}

// Newton-Raphson Reciprocal using Q30 Fixed-Point math
uint32_t Alu::reciprocalNR(uint32_t D_fp) {
    // D_fp is in Q31 format (1.0 = 0x80000000)
    // We use Q30 to prevent overflow during multiplication (1.0 = 0x40000000)
    uint32_t D_q30 = D_fp >> 1;
    
    // Initial guess: x0 = 1.5 - 0.5 * D
    // 1.5 in Q30 is 0x60000000
    uint32_t x = 0x60000000 - (D_q30 >> 1);
    
    // 5 iterations doubles precision each time (1 -> 2 -> 4 -> 8 -> 16 -> 32)
    // We need 24 bits of precision, so 5 iterations is perfect.
    for (int i = 0; i < 5; ++i) {
        // prod = D * x (Q30 * Q30 = Q60, shift right 30 to get Q30)
        uint64_t prod = (uint64_t)D_q30 * x;
        prod >>= 30;
        
        // err = 2.0 - prod (2.0 in Q30 is 0x80000000)
        uint32_t err = 0x80000000 - (uint32_t)prod;
        
        // x = x * err (Q30 * Q30 = Q60, shift right 30 to get Q30)
        x = (uint32_t)(((uint64_t)x * err) >> 30);
    }
    return x << 1; // Convert back to Q31
}

uint32_t Alu::floatDiv(uint32_t a, uint32_t b) {
    uint32_t sign_a = (a >> 31) & 1, sign_b = (b >> 31) & 1;
    int32_t  exp_a  = (a >> 23) & 0xFF, exp_b  = (b >> 23) & 0xFF;
    uint32_t mant_a = (a & 0x007FFFFF) | 0x00800000;
    uint32_t mant_b = (b & 0x007FFFFF) | 0x00800000;

    if (exp_b == 0) return 0x7F800000; // Divide by zero -> Infinity
    if (exp_a == 0) return 0;          // 0 / anything -> 0

    uint32_t result_sign = sign_a ^ sign_b;
    int32_t  result_exp  = exp_a - exp_b + 127;

    // Convert mantissas to Q31 fixed-point
    uint32_t D_fp = mant_b << 8;
    uint32_t N_fp = mant_a << 8;

    // Compute 1/D using Newton-Raphson
    uint32_t inv_D_fp = reciprocalNR(D_fp);

    // Multiply N * (1/D) -> Q31 * Q31 = Q62, shift right 31
    uint64_t q_fp_64 = (uint64_t)N_fp * inv_D_fp;
    
       uint32_t Q_fp = (uint32_t)(q_fp_64 >> 31);
    
    // Normalize: if the quotient is less than 1.0 (bit 31 is 0),
    // shift left and decrement the exponent.
    if ((Q_fp & 0x80000000) == 0) {
        Q_fp <<= 1;
        result_exp--;
    }



    uint32_t result_mant = Q_fp >> 8; // Shift back to 24-bit mantissa
    
    return packFloat(result_sign, result_exp, result_mant);
}
    Alu::Result Alu::floatCmp(uint32_t a, uint32_t b) {
        Result r{};

        // Handle +0.0 and -0.0 (they are equal in value)
        if ((a & 0x7FFFFFFF) == 0 && (b & 0x7FFFFFFF) == 0) {
            r.flags.zero = true;
            r.flags.equal = true;
            r.flags.greaterThan = false;
            return r;
        }

        uint32_t sign_a = (a >> 31) & 1;
        uint32_t sign_b = (b >> 31) & 1;

        if (sign_a != sign_b) {
            // Different signs: positive is always greater than negative
            r.flags.greaterThan = (sign_a == 0);
            r.flags.zero = false;
            r.flags.equal = false;
        } else {
            // Same signs: Compare the raw bit patterns as unsigned integers
            if (sign_a == 0) {
                // Both positive: normal unsigned comparison
                r.flags.greaterThan = (a > b);
                r.flags.equal = (a == b);
            } else {
                // Both negative: inverted comparison (larger bits = smaller value)
                r.flags.greaterThan = (a < b);
                r.flags.equal = (a == b);
            }
            r.flags.zero = r.flags.equal;
        }

        return r;
    }
// ============================================================
//  EXECUTE
// ============================================================

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
        case Opcode::SUB: 
        case Opcode::CMP: {
            res = carryLookaheadAdder(a, ~b, true, carry_out);
            r.flags.carry    = carry_out;
            r.flags.overflow = ((a ^ b) & (a ^ res) & SIGN_BIT) != 0;
            break;
        }
        case Opcode::MUL: {
            int64_t prod = multiply(lhs, rhs);
            r.value64 = prod;
            r.value = static_cast<int32_t>(prod);
            break;
        }
        case Opcode::DIV:
        case Opcode::REM: {
            if (rhs == 0) { res = 0; break; }
            if (lhs == INT32_MIN && rhs == -1) {
                res = (op == Opcode::DIV) ? static_cast<uint32_t>(INT32_MIN) : 0;
                break;
            }
            bool neg_lhs = (lhs < 0), neg_rhs = (rhs < 0);
            bool neg_quotient = neg_lhs ^ neg_rhs;
            uint32_t u_lhs = neg_lhs ? (0u - static_cast<uint32_t>(lhs)) : static_cast<uint32_t>(lhs);
            uint32_t u_rhs = neg_rhs ? (0u - static_cast<uint32_t>(rhs)) : static_cast<uint32_t>(rhs);
            
            uint32_t quotient = 0, remainder = 0;
            for (int i = 31; i >= 0; --i) {
                remainder = (remainder << 1) | ((u_lhs >> i) & 1);
                quotient = quotient << 1;
                if (remainder >= u_rhs) {
                    bool c_out_div;
                    remainder = carryLookaheadAdder(remainder, ~u_rhs, true, c_out_div);
                    quotient |= 1;
                }
            }
            if (op == Opcode::DIV) res = neg_quotient ? (0u - quotient) : quotient;
            else                   res = neg_lhs ? (0u - remainder) : remainder;
            break;
        }

        // --- FLOATING POINT ---
        case Opcode::FADD: 
            r.value = static_cast<int32_t>(floatAdd(a, b));
            return r;
        case Opcode::FSUB: 
            r.value = static_cast<int32_t>(floatAdd(a, b ^ 0x80000000u));
            return r;
        case Opcode::FMUL: 
            r.value = static_cast<int32_t>(floatMul(a, b));
            return r;
        case Opcode::FDIV: 
            r.value = static_cast<int32_t>(floatDiv(a, b));
            return r;
        case Opcode::FCMP: 
            return floatCmp(a, b);
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
        default: res = 0; break;
    }

    if (op != Opcode::MUL) {
        r.value = static_cast<int32_t>(res);
    }

    r.flags.zero     = (r.value == 0);
    r.flags.negative = (r.value & SIGN_BIT) != 0;
    r.flags.equal    = r.flags.zero;
    r.flags.greaterThan = (!r.flags.zero) && (r.flags.negative == r.flags.overflow);

    return r;
}

} // namespace risc201