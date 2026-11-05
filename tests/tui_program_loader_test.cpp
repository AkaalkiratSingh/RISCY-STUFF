#include "tui/program_loader.h"

#include <iostream>
#include <string>
#include <vector>

int main() {
    const risc201::AssemblyResult result = risc201::assembleLines({
        ".main:",
        "ADD R1, R99, R0",
        "ADD R2, R88, R0",
        "HALT",
    });
    if (result.errors.size() < 2) {
        std::cerr << "expected at least two assembly errors\n";
        return 1;
    }
    const std::string feedback = risc201::formatAssemblyErrors(result.errors);
    for (const auto& error : result.errors) {
        const std::string entry = "Line " + std::to_string(error.line_number) +
                                  ": " + error.message;
        if (feedback.find(entry) == std::string::npos) {
            std::cerr << "missing feedback entry: " << entry << "\n";
            return 1;
        }
    }
    if (feedback.find(std::to_string(result.errors.size()) + " error(s)") == std::string::npos) {
        std::cerr << "missing error count\n";
        return 1;
    }
    return 0;
}
