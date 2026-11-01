#include "utils/utils.h"

#include <cctype>

namespace risc201 {

    const char* opcodeName(Opcode op) {
        switch (op) {
            case Opcode::NOP:   return "NOP";
            case Opcode::ADD:   return "ADD";
            case Opcode::SUB:   return "SUB";
            case Opcode::AND:   return "AND";
            case Opcode::MUL:   return "MUL";
            case Opcode::DIV:   return "DIV";
            case Opcode::REM:   return "REM";
            case Opcode::FADD:  return "FADD";
            case Opcode::FSUB:  return "FSUB";
            case Opcode::FMUL:  return "FMUL";
            case Opcode::FDIV:  return "FDIV";
            case Opcode::OR:    return "OR";
            case Opcode::XOR:   return "XOR";
            case Opcode::SHL:   return "SHL";
            case Opcode::SHR:   return "SHR";
            case Opcode::LOAD:  return "LOAD";
            case Opcode::STORE: return "STORE";
            case Opcode::JMP:   return "JMP";
            case Opcode::BEQ:   return "BEQ";
            case Opcode::CALL:  return "CALL";
            case Opcode::RET:   return "RET";
            // case Opcode::PUSH:  return "PUSH";
            // case Opcode::POP:   return "POP";
            case Opcode::NOT:   return "NOT";
            case Opcode::SAR:   return "SAR";
            case Opcode::CMP:   return "CMP";
            case Opcode::MOV:   return "MOV";
            case Opcode::BGT:   return "BGT";
            case Opcode::HALT:  return "HALT";
        }
        return "?";
    }

    uint32_t floatToBits(float value) {
        static_assert(sizeof(float) == sizeof(uint32_t), "float must be 32 bits");
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    }

    float bitsToFloat(uint32_t bits) {
        static_assert(sizeof(float) == sizeof(uint32_t), "float must be 32 bits");
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    std::string toUpper(std::string s) {
        for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return s;
    }

    std::string toLower(std::string s) {
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    std::string trim(const std::string& s) {
        size_t start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        size_t end = s.find_last_not_of(" \t\r\n");
        return s.substr(start, end - start + 1);
    }

    std::string stripCommentandTrim(const std::string& raw) {
        std::string s = raw;
        auto comment_start = s.find_first_of(";#");
        if (comment_start != std::string::npos) s = s.substr(0, comment_start);
        return trim(s);
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

}
