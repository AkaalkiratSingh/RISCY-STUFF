// Macro expansion for full-descending stack ops: PUSH, POP, PUSHM/POPM
// (multi-register), CALL/RET frame setup (PRD §5.1).

#pragma once

#include <string>
#include <vector>

namespace risc201 {

class Preprocessor {
public:
    std::vector<std::string> expand(const std::vector<std::string>& source_lines);
};

} // namespace risc201
