// Microprogrammed control unit: Control Memory (CM) + microPC sequencing.

#pragma once

#include "isa201.h"
#include <cstdint>
#include <vector>

namespace risc201 {

enum class MicrocodeEncoding { Horizontal, Vertical };

// Raw control signal bundle a datapath/ALU stage reads each micro-cycle.
// TODO: flesh out with the real control lines once ALU/datapath are co-designed.
struct ControlWord {
    bool alu_enable   = false;
    bool mem_read     = false;
    bool mem_write    = false;
    bool reg_write    = false;
    bool pc_write     = false;
};

class ControlUnit {
public:
    explicit ControlUnit(MicrocodeEncoding encoding = MicrocodeEncoding::Horizontal);

    ControlWord step(Opcode current_opcode);

    uint32_t microPC() const { return micro_pc_; }

private:
    MicrocodeEncoding encoding_;
    uint32_t micro_pc_ = 0;
    // TODO: control memory (CM) table, microcode routines per PRD §5.4
    // (at least ADD, LOAD, BRANCH for Milestone 0).
};

} // namespace risc201
