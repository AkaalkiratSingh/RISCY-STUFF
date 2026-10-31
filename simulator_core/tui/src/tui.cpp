#include "tui/tui.h"

#include "assembler/assembler.h"
#include "simulator_core/decoder.h"
#include "utils/utils.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>

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

const char* exceptionName(ExceptionCode code) {
    switch (code) {
        case ExceptionCode::NONE:                  return "NONE";
        case ExceptionCode::STACK_OVERFLOW:        return "STACK_OVERFLOW";
        case ExceptionCode::STACK_UNDERFLOW:       return "STACK_UNDERFLOW";
        case ExceptionCode::INVALID_MEMORY_ACCESS: return "INVALID_MEMORY_ACCESS";
        case ExceptionCode::DIVIDE_BY_ZERO:        return "DIVIDE_BY_ZERO";
        case ExceptionCode::ILLEGAL_INSTRUCTION:   return "ILLEGAL_INSTRUCTION";
    }
    return "UNKNOWN";
}

} // namespace

Tui::Tui(Cpu& cpu,
         std::vector<std::string> raw_assembly,
         std::vector<uint32_t> code_words,
         const std::string& filename,
         bool show_trace)
    : cpu_(cpu),
      raw_assembly_(std::move(raw_assembly)),
      code_words_(std::move(code_words)),
      original_words_(code_words_),
      filename_(filename),
      show_trace_(show_trace) {
    cpu_.setTracing(true);
}

std::string Tui::formatInstr(const Instruction& in) const {
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
        case Opcode::PUSH:
        case Opcode::POP:
            return name + " " + formatReg(rd);
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
    screen_.clear();

    const int total_rows = screen_.rows();
    const int total_cols = screen_.cols();

    // Heights:
    // Bottom: Command Line (height 3) + Command Feedback (height 3) = 6 rows
    const int bottom_h = (total_rows >= 20) ? 6 : 4;
    const int cmd_h = 3;
    const int fb_h = std::max(2, bottom_h - cmd_h);
    int trace_h = 0;
    if (show_trace_) {
        trace_h = std::min(7, total_rows - (cmd_h + fb_h) - 10);
        if (trace_h < 3) trace_h = 0;
    }
    const int top_h = std::max(10, total_rows - (cmd_h + fb_h + trace_h));

    // Widths: 3-column top
    // Column 1 (Assembly): ~28%
    // Column 2 (Binary):   ~34%
    // Column 3 (Right):    Remainder
    int col1_w = (total_cols * 28) / 100;
    int col2_w = (total_cols * 34) / 100;
    if (col1_w < 20) col1_w = 20;
    if (col2_w < 26) col2_w = 26;
    int col3_w = total_cols - col1_w - col2_w;
    if (col3_w < 28) {
        col3_w = 28;
        col1_w = std::max(18, (total_cols - col3_w) * 45 / 100);
        col2_w = std::max(22, total_cols - col1_w - col3_w);
    }

    // Panes render
    renderRawAssembly(0, 0, top_h, col1_w);
    renderAssembledBinary(0, col1_w, top_h, col2_w);

    // Right column split:
    // Top: REGISTERS (height: 8 rows fits 4x4 grid + PC/SP/FLAGS)
    const int reg_h = std::min(top_h - 5, std::max(8, (top_h * 50) / 100));
    const int sub_h = top_h - reg_h;
    const int stats_w = (col3_w * 54) / 100;
    const int stack_w = col3_w - stats_w;

    renderRegisters(0, col1_w + col2_w, reg_h, col3_w);
    renderExtraStats(reg_h, col1_w + col2_w, sub_h, stats_w);
    renderStack(reg_h, col1_w + col2_w + stats_w, sub_h, stack_w);

    if (trace_h > 0) renderHazardTimeline(top_h, 0, trace_h, total_cols);

    // Bottom panes
    renderCommandLine(top_h + trace_h, 0, cmd_h, total_cols);
    renderFeedback(top_h + trace_h + cmd_h, 0, fb_h, total_cols);

    screen_.flush();
}

