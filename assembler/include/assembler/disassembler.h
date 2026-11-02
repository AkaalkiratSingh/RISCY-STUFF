#pragma once

#include "isa201.h"
#include "op_table.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace risc201 {

class Disassembler {
public:
    std::vector<std::string> disassemble(const std::vector<Instruction>& program);
    std::vector<std::string> disassembleWithLabels(const std::vector<Instruction>& program);

    bool decodeWord(uint32_t word, Instruction& out);
    std::vector<Instruction> decodeProgram(const std::vector<uint32_t>& words);

    std::string formatInstruction(const Instruction& instr);
    std::vector<std::string> formatLiterals() const;

    const std::vector<std::pair<size_t, uint32_t>>& decodeErrors() const { return decode_errors_; }
    const std::vector<uint32_t>& literalWords() const { return literal_words_; }

    bool readBinaryFile(const std::string& path, std::vector<uint32_t>& words_out);
    bool readHexFile(const std::string& path, std::vector<uint32_t>& words_out);

private:
    std::vector<std::pair<size_t, uint32_t>> decode_errors_;
    std::vector<uint32_t> literal_words_;

    std::string formatInstructionWithLabels(const Instruction& instr, int32_t address, const std::unordered_map<int32_t, std::string>& label_map);
};

} // namespace risc201
