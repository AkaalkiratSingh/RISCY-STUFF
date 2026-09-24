#include <iostream>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: risc201-asm <input.asm> [-o output.bin]\n";
        return 1;
    }
    // TODO(Divyansh): wire up Preprocessor -> Assembler -> output writer.
    std::cout << "risc201-asm: not yet implemented\n";
    return 0;
}
