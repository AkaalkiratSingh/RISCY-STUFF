#include "assembler/preprocessor.h"
#include "assembler/op_table.h"
#include "utils/utils.h"

#include <cctype>

namespace risc201 {

namespace {

constexpr int kMaxExpansionDepth = 16;
constexpr size_t kMaxOutputLines = 100000;
constexpr size_t kMaxMacroParams = 64;

// "ADD R1, R2" -> first = "ADD", rest = "R1, R2"
void splitFirstToken(const std::string& line, std::string& first, std::string& rest) {
    size_t sp = line.find_first_of(" \t");
    if (sp == std::string::npos) {
        first = line;
        rest.clear();
        return;
    }
    first = line.substr(0, sp);
    size_t start = line.find_first_not_of(" \t", sp);
    if (start == std::string::npos) rest.clear();
    else rest = line.substr(start);
}

// Splits on commas and trims each piece. Empty pieces are kept so the caller can report them.
std::vector<std::string> splitOperands(const std::string& rest) {
    std::vector<std::string> out;
    if (rest.empty()) return out;

    size_t start = 0;
    while (true) {
        size_t comma = rest.find(',', start);
        std::string tok = rest.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        out.push_back(trim(tok));
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return out;
}

bool isIdentifier(const std::string& s) {
    if (s.empty()) return false;
    if (!(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_')) return false;
    for (char c : s) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
    }
    return true;
}

// R0, R15, R99, r3 ... and SP. Parameters named like this would be confusing.
bool looksLikeRegister(const std::string& s) {
    std::string u = toUpper(s);
    if (u == "SP") return true;
    if (u.size() < 2 || u[0] != 'R') return false;
    for (size_t i = 1; i < u.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(u[i]))) return false;
    }
    return true;
}

bool isTokenChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '@' || c == '.';
}

// Replaces whole tokens only: parameter "a" never rewrites part of "data" or "R1a".
std::string substituteTokens(const std::string& text, const std::vector<std::string>& params, const std::vector<std::string>& args) {
    std::string out;
    size_t i = 0;
    while (i < text.size()) {
        if (!isTokenChar(text[i])) {
            out += text[i++];
            continue;
        }
        size_t j = i;
        while (j < text.size() && isTokenChar(text[j])) ++j;
        std::string token = text.substr(i, j - i);

        bool replaced = false;
        for (size_t p = 0; p < params.size(); ++p) {
            if (token == params[p]) {
                out += args[p];
                replaced = true;
                break;
            }
        }
        if (!replaced) out += token;
        i = j;
    }
    return out;
}

} // namespace

void Preprocessor::addError(int line_num, std::string msg) {
    errors_.push_back(PreprocessError{line_num, std::move(msg)});
}

bool Preprocessor::parseMacroHeader(const std::string& header_rest, int line_num, Macro& out) {
    std::string name_token, params_text;
    splitFirstToken(header_rest, name_token, params_text);

    if (name_token.empty()) {
        addError(line_num, "'.macro' needs a name");
        return false;
    }

    out.name = toUpper(name_token);
    bool ok = true;

    if (!isIdentifier(name_token)) {
        addError(line_num, "invalid macro name '" + name_token + "'");
        ok = false;
    }
    else if (opTable().count(out.name)) {
        addError(line_num, "macro name '" + out.name + "' clashes with an instruction mnemonic");
        ok = false;
    }
    else {
        auto existing = macros_.find(out.name);
        if (existing != macros_.end()) {
            addError(line_num, "macro '" + out.name + "' is already defined on line " + std::to_string(existing->second.definition_line));
            ok = false;
        }
    }

    for (const auto& param : splitOperands(params_text)) {
        if (param.empty()) {
            addError(line_num, "empty parameter name in '.macro " + name_token + "'");
            ok = false;
        }
        else if (!isIdentifier(param)) {
            addError(line_num, "invalid parameter name '" + param + "'");
            ok = false;
        }
        else if (looksLikeRegister(param)) {
            addError(line_num, "parameter '" + param + "' looks like a register name");
            ok = false;
        }
        else {
            bool duplicate = false;
            for (const auto& existing_param : out.params) {
                if (existing_param == param) duplicate = true;
            }
            if (duplicate) {
                addError(line_num, "duplicate parameter '" + param + "'");
                ok = false;
            }
            else out.params.push_back(param);
        }
    }

    if (out.params.size() > kMaxMacroParams) {
        addError(line_num, "macro '" + out.name + "' has too many parameters (max " + std::to_string(kMaxMacroParams) + ")");
        ok = false;
    }

    return ok;
}

