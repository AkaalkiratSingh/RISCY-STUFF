// risc201-sim shell: load program, run, print register file, and dispatch
// to the Debugger for step/continue/breakpoint/watch/reset commands.

#pragma once

#include "simulator_core/cpu.h"
#include "simulator_core/debugger.h"
#include <string>

namespace risc201 {

class Cli {
public:
    Cli();

    int run(int argc, char** argv);

private:
    void handleCommand(const std::string& line);
    // TODO(Akaal, Sept 30): `load`, `run`, `step`, `registers`.
    // TODO(Oct 14): `break`, `watch`, `continue`, `memdump`, pipeline-select flag.
};

} // namespace risc201
