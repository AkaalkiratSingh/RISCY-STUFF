#pragma once

// Vertical-microcode control unit.
// Compresses 42-bit horizontal rows into 16-bit words.
// Decoder expands 5-bit codes back to the 24 control lines.
// Fully interchangeable with the horizontal unit.

#include "control_unit/horizontal_control_unit.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace risc201 {

     inline constexpr int kHorizontalWordBits = 42;
     inline constexpr int kVerticalWordBits   = 16;
     inline constexpr int kNumControlLines    = 24;   // ControlWord bools, excluding alu_op

    // Decoder input: maps distinct control line combinations to 5-bit codes.
    enum class MicroOpCode : uint8_t {
    Idle = 0,
    Operands,
    Alu,
    AluNoFlags,
    AluImm,
    MemRead,
    MemWrite,
    Writeback,
    WritebackMem,
    Jump,
    Halt,
    Trap,
    RaWrite,
    JumpRa,
    InstructionFetch,
    DecodeDispatch,
    };
    inline constexpr size_t kNumMicroOpCodes = 16;  

    // ---- 16-bit vertical microword format -----------------------------------
    // Bit 15=0 (Operate): [14:10]=uop, [9:5]=func, [0]=END (drives datapath)
    // Bit 15=1 (Branch):  [14:12]=cond (1..6), [11:4]=target, [0]=END (tests FLAGS)
    // Successor is implicit (addr+1 or END) unless a branch is taken.

    // Returns true if row fits vertical format.
    bool canPackVertical(const MicroOp& row, int addr);

    // Packs row into vertical word. Throws if unencodable, or if decoding the word
    // would not give back exactly the same row (a control line would be lost).
    uint16_t packVertical(const MicroOp& row, int addr);

    // Decodes word into row. Invalid inputs yield a halt row.
    MicroOp unpackVertical(uint16_t word, int addr);

    const char* microOpName(MicroOpCode code);

    // Interchangeable with horizontal ControlUnit.
    class VerticalControlUnit {
    public:
        VerticalControlUnit();

        // Executes one micro-cycle. Same fault handling as ControlUnit::step: a fault is
        // honoured only on a row whose checker is active; it deletes that cycle's signals
        // and sends the unit to the TRAP row.
        ControlWord step(Opcode op, const Flags& flags,
                         ExceptionCode fault = ExceptionCode::NONE);

        ExceptionCode pendingException() const { return pending_exception_; }
        void clearException() { pending_exception_ = ExceptionCode::NONE; }

        bool routineDone() const { return done_; }
        void reset();

        uint32_t microPC() const { return micro_pc_; }
        MicrocodeEncoding encoding() const { return MicrocodeEncoding::Vertical; }

        int routineEntry(Opcode op) const;
        int frontendEntry() const { return frontend_entry_; }
        int trapEntry() const { return trap_entry_; }

        // Decoded ROM words.
        const std::vector<MicroOp>& controlMemory() const { return cm_; }

        // Packed 16-bit ROM.
        const std::vector<uint16_t>& verticalRom() const { return vrom_; }

        // Human-readable ROM listing.
        std::string dumpControlMemory() const;

    private:
        MicroOp readRow(uint32_t addr) const;   // ROM read + decode
        void restart();
        uint32_t micro_pc_ = 0;
        bool     done_ = false;
        bool     latched_ = false;

        std::vector<uint16_t> vrom_;
        std::vector<MicroOp>  cm_;                 
        std::array<int, 256>  entries_{};          
        int frontend_entry_ = 0;
        int illegal_entry_ = 0;
        int trap_entry_ = 0;
        ExceptionCode pending_exception_ = ExceptionCode::NONE;
    };

}