// Pass over the whole file: pulls every .macro ... .endm block into macros_ and
// returns the remaining lines. Because this runs before any expansion, a macro
// can be used above the place where it is defined.
std::vector<Preprocessor::SourceLine> Preprocessor::collect(const std::vector<std::string>& source_lines) {
    std::vector<SourceLine> stream;

    bool in_macro = false;
    bool current_ok = true;
    Macro current;

    for (size_t i = 0; i < source_lines.size(); ++i) {
        int line_num = static_cast<int>(i) + 1;
        std::string line = stripCommentandTrim(source_lines[i]);

        std::string first, rest;
        splitFirstToken(line, first, rest);
        std::string directive = toLower(first);

        if (directive == ".macro") {
            if (in_macro) {
                addError(line_num, "'.macro' inside macro '" + current.name + "' (macros cannot be nested in a definition)");
                continue;
            }
            in_macro = true;
            current = Macro{};
            current.definition_line = line_num;
            current_ok = parseMacroHeader(rest, line_num, current);
            continue;
        }

        if (directive == ".endm") {
            if (!in_macro) {
                addError(line_num, "'.endm' without a matching '.macro'");
                continue;
            }
            if (!rest.empty()) addError(line_num, "unexpected text after '.endm'");
            in_macro = false;
            if (current_ok) macros_[current.name] = std::move(current);
            continue;
        }

        if (in_macro) {
            if (line.empty()) continue;

            std::string probe = line;
            std::string label;
            if (extractLabel(probe, label) && label[0] == '.') {
                addError(line_num, "label '" + label + "' inside macro '" + current.name + "' cannot start with '.'");
            }
            current.body.push_back(SourceLine{line, line_num});
            continue;
        }

        stream.push_back(SourceLine{source_lines[i], line_num});
    }

    if (in_macro) {
        addError(current.definition_line, "'.macro " + current.name + "' has no matching '.endm'");
    }

    return stream;
}

void Preprocessor::emit(PreprocessResult& out, const std::string& text, int origin_line) {
    if (too_large_) return;
    if (out.lines.size() >= kMaxOutputLines) {
        addError(origin_line, "macro expansion is too large (over " + std::to_string(kMaxOutputLines) + " lines)");
        too_large_ = true;
        return;
    }
    out.lines.push_back(text);
    out.origin_line.push_back(origin_line);
}

// Expands one line. If it calls a macro, the macro body is substituted and each
// resulting line is expanded again, so macros can call other macros.
void Preprocessor::expandLine(const std::string& text, int origin_line, int depth, PreprocessResult& out) {
    std::string line = stripCommentandTrim(text);
    if (line.empty()) {
        emit(out, text, origin_line);
        return;
    }

    std::string label;
    bool has_label = extractLabel(line, label);

    if (line.empty()) {
        emit(out, text, origin_line);
        return;
    }

    std::string op_token, rest;
    splitFirstToken(line, op_token, rest);

    auto found = macros_.find(toUpper(op_token));
    if (found == macros_.end()) {
        emit(out, text, origin_line);
        return;
    }

    const Macro& macro = found->second;

    if (depth >= kMaxExpansionDepth) {
        addError(origin_line, "macro '" + macro.name + "' expands too deeply (recursive macro?)");
        return;
    }

    std::vector<std::string> args = splitOperands(rest);
    for (const auto& arg : args) {
        if (arg.empty()) {
            addError(origin_line, "empty argument in call to '" + macro.name + "'");
            return;
        }
    }
    if (args.size() != macro.params.size()) {
        addError(origin_line, "macro '" + macro.name + "' expects " + std::to_string(macro.params.size()) + " argument(s), got " + std::to_string(args.size()));
        return;
    }

    // A label on the call line goes on its own line, in front of the expansion.
    if (has_label) emit(out, label + ":", origin_line);

    for (const auto& body_line : macro.body) {
        std::string body = body_line.text;

        std::string body_label;
        bool body_has_label = extractLabel(body, body_label);

        std::string rebuilt;
        if (body_has_label) rebuilt = body_label + ":";

        if (!body.empty()) {
            std::string body_op, body_rest;
            splitFirstToken(body, body_op, body_rest);

            if (!rebuilt.empty()) rebuilt += " ";
            rebuilt += body_op;
            if (!body_rest.empty()) {
                rebuilt += " " + substituteTokens(body_rest, macro.params, args);
            }
        }

        expandLine(rebuilt, origin_line, depth + 1, out);
        if (too_large_) return;
    }
}

PreprocessResult Preprocessor::expand(const std::vector<std::string>& source_lines) {
    macros_.clear();
    errors_.clear();
    too_large_ = false;

    PreprocessResult result;
    std::vector<SourceLine> stream = collect(source_lines);

    bool in_literals = false;
    for (const auto& src : stream) {
        if (too_large_) break;

        if (!in_literals) {
            std::string probe = stripCommentandTrim(src.text);
            std::string label;
            if (extractLabel(probe, label) && label == ".literals") in_literals = true;
        }

        if (in_literals) emit(result, src.text, src.line_number);       // literals are never macro-expanded
        else expandLine(src.text, src.line_number, 0, result);
    }

    result.errors = errors_;
    return result;
}

} // namespace risc201