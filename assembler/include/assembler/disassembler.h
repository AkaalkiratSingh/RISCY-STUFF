// Binary/hex word strings -> RISC201 assembly mnemonics. Must roundtrip with
// Assembler (assemble -> disassemble -> matches original semantics).

#pragma once

#include "isa201.h"
#include <string>
#include <vector>

namespace risc201 {

class Disassembler {
public:
    std::vector<std::string> disassemble(const std::vector<Instruction>& program);
};

} // namespace risc201
