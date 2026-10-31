#pragma once

#include "simulator_core/cpu.h"
#include "tui/screen.h"
#include "tui/terminal.h"

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace risc201 {

enum class Mode { Normal, Inspect, Command, Dialog };
enum class Pane { Assembly, Binary, Registers, Stats, Memory, Timeline };

class Tui {
public:
    Tui(Cpu& cpu,
        std::vector<std::string> raw_assembly,
        std::vector<uint32_t> code_words,
        const std::string& filename = "",
        bool show_trace = false);

    void run();
    bool loadProgram(const std::string& path);

private:
    void render();
    void handleKey(const KeyEvent& ev);
    void executeCommand(const std::string& cmd);

    void cyclePane();
    void clampFocusedScroll();
    bool paneFocused(Pane p) const { return mode_ == Mode::Inspect && pane_ == p; }
    int* focusedScroll();
    std::string paneName() const;

    void renderRawAssembly(int r, int c, int h, int w);
    void renderAssembledBinary(int r, int c, int h, int w);
    void renderRegisters(int r, int c, int h, int w);
    void renderExtraStats(int r, int c, int h, int w);
    void renderStack(int r, int c, int h, int w);
    void renderHazardTimeline(int r, int c, int h, int w);
    void renderCommandLine(int r, int c, int h, int w);
    void renderFeedback(int r, int c, int h, int w);

    std::string formatInstr(const Instruction& in) const;
    std::string formatActiveStage(uint32_t pc) const;

    Cpu& cpu_;
    std::vector<std::string> raw_assembly_;
    std::vector<uint32_t> code_words_;
    std::vector<uint32_t> original_words_;
    std::string filename_;

    Terminal terminal_;
    Screen screen_;

    bool running_ = true;
    Mode mode_ = Mode::Normal;
    Pane pane_ = Pane::Assembly;
    std::string command_buffer_;
    std::string feedback_message_ = "Welcome to RISCY201 TUI! Press 's' to step, 'c' for clock, 'r' to run, '?' for help, 'q' to quit.";
    bool feedback_is_error_ = false;

    int asm_scroll_ = 0;
    int bin_scroll_ = 0;
    int mem_scroll_ = 0;
    int asm_view_h_ = 0;
    int bin_view_h_ = 0;
    int mem_view_h_ = 0;
    bool auto_scroll_ = true;

    bool show_trace_ = false;
    int trace_scroll_ = 0;

    std::set<uint32_t> breakpoints_;
};

} // namespace risc201
