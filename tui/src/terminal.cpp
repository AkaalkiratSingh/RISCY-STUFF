#include "tui/terminal.h"

#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>

namespace risc201 {

Terminal::Terminal() {
    enableRawMode();
    enterAltScreen();
    hideCursor();
    clearScreen();
}

Terminal::~Terminal() {
    clearScreen();
    showCursor();
    exitAltScreen();
    disableRawMode();
}

bool Terminal::enableRawMode() {
    if (raw_mode_) return true;
    if (tcgetattr(STDIN_FILENO, &original_termios_) == -1) return false;

    termios raw = original_termios_;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) return false;
    raw_mode_ = true;
    return true;
}

void Terminal::disableRawMode() {
    if (!raw_mode_) return;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios_);
    raw_mode_ = false;
}

KeyEvent Terminal::readKey(int timeout_ms) {
    struct pollfd pfd;
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;
    pfd.revents = 0;

    int ret = poll(&pfd, 1, timeout_ms);
    if (ret <= 0) {
        return {Key::None, 0};
    }

    char c = 0;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n == 0) {
        return {Key::Eof, 0};
    }
    if (n != 1) {
        return {Key::None, 0};
    }

    if (c == '\r' || c == '\n') {
        return {Key::Enter, 0};
    }
    if (c == 127 || c == 8) {
        return {Key::Backspace, 0};
    }
    if (c == '\t') {
        return {Key::Tab, 0};
    }

    if (c == 27) {
        // Check if there are escape sequence characters following
        struct pollfd pseq;
        pseq.fd = STDIN_FILENO;
        pseq.events = POLLIN;
        if (poll(&pseq, 1, 25) <= 0) {
            return {Key::Escape, 0};
        }

        char seq[4] = {0};
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return {Key::Escape, 0};

        if (seq[0] == '[') {
            if (poll(&pseq, 1, 25) <= 0) return {Key::Escape, 0};
            if (read(STDIN_FILENO, &seq[1], 1) != 1) return {Key::Escape, 0};

            if (seq[1] >= '0' && seq[1] <= '9') {
                if (poll(&pseq, 1, 25) <= 0) return {Key::Escape, 0};
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return {Key::Escape, 0};
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '1': return {Key::Home, 0};
                        case '3': return {Key::Backspace, 0};
                        case '4': return {Key::End, 0};
                        case '5': return {Key::PageUp, 0};
                        case '6': return {Key::PageDown, 0};
                        case '7': return {Key::Home, 0};
                        case '8': return {Key::End, 0};
                    }
                }
            } else {
                switch (seq[1]) {
                    case 'A': return {Key::Up, 0};
                    case 'B': return {Key::Down, 0};
                    case 'C': return {Key::Right, 0};
                    case 'D': return {Key::Left, 0};
                    case 'H': return {Key::Home, 0};
                    case 'F': return {Key::End, 0};
                }
            }
        } else if (seq[0] == 'O') {
            if (poll(&pseq, 1, 25) <= 0) return {Key::Escape, 0};
            if (read(STDIN_FILENO, &seq[1], 1) != 1) return {Key::Escape, 0};
            switch (seq[1]) {
                case 'H': return {Key::Home, 0};
                case 'F': return {Key::End, 0};
            }
        }
        return {Key::Escape, 0};
    }

    return {Key::Char, c};
}

bool Terminal::getWindowSize(int& rows, int& cols) {
    winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        rows = 24;
        cols = 80;
        return false;
    }
    rows = ws.ws_row;
    cols = ws.ws_col;
    return true;
}

void Terminal::enterAltScreen() {
    write(STDOUT_FILENO, "\033[?1049h", 8);
}

void Terminal::exitAltScreen() {
    write(STDOUT_FILENO, "\033[?1049l", 8);
}

void Terminal::hideCursor() {
    write(STDOUT_FILENO, "\033[?25l", 6);
}

void Terminal::showCursor() {
    write(STDOUT_FILENO, "\033[?25h", 6);
}

void Terminal::setCursorPosition(int row, int col) {
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "\033[%d;%dH", row + 1, col + 1);
    if (n > 0) {
        write(STDOUT_FILENO, buf, static_cast<size_t>(n));
    }
}

void Terminal::clearScreen() {
    write(STDOUT_FILENO, "\033[2J\033[H", 7);
}

} // namespace risc201
