#include "assembler/assembler.h"
#include "simulator_core/cpu.h"

#include <cstdint>
#include <iostream>
#include <map>
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

std::vector<Instruction> assemble(const std::vector<std::string>& source) {
    Assembler asmblr;
    if (!asmblr.assemblePass1(source)) {
        std::cerr << "  FAIL: assembly (pass 1) failed\n";
        for (const auto& e : asmblr.errors()) {
            std::cerr << "    line " << e.line_number << ": " << e.message << "\n";
        }
        ++failures;
    }
    return asmblr.assemblePass2();
}

Cpu runProgram(const std::vector<std::string>& source,
               const std::map<uint32_t, int32_t>& memory = {}) {
    Cpu cpu(PipelineVariant::FourStage);
    for (const auto& [addr, value] : memory) cpu.memory()[addr] = value;
    cpu.loadProgram(assemble(source));
    cpu.run();
    return cpu;
}

int32_t reg(const Cpu& cpu, Register r) {
    return cpu.state().registers[static_cast<size_t>(r)];
}

} // namespace

int main() {
    section("Cpu: LOAD seeds registers, ADD sums them, HALT stops the machine");
    {
        const Cpu cpu = runProgram({
            "LOAD R2, 0[R0]",
            "LOAD R3, 1[R0]",
            "ADD R1, R2, R3",
            "HALT",
        }, {{0, 5}, {1, 7}});
        CHECK(cpu.state().halted);
        CHECK(!cpu.state().pending_exception.has_value());
        CHECK_EQ(reg(cpu, Register::R2), 5);
        CHECK_EQ(reg(cpu, Register::R3), 7);
        CHECK_EQ(reg(cpu, Register::R1), 12);
    }

    section("Cpu: step() is one macro instruction, not one micro-cycle");
    {
        Cpu cpu(PipelineVariant::FourStage);
        cpu.memory()[0] = 3;
        cpu.memory()[1] = 4;
        cpu.loadProgram(assemble({
            "LOAD R1, 0[R0]",
            "LOAD R2, 1[R0]",
            "ADD R3, R1, R2",
            "HALT",
        }));
        cpu.step();
        CHECK_EQ(cpu.state().pc, 1u);
        CHECK_EQ(reg(cpu, Register::R1), 3);
        cpu.step();
        CHECK_EQ(cpu.state().pc, 2u);
        CHECK_EQ(reg(cpu, Register::R2), 4);
        cpu.step();
        CHECK_EQ(cpu.state().pc, 3u);
        CHECK_EQ(reg(cpu, Register::R3), 7);
        cpu.step();
        CHECK(cpu.state().halted);
        cpu.step();
        CHECK(cpu.state().halted);
    }

    section("Cpu: STORE/LOAD round-trip through data memory");
    {
        Cpu cpu = runProgram({
            "LOAD R1, 0[R0]",
            "STORE R1, 5[R0]",
            "LOAD R2, 5[R0]",
            "HALT",
        }, {{0, 42}});
        CHECK(cpu.state().halted);
        CHECK(!cpu.state().pending_exception.has_value());
        CHECK_EQ(cpu.memory()[5], 42);
        CHECK_EQ(reg(cpu, Register::R2), 42);
    }

    section("Cpu: JMP transfers control and skips the fall-through path");
    {
        const Cpu cpu = runProgram({
            "LOAD R1, 0[R0]",
            "JMP done",
            "LOAD R2, 1[R0]",
            "done:",
            "HALT",
        }, {{0, 1}, {1, 2}});
        CHECK(cpu.state().halted);
        CHECK(!cpu.state().pending_exception.has_value());
        CHECK_EQ(reg(cpu, Register::R1), 1);
        CHECK_EQ(reg(cpu, Register::R2), 0);
        CHECK_EQ(cpu.state().pc, 3u);
    }

    section("Cpu: flags — zero, negative, signed overflow, unsigned carry");
    {
        const Cpu cpu = runProgram({
            "LOAD R1, 0[R0]",
            "SUB R2, R1, R1",
            "HALT",
        }, {{0, 9}});
        CHECK_EQ(reg(cpu, Register::R2), 0);
        CHECK(cpu.state().flags.zero);
        CHECK(!cpu.state().flags.negative);
        CHECK(!cpu.state().flags.overflow);
    }
    {
        const Cpu cpu = runProgram({
            "LOAD R1, 0[R0]",
            "LOAD R2, 1[R0]",
            "ADD R3, R1, R2",
            "HALT",
        }, {{0, INT32_MAX}, {1, 1}});
        CHECK_EQ(reg(cpu, Register::R3), INT32_MIN);
        CHECK(cpu.state().flags.overflow);
        CHECK(cpu.state().flags.negative);
        CHECK(!cpu.state().flags.zero);
        CHECK(!cpu.state().flags.carry);
    }
    {
        const Cpu cpu = runProgram({
            "LOAD R1, 0[R0]",
            "LOAD R2, 1[R0]",
            "ADD R3, R1, R2",
            "HALT",
        }, {{0, -1}, {1, 1}});
        CHECK_EQ(reg(cpu, Register::R3), 0);
        CHECK(cpu.state().flags.carry);
        CHECK(cpu.state().flags.zero);
        CHECK(!cpu.state().flags.overflow);
    }

    section("Cpu: run() terminates without a HALT (regression: it used to hang)");
    {
        const Cpu cpu = runProgram({"ADD R1, R0, R0"});
        CHECK(cpu.state().halted);
        CHECK(cpu.state().pending_exception.value_or(ExceptionCode::NONE) ==
              ExceptionCode::INVALID_MEMORY_ACCESS);
    }

    section("Cpu: out-of-range data access raises and halts");
    {
        const Cpu cpu = runProgram({
            "LOAD R1, 99999[R0]",
            "HALT",
        });
        CHECK(cpu.state().halted);
        CHECK(cpu.state().pending_exception.value_or(ExceptionCode::NONE) ==
              ExceptionCode::INVALID_MEMORY_ACCESS);
    }

    section("Cpu: BEQ falls through when zero flag is clear");
    {
    const Cpu cpu = runProgram({
        "BEQ done",
        "LOAD R1, 0[R0]",
        "done:",
        "HALT",
    }, {{0, 7}});

    CHECK(cpu.state().halted);
    CHECK(!cpu.state().pending_exception.has_value());
    CHECK_EQ(reg(cpu, Register::R1), 7);
    }

    section("4-stage: immediate operand form ADD Rd, Rs, imm");
    {
        const Cpu cpu = runProgram({
            "LOAD R2, 0[R0]",
            "ADD R1, R2, 5",
            "HALT",
        }, {{0, 5}});
        CHECK(cpu.state().halted);
        CHECK_EQ(reg(cpu, Register::R1), 10);
    }

    section("4-stage: address arithmetic does not disturb FLAGS");
    {
        const Cpu cpu = runProgram({
            "SUB R1, R1, R1",
            "LOAD R2, 4[R0]",
            "HALT",
        });
        CHECK(cpu.state().flags.zero);
    }

    section("4-stage: RAW interlock costs exactly one cycle");
    {
        const Cpu hazard = runProgram({
            "LOAD R1, 0[R0]",
            "ADD R2, R1, R0",
            "HALT",
        }, {{0, 9}});
        CHECK_EQ(reg(hazard, Register::R2), 9);

        const Cpu independent = runProgram({
            "LOAD R1, 0[R0]",
            "ADD R2, R0, R0",
            "HALT",
        }, {{0, 9}});
        CHECK_EQ(reg(independent, Register::R2), 0);
        CHECK_EQ(independent.cycles() + 1, hazard.cycles());
    }

    section("4-stage: instructions overlap");
    {
        const Cpu cpu = runProgram({
            "ADD R1, R0, R0",
            "ADD R2, R0, R0",
            "HALT",
        });
        CHECK(cpu.state().halted);
        CHECK_EQ(cpu.retired(), 2u);
        CHECK_EQ(cpu.cycles(), 5u);
    }

    if (failures == 0) {
        std::cout << "\ncpu_integration_test: all checks passed\n";
        return 0;
    }
    std::cerr << "\ncpu_integration_test: " << failures << " check(s) failed\n";
    return 1;
}
