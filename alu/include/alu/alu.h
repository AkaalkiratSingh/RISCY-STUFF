#pragma once

#include "isa201.h"

namespace risc201 {

    class Alu {
    public:
        struct Result {
            int32_t value  = 0;
            int64_t value64 = 0;   
            Flags   flags;
        };

        Result execute(Opcode op, int32_t lhs, int32_t rhs);

        // Radix-4 Booth's multiplication
        static int64_t multiply(int32_t lhs, int32_t rhs);

    private:
        // 4-bit Carry-Lookahead Adder block.
        static uint32_t cla4(uint32_t a, uint32_t b,
                             bool carry_in, bool& carry_out);

        // 32-bit Carry-Lookahead Adder (8 chained 4-bit CLA blocks).
        static uint32_t carryLookaheadAdder(uint32_t a, uint32_t b,
                                            bool carry_in, bool& carry_out);

        // --- Soft-Float Helpers ---
        static uint32_t packFloat(uint32_t sign, int32_t exp, uint32_t mantissa);
        static uint32_t floatAdd(uint32_t a, uint32_t b);
        static uint32_t floatMul(uint32_t a, uint32_t b);
        static uint32_t floatDiv(uint32_t a, uint32_t b);
        
        // Newton-Raphson reciprocal (1/D)
        static uint32_t reciprocalNR(uint32_t D_fp);
    };

} // namespace risc201