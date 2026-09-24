#include "simulator_core/debugger.h"
#include <iostream>

namespace risc201 {

void Debugger::stepInstruction() { cpu_.step(); }

void Debugger::continueExecution() {
    // TODO(Akaal): run until halt, exception, or a hit breakpoint.
}

void Debugger::reset() {
    // TODO(Akaal): reload program, reset CpuState.
}

void Debugger::printRegisters() const {
    for (int i = 0; i < kNumGPRegisters; ++i) {
        std::cout << "R" << i << " = " << cpu_.state().registers[i] << "\n";
    }
}

void Debugger::printMemory(uint32_t /*start*/, uint32_t /*count*/) const {
    // TODO(Akaal): print a word range from cpu_.memory().
}

} // namespace risc201
