#include "control_unit/control_unit.h"

namespace risc201 {

ControlUnit::ControlUnit(MicrocodeEncoding encoding) : encoding_(encoding) {}

ControlWord ControlUnit::step(Opcode /*current_opcode*/) {
    // TODO(Milan): drive control_word from the CM table for micro_pc_,
    // then advance micro_pc_ (or reset to 0 at end of routine).
    return {};
}

} // namespace risc201
