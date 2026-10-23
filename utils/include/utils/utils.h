#pragma once

#include "isa201.h"

namespace risc201 {
    // Mnemonic for an opcode; "?" when the value is not a known opcode.
    const char* opcodeName(Opcode op);
}
