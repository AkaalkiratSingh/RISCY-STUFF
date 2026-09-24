// ASCII visualizer for stack memory state: SP position, frame boundaries,
// push/pop animations (frame-by-frame ASCII diffs) — PRD §5.5.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace risc201 {

class StackVisualizer {
public:
    std::string render(const std::vector<int32_t>& memory,
                        uint32_t sp, uint32_t stack_base) const;
};

} // namespace risc201
