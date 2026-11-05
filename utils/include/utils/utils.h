#pragma once

#include "isa201.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace risc201 {
    // Mnemonic for an opcode; "?" when the value is not a known opcode.
    const char* opcodeName(Opcode op);

    std::string formatInstruction(const Instruction& in);

    bool parseRegisterAlias(const std::string& token, Register& out);

    const char* exceptionName(ExceptionCode code);

    bool hasSuffix(const std::string& path, const std::string& suffix);

    uint32_t floatToBits(float value);
    float bitsToFloat(uint32_t bits);
    std::string trim(const std::string& s);
    std::string toUpper(std::string s);
    std::string toLower(std::string s);
    std::string stripCommentandTrim(const std::string& raw);
    bool extractLabel(std::string& line, std::string& label_out);
}