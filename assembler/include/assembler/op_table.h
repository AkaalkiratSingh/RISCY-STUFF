#pragma once

#include "isa201.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace risc201 {

    constexpr uint32_t kLiteralsMarker = 0xFFFFFFFFu;

    enum class OperandKind : uint8_t {
        REG,
        IMM,
        REG_OR_IMM,
        MEM, //imm[rs1] type
        LABEL,
        EMPTY,
    };

    struct OpInfo {
        Opcode op;
        InstrFormat format;
        std::vector<OperandKind> operand_kinds;
    };

    inline bool writesDestination(Opcode op) {
        switch (op) {
            case Opcode::ADD:
            case Opcode::SUB:
            case Opcode::MUL:
            case Opcode::DIV:
            case Opcode::REM:
            case Opcode::AND:
            case Opcode::OR: 
            case Opcode::XOR:
            case Opcode::NOT:
            case Opcode::SHL:
            case Opcode::SHR:
            case Opcode::SAR:
            case Opcode::MOV:
            case Opcode::LOAD:
                return true;
            default:
                return false;
        }
    }

    inline size_t writtenOperandCount(const OpInfo& info) {
        size_t n = 0;
        for (OperandKind k : info.operand_kinds) {
            if (k != OperandKind::EMPTY) ++n;
        }
        return n;
    }

    inline const std::unordered_map<std::string, OpInfo>& opTable() {
        static const std::unordered_map<std::string, OpInfo> table = {
            { "NOP",{ Opcode::NOP,   InstrFormat::RI_TYPE,{} } },

            { "MOV",{ Opcode::MOV,   InstrFormat::RI_TYPE,{ OperandKind::REG, OperandKind::EMPTY, OperandKind::REG_OR_IMM } } }, //check

            { "ADD",{ Opcode::ADD,   InstrFormat::RI_TYPE,{ OperandKind::REG, OperandKind::REG, OperandKind::REG_OR_IMM } } },
            { "SUB",{ Opcode::SUB,   InstrFormat::RI_TYPE,{ OperandKind::REG, OperandKind::REG, OperandKind::REG_OR_IMM } } },
            { "MUL",{ Opcode::MUL,   InstrFormat::RI_TYPE,{ OperandKind::REG, OperandKind::REG, OperandKind::REG_OR_IMM } } },
            { "DIV",{ Opcode::DIV,   InstrFormat::RI_TYPE,{ OperandKind::REG, OperandKind::REG, OperandKind::REG_OR_IMM } } },
            { "REM",{ Opcode::REM,   InstrFormat::RI_TYPE,{ OperandKind::REG, OperandKind::REG, OperandKind::REG_OR_IMM } } },
            { "AND",{ Opcode::AND,   InstrFormat::RI_TYPE,{ OperandKind::REG, OperandKind::REG, OperandKind::REG_OR_IMM } } },
            { "OR" ,{ Opcode::OR,    InstrFormat::RI_TYPE,{ OperandKind::REG, OperandKind::REG, OperandKind::REG_OR_IMM } } },
            { "XOR",{ Opcode::XOR,   InstrFormat::RI_TYPE,{ OperandKind::REG, OperandKind::REG, OperandKind::REG_OR_IMM } } },
            { "NOT",{ Opcode::NOT,   InstrFormat::RI_TYPE,{ OperandKind::REG, OperandKind::EMPTY, OperandKind::REG_OR_IMM } } }, //check
            { "SHL",{ Opcode::SHL,   InstrFormat::RI_TYPE,{ OperandKind::REG, OperandKind::REG, OperandKind::REG_OR_IMM } } },
            { "SHR",{ Opcode::SHR,   InstrFormat::RI_TYPE,{ OperandKind::REG, OperandKind::REG, OperandKind::REG_OR_IMM } } },
            { "SAR",{ Opcode::SAR,   InstrFormat::RI_TYPE,{ OperandKind::REG, OperandKind::REG, OperandKind::REG_OR_IMM } } },

            { "LOAD" ,{ Opcode::LOAD,  InstrFormat::RI_TYPE,{ OperandKind::REG, OperandKind::MEM } } },
            { "STORE",{ Opcode::STORE, InstrFormat::RI_TYPE,{ OperandKind::REG, OperandKind::MEM } } },

            { "CMP",{ Opcode::CMP,    InstrFormat::RI_TYPE,{ OperandKind::EMPTY, OperandKind::REG, OperandKind::REG_OR_IMM } } }, //check
            { "JMP" ,{ Opcode::JMP,   InstrFormat::J_TYPE,{ OperandKind::LABEL } } },
            { "BEQ" ,{ Opcode::BEQ,   InstrFormat::J_TYPE,{ OperandKind::LABEL } } },
            { "BGT" ,{ Opcode::BGT,   InstrFormat::J_TYPE,{ OperandKind::LABEL } } },
            { "CALL",{ Opcode::CALL,  InstrFormat::J_TYPE,{ OperandKind::LABEL } } },
            { "RET" ,{ Opcode::RET,   InstrFormat::RI_TYPE,{} } },

            { "HALT",{ Opcode::HALT,  InstrFormat::RI_TYPE,{} } },
        };

        return table;
    }

    inline const std::unordered_map<Opcode, std::string>& opcodeToMnemonic() {
        static const std::unordered_map<Opcode, std::string> reverse = [] {
            std::unordered_map<Opcode, std::string> m;
            for (const auto& pair : opTable()) {
                m[pair.second.op] = pair.first;
            }
            return m;
        }();

        return reverse;
    }

}