void Tui::renderRawAssembly(int r, int c, int h, int w) {
    std::string title = "RAW ASSEMBLY";
    if (!filename_.empty()) {
        title += " (" + filename_ + ")";
    }
    screen_.drawBox(r, c, h, w, title,
                    Style{Color::White, Color::Default, false},
                    Style{Color::Cyan, Color::Default, true});

    const int inner_h = h - 2;
    const int inner_w = w - 2;
    if (inner_h <= 0 || inner_w <= 0) return;

    for (int i = 0; i < inner_h; ++i) {
        size_t idx = static_cast<size_t>(asm_scroll_ + i);
        if (idx >= raw_assembly_.size()) break;

        const std::string& line = raw_assembly_[idx];
        std::string num_prefix = std::to_string(idx + 1);
        while (num_prefix.size() < 3) num_prefix = " " + num_prefix;
        num_prefix += " ";

        Style text_style{Color::Default, Color::Default, false};
        // Check for comment
        if (!line.empty() && line.find_first_not_of(" \t") != std::string::npos &&
            line[line.find_first_not_of(" \t")] == ';') {
            text_style.fg = Color::BrightBlack;
        } else if (line.find(':') != std::string::npos) {
            text_style.fg = Color::BrightYellow;
            text_style.bold = true;
        }

        screen_.drawText(r + 1 + i, c + 1, num_prefix, Style{Color::BrightBlack, Color::Default, false});
        screen_.drawText(r + 1 + i, c + 1 + static_cast<int>(num_prefix.size()), line, text_style, inner_w - static_cast<int>(num_prefix.size()));
    }
}

void Tui::renderAssembledBinary(int r, int c, int h, int w) {
    screen_.drawBox(r, c, h, w, "ASSEMBLED BINARY",
                    Style{Color::White, Color::Default, false},
                    Style{Color::BrightGreen, Color::Default, true});

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

    for (int i = 0; i < inner_h; ++i) {
        size_t pc = static_cast<size_t>(bin_scroll_ + i);
        if (pc >= code_words_.size()) break;

        const uint32_t word = code_words_[pc];
        const Instruction instr = decodeInstruction(word);
        const std::string mnemonic = formatInstr(instr);
        const std::string stage = formatActiveStage(static_cast<uint32_t>(pc));
        const bool is_active_pc = (pc == current_pc);
        const bool has_break = (breakpoints_.find(static_cast<uint32_t>(pc)) != breakpoints_.end());

        std::ostringstream oss;
        oss << (has_break ? "B" : " ")
            << "[" << std::setw(3) << std::right << pc << "] "
            << "0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << word << " "
            << std::dec << std::setfill(' ')
            << mnemonic;

        if (!stage.empty()) {
            oss << " ◄[" << stage << "]";
        } else if (is_active_pc) {
            oss << " ◄";
        }

        std::string row_str = oss.str();
        Style row_style{Color::Default, Color::Default, false};
        if (is_active_pc) {
            row_style.fg = Color::BrightGreen;
            row_style.bold = true;
            row_style.reverse = true;
        } else if (!stage.empty()) {
            row_style.fg = Color::BrightCyan;
            row_style.bold = true;
        } else if (has_break) {
            row_style.fg = Color::BrightRed;
        }

        screen_.drawText(r + 1 + i, c + 1, row_str, row_style, inner_w);
    }
}

void Tui::renderRegisters(int r, int c, int h, int w) {
    screen_.drawBox(r, c, h, w, "REGISTERS",
                    Style{Color::White, Color::Default, false},
                    Style{Color::BrightBlue, Color::Default, true});

    const int inner_h = h - 2;
    const int inner_w = w - 2;
    if (inner_h <= 0 || inner_w <= 0) return;

    const CpuState& st = cpu_.state();

    int num_cols = (inner_w >= 36) ? 4 : 2;
    int col_width = inner_w / num_cols;

    for (int i = 0; i < kNumGPRegisters; ++i) {
        int grid_r = i / num_cols;
        int grid_c = i % num_cols;
        if (grid_r >= inner_h - 2) break;

        char buf[32];
        snprintf(buf, sizeof(buf), "R%02d:%-5d", i, st.registers[static_cast<size_t>(i)]);
        Style reg_style{Color::Default, Color::Default, false};
        if (st.registers[static_cast<size_t>(i)] != 0) {
            reg_style.fg = Color::BrightWhite;
            reg_style.bold = true;
        } else {
            reg_style.fg = Color::BrightBlack;
        }
        screen_.drawText(r + 1 + grid_r, c + 1 + grid_c * col_width, buf, reg_style, col_width);
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
                    Style{Color::BrightMagenta, Color::Default, true});

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

    for (size_t i = 0; i < stat_lines.size() && static_cast<int>(i) < inner_h; ++i) {
        screen_.drawText(r + 1 + static_cast<int>(i), c + 1, stat_lines[i],
                         Style{Color::Default, Color::Default, false}, inner_w);
    }
}

