#include <iostream>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: risc201-disasm <input.bin>\n";
        return 1;
    }
    // TODO(Divyansh): wire up Disassembler -> stdout / file writer.
    std::cout << "risc201-disasm: not yet implemented\n";
    return 0;
}
