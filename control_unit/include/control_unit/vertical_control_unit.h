#pragma once

// Vertical-microcode control unit.
// Compresses 40-bit horizontal rows into 16-bit words.
// Decoder expands 5-bit codes back to 16 control lines.
// Fully interchangeable with the horizontal unit.

#include "control_unit/horizontal_control_unit.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace risc201 {

    inline constexpr int kHorizontalWordBits = 40;
    inline constexpr int kVerticalWordBits   = 16;

    // Decoder input: maps distinct control line combinations to 5-bit codes.
    enum class MicroOpCode : uint8_t {
        Idle = 0,       
        Fetch,          
        Alu,            
        AluNoFlags,     
        AluImm,         
        MemRead,        
        MemWrite,       
        Writeback,      
        WritebackMem,   
        Jump,           
        JumpMem,        
        SpDec,          
        SpInc,          
        PushWrite,      
        CallWrite,      
        PopRead,        
        Halt,           
    };
    inline constexpr size_t kNumMicroOpCodes = 17;   

    // ---- 16-bit vertical microword format -----------------------------------
    // Bit 15=0 (Operate): [14:10]=uop, [9:5]=func, [0]=END (drives datapath)
    // Bit 15=1 (Branch):  [14:12]=cond, [11:4]=target, [0]=END (tests FLAGS)
    // Successor is implicit (addr+1 or END) unless a branch is taken.

    // Returns true if row fits vertical format.
    bool canPackVertical(const MicroOp& row, int addr);

    // Packs row into vertical word. Throws if unencodable.
    uint16_t packVertical(const MicroOp& row, int addr);

    // Decodes word into row. Invalid inputs yield a halt row.
    MicroOp unpackVertical(uint16_t word, int addr);

    const char* microOpName(MicroOpCode code);

    // Interchangeable with horizontal ControlUnit.
    class VerticalControlUnit {
    public:
        VerticalControlUnit();

        // Executes one micro-cycle.
        ControlWord step(Opcode op, const Flags& flags);

        bool routineDone() const { return done_; }
        void reset();

        uint32_t microPC() const { return micro_pc_; }
        MicrocodeEncoding encoding() const { return MicrocodeEncoding::Vertical; }

        int routineEntry(Opcode op) const;

        // Decoded ROM words.
        const std::vector<MicroOp>& controlMemory() const { return cm_; }

        // Packed 16-bit ROM.
        const std::vector<uint16_t>& verticalRom() const { return vrom_; }

        // Human-readable ROM listing.
        std::string dumpControlMemory() const;

    private:
        MicroOp readRow(uint32_t addr) const;   // ROM read + decode

        uint32_t micro_pc_ = 0;
        bool     done_ = false;
        bool     latched_ = false;

        std::vector<uint16_t> vrom_;
        std::vector<MicroOp>  cm_;                 
        std::array<int, 256>  entries_{};          
        int illegal_entry_ = 0;
    };

}