void Tui::renderStack(int r, int c, int h, int w) {
    screen_.drawBox(r, c, h, w, "STACK",
                    Style{Color::White, Color::Default, false},
                    Style{Color::BrightYellow, Color::Default, true});

    const int inner_h = h - 2;
    const int inner_w = w - 2;
    if (inner_h <= 0 || inner_w <= 0) return;

    const uint32_t sp = cpu_.state().sp;
    const auto& memory = cpu_.memory();
    const uint32_t mem_sz = static_cast<uint32_t>(memory.size());

    int start_addr = static_cast<int>(std::min(sp + 1, mem_sz));
    for (int i = 0; i < inner_h; ++i) {
        int addr = start_addr - i;
        if (addr < 0) break;

        std::ostringstream oss;
        if (static_cast<uint32_t>(addr) >= mem_sz) {
            oss << "[" << std::setw(4) << addr << "] (top)";
            if (sp >= mem_sz) oss << " ◄SP";
            screen_.drawText(r + 1 + i, c + 1, oss.str(), Style{Color::BrightYellow, Color::Default, false}, inner_w);
            continue;
        }

        int32_t val = memory[static_cast<size_t>(addr)];
        oss << "[" << std::setw(4) << addr << "] " << std::setw(6) << val;
        const bool is_sp = (static_cast<uint32_t>(addr) == sp);
        if (is_sp) oss << " ◄SP";

        Style line_style{Color::Default, Color::Default, false};
        if (is_sp) {
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
                    Style{Color::BrightCyan, Color::Default, true});

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
                    Style{Color::BrightGreen, Color::Default, true});

    if (h < 3) return;

    if (command_mode_) {
        std::string prompt = ": " + command_buffer_;
        screen_.drawText(r + 1, c + 2, prompt, Style{Color::BrightWhite, Color::Default, true});
        // Render block cursor
        screen_.setCell(r + 1, c + 2 + static_cast<int>(prompt.size()), " ",
                        Style{Color::Default, Color::BrightWhite, false, false, true});
    } else {
        std::string hint = "[NORMAL MODE]  ':' cmd | 's' step | 'c' clock | 'r' run | 'j'/'k' scroll | 'T' trace | 'q' quit";
        screen_.drawText(r + 1, c + 2, hint, Style{Color::BrightBlack, Color::Default, false}, w - 4);
    }
}

void Tui::renderFeedback(int r, int c, int h, int w) {
    screen_.drawBox(r, c, h, w, "COMMAND FEEDBACK",
                    Style{Color::White, Color::Default, false},
                    Style{Color::Cyan, Color::Default, true});

    if (h < 3) return;

    Style fb_style{Color::BrightWhite, Color::Default, false};
    if (feedback_is_error_) {
        fb_style.fg = Color::BrightRed;
        fb_style.bold = true;
    } else {
        fb_style.fg = Color::BrightGreen;
    }

    screen_.drawText(r + 1, c + 2, feedback_message_, fb_style, w - 4);
}

