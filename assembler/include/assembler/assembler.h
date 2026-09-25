#pragma once

#include "isa201.h"
#include "op_table.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace risc201 {

    struct ParsedLine {
        int line_number = 0;
        std::string label;
        std::string op;
        std::vector<std::string> operands;
        uint32_t address = 0;
        bool emits_word = false;
    };

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
        uint32_t location_counter_ = 0;
        std::unordered_map<std::string, uint32_t> symbol_table_;
        std::vector<ParsedLine> parsed_lines_;
        std::vector<AssemblyError> errors_;

        bool parseLine(const std::string& raw, int line_num, ParsedLine& out);
        void addError(int line_num, std::string msg);
        // TODO(Divyansh, Sept 30 milestone): EBNF grammar-backed parser, line
        // tracking for error messages, label resolution.
    };

}

