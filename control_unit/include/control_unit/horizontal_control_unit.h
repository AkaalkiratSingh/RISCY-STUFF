#pragma once

#include "isa201.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace risc201 {

    enum class MicrocodeEncoding { Horizontal, Vertical };

    enum class BranchCond : uint8_t {
        None = 0,
        Zero,
        NotZero,
        Negative,
        Carry,
        Overflow,
        GreaterThan,   // flags.greaterThan (BGT)
    };

    struct ControlWord {
        bool ir_load = false;
        bool instr_decode = false;
        bool operand_read = false;
        bool opcode_dispatch = false;

        bool    alu_enable = false;
        bool      mem_read = false;
        bool     mem_write = false;
        bool     reg_write = false;
        bool      pc_write = false;
        bool        pc_inc = false;
        bool   alu_src_imm = false;
        bool    mem_to_reg = false;
        bool   flag_update = false;
        bool          halt = false;
        bool          trap = false;

        bool   pc_src_ra = false;
        bool  addr_check = false;    // memory guard validates the ALU-computed address THIS cycle,
                                     // before the LOAD/STORE access commits (out of bounds -> trap)
        bool      ra_write = false;

        Opcode  alu_op = Opcode::NOP;
    };

    inline bool condHolds(BranchCond c, const Flags& f) {
        switch (c) {
            case BranchCond::None:     return false;
            case BranchCond::Zero:     return f.zero;
            case BranchCond::NotZero:  return !f.zero;
            case BranchCond::Negative: return f.negative;
            case BranchCond::Carry:    return f.carry;
            case BranchCond::Overflow: return f.overflow;
            case BranchCond::GreaterThan: return f.greaterThan;
        }
        return false;
    }

    // A fault counts only in a cycle where the unit that reports it is checking.
    inline bool faultApplies(const ControlWord& c, ExceptionCode e) {
        switch (e) {
            case ExceptionCode::DIVIDE_BY_ZERO:        return c.alu_enable;
            case ExceptionCode::NONE:
            case ExceptionCode::ILLEGAL_INSTRUCTION:   return false;
        }
        return false;
    }

    std::string controlSignalNames(const ControlWord& word);

    inline constexpr uint32_t kMicroEndField = 0xFF;
    inline constexpr size_t kMaxControlMemoryRows = 255;

    struct MicroOp {
        ControlWord ctl;
        BranchCond  cond = BranchCond::None;
        int32_t     next = kMicroEnd;
        int32_t     branch_target = kMicroEnd;
    };

    class ControlUnit {
    public:
        explicit ControlUnit(MicrocodeEncoding encoding = MicrocodeEncoding::Horizontal);

        ControlWord step(Opcode op, const Flags& flags,
                         ExceptionCode fault = ExceptionCode::NONE);

        ExceptionCode pendingException() const { return pending_exception_; }
        void clearException() { pending_exception_ = ExceptionCode::NONE; }

        bool routineDone() const { return done_; }

        void reset();

        uint32_t microPC() const { return micro_pc_; }
        MicrocodeEncoding encoding() const { return encoding_; }

        int routineEntry(Opcode op) const;
        int frontendEntry() const { return frontend_entry_; }
        int trapEntry() const { return trap_entry_; }

        const std::vector<MicroOp>& controlMemory() const { return cm_; }

        static uint64_t packHorizontal(const MicroOp& row);

        std::string dumpControlMemory() const;

    private:
        void buildHorizontalRom();
        void restart();
        int  appendRaw(std::vector<MicroOp> routine);
        void appendRoutine(Opcode op, std::vector<MicroOp> routine);
        void appendBranchRoutine(Opcode op, BranchCond cond, MicroOp taken_row, MicroOp not_taken_row);

        MicrocodeEncoding encoding_;
        uint32_t          micro_pc_ = 0;
        bool              done_ = false;
        bool              latched_ = false;

        std::vector<MicroOp> cm_;
        std::unordered_map<uint8_t, int> entries_;
        int frontend_entry_ = 0;
        int illegal_entry_ = 0;
        int trap_entry_ = 0;
        ExceptionCode pending_exception_ = ExceptionCode::NONE;
    };

}
