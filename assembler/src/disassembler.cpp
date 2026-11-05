#include "assembler/disassembler.h"
#include "assembler/assembler.h"
#include "utils/utils.h"

#include <fstream>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <cstdlib>
#include <optional>

namespace risc201 {

    namespace {

        bool usesLabelOperand(const OpInfo& info) {
            return std::find(info.operand_kinds.begin(), info.operand_kinds.end(), OperandKind::LABEL) != info.operand_kinds.end();
        }

        std::optional<int32_t> entryTarget(const std::vector<Instruction>& program) {
            if (program.empty() || program[0].opcode != Opcode::JMP) return std::nullopt;
            const int32_t target = 4 + program[0].imm;
            if (target < 4 || target % 4 != 0) return std::nullopt;
            if (target >= static_cast<int32_t>(program.size()) * 4) return std::nullopt;
            return target;
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
        if (opcodeToMnemonic().find(instr.opcode) == opcodeToMnemonic().end()) {
            return "; unknown opcode";
        }
        return risc201::formatInstruction(instr);
    }

    std::string Disassembler::formatInstructionWithLabels(const Instruction& instr, int32_t address, const std::unordered_map<int32_t, std::string>& label_map) {
        auto name_it = opcodeToMnemonic().find(instr.opcode);
        if (name_it == opcodeToMnemonic().end()) {
            return "; unknown opcode";
        }

        if (usesLabelOperand(opTable().at(name_it->second))) {
            auto label_it = label_map.find(address + 4 + instr.imm);
            if (label_it != label_map.end()) return name_it->second + " " + label_it->second;
        }

        return formatInstruction(instr);
    }

    std::unordered_map<int32_t, std::string> Disassembler::buildLabelMap(const std::vector<Instruction>& program, size_t first_index, int32_t min_target) const {
        const int32_t end_address = static_cast<int32_t>(program.size()) * 4;

        std::vector<int32_t> targets;
        for (size_t i = first_index; i < program.size(); ++i) {
            auto name_it = opcodeToMnemonic().find(program[i].opcode);
            if (name_it == opcodeToMnemonic().end()) continue;
            if (!usesLabelOperand(opTable().at(name_it->second))) continue;

            const int32_t target = static_cast<int32_t>(i * 4) + 4 + program[i].imm;
            if (target < min_target || target >= end_address || target % 4 != 0) continue;
            targets.push_back(target);
        }

        std::sort(targets.begin(), targets.end());
        targets.erase(std::unique(targets.begin(), targets.end()), targets.end());

        std::unordered_map<int32_t, std::string> label_map;
        for (int32_t address : targets) {
            std::ostringstream name;
            name << "L" << std::setfill('0') << std::setw(4) << std::hex << address;
            label_map[address] = name.str();
        }
        return label_map;
    }

    std::vector<std::string> Disassembler::formatLiterals() const {
        std::vector<std::string> lines;
        if (literal_words_.empty()) return lines;

        lines.push_back(".literals:");
        for (size_t i = 0; i < literal_words_.size(); ++i) {
            uint32_t bits = literal_words_[i];
            float value = bitsToFloat(bits);

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
        const auto label_map = buildLabelMap(program, 0, 0);

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

    std::vector<std::string> Disassembler::disassembleSource(const std::vector<Instruction>& program, bool use_labels) {
        const std::optional<int32_t> entry = entryTarget(program);
        const size_t first = entry ? 1 : 0;
        const int32_t main_address = entry ? *entry : 0;

        std::unordered_map<int32_t, std::string> label_map;
        if (use_labels) label_map = buildLabelMap(program, first, static_cast<int32_t>(first * 4));
        label_map[main_address] = ".main";   // wins over a generated name at the same address

        std::vector<std::string> lines;
        if (!entry && !program.empty()) {
            lines.push_back("; no entry JMP at word 0, so .main is placed on the first instruction");
        }

        bool main_emitted = false;
        for (size_t i = first; i < program.size(); ++i) {
            const int32_t address = static_cast<int32_t>(i * 4);

            auto label_it = label_map.find(address);
            if (label_it != label_map.end()) {
                lines.push_back(label_it->second + ":");
                if (address == main_address) main_emitted = true;
            }

            lines.push_back(use_labels ? formatInstructionWithLabels(program[i], address, label_map)
                                       : formatInstruction(program[i]));
        }

        if (!main_emitted) lines.push_back(".main:");   // empty program
        return lines;
    }

    bool Disassembler::readBinaryFile(const std::string& path, std::vector<uint32_t>& words_out) {
        return readBinaryWords(path, words_out);
    }

    bool Disassembler::readHexFile(const std::string& path, std::vector<uint32_t>& words_out) {
        return readHexWords(path, words_out);
    }

} // namespace risc201