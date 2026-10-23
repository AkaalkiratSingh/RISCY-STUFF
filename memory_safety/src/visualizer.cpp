#include "memory_safety/visualizer.h"
#include <algorithm>
#include <sstream>

namespace risc201 {

std::string StackVisualizer::render(const std::vector<int32_t>& memory,
                                    uint32_t sp,
                                    uint32_t stack_base) const {
    std::ostringstream out;

    out << "Stack (top -> bottom)\n";
    out << "SP = " << sp << "\n";

    // Show the valid stack region from the current SP up to stack_base.
    const uint32_t start = std::min(
        sp, static_cast<uint32_t>(memory.size()));

    const uint32_t end = std::min(
        stack_base, static_cast<uint32_t>(memory.size()));

    if (start >= end) {
        out << "<empty>\n";
        return out.str();
    }

    for (uint32_t address = start; address < end; ++address) {
        out << "[" << address << "] "
            << memory[address];

        if (address == sp) {
            out << "  <-- SP";
        }

        out << "\n";
    }

    return out.str();
}

} // namespace risc201
