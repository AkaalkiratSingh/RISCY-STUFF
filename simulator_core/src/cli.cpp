#include "simulator_core/cli.h"
#include <iostream>

namespace risc201 {

Cli::Cli() = default;

int Cli::run(int /*argc*/, char** /*argv*/) {
    std::cout << "risc201-sim (Core Engine) -- type 'help' for commands\n";
    // TODO(Akaal): REPL loop reading commands and dispatching to Debugger.
    return 0;
}

void Cli::handleCommand(const std::string& /*line*/) {
    // TODO(Akaal): parse load/run/step/registers/break/watch/continue/memdump.
}

} // namespace risc201
