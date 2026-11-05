#pragma once

#include <cstdint>
#include <array>

namespace risc201 {

constexpr int kWordSizeBits         = 32;
constexpr int kNumGPRegisters       = 16;
constexpr int kInstructionBits      = 32;
constexpr int kDefaultMemoryWords   = 4096; // TODO: confirm final size w/ Core Engine + Memory leads
constexpr uint32_t kLiteralsMarker  = 0xFFFFFFFFu;
constexpr int32_t  kMicroEnd        = -1;

enum class Register : uint8_t {
    R0 = 0, R1, R2, R3, R4, R5, R6, R7,
    R8, R9, R10, R11, R12, R13, R14, R15,
};

struct Flags {
    bool zero     = false;
    bool negative = false;
    bool carry    = false;
    bool overflow = false;
    bool equal    = false;       
    bool greaterThan = false;
};

enum class InstrFormat : uint8_t {
    RI_TYPE,    // opcode + reg-reg-reg/immediate
    J_TYPE,     // opcode + 27-bit address/offset
};

enum class Opcode : uint8_t {
    NOP  = 0x00,
    MOV  = 0x01,
    ADD  = 0x02,
    SUB  = 0x03,
    MUL  = 0x04,
    DIV  = 0x05,
    REM  = 0x06,
    AND  = 0x07,
    OR   = 0x08,
    XOR  = 0x09, //
    NOT  = 0x0A,
    SHL  = 0x0B,
    SHR  = 0x0C,
    SAR  = 0x0D, //
    LOAD = 0x0E,
    STORE= 0x0F,
    CMP  = 0x10,
    JMP  = 0x11,
    BEQ  = 0x12,
    BGT  = 0x13,
    CALL = 0x14,
    RET  = 0x15,
    FADD = 0x16,//
    FSUB = 0x17,//
    FMUL = 0x18,//
    FDIV = 0x19,//
    FCMP = 0x1A,//
    FTOI = 0x1B,
    ITOF = 0x1C,

    HALT = 0x1F, //
};

// 32-bit fixed-width instruction word. Encoding/decoding helpers belong in
// each consuming module (assembler encodes, simulator/ALU decode) — this
// struct is the shared logical view both sides agree on.
struct Instruction {
    Opcode      opcode;
    InstrFormat format;
    bool I;
    Register    rd;
    Register    rs1;
    Register    rs2;   // R-type only
    int32_t     imm;   // I-type / J-type
};

// TODO(Milestone 0 — coordinate with Vaibhav): confirm final exception code set.
enum class ExceptionCode : uint8_t {
    NONE = 0,
    INVALID_MEMORY_ACCESS,
    DIVIDE_BY_ZERO,
    ILLEGAL_INSTRUCTION,
};

} // namespace risc201
