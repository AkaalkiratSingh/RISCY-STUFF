#pragma once

#include "isa201.h"
#include "op_table.h"

#include <string>
#include <vector>

namespace risc201 {

class Disassembler {
public:
    std::vector<std::string> disassemble(const std::vector<Instruction>& program);

    bool decodeWord(uint32_t word, Instruction& out);
    std::vector<Instruction> decodeProgram(const std::vector<uint32_t>& words);

    std::string formatInstruction(const Instruction& instr);

    const std::vector<std::pair<size_t, uint32_t>>& decodeErrors() const { return decode_errors_; }

    bool readBinaryFile(const std::string& path, std::vector<uint32_t>& words_out);
    bool readHexFile(const std::string& path, std::vector<uint32_t>& words_out);

private:
    std::vector<std::pair<size_t, uint32_t>> decode_errors_;
};

} // namespace risc201
