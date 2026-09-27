#pragma once

#include "isa201.h"
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace risc201 {

    enum class MicrocodeEncoding { Horizontal, Vertical };

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
        bool          halt = false;

        Opcode  alu_op = Opcode::NOP;
    };

    // MicroOp::next sentinel — end of the macro instruction's routine.
    inline constexpr int32_t kMicroEnd = -1;
    struct MicroOp {
        ControlWord ctl;
        int32_t     next = kMicroEnd;
    };

    class ControlUnit {
    public:
        explicit ControlUnit(MicrocodeEncoding encoding = MicrocodeEncoding::Horizontal);

        ControlWord step(Opcode op, const Flags& flags);

        bool routineDone() const { return done_; }

        void reset();

        uint32_t microPC() const { return micro_pc_; }
        MicrocodeEncoding encoding() const { return encoding_; }

        int routineEntry(Opcode op) const;

        const std::vector<MicroOp>& controlMemory() const { return cm_; }

    private:
        void buildHorizontalRom();
        int  appendRaw(std::vector<MicroOp> routine);
        void appendRoutine(Opcode op, std::vector<MicroOp> routine);

        MicrocodeEncoding encoding_;
        uint32_t          micro_pc_ = 0;
        bool              done_ = false;
        bool              latched_ = false;

        std::vector<MicroOp> cm_;
        std::unordered_map<uint8_t, int> entries_;
        int illegal_entry_ = 0;
    };

} 