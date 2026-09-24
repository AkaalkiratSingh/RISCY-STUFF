#pragma once

#include <cstdint>
#include <array>

namespace risc201 {

constexpr int kWordSizeBits      = 32;
constexpr int kNumGPRegisters    = 16;
constexpr int kInstructionBits   = 32;
constexpr int kDefaultMemoryWords = 4096; // TODO: confirm final size w/ Core Engine + Memory leads

enum class Register : uint8_t {
    R0 = 0, R1, R2, R3, R4, R5, R6, R7,
    R8, R9, R10, R11, R12, R13, R14, R15,
};

struct Flags {
    bool zero     = false;
    bool negative = false;
    bool carry    = false;
    bool overflow = false;
    bool equal    = false;       // Added for CMP
    bool greaterThan = false;    // Added for CMP
};

enum class InstrFormat : uint8_t {
    R_TYPE, // reg-reg-reg
    I_TYPE, // reg-reg-immediate
    J_TYPE, // opcode + 26-bit address/offset
};

enum class Opcode : uint8_t {
    NOP  = 0x00,
    ADD  = 0x01,
    SUB  = 0x02,
    AND  = 0x03,
    OR   = 0x04,
    XOR  = 0x05,
    SHL  = 0x06,
    SHR  = 0x07,
    LOAD = 0x08,
    STORE= 0x09,
    JMP  = 0x0A,
    BEQ  = 0x0B,
    CALL = 0x0C,
    RET  = 0x0D,
    PUSH = 0x0E,
    POP  = 0x0F,
    NOT  = 0x10, // Added for PRD completion
    SAR  = 0x11, // Added for PRD completion
    CMP  = 0x12, // Added for Compare instruction
    HALT = 0xFF,
};

// 32-bit fixed-width instruction word. Encoding/decoding helpers belong in
// each consuming module (assembler encodes, simulator/ALU decode) — this
// struct is the shared logical view both sides agree on.
struct Instruction {
    Opcode      opcode;
    InstrFormat format;
    Register    rd;
    Register    rs1;
    Register    rs2;   // R-type only
    int32_t     imm;   // I-type / J-type
};

// TODO(Milestone 0 — coordinate with Vaibhav): confirm final exception code set.
enum class ExceptionCode : uint8_t {
    NONE = 0,
    STACK_OVERFLOW,
    STACK_UNDERFLOW,
    INVALID_MEMORY_ACCESS,
    DIVIDE_BY_ZERO,
    ILLEGAL_INSTRUCTION,
};

} // namespace risc201
