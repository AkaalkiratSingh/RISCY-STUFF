// Stack overflow/underflow detection + runtime exception framework (PRD §5.5).

#pragma once

#include "isa201.h"
#include <cstdint>
#include <optional>

namespace risc201 {

class StackGuard {
public:
    StackGuard(uint32_t stack_base, uint32_t stack_limit)
        : stack_base_(stack_base),
          stack_limit_(stack_limit) {}

    // Check whether the current stack pointer is inside the valid stack range.
    // Full-descending stack: PUSH decreases SP and POP increases SP.
    std::optional<ExceptionCode> checkBounds(uint32_t sp) const;

    // Check whether a PUSH can safely decrease SP by one word.
    std::optional<ExceptionCode> checkPush(uint32_t current_sp) const;

    // Check whether a POP can safely increase SP by one word.
    std::optional<ExceptionCode> checkPop(uint32_t current_sp) const;

    // Check whether an address belongs to the valid stack memory region.
    bool isValidAddress(uint32_t address) const;

    // Return the highest address used as the stack boundary.
    uint32_t stackBase() const {
        return stack_base_;
    }

    // Return the lowest valid stack address.
    uint32_t stackLimit() const {
        return stack_limit_;
    }

private:
    uint32_t stack_base_;
    uint32_t stack_limit_;
};

} // namespace risc201
