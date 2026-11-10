#include "tui/tui.h"

#include "simulator_core/decoder.h"
#include "tui/program_loader.h"
#include "utils/utils.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <thread>
#include <optional>

namespace risc201 {

namespace {

std::vector<std::string> wrapText(const std::string& text, int width) {
    if (width <= 0) width = 1;
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = std::min(text.size(), start + static_cast<size_t>(width));
        if (end < text.size()) {
            const size_t space = text.rfind(' ', end);
            if (space != std::string::npos && space > start) end = space;
        }
        lines.push_back(text.substr(start, end - start));
        start = end;
        while (start < text.size() && text[start] == ' ') ++start;
    }
    if (lines.empty()) lines.push_back("");
    return lines;
}

// Splits explicit newlines first, then wraps each paragraph to the width.
std::vector<std::string> wrapTextLines(const std::string& text, int width) {
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        const size_t end = text.find('\n', start);
        const std::string part = end == std::string::npos
            ? text.substr(start)
            : text.substr(start, end - start);
        const std::vector<std::string> wrapped = wrapText(part, width);
        out.insert(out.end(), wrapped.begin(), wrapped.end());
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return out;
}

Style paneTitleStyle() {
    return Style{Color::BrightCyan, Color::Default, true};
}

Style selectedRowStyle() {
    return Style{Color::BrightWhite, Color::BrightBlack, true};
}

void drawSelectedLine(Screen& screen, int r, int c, const std::string& text, int width) {
    const Style style = selectedRowStyle();
    screen.fill(r, c, 1, width, " ", style);
    screen.drawText(r, c, text, style, width);
}

struct WrappedSegment {
    size_t offset = 0;
    std::string text;
};

// Breaks at spaces where possible; hard-breaks tokens longer than the width.
std::vector<WrappedSegment> wrapLine(const std::string& line, size_t width) {
    std::vector<WrappedSegment> out;
    if (width == 0) {
        out.push_back({0, line});
        return out;
    }

    size_t start = 0;
    while (start < line.size()) {
        size_t take = std::min(width, line.size() - start);
        if (start + take < line.size()) {
            const size_t space = line.rfind(' ', start + take);
            if (space != std::string::npos && space >= start) {
                take = space - start;
                if (take == 0) take = 1;
            }
        }
        out.push_back({start, line.substr(start, take)});
        start += take;
        while (start < line.size() && line[start] == ' ') ++start;
    }
    if (out.empty()) out.push_back({0, ""});
    return out;
}

int stageOrder(const char* stage) {
    if (!stage) return 99;
    const std::string name(stage);
    if (name == "MEM+WB") return 0;
    if (name == "WB") return 0;
    if (name == "MEM") return 1;
    if (name == "EX") return 2;
    if (name == "OF") return 3;
    if (name == "FETCH") return 3;
    if (name == "ID") return 4;
    if (name == "IF+ID") return 4;
    if (name == "IF") return 5;
    return 99;
}

// " ◄[WB 03]" for one stage, or " <[WB 03] [IF 77]>" when the same PC is
// active in several pipeline stages at once.
std::string controlTags(uint32_t pc, const std::vector<MicroPcView>& views) {
    struct Tag {
        int order = 0;
        std::string text;
    };

    std::vector<Tag> tags;
    for (const MicroPcView& view : views) {
        if (view.pc != pc) continue;
        std::ostringstream text;
        text << '[' << view.stage << ' ' << std::right << std::setw(2)
             << std::setfill('0') << view.micro_pc << ']';
        tags.push_back({stageOrder(view.stage), text.str()});
    }
    if (tags.empty()) return "";

    std::stable_sort(tags.begin(), tags.end(),
                     [](const Tag& a, const Tag& b) { return a.order < b.order; });
    if (tags.size() == 1) return " ◄" + tags.front().text;

    std::string out = " <";
    for (size_t i = 0; i < tags.size(); ++i) {
        if (i != 0) out += " ";
        out += tags[i].text;
    }
    out += ">";
    return out;
}

std::optional<Register> parseRegisterName(const std::string& s) {
    if (s.size() < 2 || s.size() > 3) return std::nullopt;
    if (s[0] != 'R' && s[0] != 'r') return std::nullopt;
    if (s.find_first_not_of("0123456789", 1) != std::string::npos) return std::nullopt;
    const unsigned long n = std::stoul(s.substr(1));
    if (n >= static_cast<unsigned long>(kNumGPRegisters)) return std::nullopt;
    return static_cast<Register>(n);
}

std::optional<CompareOp> parseCompareOp(const std::string& s) {
    if (s == "==" || s == "=") return CompareOp::Eq;
    if (s == "!=") return CompareOp::Ne;
    if (s == "<") return CompareOp::Lt;
    if (s == "<=") return CompareOp::Le;
    if (s == ">") return CompareOp::Gt;
    if (s == ">=") return CompareOp::Ge;
    return std::nullopt;
}

const char* compareOpSymbol(CompareOp op) {
    switch (op) {
    case CompareOp::Eq: return "==";
    case CompareOp::Ne: return "!=";
    case CompareOp::Lt: return "<";
    case CompareOp::Le: return "<=";
    case CompareOp::Gt: return ">";
    case CompareOp::Ge: return ">=";
    }
    return "?";
}

std::string formatCondition(const Condition& c) {
    return "R" + std::to_string(static_cast<int>(c.reg)) + " " + compareOpSymbol(c.op) + " " + std::to_string(c.value);
}

std::optional<Condition> parseCondition(std::istringstream& iss) {
    std::string reg_tok, op_tok, val_tok;
    if (!(iss >> reg_tok >> op_tok >> val_tok)) return std::nullopt;

    const std::optional<Register> reg = parseRegisterName(reg_tok);
    const std::optional<CompareOp> op = parseCompareOp(op_tok);
    if (!reg || !op) return std::nullopt;

    long long value = 0;
    try {
        size_t used = 0;
        value = std::stoll(val_tok, &used, 0);
        if (used != val_tok.size()) return std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
    if (value < INT32_MIN || value > INT32_MAX) return std::nullopt;

    Condition c;
    c.reg = *reg;
    c.op = *op;
    c.value = static_cast<int32_t>(value);
    return c;
}

struct CommandSpec {
    const char* name;
    const char* usage;
    const char* group;
};

constexpr CommandSpec kCommands[] = {
    { "load",       ":load <path>",                                          "files" },
    { "assemble",   ":assemble (:asm)",                                      "files" },
    { "step",       ":step [n]",                                             "run" },
    { "clock",      ":clock [n]",                                            "run" },
    { "run",        ":run",                                                  "run" },
    { "break",      ":break <pc> [if <Rn> <op> <val>]",                      "breaks" },
    { "cond",       ":cond <pc> [<Rn> <op> <val>]",                          "breaks" },
    { "clearbreak", ":clearbreak",                                           "breaks" },
    { "breaks",     ":breaks",                                               "breaks" },
    { "enable",     ":enable break <pc>|watch <id>",                         "toggle" },
    { "disable",    ":disable break <pc>|watch <id>",                        "toggle" },
    { "watch",      ":watch mem <addr>|reg <Rn>",                            "watch" },
    { "watches",    ":watches",                                              "watch" },
    { "unwatch",    ":unwatch <id>|all",                                     "watch" },
    { "reset",      ":reset",                                                "run" },
    { "save",       ":save [path]",                                          "files" },
    { "config",     ":config [stage 4|6] [rom horiz|vert] [forward on|off] [binary decimal|hex|float]", "config" },
    { "trace",      ":trace",                                                "config" },
    { "mem",        ":mem <s:c>",                                            "misc" },
    { "mem_focus",  ":mem_focus <addr>",                                     "misc" },
    { "help",       ":help",                                                 "misc" },
    { "quit",       ":quit",                                                 "misc" },
};

std::string commandHelp() {
    static constexpr const char* kGroups[] = {
        "run", "files", "breaks", "watch", "toggle", "config", "misc"
    };

    std::string text = "Commands:";
    for (const char* group : kGroups) {
        text += "\n  ";
        text += group;
        for (size_t i = std::string(group).size(); i < 6; ++i) text += ' ';
        text += "  ";

        bool first = true;
        for (const CommandSpec& spec : kCommands) {
            if (std::string(spec.group) != group) continue;
            if (!first) text += "  ";
            text += spec.usage;
            first = false;
        }
    }
    text += "\nKeys: s/n/Enter step | c clock | r run | T trace | "
            "[ ] scroll trace | q quit";
    return text;
}

struct Completion {
    size_t start = 0;
    size_t len = 0;
    std::vector<std::string> matches;
};

std::string commonPrefix(const std::vector<std::string>& values) {
    if (values.empty()) return {};
    std::string prefix = values.front();
    for (const std::string& value : values) {
        size_t i = 0;
        while (i < prefix.size() && i < value.size() && prefix[i] == value[i]) ++i;
        prefix.resize(i);
    }
    return prefix;
}

Completion completeCommand(const std::string& buffer) {
    Completion result;

    const size_t space = buffer.find(' ');
    if (space != std::string::npos) {
        const std::string head = buffer.substr(0, space);
        if (head != "load" && head != "l") return result;

        const std::string typed = buffer.substr(space + 1);
        result.start = space + 1;
        result.len = typed.size();

        const std::filesystem::path path(typed);
        const std::string directory = path.parent_path().string();
        const std::string stem = path.filename().string();

        std::error_code ec;
        std::filesystem::directory_iterator it(
            directory.empty() ? std::filesystem::path(".") : std::filesystem::path(directory), ec);
        const std::filesystem::directory_iterator end;
        while (!ec && it != end) {
            const std::string name = it->path().filename().string();
            if (name.rfind(stem, 0) == 0) {
                std::string full = directory;
                if (!full.empty() && full.back() != '/') full += '/';
                full += name;
                std::error_code type_ec;
                if (it->is_directory(type_ec)) full += '/';
                result.matches.push_back(std::move(full));
            }
            it.increment(ec);
        }
        std::sort(result.matches.begin(), result.matches.end());
        return result;
    }

    result.start = 0;
    result.len = buffer.size();
    for (const CommandSpec& spec : kCommands) {
        const std::string name = spec.name;
        if (name.rfind(buffer, 0) == 0) result.matches.push_back(name);
    }
    return result;
}

} // namespace

Tui::Tui(Cpu& cpu,
         std::vector<std::string> raw_assembly,
         std::vector<uint32_t> program_words,
         const std::string& filename,
         bool show_trace)
    : cpu_(cpu),
      debugger_(cpu),
      raw_assembly_(std::move(raw_assembly)),
      code_words_(cpu_.codeWords()),
      original_words_(std::move(program_words)),
      filename_(filename),
      show_trace_(show_trace) {
    cpu_.setTracing(true);
}

std::string Tui::describeStop(const StopInfo& info) const {
    std::ostringstream os;
    switch (info.reason) {
    case StopReason::Breakpoint:
        os << "Breakpoint at PC=" << info.pc << " (not yet executed).";
        break;
    case StopReason::TargetReached:
        os << "Reached PC=" << info.pc << ".";
        break;
    case StopReason::Watchpoint:
        os << "Watchpoint triggered.";
        break;
    case StopReason::CycleLimit:
        os << "Cycle limit reached.";
        break;
    default:
        break;
    }
    for (const WatchHit& h : info.watch_hits) {
        os << " [watch #" << h.id << "] ";
        if (h.kind == WatchKind::Register) os << "R" << h.target;
        else os << "mem[" << h.target << "]";
        os << ": " << h.old_value << " -> " << h.new_value << ".";
    }
    return os.str();
}

std::string Tui::formatActiveStage(uint32_t pc) const {
    const StageLayout layout = layoutFor(cpu_.variant());
    const auto& stages = cpu_.stages();

    // Check pipeline slots
    for (size_t i = 0; i < layout.count; ++i) {
        if (i == 0) {
            if (cpu_.fetching() && cpu_.fetchPc() == pc) {
                return "IF";
            }
        } else if (i - 1 < stages.size()) {
            const Latch& l = stages[i - 1];
            if (l.valid && l.pc == pc) {
                return layout[i].name;
            }
        }
    }
    return "";
}

void Tui::run() {
    int rows = 24, cols = 80;
    Terminal::getWindowSize(rows, cols);
    screen_.resize(rows, cols);

    while (running_) {
        int cur_r = 24, cur_c = 80;
        Terminal::getWindowSize(cur_r, cur_c);
        if (cur_r != screen_.rows() || cur_c != screen_.cols()) {
            screen_.resize(cur_r, cur_c);
        }

        render();

        KeyEvent ev = terminal_.readKey(100);
        if (ev.key == Key::Eof) {
            running_ = false;
            break;
        }
        if (ev.key != Key::None) {
            handleKey(ev);
        }
    }
}

void Tui::render() {
    clampFocusedScroll();
    screen_.clear();

    const int total_rows = screen_.rows();
    const int total_cols = screen_.cols();

    const int cmd_h = 3;
    int fb_h = 2;
    if (total_rows >= 20) {
        const int fb_inner_w = std::max(1, total_cols - 4);
        const int max_fb = std::max(3, total_rows - cmd_h - 10);
        fb_h = std::max(3, std::min(
            static_cast<int>(wrapTextLines(feedback_message_, fb_inner_w).size()) + 2,
            std::min(12, max_fb)));
    }
    if (mode_ == Mode::Command && !completion_matches_.empty()) {
        const int wanted = std::min(8, static_cast<int>(completion_matches_.size()) + 3);
        const int max_fb = std::max(2, total_rows - cmd_h - 10);
        fb_h = std::min(std::max(fb_h, wanted), max_fb);
    }
    int trace_h = 0;
    if (show_trace_) {
        trace_h = std::min(7, total_rows - (cmd_h + fb_h) - 10);
        if (trace_h < 3) trace_h = 0;
    }
    const int top_h = std::max(10, total_rows - (cmd_h + fb_h + trace_h));

    const int memory_width = static_cast<int>(std::string("[   2] 1.32118362e+19").size()) + 7;
    int binary_width = 29;
    for (size_t pc = 0; pc < code_words_.size(); ++pc) {
        const int address_width = std::max(3, static_cast<int>(std::to_string(pc).size()));
        const int mnemonic_width = static_cast<int>(formatInstruction(decodeInstruction(code_words_[pc])).size());
        binary_width = std::max(binary_width, address_width + mnemonic_width + 35);
    }
    int col3_w;
    int col2_w;
    int col1_w;
    if (total_cols >= 72) {
        col3_w = std::min(28 + memory_width, std::max(28, total_cols - 18 - binary_width));
        col2_w = std::min(binary_width, std::max(26, total_cols - col3_w - 18));
        col1_w = total_cols - col2_w - col3_w;
    } else {
        col1_w = total_cols / 3;
        col2_w = total_cols / 3;
        col3_w = total_cols - col1_w - col2_w;
    }

    // Panes render
    renderRawAssembly(0, 0, top_h, col1_w);
    renderAssembledBinary(0, col1_w, top_h, col2_w);

    const int reg_cols = registerColumns(col3_w - 2);
    const int reg_rows = (kNumGPRegisters + reg_cols - 1) / reg_cols;
    const int reserve = top_h >= 14 ? 7 : 5;
    const int reg_h = std::min(reg_rows + 5, top_h - reserve);
    const int sub_h = top_h - reg_h;
    const int stack_w = std::min(memory_width, col3_w / 2);
    const int stats_w = col3_w - stack_w;
    const int rom_h = (sub_h >= 6) ? sub_h - std::min(10, std::max(3, (sub_h * 35) / 100)) : 0;
    const int stats_h = sub_h - rom_h;

    renderRegisters(0, col1_w + col2_w, reg_h, col3_w);
    renderExtraStats(reg_h, col1_w + col2_w, stats_h, stats_w);
    if (rom_h > 0) renderControlRom(reg_h + stats_h, col1_w + col2_w, rom_h, stats_w);
    renderMemory(reg_h, col1_w + col2_w + stats_w, sub_h, stack_w);

    asm_view_h_ = std::max(0, top_h - 2);
    bin_view_h_ = asm_view_h_;

    if (trace_h > 0) renderHazardTimeline(top_h, 0, trace_h, total_cols);

    // Bottom panes
    renderCommandLine(top_h + trace_h, 0, cmd_h, total_cols);
    renderFeedback(top_h + trace_h + cmd_h, 0, fb_h, total_cols);

    if (mode_ == Mode::Inspect) {
        int fr = 0;
        int fc = 0;
        int fh = 0;
        int fw = 0;
        switch (pane_) {
            case Pane::Assembly:  fr = 0;     fc = 0;                               fh = top_h;  fw = col1_w;    break;
            case Pane::Binary:    fr = 0;     fc = col1_w;                          fh = top_h;  fw = col2_w;    break;
            case Pane::Registers: fr = 0;     fc = col1_w + col2_w;                 fh = reg_h;  fw = col3_w;    break;
            case Pane::Stats:     fr = reg_h; fc = col1_w + col2_w;                 fh = stats_h; fw = stats_w;  break;
            case Pane::ControlRom: fr = reg_h + stats_h; fc = col1_w + col2_w;      fh = rom_h;   fw = stats_w;  break;
            case Pane::Memory:    fr = reg_h; fc = col1_w + col2_w + stats_w;       fh = sub_h;  fw = stack_w;   break;
            case Pane::Timeline:  fr = top_h; fc = 0;                               fh = trace_h; fw = total_cols; break;
        }
        screen_.highlightBox(fr, fc, fh, fw, Color::BrightBlack);
    }

    if (mode_ == Mode::Dialog) renderDialog();

    screen_.flush();
}

void Tui::renderRawAssembly(int r, int c, int h, int w) {
    std::string title = "RAW ASSEMBLY";
    if (!filename_.empty()) {
        title += " (" + filename_ + ")";
    }
    screen_.drawBox(r, c, h, w, title,
                    Style{Color::White, Color::Default, false},
                    paneTitleStyle());

    const int inner_h = h - 2;
    const int inner_w = w - 2;
    if (inner_h <= 0 || inner_w <= 0) return;

    if (editing_) {
        if (edit_line_ < asm_scroll_) asm_scroll_ = edit_line_;
        if (edit_line_ >= asm_scroll_ + inner_h) asm_scroll_ = edit_line_ - inner_h + 1;
    }
    const int num_w = 4;
    const int text_w = std::max(1, inner_w - num_w);

    int row = 0;
    for (size_t idx = static_cast<size_t>(asm_scroll_);
         idx < raw_assembly_.size() && row < inner_h; ++idx) {
        const std::string& line = raw_assembly_[idx];
        const std::vector<WrappedSegment> segments =
            wrapLine(line, static_cast<size_t>(text_w));

        Style text_style{Color::Default, Color::Default, false};
        const size_t first = line.find_first_not_of(" \t");
        if (first != std::string::npos && line[first] == ';') {
            text_style.fg = Color::BrightBlack;
        } else if (line.find(':') != std::string::npos) {
            text_style.fg = Color::BrightYellow;
            text_style.bold = true;
        }

        int cursor_segment = -1;
        if (editing_ && static_cast<int>(idx) == edit_line_) {
            cursor_segment = static_cast<int>(segments.size()) - 1;
            for (size_t s = 0; s < segments.size(); ++s) {
                const size_t end = segments[s].offset + segments[s].text.size();
                if (static_cast<size_t>(edit_col_) <= end) {
                    cursor_segment = static_cast<int>(s);
                    break;
                }
            }
        }

        for (size_t s = 0; s < segments.size() && row < inner_h; ++s, ++row) {
            const int screen_row = r + 1 + row;
            std::string prefix(static_cast<size_t>(num_w), ' ');
            if (s == 0) {
                std::string number = std::to_string(idx + 1);
                while (number.size() < 3) number = " " + number;
                prefix = number + " ";
            }

            screen_.drawText(screen_row, c + 1, prefix,
                             Style{Color::BrightBlack, Color::Default, false});
            screen_.drawText(screen_row, c + 1 + num_w, segments[s].text,
                             text_style, text_w);

            if (static_cast<int>(s) == cursor_segment) {
                const int col_in_seg = edit_col_ - static_cast<int>(segments[s].offset);
                if (col_in_seg >= 0 && col_in_seg < text_w) {
                    const std::string glyph =
                        static_cast<size_t>(col_in_seg) < segments[s].text.size()
                            ? segments[s].text.substr(static_cast<size_t>(col_in_seg), 1)
                            : " ";
                    Style cursor_style = text_style;
                    cursor_style.reverse = true;
                    screen_.setCell(screen_row, c + 1 + num_w + col_in_seg,
                                    glyph, cursor_style);
                }
            }
        }
    }
}

void Tui::renderAssembledBinary(int r, int c, int h, int w) {
    std::string title = "ASSEMBLED BINARY";
    if (assembly_dirty_) title += " [STALE]";
    Style border_style{Color::White, Color::Default, false};
    Style title_style = paneTitleStyle();
    if (assembly_dirty_) {
        border_style.fg_rgb = 0xFF0000;
        title_style.fg_rgb = 0xFF0000;
    }
    screen_.drawBox(r, c, h, w, title, border_style, title_style);

    const int inner_h = h - 2;
    const int inner_w = w - 2;
    if (inner_h <= 0 || inner_w <= 0) return;

    const uint32_t current_pc = cpu_.state().pc;

    if (auto_scroll_ && !code_words_.empty()) {
        int target = static_cast<int>(current_pc);
        if (target < bin_scroll_ || target >= bin_scroll_ + inner_h) {
            bin_scroll_ = std::max(0, target - inner_h / 2);
        }
    }

    const std::vector<MicroPcView> views = cpu_.microSnapshot();
    for (int i = 0; i < inner_h; ++i) {
        size_t pc = static_cast<size_t>(bin_scroll_ + i);
        if (pc >= code_words_.size()) break;

        const uint32_t word = code_words_[pc];
        const Instruction instr = decodeInstruction(word);
        const std::string mnemonic = formatInstruction(instr);
        const std::string tags = controlTags(static_cast<uint32_t>(pc), views);
        const bool is_active_pc = (pc == current_pc);
        const auto bp_it = debugger_.breakpoints().find(static_cast<uint32_t>(pc));
        const bool has_break = bp_it != debugger_.breakpoints().end();
        const bool bp_enabled = has_break && bp_it->second.enabled;
        char bp_mark = ' ';
        if (has_break) {
            bp_mark = !bp_enabled ? 'b' : (bp_it->second.condition ? 'C' : 'B');
        }

        std::ostringstream oss;
        oss << bp_mark
            << "[" << std::setw(3) << std::right << pc << "] "
            << "0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << word << " "
            << std::dec << std::setfill(' ')
            << mnemonic;

        if (!tags.empty()) {
            oss << tags;
        } else if (is_active_pc) {
            oss << " ◄";
        }

        std::string row_str = oss.str();
        if (is_active_pc) {
            drawSelectedLine(screen_, r + 1 + i, c + 1, row_str, inner_w);
            continue;
        }

        Style row_style{Color::Default, Color::Default, false};
        if (!tags.empty()) {
            row_style.fg = Color::BrightCyan;
            row_style.bold = true;
        } else if (bp_enabled) {
            row_style.fg = Color::BrightRed;
        } else if (has_break) {
            row_style.dim = true;
        }

        screen_.drawText(r + 1 + i, c + 1, row_str, row_style, inner_w);
    }
}

int Tui::registerColumns(int width) const {
    int value_width = 5;
    for (int32_t value : cpu_.state().registers) {
        value_width = std::max(value_width, static_cast<int>(formatWord(value, interpretation_).size()));
    }
    const int columns = width / (value_width + 5);
    return columns >= 4 ? 4 : columns >= 2 ? 2 : 1;
}

void Tui::renderRegisters(int r, int c, int h, int w) {
    screen_.drawBox(r, c, h, w, "REGISTERS",
                    Style{Color::White, Color::Default, false},
                    paneTitleStyle());

    const int inner_h = h - 2;
    const int inner_w = w - 2;
    if (inner_h <= 0 || inner_w <= 0) return;

    const CpuState& st = cpu_.state();

    int num_cols = registerColumns(inner_w);
    int col_width = inner_w / num_cols;

    for (int i = 0; i < kNumGPRegisters; ++i) {
        int grid_r = i / num_cols;
        int grid_c = i % num_cols;
        if (grid_r >= inner_h - 2) break;

        char buf[32];
        snprintf(buf, sizeof(buf), "R%02d:", i);
        std::string reg_text = buf + formatWord(st.registers[static_cast<size_t>(i)], interpretation_);
        
        Style reg_style{Color::Default, Color::Default, false};
        if (st.registers[static_cast<size_t>(i)] != 0) {
            reg_style.fg = Color::BrightWhite;
            reg_style.bold = true;
        } else {
            reg_style.fg = Color::BrightBlack;
        }

        for (const auto& entry : debugger_.watchpoints()) {
            const Watchpoint& w = entry.second;
            if (w.kind == WatchKind::Register && w.target == static_cast<uint32_t>(i)) {
                if (w.enabled) {
                    reg_style.fg = Color::BrightMagenta;
                    reg_style.bold = true;
                }
                reg_text += (w.enabled ? " ◄W" : " ◄w") + std::to_string(w.id);
            }
        }

        screen_.drawText(r + 1 + grid_r, c + 1 + grid_c * col_width, reg_text, reg_style, col_width);
    }

    // Status line: PC, SP
    int status_row = r + h - 3;
    if (status_row > r) {
        std::string stage_str = formatActiveStage(st.pc);
        std::string pc_info = "PC:" + std::to_string(st.pc);
        if (!stage_str.empty()) pc_info += "(" + stage_str + ")";
        std::string sp_info = "SP:" + std::to_string(st.sp);

        screen_.drawText(status_row, c + 1, pc_info, Style{Color::BrightGreen, Color::Default, true});
        screen_.drawText(status_row, c + 1 + inner_w / 2, sp_info, Style{Color::BrightYellow, Color::Default, true});
    }

    int flags_row = r + h - 2;
    if (flags_row > r) {
        std::ostringstream oss;
        oss << "FLAGS:[Z:" << st.flags.zero
            << " N:" << st.flags.negative
            << " C:" << st.flags.carry
            << " V:" << st.flags.overflow << "]";

        if (st.pending_exception) {
            oss << " !" << exceptionName(*st.pending_exception) << "!";
            screen_.drawText(flags_row, c + 1, oss.str(), Style{Color::BrightRed, Color::Default, true});
        } else if (st.halted) {
            oss << " [HALT]";
            screen_.drawText(flags_row, c + 1, oss.str(), Style{Color::BrightYellow, Color::Default, true});
        } else {
            screen_.drawText(flags_row, c + 1, oss.str(), Style{Color::BrightCyan, Color::Default, false});
        }
    }
}

void Tui::renderExtraStats(int r, int c, int h, int w) {
    screen_.drawBox(r, c, h, w, "EXTRA STATS",
                    Style{Color::White, Color::Default, false},
                    paneTitleStyle());

    const int inner_h = h - 2;
    const int inner_w = w - 2;
    if (inner_h <= 0 || inner_w <= 0) return;

    const PipelineTrace& hazards = cpu_.trace();

    // Count non-zero data memory words
    size_t non_zero_mem = 0;
    for (int32_t val : cpu_.memory()) {
        if (val != 0) ++non_zero_mem;
    }
    double mem_pct = cpu_.memory().empty() ? 0.0 : (100.0 * non_zero_mem / cpu_.memory().size());

    std::vector<std::string> stat_lines;
    stat_lines.push_back("• Run: " + std::to_string(cpu_.retired()) + " instrs");
    stat_lines.push_back("• Cycles: " + std::to_string(cpu_.cycles()));

    char mem_buf[64];
    snprintf(mem_buf, sizeof(mem_buf), "• Mem: %zu/%zu (%.1f%%)", non_zero_mem, cpu_.memory().size(), mem_pct);
    stat_lines.push_back(mem_buf);

    stat_lines.push_back("• Stall/Flush: " + std::to_string(hazards.stalls) + "/" + std::to_string(hazards.flushes));

    std::string pipe_info = "• " + variantName(cpu_.variant()) +
                            (cpu_.forwarding() ? " (Fwd)" : " (NoFwd)");
    stat_lines.push_back(pipe_info);

    stat_lines.push_back(cpu_.encoding() == MicrocodeEncoding::Vertical ? "• ROM: vert"
                                                                       : "• ROM: horiz");

    size_t bp_on = 0;
    for (const auto& e : debugger_.breakpoints()) if (e.second.enabled) ++bp_on;
    size_t wp_on = 0;
    for (const auto& e : debugger_.watchpoints()) if (e.second.enabled) ++wp_on;
    stat_lines.push_back("• Brk: " + std::to_string(debugger_.breakpoints().size()) +
                         " (" + std::to_string(bp_on) + " on)");
    stat_lines.push_back("• Wch: " + std::to_string(debugger_.watchpoints().size()) +
                         " (" + std::to_string(wp_on) + " on)");

    for (size_t i = 0; i < stat_lines.size() && static_cast<int>(i) < inner_h; ++i) {
        screen_.drawText(r + 1 + static_cast<int>(i), c + 1, stat_lines[i],
                         Style{Color::Default, Color::Default, false}, inner_w);
    }
}

void Tui::renderControlRom(int r, int c, int h, int w) {
    const std::string title =
        cpu_.encoding() == MicrocodeEncoding::Vertical ? "CONTROL ROM V" : "CONTROL ROM H";
    screen_.drawBox(r, c, h, w, title,
                    Style{Color::White, Color::Default, false},
                    paneTitleStyle());

    const int inner_h = h - 2;
    const int inner_w = w - 2;
    if (inner_h <= 0 || inner_w <= 0) return;

    const auto& rom = cpu_.controlMemory();
    if (rom.empty()) return;

    std::vector<int> active;
    for (const MicroPcView& view : cpu_.microSnapshot()) active.push_back(view.micro_pc);

    const std::vector<int>& ends = cpu_.routineEnds();
    constexpr int kHeadWidth = 5;
    const size_t signal_width = inner_w > kHeadWidth
        ? static_cast<size_t>(inner_w - kHeadWidth)
        : 1;

    const int max_top = std::max(0, static_cast<int>(rom.size()) - 1);
    rom_scroll_ = std::max(0, std::min(rom_scroll_, max_top));

    int row = 0;
    for (int address = rom_scroll_; row < inner_h && address < static_cast<int>(rom.size());
         ++address) {
        const bool on = std::find(active.begin(), active.end(), address) != active.end();

        std::ostringstream head;
        head << (on ? "►" : " ") << std::right << std::setw(3) << std::setfill('0')
             << address << std::setfill(' ') << " ";
        const std::vector<WrappedSegment> parts =
            wrapLine(controlSignalNames(rom[static_cast<size_t>(address)].ctl), signal_width);

        for (size_t p = 0; p < parts.size() && row < inner_h; ++p, ++row) {
            std::string text = p == 0
                ? head.str()
                : std::string(static_cast<size_t>(kHeadWidth), ' ');
            text += parts[p].text;
            if (on) {
                drawSelectedLine(screen_, r + 1 + row, c + 1, text, inner_w);
            } else {
                screen_.drawText(r + 1 + row, c + 1, text,
                                 Style{Color::Default, Color::Default, false}, inner_w);
            }
        }
        if (std::binary_search(ends.begin(), ends.end(), address)) ++row;
    }
}

void Tui::renderMemory(int r, int c, int h, int w) {
    const uint32_t sp = cpu_.state().sp;
    const auto& memory = cpu_.memory();
    const uint32_t mem_sz = static_cast<uint32_t>(memory.size());
    const int inner_h = h - 2;
    const int inner_w = w - 2;
    const uint32_t rows = static_cast<uint32_t>(std::max(0, inner_h - 1));

    uint32_t top = mem_anchor_;
    if (mem_follow_sp_) {
        const uint32_t bottom = (top > rows) ? (top - rows) : 0;
        if (sp < bottom) {
            top = std::min(sp + rows, mem_sz);
        } else if (sp > top) {
            top = std::min(sp, mem_sz);
        }
        mem_anchor_ = top;
    } else {
        mem_anchor_ = std::min(std::max(mem_anchor_, rows), mem_sz);
        top = mem_anchor_;
    }

    std::string title = "MEMORY @ " + std::to_string(top);
    if (mem_follow_sp_ && static_cast<int>(title.size()) + 5 <= w - 6) title += " (SP)";
    screen_.drawBox(r, c, h, w, title,
                    Style{Color::White, Color::Default, false},
                    paneTitleStyle());

    if (inner_h <= 0 || inner_w <= 0) return;

    for (int i = 0; i < inner_h; ++i) {
        const int addr = static_cast<int>(top) - i;
        if (addr < 0) break;

        std::ostringstream oss;
        if (static_cast<uint32_t>(addr) >= mem_sz) {
            oss << "[" << std::setw(4) << addr << "] (top)";
            if (sp >= mem_sz) oss << " ◄SP";
            screen_.drawText(r + 1 + i, c + 1, oss.str(), Style{Color::BrightYellow, Color::Default, false}, inner_w);
            continue;
        }

        int32_t val = memory[static_cast<size_t>(addr)];
        oss << "[" << std::setw(4) << addr << "] " << std::setw(6) << formatWord(val, interpretation_);
        const bool is_sp = (static_cast<uint32_t>(addr) == sp);
        if (is_sp) oss << " ◄SP";

        bool is_watched = false;

        for (const auto& entry : debugger_.watchpoints()) {
            const Watchpoint& w = entry.second;
            if (w.kind == WatchKind::Memory && w.target == static_cast<uint32_t>(addr)) {
                oss << (w.enabled ? " ◄W" : " ◄w") << w.id;
                if (w.enabled) is_watched = true;
            }
        }

        Style line_style{Color::Default, Color::Default, false};
        if (is_watched) {
            line_style.fg = Color::BrightMagenta;
            line_style.bold = true;
        } else if (is_sp) {
            line_style.fg = Color::BrightYellow;
            line_style.bold = true;
        } else if (val != 0) {
            line_style.fg = Color::BrightWhite;
            line_style.bold = true;
        } else {
            line_style.fg = Color::BrightBlack;
        }

        screen_.drawText(r + 1 + i, c + 1, oss.str(), line_style, inner_w);
    }
}

void Tui::renderHazardTimeline(int r, int c, int h, int w) {
    const PipelineTrace& hazards = cpu_.trace();
    const std::string title = "HAZARD TIMELINE - " + std::to_string(hazards.flushes) + " flush, " +
                              std::to_string(hazards.stalls) + " stall, " +
                              std::to_string(hazards.faults) + " fault";
    screen_.drawBox(r, c, h, w, title,
                    Style{Color::White, Color::Default, false},
                    paneTitleStyle());

    const int inner_h = h - 2;
    const int inner_w = w - 2;
    if (inner_h <= 0 || inner_w <= 0) return;

    const auto& events = hazards.events;
    const int total = static_cast<int>(events.size());
    if (total == 0) {
        screen_.drawText(r + 1, c + 2, "no hazard events yet",
                         Style{Color::BrightBlack}, inner_w);
        return;
    }

    int first = total - inner_h - trace_scroll_;
    if (first > total - 1) first = total - 1;
    if (first < 0) first = 0;

    for (int i = 0; i < inner_h && first + i < total; ++i) {
        const HazardEvent& e = events[static_cast<size_t>(first + i)];
        std::string line = std::to_string(e.cycle) + " | " + hazardName(e.kind) +
                           " | pc=" + std::to_string(e.pc);
        if (e.kind == HazardKind::Flush) {
            line += " -> " + std::to_string(e.target) + " (depth " + std::to_string(e.depth) +
                    ", squashed " + std::to_string(e.discarded) + ")";
        }
        Style line_style;
        line_style.fg = Color::Cyan;
        if (e.kind == HazardKind::Stall) line_style.fg = Color::Yellow;
        if (e.kind == HazardKind::Fault) line_style.fg = Color::Red;
        screen_.drawText(r + 1 + i, c + 2, line, line_style, inner_w);
    }
}

void Tui::renderCommandLine(int r, int c, int h, int w) {
    screen_.drawBox(r, c, h, w, "COMMAND LINE (Input)",
                    Style{Color::White, Color::Default, false},
                    paneTitleStyle());

    if (h < 3) return;

    if (mode_ == Mode::Command) {
        std::string prompt = ": " + command_buffer_;
        screen_.drawText(r + 1, c + 2, prompt, Style{Color::BrightWhite, Color::Default, true});
        // Render block cursor
        screen_.setCell(r + 1, c + 2 + static_cast<int>(prompt.size()), " ",
                        Style{Color::Default, Color::BrightWhite, false, false, true});
    } else if (editing_) {
        std::string hint = "[EDIT " + std::to_string(edit_line_ + 1) + ":" + std::to_string(edit_col_ + 1) +
                           "]  type insert | Enter split | Backspace | Arrows | Esc done";
        screen_.drawText(r + 1, c + 2, hint, Style{Color::BrightWhite, Color::Default, true}, w - 4);
    } else {
        std::string hint;
        if (mode_ == Mode::Inspect) {
            if (pane_ == Pane::Assembly) {
                hint = "[INSPECT ASM]  Tab next pane | j/k scroll | 'e' edit | ':' cmd | Esc normal | 'q' quit";
            } else if (pane_ == Pane::Memory) {
                hint = "[INSPECT MEM]  Tab next pane | j/k scroll | g/G ends | 'f' follow SP | ':' cmd | Esc normal";
            } else {
                hint = "[INSPECT " + paneName() + "]  Tab next pane | j/k scroll | ':' cmd | Esc normal | 'q' quit";
            }
        } else {
            hint = "[NORMAL MODE]  ':' cmd | 's' step | 'c' clock | 'r' run | Tab inspect | 'T' trace | 'q' quit";
        }
        screen_.drawText(r + 1, c + 2, hint, Style{Color::BrightBlack, Color::Default, false}, w - 4);
    }
}

void Tui::renderFeedback(int r, int c, int h, int w) {
    const bool paged = feedback_message_.find('\n') != std::string::npos;
    screen_.drawBox(r, c, h, w, paged ? "COMMAND FEEDBACK (PgUp/PgDn)" : "COMMAND FEEDBACK",
                    Style{Color::White, Color::Default, false},
                    paneTitleStyle());

    if (h < 3) return;

    const int inner_w = std::max(1, w - 4);
    const int inner_h = h - 2;

    if (mode_ == Mode::Command && !completion_matches_.empty()) {
        screen_.drawText(r + 1, c + 2, "Tab: complete/cycle",
                         Style{Color::BrightBlack, Color::Default, false}, inner_w);
        const int rows = std::max(0, inner_h - 1);
        int top = 0;
        if (completion_index_ >= rows && rows > 0) top = completion_index_ - rows + 1;
        for (int i = 0; i < rows; ++i) {
            const int index = top + i;
            if (index >= static_cast<int>(completion_matches_.size())) break;
            const std::string& candidate = completion_matches_[static_cast<size_t>(index)];
            if (index == completion_index_) {
                drawSelectedLine(screen_, r + 2 + i, c + 2, candidate, inner_w);
            } else {
                screen_.drawText(r + 2 + i, c + 2, candidate,
                                 Style{Color::Default, Color::Default, false}, inner_w);
            }
        }
        return;
    }

    Style fb_style{Color::BrightWhite, Color::Default, false};
    if (feedback_is_error_) {
        fb_style.fg = Color::BrightRed;
        fb_style.bold = true;
    } else {
        fb_style.fg = Color::BrightGreen;
    }

    const std::vector<std::string> lines = wrapTextLines(feedback_message_, inner_w);
    const int start = std::min(feedback_scroll_, std::max(0, static_cast<int>(lines.size()) - inner_h));
    bool warning_section = false;
    for (int index = 0; index < start + inner_h && index < static_cast<int>(lines.size()); ++index) {
        if (lines[static_cast<size_t>(index)].rfind("Warnings (", 0) == 0) warning_section = true;
        if (index < start) continue;
        Style line_style = fb_style;
        if (warning_section) {
            line_style.fg = Color::BrightYellow;
            line_style.bold = true;
        }
        screen_.drawText(r + 1 + index - start, c + 2,
                         lines[static_cast<size_t>(index)], line_style, inner_w);
    }
}

void Tui::resetCompletion() {
    completion_matches_.clear();
    completion_base_.clear();
    completion_index_ = -1;
}

int* Tui::focusedScroll() {
    switch (pane_) {
        case Pane::Assembly: return &asm_scroll_;
        case Pane::Binary:   return &bin_scroll_;
        default:             return nullptr;
    }
}

uint32_t Tui::memoryTop() const {
    const uint32_t mem_sz = static_cast<uint32_t>(cpu_.memory().size());
    return std::min(cpu_.state().sp + 1, mem_sz);
}

void Tui::scrollFocused(int delta) {
    if (pane_ == Pane::ControlRom) {
        rom_scroll_ = std::max(0, rom_scroll_ + delta);
        return;
    }
    if (pane_ == Pane::Memory) {
        mem_follow_sp_ = false;
        const int next = static_cast<int>(mem_anchor_) - delta;
        const int limit = static_cast<int>(cpu_.memory().size());
        mem_anchor_ = static_cast<uint32_t>(std::max(0, std::min(next, limit)));
        return;
    }
    if (int* scroll = focusedScroll()) {
        auto_scroll_ = false;
        *scroll = std::max(0, *scroll + delta);
    }
}

void Tui::clampFocusedScroll() {
    asm_scroll_ = std::max(0, std::min(asm_scroll_,
        std::max(0, static_cast<int>(raw_assembly_.size()) - asm_view_h_)));
    bin_scroll_ = std::max(0, std::min(bin_scroll_,
        std::max(0, static_cast<int>(code_words_.size()) - bin_view_h_)));
    mem_anchor_ = std::min(mem_anchor_, static_cast<uint32_t>(cpu_.memory().size()));
    const int rom_max = std::max(0, static_cast<int>(cpu_.controlMemory().size()) - 1);
    rom_scroll_ = std::max(0, std::min(rom_scroll_, rom_max));
}

void Tui::cyclePane() {
    for (int guard = 0; guard < 8; ++guard) {
        switch (pane_) {
            case Pane::Assembly:  pane_ = Pane::Binary; break;
            case Pane::Binary:    pane_ = Pane::Registers; break;
            case Pane::Registers: pane_ = Pane::Stats; break;
            case Pane::Stats:     pane_ = Pane::ControlRom; break;
            case Pane::ControlRom: pane_ = Pane::Memory; break;
            case Pane::Memory:    pane_ = Pane::Timeline; break;
            case Pane::Timeline:  pane_ = Pane::Assembly; break;
        }
        if (pane_ != Pane::Timeline || show_trace_) break;
    }
}

std::string Tui::paneName() const {
    switch (pane_) {
        case Pane::Assembly:  return "ASM";
        case Pane::Binary:    return "BIN";
        case Pane::Registers: return "REGS";
        case Pane::Stats:     return "STATS";
        case Pane::ControlRom: return "ROM";
        case Pane::Memory:    return "MEM";
        case Pane::Timeline:  return "TRACE";
    }
    return "?";
}

void Tui::openConfigDialog() {
    dialog_rows_.clear();
    dialog_rows_.push_back({"stage", cpu_.variant() == PipelineVariant::SixStage ? "6-stage" : "4-stage"});
    dialog_rows_.push_back({"ROM", cpu_.encoding() == MicrocodeEncoding::Vertical ? "vertical" : "horizontal"});
    dialog_rows_.push_back({"forwarding", cpu_.forwarding() ? "on" : "off"});
    dialog_rows_.push_back({"binary interpretation", interpretationName(interpretation_)});
    dialog_selected_ = 0;
    mode_ = Mode::Dialog;
    feedback_message_ = "CONFIG - Up/Down select, Left/Right change, Enter apply, Esc cancel";
    feedback_is_error_ = false;
}

void Tui::cycleDialogValue() {
    if (dialog_rows_.empty()) return;
    DialogRow& row = dialog_rows_[static_cast<size_t>(dialog_selected_)];
    if (dialog_selected_ == 0) {
        row.value = row.value == "4-stage" ? "6-stage" : "4-stage";
    } else if (dialog_selected_ == 1) {
        row.value = row.value == "horizontal" ? "vertical" : "horizontal";
    } else if (dialog_selected_ == 2) {
        row.value = row.value == "on" ? "off" : "on";
    } else {
        row.value = row.value == "Decimal Integer" ? "Hexadecimal" :
            row.value == "Hexadecimal" ? "Floating Point Number" : "Decimal Integer";
    }
}

void Tui::applyDialog() {
    if (dialog_rows_.size() < 4) {
        mode_ = Mode::Normal;
        return;
    }
    const PipelineVariant v = dialog_rows_[0].value == "6-stage" ? PipelineVariant::SixStage
                                                                : PipelineVariant::FourStage;
    const MicrocodeEncoding e = dialog_rows_[1].value == "vertical" ? MicrocodeEncoding::Vertical
                                                                    : MicrocodeEncoding::Horizontal;
    if (v != cpu_.variant() || e != cpu_.encoding()) {
        cpu_.reconfigure(v, e);
        feedback_message_ = "config applied - run reset";
    } else {
        feedback_message_ = "config applied";
    }
    cpu_.setForwarding(dialog_rows_[2].value == "on");
    interpretation_ = dialog_rows_[3].value == "Hexadecimal" ? BinaryInterpretation::Hexadecimal :
        dialog_rows_[3].value == "Floating Point Number" ? BinaryInterpretation::FloatingPoint :
        BinaryInterpretation::Decimal;
    feedback_is_error_ = false;
    mode_ = Mode::Normal;
}

void Tui::renderDialog() {
    const int w = std::min(50, screen_.cols() - 4);
    const int h = 9;
    if (w < 34 || screen_.rows() < h + 4) return;

    const int r = (screen_.rows() - h) / 2;
    const int c = (screen_.cols() - w) / 2;
    Style face;
    face.fg_rgb = 0x00FF00;
    Style title_face = face;
    title_face.bold = true;

    screen_.fill(r, c, h, w, " ", face);
    screen_.drawBox(r, c, h, w, "CPU CONFIG", face, title_face);

    for (size_t i = 0; i < dialog_rows_.size(); ++i) {
        Style st = face;
        if (static_cast<int>(i) == dialog_selected_) {
            st.bg = Color::BrightBlack;
            st.bold = true;
            screen_.fill(r + 1 + static_cast<int>(i), c + 2, 1, w - 4, " ", st);
        }
        screen_.drawText(r + 1 + static_cast<int>(i), c + 2,
                         dialog_rows_[i].label + ": " + dialog_rows_[i].value, st, w - 4);
    }
    Style footer_face = face;
    footer_face.dim = true;
    screen_.drawText(r + h - 2, c + 2, "stage/ROM changes restart the run", footer_face, w - 4);
}

void Tui::handleEditorKey(const KeyEvent& ev) {
    if (raw_assembly_.empty()) {
        editing_ = false;
        return;
    }

    std::string& line = raw_assembly_[static_cast<size_t>(edit_line_)];
    const int line_len = static_cast<int>(line.size());

    switch (ev.key) {
        case Key::Escape:
            editing_ = false;
            feedback_message_ = "Editor closed - ':assemble' applies the buffer.";
            feedback_is_error_ = false;
            return;
        case Key::Left:
            if (edit_col_ > 0) --edit_col_;
            return;
        case Key::Right:
            if (edit_col_ < line_len) ++edit_col_;
            return;
        case Key::Home:
            edit_col_ = 0;
            return;
        case Key::End:
            edit_col_ = line_len;
            return;
        case Key::Up:
            if (edit_line_ > 0) {
                --edit_line_;
                edit_col_ = std::min(edit_col_, static_cast<int>(raw_assembly_[static_cast<size_t>(edit_line_)].size()));
            }
            return;
        case Key::Down:
            if (edit_line_ + 1 < static_cast<int>(raw_assembly_.size())) {
                ++edit_line_;
                edit_col_ = std::min(edit_col_, static_cast<int>(raw_assembly_[static_cast<size_t>(edit_line_)].size()));
            }
            return;
        case Key::Enter: {
            std::string tail = line.substr(static_cast<size_t>(edit_col_));
            line.erase(static_cast<size_t>(edit_col_));
            raw_assembly_.insert(raw_assembly_.begin() + edit_line_ + 1, std::move(tail));
            ++edit_line_;
            edit_col_ = 0;
            assembly_dirty_ = true;
            return;
        }
        case Key::Backspace:
            if (edit_col_ > 0) {
                line.erase(static_cast<size_t>(edit_col_ - 1), 1);
                --edit_col_;
                assembly_dirty_ = true;
            } else if (edit_line_ > 0) {
                const int prev_len = static_cast<int>(raw_assembly_[static_cast<size_t>(edit_line_ - 1)].size());
                raw_assembly_[static_cast<size_t>(edit_line_ - 1)] += line;
                raw_assembly_.erase(raw_assembly_.begin() + edit_line_);
                --edit_line_;
                edit_col_ = prev_len;
                assembly_dirty_ = true;
            }
            return;
        case Key::Char:
            if (ev.ch >= 32 && ev.ch <= 126) {
                line.insert(static_cast<size_t>(edit_col_), 1, ev.ch);
                ++edit_col_;
                assembly_dirty_ = true;
            }
            return;
        default:
            return;
    }
}

void Tui::handleKey(const KeyEvent& ev) {
    if (mode_ == Mode::Dialog) {
        if (ev.key == Key::Escape) {
            mode_ = Mode::Normal;
            feedback_message_ = "config cancelled";
            feedback_is_error_ = false;
        } else if (ev.key == Key::Enter) {
            applyDialog();
        } else if (ev.key == Key::Down) {
            if (dialog_selected_ + 1 < static_cast<int>(dialog_rows_.size())) ++dialog_selected_;
        } else if (ev.key == Key::Up) {
            if (dialog_selected_ > 0) --dialog_selected_;
        } else if (ev.key == Key::Left || ev.key == Key::Right ||
                   (ev.key == Key::Char && ev.ch == ' ')) {
            cycleDialogValue();
        }
        return;
    }
    if (mode_ == Mode::Command) {
        if (ev.key == Key::Escape) {
            mode_ = Mode::Normal;
            command_buffer_.clear();
            resetCompletion();
            feedback_message_ = "Command input cancelled.";
            feedback_is_error_ = false;
        } else if (ev.key == Key::Enter) {
            mode_ = Mode::Normal;
            const std::string command = command_buffer_;
            command_buffer_.clear();
            resetCompletion();
            executeCommand(command);
        } else if (ev.key == Key::Tab) {
            if (command_buffer_ != completion_base_) {
                Completion completion = completeCommand(command_buffer_);
                completion_matches_ = std::move(completion.matches);
                completion_replace_start_ = completion.start;
                completion_replace_len_ = completion.len;
                completion_base_ = command_buffer_;
                completion_index_ = -1;
            }
            if (!completion_matches_.empty()) {
                const std::string token =
                    command_buffer_.substr(completion_replace_start_, completion_replace_len_);
                if (completion_index_ < 0) {
                    const std::string prefix = commonPrefix(completion_matches_);
                    if (prefix.size() > token.size()) {
                        command_buffer_.replace(completion_replace_start_,
                                                completion_replace_len_, prefix);
                        completion_replace_len_ = prefix.size();
                        completion_base_ = command_buffer_;
                        return;
                    }
                    if (token.empty()) return;
                    completion_index_ = 0;
                } else {
                    completion_index_ =
                        (completion_index_ + 1) % static_cast<int>(completion_matches_.size());
                }
                const std::string& match =
                    completion_matches_[static_cast<size_t>(completion_index_)];
                command_buffer_.replace(completion_replace_start_,
                                        completion_replace_len_, match);
                completion_replace_len_ = match.size();
                completion_base_ = command_buffer_;
            }
        } else if (ev.key == Key::Backspace) {
            resetCompletion();
            if (!command_buffer_.empty()) {
                command_buffer_.pop_back();
            }
        } else if (ev.key == Key::Char && ev.ch >= 32 && ev.ch <= 126) {
            resetCompletion();
            command_buffer_.push_back(ev.ch);
        }
        return;
    }

    if (mode_ == Mode::Inspect) {
        if (editing_) { handleEditorKey(ev); return; }
        if (ev.key == Key::Tab) { cyclePane(); return; }
        if (ev.key == Key::Escape) {
            mode_ = Mode::Normal;
            feedback_message_ = "NORMAL MODE";
            feedback_is_error_ = false;
            return;
        }
        if (ev.key == Key::Char && ev.ch == 'e' && pane_ == Pane::Assembly) {
            if (raw_assembly_.empty()) {
                raw_assembly_.push_back("");
                edit_line_ = 0;
                edit_col_ = 0;
            } else {
                edit_line_ = std::max(0, std::min(edit_line_, static_cast<int>(raw_assembly_.size()) - 1));
                edit_col_ = std::min(edit_col_, static_cast<int>(raw_assembly_[static_cast<size_t>(edit_line_)].size()));
            }
            editing_ = true;
            feedback_message_ = "EDIT - type to insert, Enter splits, Esc closes, ':assemble' applies.";
            feedback_is_error_ = false;
            return;
        }
        if (ev.key == Key::Char && (ev.ch == 'j' || ev.ch == 'k')) {
            scrollFocused(ev.ch == 'j' ? 1 : -1);
            return;
        }
        if (ev.key == Key::Char && ev.ch == 'g') {
            if (pane_ == Pane::Memory) {
                mem_anchor_ = 0;
                mem_follow_sp_ = false;
            } else if (pane_ == Pane::ControlRom) {
                rom_scroll_ = 0;
            } else if (int* scroll = focusedScroll()) {
                auto_scroll_ = false;
                *scroll = 0;
            }
            return;
        }
        if (ev.key == Key::Char && ev.ch == 'G') {
            if (pane_ == Pane::Memory) {
                mem_anchor_ = static_cast<uint32_t>(cpu_.memory().size());
                mem_follow_sp_ = false;
            } else if (pane_ == Pane::Assembly) {
                auto_scroll_ = false;
                asm_scroll_ = static_cast<int>(raw_assembly_.size());
            } else if (pane_ == Pane::Binary) {
                auto_scroll_ = false;
                bin_scroll_ = static_cast<int>(code_words_.size());
            } else if (pane_ == Pane::ControlRom) {
                rom_scroll_ = static_cast<int>(cpu_.controlMemory().size());
            }
            return;
        }
        if (ev.key == Key::Char && ev.ch == 'f' && pane_ == Pane::Memory) {
            if (mem_follow_sp_) {
                mem_follow_sp_ = false;
                feedback_message_ = "memory: follow SP off";
            } else {
                mem_follow_sp_ = true;
                feedback_message_ = "memory: following SP";
            }
            feedback_is_error_ = false;
            return;
        }
    }

    // Normal mode keys
    switch (ev.key) {
        case Key::Char:
            switch (ev.ch) {
                case ':':
                    mode_ = Mode::Command;
                    command_buffer_.clear();
                    resetCompletion();
                    feedback_message_ = "Type command and press Enter (Tab completes, Esc cancels).";
                    feedback_is_error_ = false;
                    break;
                case 's':
                case 'n':
                    executeCommand("step");
                    break;
                case 'c':
                    executeCommand("clock");
                    break;
                case 'r':
                    executeCommand("run");
                    break;
                case 'T':
                    show_trace_ = !show_trace_;
                    trace_scroll_ = 0;
                    feedback_message_ = show_trace_ ? "Hazard timeline shown."
                                                    : "Hazard timeline hidden.";
                    feedback_is_error_ = false;
                    break;
                case '[':
                    show_trace_ = true;
                    ++trace_scroll_;
                    break;
                case ']':
                    trace_scroll_ = std::max(0, trace_scroll_ - 1);
                    break;
                case 'a':
                    auto_scroll_ = true;
                    feedback_message_ = "Auto-scroll re-enabled.";
                    feedback_is_error_ = false;
                    break;
                case 'g':
                    auto_scroll_ = false;
                    bin_scroll_ = 0;
                    asm_scroll_ = 0;
                    break;
                case 'G':
                    auto_scroll_ = false;
                    bin_scroll_ = static_cast<int>(code_words_.size());
                    asm_scroll_ = static_cast<int>(raw_assembly_.size());
                    break;
                case '?':
                case 'h':
                    executeCommand("help");
                    break;
                case 'q':
                    running_ = false;
                    break;
                default:
                    break;
            }
            break;
        case Key::Enter:
            executeCommand("step");
            break;
        case Key::Down:
            scrollFocused(1);
            break;
        case Key::Up:
            scrollFocused(-1);
            break;
        case Key::PageDown:
            if (feedback_message_.find('\n') != std::string::npos) {
                const auto lines = wrapTextLines(feedback_message_, std::max(1, screen_.cols() - 4));
                feedback_scroll_ = std::min(feedback_scroll_ + 8,
                                            std::max(0, static_cast<int>(lines.size()) - 1));
            } else {
                scrollFocused(10);
            }
            break;
        case Key::PageUp:
            if (feedback_message_.find('\n') != std::string::npos) {
                feedback_scroll_ = std::max(0, feedback_scroll_ - 8);
            } else {
                scrollFocused(-10);
            }
            break;
        case Key::Tab:
            if (mode_ == Mode::Normal) {
                mode_ = Mode::Inspect;
                feedback_message_ = "INSPECT MODE - Tab cycles panes, j/k scroll, Esc returns";
                feedback_is_error_ = false;
            } else {
                cyclePane();
            }
            break;
        case Key::Escape:
            auto_scroll_ = true;
            feedback_message_ = "Auto-scroll re-enabled.";
            feedback_is_error_ = false;
            break;
        default:
            break;
    }
}

void Tui::executeCommand(const std::string& raw_cmd) {
    // Trim leading/trailing whitespace
    size_t first = raw_cmd.find_first_not_of(" \t");
    if (first == std::string::npos) return;
    size_t last = raw_cmd.find_last_not_of(" \t");
    std::string cmd = raw_cmd.substr(first, last - first + 1);

    // If cmd starts with ':', strip it
    if (!cmd.empty() && cmd[0] == ':') {
        cmd = cmd.substr(1);
        first = cmd.find_first_not_of(" \t");
        if (first == std::string::npos) return;
        cmd = cmd.substr(first);
    }

    std::istringstream iss(cmd);
    std::string action;
    iss >> action;

    feedback_is_error_ = false;
    feedback_scroll_ = 0;

    if (action == "q" || action == "quit" || action == "exit") {
        running_ = false;
    } else if (action == "s" || action == "step") {
        int count = 1;
        iss >> count;
        if (count <= 0) count = 1;

        if (cpu_.state().halted) {
            feedback_message_ = "CPU is halted. Use ':reset' to re-run.";
            feedback_is_error_ = true;
            return;
        }

        int stepped = 0;
        StopInfo stop;
        while (stepped < count && !cpu_.state().halted) {
            stop = debugger_.stepInstruction();
            ++stepped;
            if (stop.reason == StopReason::Breakpoint || !stop.watch_hits.empty()) break;
        }
        if (stop.reason == StopReason::Breakpoint || !stop.watch_hits.empty()) {
            feedback_message_ = describeStop(stop);
            return;
        }
        feedback_message_ = "Stepped " + std::to_string(stepped) + " instruction(s). PC=" +
                            std::to_string(cpu_.state().pc) +
                            (cpu_.state().halted ? " [HALTED]" : "");
    } else if (action == "c" || action == "clock" || action == "tick") {
        int count = 1;
        iss >> count;
        if (count <= 0) count = 1;

        if (cpu_.state().halted) {
            feedback_message_ = "CPU is halted. Use ':reset' to re-run.";
            feedback_is_error_ = true;
            return;
        }

        int ticked = 0;
        StopInfo stop;
        while (ticked < count && !cpu_.state().halted) {
            stop = debugger_.stepCycle();
            ++ticked;
            if (stop.reason == StopReason::Breakpoint || !stop.watch_hits.empty()) break;
        }
        if (stop.reason == StopReason::Breakpoint || !stop.watch_hits.empty()) {
            feedback_message_ = describeStop(stop);
            return;
        }
        feedback_message_ = "Ticked " + std::to_string(ticked) + " clock(s). Cycles=" +
                            std::to_string(cpu_.cycles()) +
                            (cpu_.state().halted ? " [HALTED]" : "");
    } else if (action == "r" || action == "run") {
        if (cpu_.state().halted) {
            feedback_message_ = "CPU is halted. Use ':reset' to re-run.";
            feedback_is_error_ = true;
            return;
        }

        // Drain any pending keystroke (such as trailing newline) before starting run
        while (true) {
            KeyEvent k = terminal_.readKey(0);
            if (k.key == Key::None || k.key == Key::Eof) break;
        }

        const uint64_t initial_retired = cpu_.retired();
        bool stopped = false;
        StopInfo stop;
        bool interrupted = false;

        while (!cpu_.state().halted) {
            stop = debugger_.stepCycle();

            const uint64_t instrs_run = (cpu_.retired() >= initial_retired)
                                            ? (cpu_.retired() - initial_retired)
                                            : 0;
            // 0.1 ms (100 us) pause between clock pulses, reduced by 10% per 100 instructions
            const uint64_t hundreds = instrs_run / 100;
            const double current_pause_us = 1000.0 * std::pow(0.9, static_cast<double>(hundreds));

            if (stop.reason == StopReason::Breakpoint || !stop.watch_hits.empty()) {
                stopped = true;
                break;
            }

            // Check if user pressed any key to pause (ignore EOF)
            KeyEvent key_ev = terminal_.readKey(0);
            if (key_ev.key != Key::None && key_ev.key != Key::Eof) {
                if (key_ev.key == Key::Char && key_ev.ch == 'q') {
                    running_ = false;
                }
                interrupted = true;
                break;
            }

            render();

            if (current_pause_us >= 1.0) {
                std::this_thread::sleep_for(
                    std::chrono::microseconds(static_cast<int64_t>(current_pause_us)));
            }
        }

        if (interrupted) {
            feedback_message_ = "Execution paused at PC=" + std::to_string(cpu_.state().pc) +
                                ", cycle " + std::to_string(cpu_.cycles()) + ".";
        } else if (stopped) {
               feedback_message_ = describeStop(stop) + " (cycle " + std::to_string(cpu_.cycles()) + ").";
        } else if (cpu_.state().halted) {
            feedback_message_ = "Program halted after " + std::to_string(cpu_.retired()) +
                                " instructions, " + std::to_string(cpu_.cycles()) + " cycles.";
        }
        render();
    } else if (action == "b" || action == "break") {
        uint32_t pc = 0;
        if (!(iss >> pc)) {
            feedback_message_ = "Usage: :break <pc> [if <Rn> <op> <value>]";
            feedback_is_error_ = true;
        } else if (pc >= code_words_.size()) {
            feedback_message_ = "PC " + std::to_string(pc) + " is outside the program (0.." +
                                std::to_string(code_words_.empty() ? 0 : code_words_.size() - 1) + ").";
            feedback_is_error_ = true;
        } else {
            std::string kw;
            iss >> kw;
            if (kw.empty()) {
                if (debugger_.toggleBreakpoint(pc)) {
                    feedback_message_ = "Added breakpoint at PC=" + std::to_string(pc);
                } else {
                    feedback_message_ = "Removed breakpoint at PC=" + std::to_string(pc);
                }
            } else if (kw == "if") {
                const std::optional<Condition> cond = parseCondition(iss);
                if (!cond) {
                    feedback_message_ = "Usage: :break <pc> if <Rn> <op> <value>   (op: == != < <= > >=)";
                    feedback_is_error_ = true;
                } else if (debugger_.hasBreakpoint(pc)) {
                    debugger_.setBreakpointCondition(pc, cond);
                    feedback_message_ = "Breakpoint at PC=" + std::to_string(pc) +
                                        " now stops when " + formatCondition(*cond);
                } else {
                    debugger_.addBreakpoint(pc, cond);
                    feedback_message_ = "Added breakpoint at PC=" + std::to_string(pc) +
                                        " when " + formatCondition(*cond);
                }
            } else {
                feedback_message_ = "Usage: :break <pc> [if <Rn> <op> <value>]";
                feedback_is_error_ = true;
            }
        }
    } else if (action == "cb" || action == "clearbreak") {
        debugger_.clearBreakpoints();
        feedback_message_ = "All breakpoints cleared.";
    } else if (action == "cond") {
        uint32_t pc = 0;
        if (!(iss >> pc)) {
            feedback_message_ = "Usage: :cond <pc> [<Rn> <op> <value>]  (no condition = clear it)";
            feedback_is_error_ = true;
        } else if (!debugger_.hasBreakpoint(pc)) {
            feedback_message_ = "No breakpoint at PC=" + std::to_string(pc) + ". Use :break first.";
            feedback_is_error_ = true;
        } else {
            std::string rest;
            std::getline(iss, rest);
            if (rest.find_first_not_of(" \t") == std::string::npos) {
                debugger_.setBreakpointCondition(pc, std::nullopt);
                feedback_message_ = "Cleared condition on breakpoint at PC=" + std::to_string(pc);
            } else {
                std::istringstream cond_stream(rest);
                const std::optional<Condition> cond = parseCondition(cond_stream);
                if (!cond) {
                    feedback_message_ = "Usage: :cond <pc> <Rn> <op> <value>   (op: == != < <= > >=)";
                    feedback_is_error_ = true;
                } else {
                    debugger_.setBreakpointCondition(pc, cond);
                    feedback_message_ = "Breakpoint at PC=" + std::to_string(pc) +
                                        " now stops when " + formatCondition(*cond);
                }
            }
        }
    } else if (action == "enable" || action == "disable") {
        const bool on = (action == "enable");
        std::string what, num;
        iss >> what >> num;
        const bool numeric = !num.empty() && num.size() <= 9 &&
                             num.find_first_not_of("0123456789") == std::string::npos;
        if ((what != "break" && what != "watch") || !numeric) {
            feedback_message_ = "Usage: :" + action + " break <pc> | :" + action + " watch <id>";
            feedback_is_error_ = true;
        } else {
            const unsigned long n = std::stoul(num);
            const bool ok = (what == "break")
                ? debugger_.enableBreakpoint(static_cast<uint32_t>(n), on)
                : debugger_.enableWatchpoint(static_cast<int>(n), on);
            if (ok) {
                feedback_message_ = std::string(what == "break" ? "Breakpoint at PC=" : "Watchpoint #") +
                                    num + (on ? " enabled." : " disabled.");
            } else {
                feedback_message_ = std::string("No ") + (what == "break" ? "breakpoint at PC=" : "watchpoint #") +
                                    num + ".";
                feedback_is_error_ = true;
            }
        }
    } else if (action == "breaks" || action == "lb") {
        const auto& bps = debugger_.breakpoints();
        if (bps.empty()) {
            feedback_message_ = "No breakpoints.";
        } else {
            std::ostringstream oss;
            bool first = true;
            for (const auto& entry : bps) {
                const Breakpoint& bp = entry.second;
                if (!first) oss << " | ";
                first = false;
                oss << "PC=" << bp.pc << (bp.enabled ? " [on]" : " [off]");
                if (bp.condition) oss << " if " << formatCondition(*bp.condition);
                oss << " hits=" << bp.hits;
            }
            feedback_message_ = oss.str();
        }
    } else if (action == "w" || action == "watch") {
        std::string kind;
        std::string target;
        iss >> kind >> target;
        if (kind == "mem" && !target.empty() &&
            target.find_first_not_of("0123456789") == std::string::npos) {
            const int id = debugger_.addMemoryWatchpoint(static_cast<uint32_t>(std::stoul(target)));
            if (id < 0) {
                feedback_message_ = "Address " + target + " is outside data memory.";
                feedback_is_error_ = true;
            } else {
                feedback_message_ = "Watchpoint #" + std::to_string(id) + " on mem[" + target + "]";
            }
        } else if (kind == "reg" && target.size() >= 2 && (target[0] == 'R' || target[0] == 'r') &&
                   target.find_first_not_of("0123456789", 1) == std::string::npos &&
                   std::stoul(target.substr(1)) < static_cast<unsigned long>(kNumGPRegisters)) {
            const unsigned long n = std::stoul(target.substr(1));
            const int id = debugger_.addRegisterWatchpoint(static_cast<Register>(n));
            feedback_message_ = "Watchpoint #" + std::to_string(id) + " on R" + std::to_string(n);
        } else {
            feedback_message_ = "Usage: :watch mem <addr> | :watch reg <R0-R15>";
            feedback_is_error_ = true;
        }
    } else if (action == "uw" || action == "unwatch") {
        std::string which;
        iss >> which;
        if (which == "all") {
            debugger_.clearWatchpoints();
            feedback_message_ = "All watchpoints cleared.";
        } else if (!which.empty() && which.find_first_not_of("0123456789") == std::string::npos &&
                   debugger_.removeWatchpoint(std::stoi(which))) {
            feedback_message_ = "Removed watchpoint #" + which;
        } else {
            feedback_message_ = "Usage: :unwatch <id> | :unwatch all";
            feedback_is_error_ = true;
        }
    } else if (action == "watches" || action == "lw") {
        const auto& wps = debugger_.watchpoints();
        if (wps.empty()) {
            feedback_message_ = "No watchpoints.";
        } else {
            std::ostringstream oss;
            bool first = true;
            for (const auto& entry : wps) {
                const Watchpoint& w = entry.second;
                if (!first) oss << " | ";
                first = false;
                oss << "#" << w.id << " ";
                if (w.kind == WatchKind::Register) oss << "R" << w.target;
                else                               oss << "mem[" << w.target << "]";
                oss << (w.enabled ? " [on]" : " [off]")
                    << " last=" << w.last << " hits=" << w.hits;
            }
            feedback_message_ = oss.str();
        }
    } else if (action == "save" || action == "w") {
        std::string path;
        iss >> path;
        if (!path.empty()) filename_ = path;
        if (filename_.empty()) {
            feedback_message_ = "No filename. Use :save <path>.";
            feedback_is_error_ = true;
        } else if (raw_assembly_.empty()) {
            feedback_message_ = "No assembly buffer to save.";
            feedback_is_error_ = true;
        } else {
            std::ofstream out(filename_);
            if (!out) {
                feedback_message_ = "Cannot write " + filename_;
                feedback_is_error_ = true;
            } else {
                for (const std::string& line : raw_assembly_) out << line << '\n';
                out.close();
                if (!out) {
                    feedback_message_ = "Cannot write " + filename_;
                    feedback_is_error_ = true;
                } else {
                    feedback_message_ = "Saved " + std::to_string(raw_assembly_.size()) +
                                        " line(s) to " + filename_;
                }
            }
        }
    } else if (action == "reset") {
        debugger_.reset();
        bin_scroll_ = 0;
        asm_scroll_ = 0;
        auto_scroll_ = true;
        feedback_message_ = "CPU reset to initial state.";
    } else if (action == "config") {
        std::string what;
        std::string value;
        iss >> what >> value;
        feedback_is_error_ = false;
        if (what.empty()) {
            openConfigDialog();
        } else if (what == "stage" && (value == "4" || value == "6")) {
            const PipelineVariant chosen = value == "6" ? PipelineVariant::SixStage
                                                        : PipelineVariant::FourStage;
            cpu_.reconfigure(chosen, cpu_.encoding());
            feedback_message_ = "config: " + variantName(chosen) + " pipeline - run reset.";
        } else if (what == "rom" && (value == "horiz" || value == "vert")) {
            const MicrocodeEncoding chosen = value == "vert" ? MicrocodeEncoding::Vertical
                                                             : MicrocodeEncoding::Horizontal;
            cpu_.reconfigure(cpu_.variant(), chosen);
            feedback_message_ = std::string("config: ") + (value == "vert" ? "vertical" : "horizontal") +
                                " ROM - run reset.";
        } else if (what == "forward" && (value == "on" || value == "off")) {
            cpu_.setForwarding(value == "on");
            feedback_message_ = "config: forwarding " + value;
        } else if (what == "binary" && (value == "decimal" || value == "hex" || value == "float")) {
            interpretation_ = value == "hex" ? BinaryInterpretation::Hexadecimal :
                value == "float" ? BinaryInterpretation::FloatingPoint : BinaryInterpretation::Decimal;
            feedback_message_ = std::string("config: binary interpretation ") + interpretationName(interpretation_);
        } else {
            feedback_message_ = "Usage: :config [stage 4|6] [rom horiz|vert] [forward on|off] [binary decimal|hex|float]";
            feedback_is_error_ = true;
        }
    } else if (action == "trace") {
        show_trace_ = !show_trace_;
        trace_scroll_ = 0;
        feedback_message_ = show_trace_ ? "Hazard timeline shown." : "Hazard timeline hidden.";
        feedback_is_error_ = false;
    } else if (action == "mem_focus") {
        uint32_t addr = 0;
        if (!(iss >> addr)) {
            feedback_message_ = "Usage: :mem_focus <address>";
            feedback_is_error_ = true;
        } else {
            mem_follow_sp_ = false;
            mem_anchor_ = std::min(addr, static_cast<uint32_t>(cpu_.memory().size()));
            feedback_message_ = "memory view at " + std::to_string(mem_anchor_);
            feedback_is_error_ = false;
        }
    } else if (action == "mem") {
        std::string range_str;
        iss >> range_str;
        size_t colon = range_str.find(':');
        if (colon != std::string::npos) {
            uint32_t start = static_cast<uint32_t>(std::stoul(range_str.substr(0, colon)));
            uint32_t count = static_cast<uint32_t>(std::stoul(range_str.substr(colon + 1)));
            std::ostringstream oss;
            oss << "MEM[" << start << ".." << (start + count) << "]: ";
            for (uint32_t i = 0; i < count && start + i < cpu_.memory().size(); ++i) {
                oss << cpu_.memory()[start + i] << " ";
            }
            feedback_message_ = oss.str();
            mem_follow_sp_ = false;
            mem_anchor_ = std::min(start, static_cast<uint32_t>(cpu_.memory().size()));
        } else {
            feedback_message_ = "Usage: :mem <start>:<count> (e.g. :mem 4090:5)";
            feedback_is_error_ = true;
        }
    } else if (action == "load" || action == "l") {
        std::string path;
        iss >> path;
        if (path.empty()) {
            feedback_message_ = "Usage: :load <path/to/program.asm>";
            feedback_is_error_ = true;
        } else {
            loadProgram(path);
        }
    } else if (action == "assemble" || action == "asm") {
        AssemblyResult assembled = assembleLines(raw_assembly_);
        if (!assembled.errors.empty()) {
            feedback_message_ = formatAssemblyErrors(assembled.errors);
            if (!assembled.warnings.empty()) {
                feedback_message_ += "\n" + formatAssemblyWarnings(assembled.warnings);
            }
            feedback_is_error_ = true;
        } else if (assembled.words.empty()) {
            feedback_message_ = "Assemble failed: produced 0 instructions";
            feedback_is_error_ = true;
        } else {
            installProgram(std::move(assembled.words));
            assembly_dirty_ = false;
            feedback_message_ = "Assembled " + std::to_string(code_words_.size()) + " instructions.";
            if (!assembled.warnings.empty()) {
                feedback_message_ += "\n" + formatAssemblyWarnings(assembled.warnings);
            }
            feedback_is_error_ = false;
        }
    } else if (action == "h" || action == "help" || action == "?") {
        feedback_message_ = commandHelp();
    } else {
        feedback_message_ = "Unknown command '" + action + "'. Type ':help' for available commands.";
        feedback_is_error_ = true;
    }
}

void Tui::installProgram(std::vector<uint32_t> words) {
    std::fill(cpu_.memory().begin(), cpu_.memory().end(), 0);
    cpu_.loadWords(words);
    code_words_ = cpu_.codeWords();
    original_words_ = std::move(words);
    debugger_.clearBreakpoints();
    debugger_.clearWatchpoints();
    bin_scroll_ = 0;
    asm_scroll_ = 0;
    rom_scroll_ = 0;
    auto_scroll_ = true;
}

bool Tui::loadProgram(const std::string& raw_path) {
    feedback_scroll_ = 0;
    ProgramFile file = readProgramFile(raw_path);
    if (!file.error.empty()) {
        feedback_message_ = "Error: " + file.error;
        feedback_is_error_ = true;
        return false;
    }

    std::vector<uint32_t> new_words;
    std::vector<AssemblyError> warnings;
    if (file.kind == SourceKind::Assembly) {
        AssemblyResult assembled = assembleLines(file.lines);
        if (!assembled.errors.empty()) {
            feedback_message_ = formatAssemblyErrors(assembled.errors);
            if (!assembled.warnings.empty()) {
                feedback_message_ += "\n" + formatAssemblyWarnings(assembled.warnings);
            }
            feedback_is_error_ = true;
            return false;
        }
        warnings = std::move(assembled.warnings);
        new_words = std::move(assembled.words);
    } else {
        new_words = std::move(file.words);
    }

    if (new_words.empty()) {
        feedback_message_ = "Warning: file loaded but produced 0 instructions";
        feedback_is_error_ = true;
        return false;
    }

    // Apply new program
    raw_assembly_ = std::move(file.lines);
    filename_ = file.path;
    assembly_dirty_ = false;
    installProgram(std::move(new_words));

    feedback_message_ = "Loaded and compiled " + std::to_string(code_words_.size()) + " instructions from " + file.path;
    if (!warnings.empty()) {
        feedback_message_ += "\n" + formatAssemblyWarnings(warnings);
    }
    feedback_is_error_ = false;
    return true;
}

} // namespace risc201
