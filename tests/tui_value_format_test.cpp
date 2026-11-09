#include "tui/value_format.h"

#include <iostream>
#include <limits>

int main() {
    using risc201::BinaryInterpretation;
    using risc201::formatWord;
    const auto check = [](int32_t word, BinaryInterpretation interpretation, const char* expected) {
        const std::string actual = formatWord(word, interpretation);
        if (actual == expected) return true;
        std::cerr << "expected " << expected << ", got " << actual << '\n';
        return false;
    };
    bool ok = true;
    ok &= check(-1, BinaryInterpretation::Decimal, "-1");
    ok &= check(std::numeric_limits<int32_t>::min(), BinaryInterpretation::Decimal, "-2147483648");
    ok &= check(0, BinaryInterpretation::Hexadecimal, "0x00000000");
    ok &= check(-1, BinaryInterpretation::Hexadecimal, "0xFFFFFFFF");
    ok &= check(1069547520, BinaryInterpretation::Hexadecimal, "0x3FC00000");
    ok &= check(1069547520, BinaryInterpretation::FloatingPoint, "1.5");
    ok &= check(-1071644672, BinaryInterpretation::FloatingPoint, "-2.5");
    ok &= check(0, BinaryInterpretation::FloatingPoint, "0");
    ok &= check(std::numeric_limits<int32_t>::min(), BinaryInterpretation::FloatingPoint, "-0");
    ok &= check(2139095040, BinaryInterpretation::FloatingPoint, "inf");
    ok &= check(-8388608, BinaryInterpretation::FloatingPoint, "-inf");
    ok &= check(2143289344, BinaryInterpretation::FloatingPoint, "nan");
    return ok ? 0 : 1;
}
