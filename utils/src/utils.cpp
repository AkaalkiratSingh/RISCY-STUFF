#include "utils/utils.h"

namespace risc201 {

    const char* opcodeName(Opcode op) {
        switch (op) {
            case Opcode::NOP:   return "NOP";
            case Opcode::ADD:   return "ADD";
            case Opcode::SUB:   return "SUB";
            case Opcode::AND:   return "AND";
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
            case Opcode::PUSH:  return "PUSH";
            case Opcode::POP:   return "POP";
            case Opcode::NOT:   return "NOT";
            case Opcode::SAR:   return "SAR";
            case Opcode::CMP:   return "CMP";
            case Opcode::MOV:   return "MOV";
            case Opcode::MUL:   return "MUL";
            case Opcode::HALT:  return "HALT";
        }
        return "?";
    }

}
