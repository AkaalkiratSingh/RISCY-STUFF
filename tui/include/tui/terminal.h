#pragma once

#include <termios.h>
#include <string>

namespace risc201 {

enum class Key {
    None,
    Eof,
    Char,
    Enter,
    Backspace,
    Tab,
    Escape,
    Up,
    Down,
    Left,
    Right,
    PageUp,
    PageDown,
    Home,
    End
};

struct KeyEvent {
    Key key = Key::None;
    char ch = 0;
};

class Terminal {
public:
    Terminal();
    ~Terminal();

    Terminal(const Terminal&) = delete;
    Terminal& operator=(const Terminal&) = delete;

    bool enableRawMode();
    void disableRawMode();

    KeyEvent readKey(int timeout_ms = -1);

    static bool getWindowSize(int& rows, int& cols);

    static void enterAltScreen();
    static void exitAltScreen();
    static void hideCursor();
    static void showCursor();
    static void setCursorPosition(int row, int col);
    static void clearScreen();

private:
    termios original_termios_{};
    bool raw_mode_ = false;
};

} // namespace risc201
