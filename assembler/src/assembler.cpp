#include "assembler/assembler.h"

#include <sstream>
#include <cctype>
#include <fstream>
#include <iomanip>

namespace risc201 {

    namespace {

        constexpr int32_t kImm18Min = -(1 << 17);
        constexpr int32_t kImm18Max = (1 << 17) - 1;
        constexpr int32_t kOffset27Min = -(1 << 26);
        constexpr int32_t kOffset27Max = (1 << 26) - 1;

        std::string stripCommentandTrim(const std::string raw) {
            std::string s = raw;
            auto comment_start = s.find_first_of(";#");
            if (comment_start != std::string::npos) s = s.substr(0, comment_start);

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
            
            for (auto& c : op_out) c = static_cast<char>(std::toupper(c));

            if (sp == std::string::npos) return;

            std::string rest = line.substr(sp + 1);
            std::stringstream ss(rest);
            std::string tok;

            while (std::getline(ss, tok, ',')) {
                size_t start = tok.find_first_not_of(" \t");
                size_t end = tok.find_last_not_of(" \t");
                if (start != std::string::npos) operands_out.push_back(tok.substr(start, end - start + 1));
            }

            return;
        }

        bool parseRegister(const std::string& token, Register& reg_out) {
            if (token.size() < 2) return false;
            char first = static_cast<char>(std::toupper(token[0]));
            if (first != 'R') return false;

            std::string digits = token.substr(1);
            if (digits.empty()) return false;
            for (auto digit: digits) {
                if (!isdigit(digit)) return false;
            }

            int value;
            try {
                value = std::stoi(digits);
            }
            catch (...) {
                return false;
            }
            if (value < 0 || value > 15) return false;

            reg_out = static_cast<Register>(value);

            return true;
        }

        bool parseImmediate(const std::string& token, int32_t& value_out) {
            if (token.empty()) return false;

            bool negative = false;
            std::string s = token;
            if (s[0] == '-') {
                negative = true;
                s = s.substr(1);
            }
            if (s.empty()) return false;

            int base = 10;
            if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
                base = 16;
                s = s.substr(2);
            }

            try {
                size_t pos;
                long parsed = std::stol(s, &pos, base);
                if (pos != s.size()) return false;
                if (negative) value_out = static_cast<int32_t>(-parsed);
                else value_out = static_cast<int32_t>(parsed);
                return true;
            }
            catch (...) {
                return false;
            }
        }

        bool parseMemOperand(const std::string& token, int32_t& imm_out, Register& reg_out) {
            auto open = token.find('[');
            auto close = token.find(']');
            if (open == std::string::npos || close == std::string::npos || close < open) return false;
            if (close != token.size() - 1) return false;

            std::string imm_part = token.substr(0, open);
            std::string reg_part = token.substr(open + 1, close - open - 1);

            if (!parseImmediate(imm_part, imm_out)) return false;
            if (!parseRegister(reg_part, reg_out)) return false;

            return true;
        }

        bool inRange(int32_t value, int32_t lo, int32_t hi) {
            return (value >= lo && value <= hi);
        }

        int32_t truncateToBits(int32_t value, int bits) {
            uint32_t mask;
            if (bits >= 32) mask = 0xFFFFFFFFu;
            else mask = (1u << bits) - 1u;
            uint32_t truncated = static_cast<uint32_t>(value) & mask;

            uint32_t sign_bit = 1u << (bits - 1);
            if (truncated & sign_bit) {
                truncated |= ~mask;
            }
            return static_cast<int32_t>(truncated);
        }

