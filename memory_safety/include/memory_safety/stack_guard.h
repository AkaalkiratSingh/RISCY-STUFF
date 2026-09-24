// Stack overflow/underflow detection + runtime exception framework (PRD §5.5).

#pragma once

#include "isa201.h"
#include <cstdint>
#include <optional>

namespace risc201 {

class StackGuard {
public:
    StackGuard(uint32_t stack_base, uint32_t stack_limit)
        : stack_base_(stack_base), stack_limit_(stack_limit) {}

    std::optional<ExceptionCode> checkBounds(uint32_t sp) const;

private:
    uint32_t stack_base_;
    uint32_t stack_limit_;
    // TODO(Vaibhav): wire into simulator_core so every push/pop is checked.
};

} // namespace risc201
