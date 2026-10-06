#pragma once

#include "isa201.h"

#include <cstdint>

namespace risc201 {
    //   [31:27] opcode
    //   RI_TYPE: [26] I  [25:22] rd  [21:18] rs1
    //            I=1 -> [17:0]  imm (18-bit signed)
    //            I=0 -> [17:14] rs2
    //   J_TYPE : [26:0]  offset (27-bit signed, relative to PC+4, in bytes)
    Instruction decodeInstruction(uint32_t word);
}
