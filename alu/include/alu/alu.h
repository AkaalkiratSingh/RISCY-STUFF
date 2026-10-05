#pragma once

#include "isa201.h"

namespace risc201 {

    class Alu {
    public:
        struct Result {
            int32_t value = 0;
            Flags   flags;
        };

        Result execute(Opcode op, int32_t lhs, int32_t rhs);

    private:
        // 4-bit Carry-Lookahead Adder block.
        static uint32_t cla4(uint32_t a, uint32_t b,
                             bool carry_in, bool& carry_out);

        // 32-bit Carry-Lookahead Adder (8 chained 4-bit CLA blocks).
        static uint32_t carryLookaheadAdder(uint32_t a, uint32_t b,
                                            bool carry_in, bool& carry_out);
    };

} 