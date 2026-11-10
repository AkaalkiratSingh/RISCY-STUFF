#pragma once

// Vertical-microcode control unit.
// Compresses 42-bit horizontal rows into 12-bit words.

#include "control_unit/horizontal_control_unit.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace risc201 {

     inline constexpr int kHorizontalWordBits = 42;
     inline constexpr int kVerticalWordBits   = 12;
     inline constexpr int kNumControlLines    = 18;   // ControlWord bools, excluding alu_op

    // Decoder input: maps distinct control line combinations to 4-bit codes.
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

    // ---- 12-bit vertical microword format -----------------------------------
    // Bit 11=0 (Operate): [10:7]=uop, [6:2]=func, [1]=0 (reserved), [0]=END
    // Bit 11=1 (Branch):  [10:8]=cond (1..6), [7:1]=target, [0]=END (tests FLAGS)
    // Successor is implicit (addr+1 or END) unless a branch is taken.

    // A branch target is 7 bits: addresses 0..126; 0x7F encodes END.
    inline constexpr unsigned kVBranchBit  = 11;
    inline constexpr unsigned kVUopShift   = 7;
    inline constexpr unsigned kVUopMask    = 0xFu;
    inline constexpr unsigned kVFuncShift  = 2;
    inline constexpr unsigned kVFuncMask   = 0x1Fu;
    inline constexpr unsigned kVCondShift  = 8;
    inline constexpr unsigned kVCondMask   = 0x7u;
    inline constexpr unsigned kVTargetShift = 1;
    inline constexpr unsigned kVTargetMask = 0x7Fu;               // 7 bits
    inline constexpr unsigned kVEndField   = 0x7Fu;               // END in the target field
    inline constexpr unsigned kVEndBit     = 0;

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
