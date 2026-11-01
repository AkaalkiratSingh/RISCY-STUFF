#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace risc201 {

enum class Color : uint8_t {
    Default = 0,
    Black = 30,
    Red = 31,
    Green = 32,
    Yellow = 33,
    Blue = 34,
    Magenta = 35,
    Cyan = 36,
    White = 37,
    BrightBlack = 90,
    BrightRed = 91,
    BrightGreen = 92,
    BrightYellow = 93,
    BrightBlue = 94,
    BrightMagenta = 95,
    BrightCyan = 96,
    BrightWhite = 97
};

struct Style {
    Color fg = Color::Default;
    Color bg = Color::Default;
    bool bold = false;
    bool dim = false;
    bool reverse = false;
    int fg_rgb = -1;

    bool operator==(const Style& o) const {
        return fg == o.fg && bg == o.bg && bold == o.bold && dim == o.dim &&
               reverse == o.reverse && fg_rgb == o.fg_rgb;
    }
    bool operator!=(const Style& o) const { return !(*this == o); }
};

struct Cell {
    std::string glyph = " ";
    Style style{};
};

class Screen {
public:
    Screen(int rows = 24, int cols = 80);

    void resize(int rows, int cols);
    void clear();

    int rows() const { return rows_; }
    int cols() const { return cols_; }

    void setCell(int r, int c, const std::string& glyph, Style style = {});
    void drawText(int r, int c, const std::string& text, Style style = {}, int max_len = -1);
    void drawBox(int r, int c, int h, int w, const std::string& title = "", Style style = {}, Style title_style = {});
    void fill(int r, int c, int h, int w, const std::string& glyph = " ", Style style = {});
    void recolour(int r, int c, int h, int w, int rgb);

    void flush();

private:
    int rows_ = 24;
    int cols_ = 80;
    std::vector<Cell> buffer_;
};

} // namespace risc201
