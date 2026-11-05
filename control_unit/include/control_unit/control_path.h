#pragma once

#include "control_unit/horizontal_control_unit.h"

#include <vector>

namespace risc201 {

class VerticalControlUnit;

struct ControlStep {
    int           micro_pc = 0;
    ControlWord   signals;
    BranchCond    condition = BranchCond::None;
    int           next = kMicroEnd;
    ExceptionCode exception = ExceptionCode::NONE;
};

// Shared fetch/decode rows, in execution order.
std::vector<int> frontendRows(const ControlUnit& cu);
std::vector<int> frontendRows(const VerticalControlUnit& cu);

// Reachable rows of one opcode routine, including both branch successors.
std::vector<int> routineRows(const ControlUnit& cu, Opcode op);
std::vector<int> routineRows(const VerticalControlUnit& cu, Opcode op);

// Last physical row of every distinct routine, sorted. Used to separate routines
// visually in the ROM listing without changing addresses.
std::vector<int> routineEnds(const ControlUnit& cu);
std::vector<int> routineEnds(const VerticalControlUnit& cu);

// Complete logical path: shared prefix, dispatch, then the routine or trap.
std::vector<ControlStep> controlPath(const ControlUnit& cu, Opcode op,
                                     const Flags& flags,
                                     ExceptionCode fault = ExceptionCode::NONE);
std::vector<ControlStep> controlPath(const VerticalControlUnit& cu, Opcode op,
                                     const Flags& flags,
                                     ExceptionCode fault = ExceptionCode::NONE);

} // namespace risc201
