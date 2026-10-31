#include "tui/screen.h"

#include <unistd.h>
#include <algorithm>

namespace risc201 {

Screen::Screen(int rows, int cols) : rows_(rows), cols_(cols), buffer_(rows * cols) {}

void Screen::resize(int rows, int cols) {
    if (rows <= 0 || cols <= 0) return;
    rows_ = rows;
    cols_ = cols;
    buffer_.assign(rows_ * cols_, Cell{});
}

void Screen::clear() {
    for (auto& cell : buffer_) {
        cell.glyph = " ";
        cell.style = Style{};
    }
}

void Screen::setCell(int r, int c, const std::string& glyph, Style style) {
    if (r < 0 || r >= rows_ || c < 0 || c >= cols_) return;
    Cell& cell = buffer_[r * cols_ + c];
    cell.glyph = glyph;
    cell.style = style;
}

void Screen::drawText(int r, int c, const std::string& text, Style style, int max_len) {
    if (r < 0 || r >= rows_ || c >= cols_) return;
    int cur_col = c;
    size_t i = 0;
    while (i < text.size() && (max_len < 0 || (cur_col - c) < max_len) && cur_col < cols_) {
        if (cur_col < 0) {
            // Skip characters until we reach visible column
            unsigned char ch = static_cast<unsigned char>(text[i]);
            size_t len = 1;
            if ((ch & 0x80) == 0) len = 1;
            else if ((ch & 0xE0) == 0xC0) len = 2;
            else if ((ch & 0xF0) == 0xE0) len = 3;
            else if ((ch & 0xF8) == 0xF0) len = 4;
            i += len;
            cur_col++;
            continue;
        }

        unsigned char ch = static_cast<unsigned char>(text[i]);
        size_t len = 1;
        if ((ch & 0x80) == 0) len = 1;
        else if ((ch & 0xE0) == 0xC0) len = 2;
        else if ((ch & 0xF0) == 0xE0) len = 3;
        else if ((ch & 0xF8) == 0xF0) len = 4;

        if (i + len > text.size()) len = text.size() - i;
        std::string glyph = text.substr(i, len);
        setCell(r, cur_col, glyph, style);
        i += len;
        cur_col++;
    }
}

void Screen::recolour(int r, int c, int h, int w, int rgb) {
    for (int row = r; row < r + h; ++row) {
        for (int col = c; col < c + w; ++col) {
            if (row < 0 || row >= rows_ || col < 0 || col >= cols_) continue;
            buffer_[row * cols_ + col].style.fg_rgb = rgb;
        }
    }
}

void Screen::fill(int r, int c, int h, int w, const std::string& glyph, Style style) {
    for (int row = r; row < r + h && row < rows_; ++row) {
        if (row < 0) continue;
        for (int col = c; col < c + w && col < cols_; ++col) {
            if (col < 0) continue;
            setCell(row, col, glyph, style);
        }
    }
}

void Screen::drawBox(int r, int c, int h, int w, const std::string& title, Style style, Style title_style) {
    if (h < 2 || w < 2) return;

    // Corners
    setCell(r, c, "┌", style);
    setCell(r, c + w - 1, "┐", style);
    setCell(r + h - 1, c, "└", style);
    setCell(r + h - 1, c + w - 1, "┘", style);

    // Top and bottom borders
    for (int col = c + 1; col < c + w - 1; ++col) {
        setCell(r, col, "─", style);
        setCell(r + h - 1, col, "─", style);
    }

    // Left and right borders
    for (int row = r + 1; row < r + h - 1; ++row) {
        setCell(row, c, "│", style);
        setCell(row, c + w - 1, "│", style);
    }

    // Optional Title: ┌── Title ───┐
    if (!title.empty() && w > 4) {
        setCell(r, c + 1, "─", style);
        setCell(r, c + 2, " ", style);
        int available = w - 6;
        drawText(r, c + 3, title, title_style, available);
        int title_len = std::min(static_cast<int>(title.size()), available);
        setCell(r, c + 3 + title_len, " ", style);
    }
}

void Screen::flush() {
    std::string out;
    out.reserve(rows_ * cols_ * 4);
    out.append("\033[H"); // Cursor home

    Style cur_style{};
    bool style_active = false;

    for (int r = 0; r < rows_; ++r) {
        for (int c = 0; c < cols_; ++c) {
            const Cell& cell = buffer_[r * cols_ + c];

            if (!style_active || cell.style != cur_style) {
                out.append("\033[0");
                if (cell.style.bold) out.append(";1");
                if (cell.style.dim) out.append(";2");
                if (cell.style.reverse) out.append(";7");
                if (cell.style.fg_rgb >= 0) {
                    out.append(";38;2;");
                    out.append(std::to_string((cell.style.fg_rgb >> 16) & 0xFF));
                    out.append(";");
                    out.append(std::to_string((cell.style.fg_rgb >> 8) & 0xFF));
                    out.append(";");
                    out.append(std::to_string(cell.style.fg_rgb & 0xFF));
                } else if (cell.style.fg != Color::Default) {
                    out.append(";");
                    out.append(std::to_string(static_cast<int>(cell.style.fg)));
                }
                if (cell.style.bg != Color::Default) {
                    out.append(";");
                    out.append(std::to_string(static_cast<int>(cell.style.bg)));
                }
                out.append("m");
                cur_style = cell.style;
                style_active = true;
            }

            out.append(cell.glyph.empty() ? " " : cell.glyph);
        }
        if (r + 1 < rows_) {
            out.append("\r\n");
        }
    }

    if (style_active) {
        out.append("\033[0m");
    }

    write(STDOUT_FILENO, out.data(), out.size());
}

} // namespace risc201
