#include "memory_safety/visualizer.h"

namespace risc201 {

std::string StackVisualizer::render(const std::vector<int32_t>& /*memory*/,
                                     uint32_t /*sp*/, uint32_t /*stack_base*/) const {
    // TODO(Vaibhav, Oct 14 milestone): render stack contents / SP / frames.
    return "";
}

} // namespace risc201
