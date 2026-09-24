#include "memory_safety/stack_guard.h"

namespace risc201 {

std::optional<ExceptionCode> StackGuard::checkBounds(uint32_t sp) const {
    // TODO(Vaibhav, Sept 30 milestone): full-descending bounds check.
    if (sp > stack_base_) return ExceptionCode::STACK_UNDERFLOW;
    if (sp < stack_limit_) return ExceptionCode::STACK_OVERFLOW;
    return std::nullopt;
}

} // namespace risc201
