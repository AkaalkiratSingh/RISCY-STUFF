#include "assembler/assembler.h"

#include <sstream>
#include <cctype>

namespace risc201 {

    namespace {

        std::string stripCommentandTrim(const std::string raw) {
            std::string s = raw;
            auto semicolon = s.find(';');
            if (semicolon != std::string::npos) s = s.substr(0, semicolon);

            size_t start = s.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) return "";
            size_t end = s.find_last_not_of(" \t\r\n");

            return s.substr(start, end - start + 1);
        }

        bool extractLabel(std::string& line, std::string& label_out) {
            auto colon = line.find(':');
            if (colon == std::string::npos) return false;

            std::string candidate = line.substr(0, colon);
            if (candidate.empty()) return false;
            if (candidate.find_first_of(" \t") != std::string::npos) return false;
            
            label_out = candidate;
            
            line = line.substr(colon + 1);
            size_t start = line.find_first_not_of(" \t");
            if (start == std::string::npos) line = "";
            else line = line.substr(start);

            return true;
        }

        void tokenizeInstruction(const std::string& line, std::string& op_out, std::vector<std::string>& operands_out) {
            if (line.empty()) return;

            size_t sp = line.find_first_of(" \t");
            if (sp == std::string::npos) op_out = line;
            else op_out = line.substr(0, sp);

            if (sp == std::string::npos) return;

            std::string rest = line.substr(sp + 1);
            std::stringstream ss(rest);
            std::string tok;

            while (std::getline(ss, tok, ',')) {
                size_t start = tok.find_first_not_of(" \t");
                size_t end = tok.find_last_not_of(" \t");
                if (start != std::string::npos) operands_out.push_back(tok.substr(start, end - start + 1));
            }
        }

    }

    bool Assembler::parseLine(const std::string& raw, int line_num, ParsedLine& out) {
        std::string line = stripCommentandTrim(raw);
        if (line.empty()) return false;

        std::string label;
        bool has_label = extractLabel(line, label);
        if (has_label) {
            if (symbol_table_.count(label)) addError(line_num, "duplicate label '" + label + "'");
            else symbol_table_[label] = location_counter_;
        }

        out.line_number = line_num;
        out.label = label;
        out.address = location_counter_;

       if (line.empty()) {
            out.emits_word = false;
            return true;
       }

       tokenizeInstruction(line, out.op, out.operands);

       auto valid = opTable().find(out.op);
       if (valid == opTable().end()) {
            addError(line_num, "unknown operation '" + out.op + "'");
            out.emits_word = false;
            return true;
       }

       if (out.operands.size() != valid->second.operand_count) addError(line_num, "'" + out.op + "' expects " + std::to_string(valid->second.operand_count) + " operand(s), got " + std::to_string(out.operands.size()));

       out.emits_word = true;

       return true;
    }

    void Assembler::addError(int line_num, std::string msg) {
        errors_.push_back(AssemblyError{line_num, std::move(msg)});
    }

    bool Assembler::assemblePass1(const std::vector<std::string>& source_lines) {
        symbol_table_.clear();
        errors_.clear();
        parsed_lines_.clear();
        location_counter_ = 0;

        for (size_t i = 0; i < source_lines.size(); ++i) {
            ParsedLine pl;

            if (parseLine(source_lines[i], static_cast<int>(i) + 1, pl)) {
                parsed_lines_.push_back(pl);
                if (pl.emits_word) location_counter_ += 4;
            }
        }

        return errors_.empty();
    }

    std::vector<Instruction> Assembler::assemblePass2() {
        // TODO(Divyansh): resolve addresses, emit binary/hex.
        return {};
    }

} // namespace risc201
