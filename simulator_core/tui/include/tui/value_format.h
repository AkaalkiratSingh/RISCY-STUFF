#pragma once

#include <cstdint>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>

namespace risc201 {

enum class BinaryInterpretation { Decimal, Hexadecimal, FloatingPoint };

inline const char* interpretationName(BinaryInterpretation interpretation) {
    switch (interpretation) {
        case BinaryInterpretation::Decimal: return "Decimal Integer";
        case BinaryInterpretation::Hexadecimal: return "Hexadecimal";
        case BinaryInterpretation::FloatingPoint: return "Floating Point Number";
    }
    return "Decimal Integer";
}

inline std::string formatWord(int32_t word, BinaryInterpretation interpretation) {
    if (interpretation == BinaryInterpretation::Decimal) return std::to_string(word);
    std::ostringstream out;
    if (interpretation == BinaryInterpretation::Hexadecimal) {
        out << "0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0')
            << static_cast<uint32_t>(word);
    } else {
        static_assert(sizeof(float) == sizeof(word) && std::numeric_limits<float>::is_iec559);
        float value;
        std::memcpy(&value, &word, sizeof(value));
        out << std::setprecision(std::numeric_limits<float>::max_digits10) << value;
    }
    return out.str();
}

}
