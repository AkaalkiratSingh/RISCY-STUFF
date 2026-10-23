#include "memory_safety/stack_guard.h"

namespace risc201 {

std::optional<ExceptionCode> StackGuard::checkBounds(uint32_t sp) const {
    // Check that the stack configuration itself is valid.
    if (stack_limit_ > stack_base_) {
        return ExceptionCode::INVALID_MEMORY_ACCESS;
    }

    // SP above the stack base means the stack has been popped too far.
    if (sp > stack_base_) {
        return ExceptionCode::STACK_UNDERFLOW;
    }

    // SP below the stack limit means the stack has grown too far.
    if (sp < stack_limit_) {
        return ExceptionCode::STACK_OVERFLOW;
    }

    // SP is inside the valid stack range.
    return std::nullopt;
}

std::optional<ExceptionCode> StackGuard::checkPush(uint32_t current_sp) const {
    // A full-descending PUSH decreases SP by one word.
    // Check the new SP before changing the actual CPU SP.
    if (current_sp == 0) {
        return ExceptionCode::STACK_OVERFLOW;
    }

    const uint32_t new_sp = current_sp - 1;

    // The new SP must remain inside the stack boundary.
    if (new_sp < stack_limit_) {
        return ExceptionCode::STACK_OVERFLOW;
    }

    return std::nullopt;
}

std::optional<ExceptionCode> StackGuard::checkPop(uint32_t current_sp) const {
    // A full-descending POP increases SP by one word.
    // If SP is already at the stack base, there is nothing to pop.
    if (current_sp >= stack_base_) {
        return ExceptionCode::STACK_UNDERFLOW;
    }

    return std::nullopt;
}

bool StackGuard::isValidAddress(uint32_t address) const {
    // Stack memory uses [stack_limit, stack_base).
    // stack_base is the first address outside the stack.
    return address >= stack_limit_ &&
           address < stack_base_;
}

} // namespace risc201
