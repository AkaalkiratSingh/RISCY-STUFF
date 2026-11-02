#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace risc201 {

struct PreprocessError {
    int line_number = 0;
    std::string message;
};

struct PreprocessResult {
    std::vector<std::string> lines;
    std::vector<int> origin_line;
    std::vector<std::string> origin_context;
    std::vector<PreprocessError> errors;
};

class Preprocessor {
public:
    PreprocessResult expand(const std::vector<std::string>& source_lines);

private:
    struct SourceLine {
        std::string text;
        int line_number = 0;
    };

    struct Macro {
        std::string name;
        std::vector<std::string> params;
        std::vector<SourceLine> body;
        int definition_line = 0;
        bool builtin = false;
    };

    std::unordered_map<std::string, Macro> macros_;
    std::vector<PreprocessError> errors_;
    std::vector<std::string> macro_stack_;
    bool too_large_ = false;
    int expansion_count_ = 0;

    void loadBuiltins();
    std::string context() const;
    void addError(int line_num, std::string msg);
    std::vector<SourceLine> collect(const std::vector<std::string>& source_lines);
    bool parseMacroHeader(const std::string& header_rest, int line_num, Macro& out);
    void emit(PreprocessResult& out, const std::string& text, int origin_line);
    void expandLine(const std::string& text, int origin_line, int depth, PreprocessResult& out);
};

} // namespace risc201