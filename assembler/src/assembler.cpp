#include "assembler/assembler.h"

namespace risc201 {

bool Assembler::assemblePass1(const std::vector<std::string>& /*source_lines*/) {
    // TODO(Divyansh): symbol table construction, label resolution, syntax
    // validation with line-numbered errors.
    return true;
}

std::vector<Instruction> Assembler::assemblePass2() {
    // TODO(Divyansh): resolve addresses, emit binary/hex.
    return {};
}

} // namespace risc201
