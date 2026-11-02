#include "tui/program_loader.h"

#include "assembler/preprocessor.h"
#include "simulator_core/decoder.h"
#include "utils/utils.h"

#include <fstream>

namespace risc201 {

namespace {

bool hasSuffix(const std::string& path, const std::string& suffix) {
    return path.size() >= suffix.size() &&
           path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string normalizePath(const std::string& raw) {
    std::string path = trim(raw);
    if (!path.empty() && (path.front() == '"' || path.front() == '\'') && path.front() == path.back()) {
        path = path.substr(1, path.size() - 2);
    }
    return path;
}

bool readHexWords(const std::string& path, std::vector<uint32_t>& out) {
    std::ifstream in(path);
    if (!in) return false;

    std::string line;
    while (std::getline(in, line)) {
        std::string token = trim(line);
        if (token.empty()) continue;
        if (token.rfind("0x", 0) == 0 || token.rfind("0X", 0) == 0) {
            token = token.substr(2);
        }
        try {
            size_t consumed = 0;
            const unsigned long value = std::stoul(token, &consumed, 16);
            if (consumed != token.size() || value > 0xFFFFFFFFul) return false;
            out.push_back(static_cast<uint32_t>(value));
        } catch (...) {
            return false;
        }
    }
    return true;
}

bool readBinaryWords(const std::string& path, std::vector<uint32_t>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;

    unsigned char bytes[4];
    while (true) {
        in.read(reinterpret_cast<char*>(bytes), 4);
        const std::streamsize got = in.gcount();
        if (got == 0) break;
        if (got != 4) return false;
        out.push_back((static_cast<uint32_t>(bytes[0]) << 24) |
                      (static_cast<uint32_t>(bytes[1]) << 16) |
                      (static_cast<uint32_t>(bytes[2]) << 8) |
                      static_cast<uint32_t>(bytes[3]));
    }
    return true;
}

std::string formatReg(int r) {
    return "R" + std::to_string(r);
}

} // namespace

std::string formatInstr(const Instruction& in) {
    const std::string name = opcodeName(in.opcode);
    const int rd  = static_cast<int>(in.rd);
    const int rs1 = static_cast<int>(in.rs1);
    const int rs2 = static_cast<int>(in.rs2);

    switch (in.opcode) {
        case Opcode::NOP:
        case Opcode::RET:
        case Opcode::HALT:
            return name;
        case Opcode::JMP:
        case Opcode::BEQ:
        case Opcode::CALL:
            return name + " " + std::to_string(in.imm);
        case Opcode::LOAD:
        case Opcode::STORE:
            return name + " " + formatReg(rd) + ", " + std::to_string(in.imm) +
                   "[" + formatReg(rs1) + "]";
        default: {
            const std::string third = in.I ? std::to_string(in.imm) : formatReg(rs2);
            return name + " " + formatReg(rd) + ", " + formatReg(rs1) + ", " + third;
        }
    }
}

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
        file.lines.push_back(formatInstr(decodeInstruction(word)));
    }
    return file;
}

} // namespace risc201
