#include "simulator_core/cli.h"

int main(int argc, char** argv) {
    risc201::Cli cli;
    return cli.run(argc, argv);
}
