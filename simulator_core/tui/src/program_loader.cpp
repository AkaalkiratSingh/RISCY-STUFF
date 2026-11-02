#include "tui/program_loader.h"

#include "assembler/preprocessor.h"
#include "simulator_core/decoder.h"
#include "utils/utils.h"

#include <fstream>

namespace risc201 {

namespace {

std::string normalizePath(const std::string& raw) {
    std::string path = trim(raw);
    if (!path.empty() && (path.front() == '"' || path.front() == '\'') && path.front() == path.back()) {
        path = path.substr(1, path.size() - 2);
    }
    return path;
}

} // namespace

AssemblyResult assembleLines(const std::vector<std::string>& lines) {
    Preprocessor preprocessor;
    const PreprocessResult expanded = preprocessor.expand(lines);

    AssemblyResult result;
    if (!expanded.errors.empty()) {
        for (const auto& err : expanded.errors) {
            result.errors.push_back(AssemblyError{err.line_number, err.message});
        }
        return result;
    }

    auto originalLine = [&](int n) {
        if (n >= 1 && n <= static_cast<int>(expanded.origin_line.size())) {
            return expanded.origin_line[static_cast<size_t>(n - 1)];
        }
        return n;
    };
    auto contextOf = [&](int n) -> std::string {
        if (n >= 1 && n <= static_cast<int>(expanded.origin_context.size()) &&
            !expanded.origin_context[static_cast<size_t>(n - 1)].empty()) {
            return " (" + expanded.origin_context[static_cast<size_t>(n - 1)] + ")";
        }
        return "";
    };

    Assembler assembler;
    assembler.assemblePass1(expanded.lines);
    const std::vector<Instruction> program = assembler.assemblePass2();

    for (const auto& err : assembler.errors()) {
        result.errors.push_back(
            AssemblyError{originalLine(err.line_number), err.message + contextOf(err.line_number)});
    }
    if (result.errors.empty()) {
        for (const auto& instr : program) {
            result.words.push_back(assembler.encodeInstruction(instr));
        }
        for (uint32_t word : assembler.literalSection()) {
            result.words.push_back(word);
        }
    }
    return result;
}

ProgramFile readProgramFile(const std::string& raw_path) {
    ProgramFile file;
    const std::string path = normalizePath(raw_path);
    file.path = path;
    if (path.empty()) {
        file.error = "empty file path";
        return file;
    }

    if (hasSuffix(path, ".hex")) {
        if (!readHexWords(path, file.words)) {
            file.error = "failed to read hex file " + path;
            return file;
        }
    } else if (hasSuffix(path, ".bin")) {
        if (!readBinaryWords(path, file.words)) {
            file.error = "failed to read binary file " + path;
            return file;
        }
    } else {
        std::ifstream in(path);
        if (!in) {
            file.error = "cannot open file " + path;
            return file;
        }
        std::string line;
        while (std::getline(in, line)) {
            file.lines.push_back(line);
        }
        return file;
    }

    file.kind = SourceKind::Words;
    for (uint32_t word : file.words) {
        if (word == kLiteralsMarker) break;
        file.lines.push_back(formatInstruction(decodeInstruction(word)));
    }
    return file;
}

} // namespace risc201