        bool assignNextRegister(Instruction& out, size_t reg_slot_idx, Register r) {
            switch (reg_slot_idx) {
                case 0: out.rd = r; return true;
                case 1: out.rs1 = r; return true;
                case 2: out.rs2 = r; return true;
                default: return false;
            }
        }

    }

    void Assembler::addError(int line_num, std::string msg) {
        errors_.push_back(AssemblyError{line_num, std::move(msg)});
    }

    void Assembler::addWarning(int line_num, std::string msg) {
        warnings_.push_back(AssemblyError{line_num, std::move(msg)});
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

       if (out.operands.size() != valid->second.operand_kinds.size()) addError(line_num, "'" + out.op + "' expects " + std::to_string(valid->second.operand_kinds.size()) + " operand(s), got " + std::to_string(out.operands.size()));

       out.emits_word = true;

       return true;
    }

    bool Assembler::resolveInstruction(const ParsedLine& pl, const OpInfo& info, Instruction& out) {
        if (pl.operands.size() != info.operand_kinds.size()) {
            return false;
        }

        out.opcode = info.op;
        out.format = info.format;
        out.I = false;
        out.rd = Register::R0;
        out.rs1 = Register::R0;
        out.rs2 = Register::R0;
        out.imm = 0;

        size_t reg_slot_idx = 0;
        bool have_imm_or_label = false;
        std::string imm_token;

        for (size_t i = 0; i < info.operand_kinds.size(); ++i) {
            const std::string& token = pl.operands[i];
            OperandKind kind = info.operand_kinds[i];

            switch (kind) {
                case OperandKind::REG: {
                    Register r;
                    if (!parseRegister(token, r)) {
                        addError(pl.line_number, "invalid register '" + token + "'");
                        return false;
                    }
                    if (!assignNextRegister(out, reg_slot_idx, r)) {
                        addError(pl.line_number, "internal error: too many register operands for '" + pl.op + "'");
                        return false;
                    }
                    ++reg_slot_idx;

                    break;
                }

                case OperandKind::IMM: {
                    int32_t v;
                    if (!parseImmediate(token, v)) {
                        addError(pl.line_number, "invalid immediate '" + token + "'");
                        return false;
                    }
                    out.imm = v;
                    out.I = true;
                    have_imm_or_label = true;
                    imm_token = token;

                    break;
                }

                case OperandKind::REG_OR_IMM: {
                    Register r;
                    int32_t v;
                    if (parseRegister(token, r)) {
                        if (!assignNextRegister(out, reg_slot_idx, r)) {
                            addError(pl.line_number, "internal error: too many register operands for '" + pl.op + "'");
                            return false;
                        }
                        ++reg_slot_idx;
                        out.I = false;
                    }
                    else if (parseImmediate(token, v)) {
                        out.imm = v;
                        out.I = true;
                        have_imm_or_label = true;
                        imm_token = token;
                    }
                    else {
                        addError(pl.line_number, "'" + token + "' is not a valid register or immediate");
                        return false;
                    }

                    break;
                }

                case OperandKind::MEM: {
                    int32_t mem_imm;
                    Register base_reg;
                    if (!parseMemOperand(token, mem_imm, base_reg)) {
                        addError(pl.line_number, "malformed memory operand '" + token + "' (expected imm[reg])");
                        return false;
                    }
                    out.rs1 = base_reg;
                    out.imm = mem_imm;
                    out.I = true;
                    have_imm_or_label = true;
                    imm_token = token;

                    break;
                }

                case OperandKind::LABEL: {
                    auto symbol = symbol_table_.find(token);
                    if (symbol == symbol_table_.end()) {
                        addError(pl.line_number, "undefined label '" + token + "'");
                        return false;
                    }
                    int32_t target = static_cast<int32_t>(symbol->second);
                    int32_t current_pc = static_cast<int32_t>(pl.address);
                    out.imm = target - (current_pc + 4);
                    have_imm_or_label = true;
                    imm_token = token + " (resolved offset " + std::to_string(out.imm) + ")";;

                    break;
                }
            }
        }

        if (have_imm_or_label) {
            int no_of_bits;
            int32_t lo;
            int32_t hi;
            if (info.format == InstrFormat::J_TYPE) {
                no_of_bits = 27;
                lo = kOffset27Min;
                hi = kOffset27Max;
            }
            else {
                no_of_bits = 18;
                lo = kImm18Min;
                hi = kImm18Max;
            }

            if (!inRange(out.imm, lo, hi)) {
                int32_t truncated = truncateToBits(out.imm, no_of_bits);
                addWarning(pl.line_number, "value " + imm_token + " out of range for " + std::to_string(no_of_bits) + "-bit field, truncated to " + std::to_string(truncated));
                out.imm = truncated;
            }
        }

        return true;
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
        warnings_.clear();
        std::vector<Instruction> program;

        for (const auto& pl : parsed_lines_) {
            if (!pl.emits_word) continue;

            auto valid = opTable().find(pl.op);
            if (valid == opTable().end()) {
                addError(pl.line_number, "internal error: '" + pl.op + "' not found in op table during Pass 2");
                continue;
            }

            Instruction instr;
            if (resolveInstruction(pl, valid->second, instr)) {
                program.push_back(instr);
            }
        }

        if (errors_.empty()) return program;
        else return std::vector<Instruction>{};
    }

    uint32_t Assembler::encodeInstruction(const Instruction& instr) {
        uint32_t word = 0;

        uint32_t opcode_bits = static_cast<uint32_t>(instr.opcode) & 0x1F;
        word |= opcode_bits << 27;

        if (instr.format == InstrFormat::J_TYPE) {
            uint32_t offset_bits = static_cast<uint32_t>(instr.imm) & 0x7FFFFFF;
            word |= offset_bits;
        } else {
            uint32_t i_bit;
            if (instr.I) i_bit = 1;
            else i_bit = 0;
            word |= i_bit << 26;

            uint32_t rd_bits = static_cast<uint32_t>(instr.rd) & 0xF;
            word |= rd_bits << 22;

            uint32_t rs1_bits = static_cast<uint32_t>(instr.rs1) & 0xF;
            word |= rs1_bits << 18;

            if (instr.I) {
                uint32_t imm_bits = static_cast<uint32_t>(instr.imm) & 0x3FFFF;
                word |= imm_bits;
            }
            else {
                uint32_t rs2_bits = static_cast<uint32_t>(instr.rs2) & 0xF;
                word |= rs2_bits << 14;
            }
        }

        return word;
    }

    bool Assembler::writeBinaryFile(const std::string& path, const std::vector<uint32_t>& words) {
        std::ofstream out(path, std::ios::binary);
        if (!out) return false;

        for (uint32_t word : words) {
            unsigned char bytes[4] = {
                static_cast<unsigned char>((word >> 24) & 0xFF),
                static_cast<unsigned char>((word >> 16) & 0xFF),
                static_cast<unsigned char>((word >> 8) & 0xFF),
                static_cast<unsigned char>(word & 0xFF),
            };
            out.write(reinterpret_cast<const char*>(bytes), 4);
            if (!out) return false;
        }

        return true;
    }

    bool Assembler::writeHexFile(const std::string& path, const std::vector<uint32_t>& words) {
        std::ofstream out(path);
        if (!out) return false;

        for (uint32_t word : words) {
            out << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << word << "\n";
            if (!out) return false;
        }
        
        return true;
    }

} // namespace risc201
