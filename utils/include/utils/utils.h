#pragma once

#include "isa201.h"

#include <cstdint>
#include <cstring>

namespace risc201 {
    // Mnemonic for an opcode; "?" when the value is not a known opcode.
    const char* opcodeName(Opcode op);

    uint32_t floatToBits(float value);
    float bitsToFloat(uint32_t bits);
}
