// Instruction decoder in isolation, plus a round-trip proof that running an
// encoded image (the assembler's output) through Cpu::loadWords gives exactly
// the same machine state as loading the assembler's Instruction structs.

#include "assembler/assembler.h"
#include "simulator_core/cpu.h"
#include "simulator_core/decoder.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

#define CHECK(cond) do {                                                   \
    if (!(cond)) {                                                         \
        std::cerr << "  FAIL line " << __LINE__ << ": " #cond << "\n";     \
        ++failures;                                                        \
    }                                                                      \
} while (0)

#define CHECK_EQ(actual, expected) do {                                    \
    const auto got_  = (actual);                                           \
    const auto want_ = (expected);                                         \
    if (!(got_ == want_)) {                                                \
        std::cerr << "  FAIL line " << __LINE__ << ": " #actual            \
                  << " == " #expected << "\n    got  " << got_             \
                  << "\n    want " << want_ << "\n";                       \
        ++failures;                                                        \
    }                                                                      \
} while (0)

void section(const char* name) { std::cout << name << "\n"; }

using namespace risc201;

// Assemble one source program and encode it into a program image.
std::vector<uint32_t> encodeProgram(const std::vector<std::string>& source) {
    Assembler assembler;
    if (!assembler.assemblePass1(source)) {
        std::cerr << "  FAIL: assembly (pass 1) failed\n";
        for (const auto& e : assembler.errors()) {
            std::cerr << "    line " << e.line_number << ": " << e.message << "\n";
        }
        ++failures;
        return {};
    }

    std::vector<uint32_t> words;
    for (const auto& instr : assembler.assemblePass2()) {
        words.push_back(assembler.encodeInstruction(instr));
    }
    return words;
}

Instruction decodeFirst(const std::vector<std::string>& source) {
    const std::vector<uint32_t> words = encodeProgram(source);
    if (words.empty()) return Instruction{};
    return decodeInstruction(words.front());
}

} // namespace

int main() {
    section("Decoder: RI_TYPE register form keeps rs2 and clears imm");
    {
        const Instruction d = decodeFirst({"ADD R3, R1, R2"});
        CHECK(d.opcode == Opcode::ADD);
        CHECK(d.format == InstrFormat::RI_TYPE);
        CHECK(!d.I);
        CHECK(d.rd == Register::R3);
        CHECK(d.rs1 == Register::R1);
        CHECK(d.rs2 == Register::R2);
        CHECK_EQ(d.imm, 0);
    }

    section("Decoder: RI_TYPE immediate form (I bit set) loads imm");
    {
        const Instruction d = decodeFirst({"ADD R4, R5, 7"});
        CHECK(d.opcode == Opcode::ADD);
        CHECK(d.I);
        CHECK(d.rd == Register::R4);
        CHECK(d.rs1 == Register::R5);
        CHECK_EQ(d.imm, 7);
    }

    section("Decoder: negative immediate sign-extends");
    {
        const Instruction d = decodeFirst({"ADD R1, R0, -3"});
        CHECK(d.I);
        CHECK_EQ(d.imm, -3);
    }

    section("Decoder: LOAD/STORE memory form is imm[rs1]");
    {
        const Instruction load = decodeFirst({"LOAD R6, 8[R7]"});
        CHECK(load.opcode == Opcode::LOAD);
        CHECK(load.I);
        CHECK(load.rd == Register::R6);
        CHECK(load.rs1 == Register::R7);
        CHECK_EQ(load.imm, 8);

        const Instruction store = decodeFirst({"STORE R1, 4[R0]"});
        CHECK(store.opcode == Opcode::STORE);
        CHECK(store.rd == Register::R1);
        CHECK(store.rs1 == Register::R0);
        CHECK_EQ(store.imm, 4);
    }

    section("Decoder: J_TYPE carries a signed PC-relative offset");
    {
        const Instruction d = decodeFirst({"JMP target", "NOP", "target:", "HALT"});
        CHECK(d.opcode == Opcode::JMP);
        CHECK(d.format == InstrFormat::J_TYPE);
        CHECK_EQ(d.imm, 4);  // target word 2 => byte 8; 8 - (0 + 4) = 4
    }

    section("Decoder: no-operand instructions");
    {
        const Instruction halt = decodeFirst({"HALT"});
        CHECK(halt.opcode == Opcode::HALT);
        CHECK(halt.format == InstrFormat::RI_TYPE);
        CHECK(!halt.I);

        const Instruction nop = decodeFirst({"NOP"});
        CHECK(nop.opcode == Opcode::NOP);
    }

    section("Decoder: undefined opcode bits are named '?'");
    {
        const Instruction d = decodeInstruction(0x13u << 27);
        CHECK(d.opcode == static_cast<Opcode>(0x13));
        CHECK_EQ(std::string(opcodeName(d.opcode)), std::string("?"));
    }

    section("Decoder: encoded image runs identically to the decoded program");
    {
        const std::vector<std::string> source = {
            "ADD R1, R0, 12",
            "SUB R2, R1, 2",
            "LOAD R3, 4[R0]",
            "STORE R2, 5[R0]",
            "JMP skip",
            "ADD R4, R0, 99",
            "skip:",
            "AND R5, R1, R2",
            "HALT",
        };

        const std::vector<uint32_t> words = encodeProgram(source);

        Assembler assembler;
        assembler.assemblePass1(source);
        const std::vector<Instruction> program = assembler.assemblePass2();

        Cpu viaWords(PipelineVariant::FourStage);
        viaWords.memory()[4] = 7;
        viaWords.loadWords(words);
        viaWords.run();

        Cpu viaProgram(PipelineVariant::FourStage);
        viaProgram.memory()[4] = 7;
        viaProgram.loadProgram(program);
        viaProgram.run();

        CHECK(viaWords.state().halted);
        CHECK(!viaWords.state().pending_exception.has_value());

        for (size_t i = 0; i < kNumGPRegisters; ++i) {
            CHECK_EQ(viaWords.state().registers[i], viaProgram.state().registers[i]);
        }
        CHECK_EQ(viaWords.state().pc, viaProgram.state().pc);
        CHECK_EQ(viaWords.state().halted, viaProgram.state().halted);
        CHECK_EQ(viaWords.retired(), viaProgram.retired());
        CHECK_EQ(viaWords.cycles(), viaProgram.cycles());
        CHECK_EQ(viaWords.memory()[5], viaProgram.memory()[5]);
        CHECK_EQ(viaWords.memory()[5], 10);  // SUB result stored through STORE

        // R2 = 12 - 2, R3 = LOADed 7, R4 skipped, R5 = 12 & 10.
        CHECK_EQ(viaWords.state().registers[static_cast<size_t>(Register::R2)], 10);
        CHECK_EQ(viaWords.state().registers[static_cast<size_t>(Register::R3)], 7);
        CHECK_EQ(viaWords.state().registers[static_cast<size_t>(Register::R4)], 0);
        CHECK_EQ(viaWords.state().registers[static_cast<size_t>(Register::R5)], 8);
    }

    if (failures == 0) {
        std::cout << "\ndecode_test: all checks passed\n";
        return 0;
    }
    std::cerr << "\ndecode_test: " << failures << " check(s) failed\n";
    return 1;
}
