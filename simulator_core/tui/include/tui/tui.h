#pragma once

#include "simulator_core/cpu.h"
#include "simulator_core/debugger.h"
#include "tui/screen.h"
#include "tui/terminal.h"
#include "tui/value_format.h"

#include <cstdint>
#include <string>
#include <vector>

namespace risc201 {

enum class Mode { Normal, Inspect, Command, Dialog };
enum class Pane { Assembly, Binary, Registers, Stats, ControlRom, Memory, Timeline };

struct DialogRow {
    std::string label;
    std::string value;
};

class Tui {
public:
    Tui(Cpu& cpu,
        std::vector<std::string> raw_assembly,
        std::vector<uint32_t> program_words,
        const std::string& filename = "",
        bool show_trace = false);

    void run();
    bool loadProgram(const std::string& path);

private:
    void render();
    void handleKey(const KeyEvent& ev);
    void executeCommand(const std::string& cmd);
    void resetCompletion();

    void cyclePane();
    void clampFocusedScroll();
    void installProgram(std::vector<uint32_t> words);
    void handleEditorKey(const KeyEvent& ev);
    void openConfigDialog();
    void applyDialog();
    void cycleDialogValue();
    void renderDialog();
    bool paneFocused(Pane p) const { return mode_ == Mode::Inspect && pane_ == p; }
    int* focusedScroll();
    void scrollFocused(int delta);
    uint32_t memoryTop() const;
    std::string paneName() const;

    void renderRawAssembly(int r, int c, int h, int w);
    void renderAssembledBinary(int r, int c, int h, int w);
    void renderRegisters(int r, int c, int h, int w);
    void renderExtraStats(int r, int c, int h, int w);
    void renderControlRom(int r, int c, int h, int w);
    void renderMemory(int r, int c, int h, int w);
    void renderHazardTimeline(int r, int c, int h, int w);
    void renderCommandLine(int r, int c, int h, int w);
    void renderFeedback(int r, int c, int h, int w);

    std::string formatActiveStage(uint32_t pc) const;
    int registerColumns(int width) const;
    std::string describeStop(const StopInfo& info) const;

    Cpu& cpu_;
    Debugger debugger_;
    std::vector<std::string> raw_assembly_;
    std::vector<uint32_t> code_words_;
    std::vector<uint32_t> original_words_;
    std::string filename_;
    bool assembly_dirty_ = false;

    Terminal terminal_;
    Screen screen_;

    bool running_ = true;
    Mode mode_ = Mode::Normal;
    Pane pane_ = Pane::Assembly;
    std::string command_buffer_;
    std::vector<std::string> completion_matches_;
    std::string completion_base_;
    size_t completion_replace_start_ = 0;
    size_t completion_replace_len_ = 0;
    int completion_index_ = -1;
    std::string feedback_message_ = "Welcome to RISCY201 TUI! Press 's' to step, 'c' for clock, 'r' to run, '?' for help, 'q' to quit.";
    bool feedback_is_error_ = false;
    int feedback_scroll_ = 0;

    int asm_scroll_ = 0;
    int bin_scroll_ = 0;
    uint32_t mem_anchor_ = 0;
    bool mem_follow_sp_ = true;
    int asm_view_h_ = 0;
    int bin_view_h_ = 0;
    bool auto_scroll_ = true;

    bool editing_ = false;
    int edit_line_ = 0;
    int edit_col_ = 0;

    bool show_trace_ = false;
    int trace_scroll_ = 0;

    int rom_scroll_ = 0;

    std::vector<DialogRow> dialog_rows_;
    int dialog_selected_ = 0;
    BinaryInterpretation interpretation_ = BinaryInterpretation::Decimal;

};

} // namespace risc201