void Tui::handleKey(const KeyEvent& ev) {
    if (command_mode_) {
        if (ev.key == Key::Escape) {
            command_mode_ = false;
            command_buffer_.clear();
            feedback_message_ = "Command input cancelled.";
            feedback_is_error_ = false;
        } else if (ev.key == Key::Enter) {
            command_mode_ = false;
            executeCommand(command_buffer_);
            command_buffer_.clear();
        } else if (ev.key == Key::Backspace) {
            if (!command_buffer_.empty()) {
                command_buffer_.pop_back();
            }
        } else if (ev.key == Key::Char && ev.ch >= 32 && ev.ch <= 126) {
            command_buffer_.push_back(ev.ch);
        }
        return;
    }

    // Normal mode keys
    switch (ev.key) {
        case Key::Char:
            switch (ev.ch) {
                case ':':
                    command_mode_ = true;
                    command_buffer_.clear();
                    feedback_message_ = "Type command and press Enter (or Esc to cancel). Commands: step, clock, run, break, reset, help, quit";
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
                case 'j':
                    auto_scroll_ = false;
                    ++bin_scroll_;
                    ++asm_scroll_;
                    break;
                case 'k':
                    auto_scroll_ = false;
                    bin_scroll_ = std::max(0, bin_scroll_ - 1);
                    asm_scroll_ = std::max(0, asm_scroll_ - 1);
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
            auto_scroll_ = false;
            ++bin_scroll_;
            ++asm_scroll_;
            break;
        case Key::Up:
            auto_scroll_ = false;
            bin_scroll_ = std::max(0, bin_scroll_ - 1);
            asm_scroll_ = std::max(0, asm_scroll_ - 1);
            break;
        case Key::PageDown:
            auto_scroll_ = false;
            bin_scroll_ += 10;
            asm_scroll_ += 10;
            break;
        case Key::PageUp:
            auto_scroll_ = false;
            bin_scroll_ = std::max(0, bin_scroll_ - 10);
            asm_scroll_ = std::max(0, asm_scroll_ - 10);
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
        while (stepped < count && !cpu_.state().halted) {
            cpu_.stepInstruction();
            ++stepped;
            if (breakpoints_.find(cpu_.state().pc) != breakpoints_.end()) {
                feedback_message_ = "Breakpoint hit at PC=" + std::to_string(cpu_.state().pc);
                return;
            }
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
        while (ticked < count && !cpu_.state().halted) {
            cpu_.step();
            ++ticked;
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
        bool hit_break = false;
        bool interrupted = false;

        while (!cpu_.state().halted) {
            cpu_.step();

            const uint64_t instrs_run = (cpu_.retired() >= initial_retired)
                                            ? (cpu_.retired() - initial_retired)
                                            : 0;
            // 0.1 ms (100 us) pause between clock pulses, reduced by 10% per 100 instructions
            const uint64_t hundreds = instrs_run / 100;
            const double current_pause_us = 1000.0 * std::pow(0.9, static_cast<double>(hundreds));

            if (breakpoints_.find(cpu_.state().pc) != breakpoints_.end()) {
                hit_break = true;
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
        } else if (hit_break) {
            feedback_message_ = "Breakpoint hit at PC=" + std::to_string(cpu_.state().pc) +
                                " (cycle " + std::to_string(cpu_.cycles()) + ").";
        } else if (cpu_.state().halted) {
            feedback_message_ = "Program halted after " + std::to_string(cpu_.retired()) +
                                " instructions, " + std::to_string(cpu_.cycles()) + " cycles.";
        }
        render();
    } else if (action == "b" || action == "break") {
        uint32_t pc = 0;
        if (iss >> pc) {
            if (breakpoints_.find(pc) != breakpoints_.end()) {
                breakpoints_.erase(pc);
                feedback_message_ = "Removed breakpoint at PC=" + std::to_string(pc);
            } else {
                breakpoints_.insert(pc);
                feedback_message_ = "Added breakpoint at PC=" + std::to_string(pc);
            }
        } else {
            feedback_message_ = "Usage: :break <pc>";
            feedback_is_error_ = true;
        }
    } else if (action == "cb" || action == "clearbreak") {
        breakpoints_.clear();
        feedback_message_ = "All breakpoints cleared.";
    } else if (action == "reset") {
        std::fill(cpu_.memory().begin(), cpu_.memory().end(), 0);
        cpu_.loadWords(original_words_);
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
            feedback_message_ = "config: " + variantName(cpu_.variant()) + ", " +
                                (cpu_.forwarding() ? "forwarding" : "no-forwarding") + ", " +
                                (cpu_.encoding() == MicrocodeEncoding::Vertical ? "vertical" : "horizontal") +
                                " ROM";
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
        } else {
            feedback_message_ = "Usage: :config [stage 4|6] [rom horiz|vert] [forward on|off]";
            feedback_is_error_ = true;
        }
    } else if (action == "trace") {
        show_trace_ = !show_trace_;
        trace_scroll_ = 0;
        feedback_message_ = show_trace_ ? "Hazard timeline shown." : "Hazard timeline hidden.";
        feedback_is_error_ = false;
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
    } else if (action == "h" || action == "help" || action == "?") {
        feedback_message_ = "Commands: :load <path>, :step [n], :clock [n], :run, :break <pc>, :clearbreak, :reset, :mem <s:c>, :config [stage 4|6] [rom horiz|vert] [forward on|off], :trace, :quit. Keys: s/n/Enter (step), c (clock), r (run), j/k (scroll), T (hazard timeline), [ ] (scroll timeline), q (quit)";
    } else {
        feedback_message_ = "Unknown command '" + action + "'. Type ':help' for available commands.";
        feedback_is_error_ = true;
    }
}

bool Tui::loadProgram(const std::string& raw_path) {
    std::string path = trim(raw_path);
    if (!path.empty() && (path.front() == '"' || path.front() == '\'') && path.front() == path.back()) {
        path = path.substr(1, path.size() - 2);
    }
    if (path.empty()) {
        feedback_message_ = "Error: empty file path";
        feedback_is_error_ = true;
        return false;
    }

    std::vector<std::string> new_raw_lines;
    std::vector<uint32_t> new_words;

    if (hasSuffix(path, ".hex")) {
        if (!readHexWords(path, new_words)) {
            feedback_message_ = "Error: failed to read hex file " + path;
            feedback_is_error_ = true;
            return false;
        }
        for (uint32_t w : new_words) {
            Instruction instr = decodeInstruction(w);
            new_raw_lines.push_back(formatInstr(instr));
        }
    } else if (hasSuffix(path, ".bin")) {
        if (!readBinaryWords(path, new_words)) {
            feedback_message_ = "Error: failed to read binary file " + path;
            feedback_is_error_ = true;
            return false;
        }
        for (uint32_t w : new_words) {
            Instruction instr = decodeInstruction(w);
            new_raw_lines.push_back(formatInstr(instr));
        }
    } else {
        std::ifstream file(path);
        if (!file) {
            feedback_message_ = "Error: cannot open file " + path;
            feedback_is_error_ = true;
            return false;
        }
        std::string line;
        while (std::getline(file, line)) {
            new_raw_lines.push_back(line);
        }

        Assembler assembler;
        assembler.assemblePass1(new_raw_lines);
        const std::vector<Instruction> program = assembler.assemblePass2();

        if (!assembler.errors().empty()) {
            const auto& err = assembler.errors().front();
            feedback_message_ = "Assembly error (line " + std::to_string(err.line_number) + "): " + err.message;
            feedback_is_error_ = true;
            return false;
        }

        for (const auto& instr : program) {
            new_words.push_back(assembler.encodeInstruction(instr));
        }
    }

    if (new_words.empty()) {
        feedback_message_ = "Warning: file loaded but produced 0 instructions";
        feedback_is_error_ = true;
        return false;
    }

    // Apply new program
    std::fill(cpu_.memory().begin(), cpu_.memory().end(), 0);
    cpu_.loadWords(new_words);

    raw_assembly_ = std::move(new_raw_lines);
    code_words_ = std::move(new_words);
    original_words_ = code_words_;
    filename_ = path;

    breakpoints_.clear();
    bin_scroll_ = 0;
    asm_scroll_ = 0;
    auto_scroll_ = true;

    feedback_message_ = "Loaded and compiled " + std::to_string(code_words_.size()) + " instructions from " + path;
    feedback_is_error_ = false;
    return true;
}

} // namespace risc201
