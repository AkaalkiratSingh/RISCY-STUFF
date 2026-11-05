#include "assembler/preprocessor.h"
#include "assembler/op_table.h"
#include "utils/utils.h"

#include <cctype>

namespace risc201 {

namespace {

constexpr int kMaxExpansionDepth = 16;
constexpr size_t kMaxOutputLines = 100000;
constexpr size_t kMaxMacroParams = 64;

const char* const kBuiltinMacros[] = {
    ".macro PUSH reg",
    "    SUB sp, sp, 1",
    "    STORE reg, 0[sp]",
    ".endm",
    ".macro POP reg",
    "    LOAD reg, 0[sp]",
    "    ADD sp, sp, 1",
    ".endm",
};

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

bool looksLikeRegister(const std::string& s) {
    Register alias;
    if (parseRegisterAlias(s, alias)) return true;
    std::string u = toUpper(s);
    if (u.size() < 2 || u[0] != 'R') return false;
    for (size_t i = 1; i < u.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(u[i]))) return false;
    }
    return true;
}

bool isTokenChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '@' || c == '.';
}

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

std::vector<std::string> localTokens(const std::string& text) {
    std::vector<std::string> found;
    size_t i = 0;
    while (i < text.size()) {
        if (!isTokenChar(text[i])) {
            ++i;
            continue;
        }
        size_t j = i;
        while (j < text.size() && isTokenChar(text[j])) ++j;
        if (text[i] == '@') found.push_back(text.substr(i, j - i));
        i = j;
    }
    return found;
}

std::string renameLocalTokens(const std::string& text, const std::string& prefix) {
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

        if (token[0] == '@') out += prefix + token.substr(1);
        else out += token;
        i = j;
    }
    return out;
}

}

void Preprocessor::loadBuiltins() {
    std::vector<std::string> lines(std::begin(kBuiltinMacros), std::end(kBuiltinMacros));
    collect(lines);
    for (auto& entry : macros_) entry.second.builtin = true;
}

std::string Preprocessor::context() const {
    if (macro_stack_.empty()) return "";

    std::string text = "in expansion of '" + macro_stack_.back() + "'";
    for (int i = static_cast<int>(macro_stack_.size()) - 2; i >= 0; --i) {
        text += ", called from '" + macro_stack_[i] + "'";
    }
    return text;
}

void Preprocessor::addError(int line_num, std::string msg) {
    std::string where = context();
    if (!where.empty()) msg += " (" + where + ")";
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
            if (existing->second.builtin) addError(line_num, "macro '" + out.name + "' is built in and cannot be redefined");
            else addError(line_num, "macro '" + out.name + "' is already defined on line " + std::to_string(existing->second.definition_line));
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

std::vector<Preprocessor::SourceLine> Preprocessor::collect(const std::vector<std::string>& source_lines) {
    std::vector<SourceLine> stream;

    bool in_macro = false;
    bool in_literals = false;
    bool current_ok = true;
    Macro current;
    std::vector<std::pair<std::string, int>> local_defs;
    std::vector<std::pair<std::string, int>> local_uses;

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
            if (in_literals) {
                addError(line_num, "'.macro' is not allowed after '.literals:' (that section holds only literals)");
                current_ok = false;
            }
            local_defs.clear();
            local_uses.clear();
            continue;
        }

        if (directive == ".endm") {
            if (!in_macro) {
                addError(line_num, "'.endm' without a matching '.macro'");
                continue;
            }
            if (!rest.empty()) addError(line_num, "unexpected text after '.endm'");

            for (const auto& use : local_uses) {
                bool defined = false;
                for (const auto& def : local_defs) {
                    if (def.first == use.first) defined = true;
                }
                if (!defined) {
                    addError(use.second, "local label '" + use.first + "' is used but never defined in macro '" + current.name + "'");
                }
            }

            in_macro = false;
            if (current_ok) macros_[current.name] = std::move(current);
            continue;
        }

        if (in_macro) {
            if (line.empty()) continue;

            std::string probe = line;
            std::string label;
            if (extractLabel(probe, label)) {
                if (label[0] == '.') {
                    addError(line_num, "label '" + label + "' inside macro '" + current.name + "' cannot start with '.'");
                }
                else if (label[0] == '@') {
                    bool duplicate = false;
                    for (const auto& def : local_defs) {
                        if (def.first == label) duplicate = true;
                    }
                    if (!isIdentifier(label.substr(1))) addError(line_num, "invalid local label '" + label + "'");
                    else if (duplicate) addError(line_num, "local label '" + label + "' is defined twice in macro '" + current.name + "'");
                    else local_defs.push_back({label, line_num});
                }
            }

            for (const auto& token : localTokens(probe)) {
                if (!isIdentifier(token.substr(1))) addError(line_num, "invalid local label '" + token + "'");
                else local_uses.push_back({token, line_num});
            }

            current.body.push_back(SourceLine{line, line_num});
            continue;
        }

        if (!in_literals) {
            std::string probe = line;
            std::string label;
            if (extractLabel(probe, label) && label == ".literals") in_literals = true;
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
    out.origin_context.push_back(context());
}

void Preprocessor::expandLine(const std::string& text, int origin_line, int depth, PreprocessResult& out) {
    std::string line = stripCommentandTrim(text);
    if (line.empty()) {
        emit(out, text, origin_line);
        return;
    }

    if (depth == 0) {
        std::vector<std::string> stray = localTokens(line);
        if (!stray.empty()) {
            addError(origin_line, "local label '" + stray[0] + "' can only be used inside a macro body");
            return;
        }
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

    if (has_label) emit(out, label + ":", origin_line);
    std::string local_prefix = "__" + macro.name + "_" + std::to_string(++expansion_count_) + "_";

    macro_stack_.push_back(macro.name);

    for (const auto& body_line : macro.body) {
        std::string body = body_line.text;

        std::string body_label;
        bool body_has_label = extractLabel(body, body_label);
        if (body_has_label && body_label[0] == '@') body_label = local_prefix + body_label.substr(1);

        std::string rebuilt;
        if (body_has_label) rebuilt = body_label + ":";

        if (!body.empty()) {
            std::string body_op, body_rest;
            splitFirstToken(body, body_op, body_rest);

            if (!rebuilt.empty()) rebuilt += " ";
            rebuilt += body_op;
            if (!body_rest.empty()) {
                rebuilt += " " + renameLocalTokens(substituteTokens(body_rest, macro.params, args), local_prefix);
            }
        }

        expandLine(rebuilt, origin_line, depth + 1, out);
        if (too_large_) break;
    }
    macro_stack_.pop_back();
}

PreprocessResult Preprocessor::expand(const std::vector<std::string>& source_lines) {
    macros_.clear();
    errors_.clear();
    macro_stack_.clear();
    too_large_ = false;
    expansion_count_ = 0;

    loadBuiltins();

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

        if (in_literals) emit(result, src.text, src.line_number);
        else expandLine(src.text, src.line_number, 0, result);
    }

    result.errors = errors_;
    return result;
}

} // namespace risc201