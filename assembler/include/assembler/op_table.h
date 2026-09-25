#pragma once

#include "isa201.h"
#include <string>
#include <unordered_map>

namespace risc201 {

    struct OpInfo {
        Opcode op;
        InstrFormat format;
        size_t operand_count;
    };

    inline const std::unordered_map<std::string, OpInfo>& opTable() {
        static const std::unordered_map<std::string, OpInfo> table = {
            {"NOP",   {Opcode::NOP,   InstrFormat::R_TYPE, 0}},
            {"ADD",   {Opcode::ADD,   InstrFormat::R_TYPE, 3}},
            {"SUB",   {Opcode::SUB,   InstrFormat::R_TYPE, 3}},
            {"AND",   {Opcode::AND,   InstrFormat::R_TYPE, 3}},
            {"OR",    {Opcode::OR,    InstrFormat::R_TYPE, 3}},
            {"XOR",   {Opcode::XOR,   InstrFormat::R_TYPE, 3}},
            {"SHL",   {Opcode::SHL,   InstrFormat::R_TYPE, 3}},
            {"SHR",   {Opcode::SHR,   InstrFormat::R_TYPE, 3}},
            {"LOAD",  {Opcode::LOAD,  InstrFormat::I_TYPE, 2}},
            {"STORE", {Opcode::STORE, InstrFormat::I_TYPE, 2}},
            {"JMP",   {Opcode::JMP,   InstrFormat::J_TYPE, 1}},
            {"BEQ",   {Opcode::BEQ,   InstrFormat::I_TYPE, 3}},
            {"CALL",  {Opcode::CALL,  InstrFormat::J_TYPE, 1}},
            {"RET",   {Opcode::RET,   InstrFormat::R_TYPE, 0}},
            {"PUSH",  {Opcode::PUSH,  InstrFormat::R_TYPE, 1}},
            {"POP",   {Opcode::POP,   InstrFormat::R_TYPE, 1}},
            {"HALT",  {Opcode::HALT,  InstrFormat::R_TYPE, 0}},
        };

        return table;
    }

}