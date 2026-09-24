#pragma once

#include "isa201.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace risc201 {

    struct AssemblyError {
        int line_number = 0;
        std::string message;
    };

    class Assembler {
    public:
        bool assemblePass1(const std::vector<std::string>& source_lines);

        std::vector<Instruction> assemblePass2();

        const std::unordered_map<std::string, uint32_t>& symbolTable() const {
            return symbol_table_;
        }
        const std::vector<AssemblyError>& errors() const { return errors_; }

    private:
        std::unordered_map<std::string, uint32_t> symbol_table_;
        std::vector<AssemblyError> errors_;
        // TODO(Divyansh, Sept 30 milestone): EBNF grammar-backed parser, line
        // tracking for error messages, label resolution.
    };

}

