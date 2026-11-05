#pragma once

#include "assembler/assembler.h"
#include "isa201.h"

#include <cstdint>
#include <string>
#include <vector>

namespace risc201 {

enum class SourceKind { Assembly, Words };

struct AssemblyResult {
    std::vector<uint32_t> words;
    std::vector<AssemblyError> errors;
    std::vector<AssemblyError> warnings;
};

struct ProgramFile {
    SourceKind kind = SourceKind::Assembly;
    std::string path;
    std::vector<std::string> lines;
    std::vector<uint32_t> words;
    std::string error;
};

AssemblyResult assembleLines(const std::vector<std::string>& lines);
std::string formatAssemblyErrors(const std::vector<AssemblyError>& errors);
std::string formatAssemblyWarnings(const std::vector<AssemblyError>& warnings);
ProgramFile readProgramFile(const std::string& path);

} // namespace risc201
