#pragma once

#include "isa201.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace risc201 {

    enum class MicrocodeEncoding { Horizontal, Vertical };

    // Condition tested by a micro-branch row against the FLAGS register.
    // None = unconditional: the row always continues to MicroOp::next.
    enum class BranchCond : uint8_t {
        None = 0,
        Zero,       // Z set
        NotZero,    // Z clear
        Negative,   // N set
        Carry,      // C set
        Overflow,   // V set
    };

    // Raw control signal bundle a datapath/ALU stage reads each micro-cycle.
    struct ControlWord {
        bool    alu_enable = false;
        bool      mem_read = false;
        bool     mem_write = false;
        bool     reg_write = false;
        bool      pc_write = false;  // PC <- instruction target (taken transfer)
        bool        pc_inc = false;  // PC <- PC + 1 (sequential fetch)
        bool   alu_src_imm = false;  // ALU operand B: immediate (1) vs rs2 (0)
        bool    mem_to_reg = false;  // writeback mux: loaded word (1) vs ALU result (0)
        bool        sp_dec = false;  // SP <- SP - 1 (full-descending push side)
        bool        sp_inc = false;  // SP <- SP + 1 (pop side)
        bool   flag_update = false;  // latch Z/N/C/V from the ALU result
        bool          halt = false;

        Opcode  alu_op = Opcode::NOP;
    };

    // MicroOp::next / branch_target sentinel — end of the macro instruction's routine.
    inline constexpr int32_t kMicroEnd = -1;

    // Width of the microPC and of the address fields in the packed word.
    inline constexpr int kMicroAddrBits = 8;
    inline constexpr uint32_t kMicroEndField = 0xFF;   // kMicroEnd as stored in the packed word
    inline constexpr size_t kMaxControlMemoryRows = 255;

    struct MicroOp {
        ControlWord ctl;
        BranchCond  cond = BranchCond::None;   // None: always go to `next`
        int32_t     next = kMicroEnd;          // fall-through / condition-false address
        int32_t     branch_target = kMicroEnd; // address used when `cond` holds
    };

    class ControlUnit {
    public:
        explicit ControlUnit(MicrocodeEncoding encoding = MicrocodeEncoding::Horizontal);

        // Advance one micro-cycle of the routine for `op` and return the control
        // signals for that cycle. `flags` is only consulted by branch rows.
        ControlWord step(Opcode op, const Flags& flags);

        bool routineDone() const { return done_; }

        void reset();

        uint32_t microPC() const { return micro_pc_; }
        MicrocodeEncoding encoding() const { return encoding_; }

        int routineEntry(Opcode op) const;

        const std::vector<MicroOp>& controlMemory() const { return cm_; }

        // Pack one row into the 36-bit horizontal microword (see docs/control_unit_design.md §3).
        static uint64_t packHorizontal(const MicroOp& row);

        // Human-readable listing of the whole control memory (for docs / CLI microPC view).
        std::string dumpControlMemory() const;

    private:
        void buildHorizontalRom();
        int  appendRaw(std::vector<MicroOp> routine);
        void appendRoutine(Opcode op, std::vector<MicroOp> routine);
        // 3-row conditional routine: [test] -> (cond ? taken : not_taken).
        void appendBranchRoutine(Opcode op, BranchCond cond, MicroOp taken_row, MicroOp not_taken_row);

        MicrocodeEncoding encoding_;
        uint32_t          micro_pc_ = 0;
        bool              done_ = false;
        bool              latched_ = false;

        std::vector<MicroOp> cm_;
        std::unordered_map<uint8_t, int> entries_;
        int illegal_entry_ = 0;
    };

}
