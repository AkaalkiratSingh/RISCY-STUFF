#include "assembler/disassembler.h"

#include <fstream>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace risc201 {

    namespace {

        std::string regName(Register r) {
            return "R" + std::to_string(static_cast<int>(r));
        }

    }

    bool Disassembler::decodeWord(uint32_t word, Instruction& out) {
        uint32_t opcode_bits = (word >> 27) & 0x1F;
        Opcode opcode = static_cast<Opcode>(opcode_bits);

        auto name_it = opcodeToMnemonic().find(opcode);
        if (name_it == opcodeToMnemonic().end()) return false;

        auto info_it = opTable().find(name_it->second);
        const OpInfo& info = info_it->second;

        out.opcode = opcode;
        out.format = info.format;
        out.rd = Register::R0;
        out.rs1 = Register::R0;
        out.rs2 = Register::R0;
        out.imm = 0;
        out.I = false;

        if (info.format == InstrFormat::J_TYPE) {
            uint32_t raw_offset = word & 0x7FFFFFF;
            bool negative = raw_offset & 0x4000000;
            if (negative) {
                raw_offset |= 0xF8000000;
            }
            out.imm = static_cast<int32_t>(raw_offset);
        }
        else {
            bool i_bit = (word >> 26) & 0x1;
            out.I = i_bit;

            uint32_t rd_bits = (word >> 22) & 0xF;
            out.rd = static_cast<Register>(rd_bits);

            uint32_t rs1_bits = (word >> 18) & 0xF;
            out.rs1 = static_cast<Register>(rs1_bits);

            if (i_bit) {
                uint32_t raw_imm = word & 0x3FFFF;
                bool negative = raw_imm & 0x20000;
                if (negative) {
                    raw_imm |= 0xFFFC0000;
                }
                out.imm = static_cast<int32_t>(raw_imm);
            } else {
                uint32_t rs2_bits = (word >> 14) & 0xF;
                out.rs2 = static_cast<Register>(rs2_bits);
            }
        }

        return true;
    }

    std::vector<Instruction> Disassembler::decodeProgram(const std::vector<uint32_t>& words) {
        decode_errors_.clear();
        literal_words_.clear();
        std::vector<Instruction> program;

        for (size_t i = 0; i < words.size(); ++i) {
            if (words[i] == kLiteralsMarker) {
                literal_words_.assign(words.begin() + i + 1, words.end());
                break;
            }

            Instruction instr;
            if (decodeWord(words[i], instr)) {
                program.push_back(instr);
            }
            else {
                decode_errors_.push_back({i, words[i]});
                instr.opcode = static_cast<Opcode>(0xFF);
                program.push_back(instr);
            }
        }

        return program;
    }

    std::string Disassembler::formatInstruction(const Instruction& instr) {
        auto name_it = opcodeToMnemonic().find(instr.opcode);
        if (name_it == opcodeToMnemonic().end()) {
            return "; unknown opcode";
        }
        const std::string& mnemonic = name_it->second;

        auto info_it = opTable().find(mnemonic);
        const OpInfo& info = info_it->second;

        std::vector<std::string> operand_strs;
        size_t reg_slot_idx = 0;

        for (OperandKind kind : info.operand_kinds) {
            switch (kind) {
                case OperandKind::REG: {
                    Register r;
                    if (reg_slot_idx == 0) r = instr.rd;
                    else if (reg_slot_idx == 1) r = instr.rs1;
                    else r = instr.rs2;
                    operand_strs.push_back(regName(r));
                    ++reg_slot_idx;

                    break;
                }
                case OperandKind::REG_OR_IMM: {
                    if (instr.I) {
                        operand_strs.push_back(std::to_string(instr.imm));
                    }
                    else {
                        Register r;
                        if (reg_slot_idx == 0) r = instr.rd;
                        else if (reg_slot_idx == 1) r = instr.rs1;
                        else r = instr.rs2;
                        operand_strs.push_back(regName(r));
                        ++reg_slot_idx;
                    }

                    break;
                }
                case OperandKind::IMM: {
                    operand_strs.push_back(std::to_string(instr.imm));

                    break;
                }
                case OperandKind::MEM: {
                    operand_strs.push_back(std::to_string(instr.imm) + "[" + regName(instr.rs1) + "]");

                    break;
                }
                case OperandKind::LABEL: {
                    operand_strs.push_back(std::to_string(instr.imm));

                    break;
                }
            }
        }

        std::string result = mnemonic;
        for (size_t i = 0; i < operand_strs.size(); ++i) {
            if (i == 0) result += " ";
            else result += ", ";
            result += operand_strs[i];
        }

        return result;
    }

    std::string Disassembler::formatInstructionWithLabels(const Instruction& instr, int32_t address, const std::unordered_map<int32_t, std::string>& label_map) {
        auto name_it = opcodeToMnemonic().find(instr.opcode);
        if (name_it == opcodeToMnemonic().end()) {
            return "; unknown opcode";
        }
        const std::string& mnemonic = name_it->second;

        auto info_it = opTable().find(mnemonic);
        const OpInfo& info = info_it->second;

        std::vector<std::string> operand_strs;
        size_t reg_slot_idx = 0;

        for (OperandKind kind : info.operand_kinds) {
            switch (kind) {
                case OperandKind::REG: {
                    Register r;
                    if (reg_slot_idx == 0) r = instr.rd;
                    else if (reg_slot_idx == 1) r = instr.rs1;
                    else r = instr.rs2;
                    operand_strs.push_back(regName(r));
                    ++reg_slot_idx;

                    break;
                }
                case OperandKind::REG_OR_IMM: {
                    if (instr.I) {
                        operand_strs.push_back(std::to_string(instr.imm));
                    }
                    else {
                        Register r;
                        if (reg_slot_idx == 0) r = instr.rd;
                        else if (reg_slot_idx == 1) r = instr.rs1;
                        else r = instr.rs2;
                        operand_strs.push_back(regName(r));
                        ++reg_slot_idx;
                    }

                    break;
                }
                case OperandKind::IMM: {
                    operand_strs.push_back(std::to_string(instr.imm));

                    break;
                }
                case OperandKind::MEM: {
                    operand_strs.push_back(std::to_string(instr.imm) + "[" + regName(instr.rs1) + "]");

                    break;
                }
                case OperandKind::LABEL: {
                    int32_t target = address + 4 + instr.imm;
                    auto label_it = label_map.find(target);
                    if (label_it != label_map.end()) {
                        operand_strs.push_back(label_it->second);
                    }
                    else {
                        operand_strs.push_back(std::to_string(instr.imm));
                    }

                    break;
                }
            }
        }

        std::string result = mnemonic;
        for (size_t i = 0; i < operand_strs.size(); ++i) {
            if (i == 0) result += " ";
            else result += ", ";
            result += operand_strs[i];
        }

        return result;
    }

    std::vector<std::string> Disassembler::formatLiterals() const {
        std::vector<std::string> lines;
        if (literal_words_.empty()) return lines;

        lines.push_back(".literals:");
        for (size_t i = 0; i < literal_words_.size(); ++i) {
            uint32_t bits = literal_words_[i];
            float value;
            std::memcpy(&value, &bits, sizeof(value));

            std::string name = "lit" + std::to_string(i);
            if (!std::isfinite(value)) {
                std::ostringstream hex;
                hex << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << bits;
                lines.push_back("; " + name + ": non-finite float, raw bits 0x" + hex.str());
                continue;
            }

            std::string text;
            for (int precision = 1; precision <= 9; ++precision) {
                std::ostringstream os;
                os << std::setprecision(precision) << value;
                text = os.str();
                if (std::strtof(text.c_str(), nullptr) == value) break;
            }
            if (text.find_first_of(".eE") == std::string::npos) text += ".0";

            lines.push_back(name + ": " + text + "f");
        }
        return lines;
    }

    std::vector<std::string> Disassembler::disassemble(const std::vector<Instruction>& program) {
        std::vector<std::string> lines;
        for (const auto& instr : program) {
            lines.push_back(formatInstruction(instr));
        }

        return lines;
    }

    std::vector<std::string> Disassembler::disassembleWithLabels(const std::vector<Instruction>& program) {
        std::vector<int32_t> target_addresses;
        std::unordered_map<int32_t, bool> seen_target;

        for (size_t i = 0; i < program.size(); ++i) {
            const Instruction& instr = program[i];
            int32_t address = static_cast<int32_t>(i * 4);

            auto name_it = opcodeToMnemonic().find(instr.opcode);
            if (name_it == opcodeToMnemonic().end()) continue;

            auto info_it = opTable().find(name_it->second);
            const OpInfo& info = info_it->second;

            bool uses_label = false;
            for (OperandKind kind : info.operand_kinds) {
                if (kind == OperandKind::LABEL) {
                    uses_label = true;
                    break;
                }
            }
            if (!uses_label) continue;

            int32_t target = address + 4 + instr.imm;
            if (!seen_target[target]) {
                seen_target[target] = true;
                target_addresses.push_back(target);
            }
        }

        std::sort(target_addresses.begin(), target_addresses.end());

        std::unordered_map<int32_t, std::string> label_map;
        for (size_t i = 0; i < target_addresses.size(); ++i) {
            std::ostringstream name;
            name << "L" << std::setfill('0') << std::setw(4) << std::hex << target_addresses[i];
            label_map[target_addresses[i]] = name.str();
        }

        std::vector<std::string> lines;
        for (size_t i = 0; i < program.size(); ++i) {
            int32_t address = static_cast<int32_t>(i * 4);

            auto label_it = label_map.find(address);
            if (label_it != label_map.end()) {
                lines.push_back(label_it->second + ":");
            }

            lines.push_back(formatInstructionWithLabels(program[i], address, label_map));
        }

        return lines;
    }

    bool Disassembler::readBinaryFile(const std::string& path, std::vector<uint32_t>& words_out) {
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;

        words_out.clear();
        unsigned char bytes[4];
        while (in.read(reinterpret_cast<char*>(bytes), 4)) {
            uint32_t word = (static_cast<uint32_t>(bytes[0]) << 24) | (static_cast<uint32_t>(bytes[1]) << 16) | (static_cast<uint32_t>(bytes[2]) << 8) | static_cast<uint32_t>(bytes[3]);
            words_out.push_back(word);
        }

        if (!in.eof()) return false;

        return true;
    }

    bool Disassembler::readHexFile(const std::string& path, std::vector<uint32_t>& words_out) {
        std::ifstream in(path);
        if (!in) return false;

        words_out.clear();
        std::string line;
        while (std::getline(in, line)) {
            size_t start = line.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) continue;
            size_t end = line.find_last_not_of(" \t\r\n");
            std::string trimmed = line.substr(start, end - start + 1);
            if (trimmed.empty()) continue;

            try {
                unsigned long word = std::stoul(trimmed, nullptr, 16);
                words_out.push_back(static_cast<uint32_t>(word));
            }
            catch (...) {
                return false;
            }
        }

        return true;
    }

} // namespace risc201
