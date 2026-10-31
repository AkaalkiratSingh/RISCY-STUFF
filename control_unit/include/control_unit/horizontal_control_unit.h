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
    };

    struct ControlWord {
        bool    alu_enable = false;
        bool      mem_read = false;
        bool     mem_write = false;
        bool     reg_write = false;
        bool      pc_write = false;
        bool        pc_inc = false;
        bool   alu_src_imm = false;
        bool    mem_to_reg = false;
        bool        sp_dec = false;
        bool        sp_inc = false;
        bool   flag_update = false;
        bool          halt = false;
        bool          trap = false;

        bool  mem_addr_sp = false;
        bool  mem_data_pc = false;
        bool   pc_src_mem = false;
        bool  stack_check = false;

        Opcode  alu_op = Opcode::NOP;
    };

    inline constexpr int32_t kMicroEnd = -1;

    inline constexpr int kMicroAddrBits = 8;
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
                         ExceptionCode alu_fault = ExceptionCode::NONE);

        ExceptionCode pendingException() const { return pending_exception_; }
        void clearException() { pending_exception_ = ExceptionCode::NONE; }

        bool routineDone() const { return done_; }

        void reset();

        uint32_t microPC() const { return micro_pc_; }
        MicrocodeEncoding encoding() const { return encoding_; }

        int routineEntry(Opcode op) const;
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
        int illegal_entry_ = 0;
        int trap_entry_ = 0;
        ExceptionCode pending_exception_ = ExceptionCode::NONE;
    };

}
