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
    const risc201::AssemblyResult warned = risc201::assembleLines({
        ".main:",
        "ADD R1, R0, 200000",
        "HALT",
    });
    if (!warned.errors.empty() || warned.warnings.size() != 1 || warned.words.empty() ||
        warned.warnings[0].line_number != 2) {
        std::cerr << "expected a successful assembly with one line-mapped warning\n";
        return 1;
    }
    const std::string warning_feedback = risc201::formatAssemblyWarnings(warned.warnings);
    if (warning_feedback.find("Warnings (1):\nLine 2: value 200000 out of range") == std::string::npos) {
        std::cerr << "missing warning feedback\n";
        return 1;
    }
    const risc201::AssemblyResult mixed = risc201::assembleLines({
        ".main:",
        "ADD R1, R0, 200000",
        "ADD R2, R99, R0",
        "HALT",
    });
    if (mixed.errors.empty() || mixed.warnings.size() != 1 || !mixed.words.empty()) {
        std::cerr << "expected both an error and a warning\n";
        return 1;
    }
    return 0;
}
