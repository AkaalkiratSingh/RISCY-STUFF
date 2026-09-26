#pragma once

#include "isa201.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace risc201 {

    enum class OperandKind : uint8_t {
    REG,
    IMM,
    REG_OR_IMM,
    MEM, //imm[rs1] type
    LABEL,
};

    struct OpInfo {
        Opcode op;
        InstrFormat format;
        std::vector<OperandKind> operand_kinds;
    };

    inline const std::unordered_map<std::string, OpInfo>& opTable() {
        static const std::unordered_map<std::string, OpInfo> table = {
            {"NOP",   {Opcode::NOP,   InstrFormat::RI_TYPE, {}}},
            {"ADD",   {Opcode::ADD,   InstrFormat::RI_TYPE, {OperandKind::REG, OperandKind::REG, OperandKind::REG_OR_IMM}}},
            {"SUB",   {Opcode::SUB,   InstrFormat::RI_TYPE, {OperandKind::REG, OperandKind::REG, OperandKind::REG_OR_IMM}}},
            {"AND",   {Opcode::AND,   InstrFormat::RI_TYPE, {OperandKind::REG, OperandKind::REG, OperandKind::REG_OR_IMM}}},
            {"OR",    {Opcode::OR,    InstrFormat::RI_TYPE, {OperandKind::REG, OperandKind::REG, OperandKind::REG_OR_IMM}}},
            {"XOR",   {Opcode::XOR,   InstrFormat::RI_TYPE, {OperandKind::REG, OperandKind::REG, OperandKind::REG_OR_IMM}}},
            {"SHL",   {Opcode::SHL,   InstrFormat::RI_TYPE, {OperandKind::REG, OperandKind::REG, OperandKind::REG_OR_IMM}}},
            {"SHR",   {Opcode::SHR,   InstrFormat::RI_TYPE, {OperandKind::REG, OperandKind::REG, OperandKind::REG_OR_IMM}}},
            {"LOAD",  {Opcode::LOAD,  InstrFormat::RI_TYPE, {OperandKind::REG, OperandKind::MEM}}},
            {"STORE", {Opcode::STORE, InstrFormat::RI_TYPE, {OperandKind::REG, OperandKind::MEM}}},
            {"JMP",   {Opcode::JMP,   InstrFormat::J_TYPE,  {OperandKind::LABEL}}},
            {"BEQ",   {Opcode::BEQ,   InstrFormat::J_TYPE,  {OperandKind::LABEL}}},
            {"CALL",  {Opcode::CALL,  InstrFormat::J_TYPE,  {OperandKind::LABEL}}},
            {"RET",   {Opcode::RET,   InstrFormat::RI_TYPE, {}}},
            {"PUSH",  {Opcode::PUSH,  InstrFormat::RI_TYPE, {OperandKind::REG}}},
            {"POP",   {Opcode::POP,   InstrFormat::RI_TYPE, {OperandKind::REG}}},
            {"HALT",  {Opcode::HALT,  InstrFormat::RI_TYPE, {}}},
            };

        return table;
    }

}