#pragma once

#include "isa201.h"

namespace risc201 {

    class Alu {
    public:
        struct Result {
            int32_t value = 0;
            Flags   flags;
        };

        // TODO: add, sub, mul, div (Booth's / restoring division), AND/OR/XOR/NOT, shift/rotate. Each op should set Z/N/C/V per PRD §2.
        Result execute(Opcode op, int32_t lhs, int32_t rhs);
    };

}


