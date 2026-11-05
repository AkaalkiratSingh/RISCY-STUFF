#include "simulator_core/cpu.h"
#include <iostream>
#include <vector>

using namespace risc201;
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << "line " << __LINE__ << ": " #x "\n"; ++failures; } } while (0)

uint32_t imm(Opcode op, unsigned rd, unsigned rs, int value) {
    return (static_cast<uint32_t>(op) << 27) | (1u << 26) | (rd << 22) |
           (rs << 18) | (static_cast<uint32_t>(value) & 0x3ffff);
}
uint32_t op(Opcode code) { return static_cast<uint32_t>(code) << 27; }

int main() {
    for (auto variant : {PipelineVariant::FourStage, PipelineVariant::SixStage}) {
        for (auto encoding : {MicrocodeEncoding::Horizontal, MicrocodeEncoding::Vertical}) {
            for (bool forwarding : {false, true}) {
                const std::vector<std::vector<uint32_t>> programs = {
                    {imm(Opcode::MOV, 1, 0, 7), imm(Opcode::ADD, 2, 1, 3),
                     imm(Opcode::STORE, 2, 0, 0), imm(Opcode::LOAD, 3, 0, 0), op(Opcode::HALT)},
                    {imm(Opcode::SUB, 1, 0, 0), op(Opcode::BEQ) | 4, op(Opcode::HALT), op(Opcode::HALT)},
                    {imm(Opcode::SUB, 1, 0, 1), op(Opcode::BEQ) | 4, op(Opcode::HALT), op(Opcode::HALT)},
                    {imm(Opcode::MOV, 1, 0, 7), imm(Opcode::DIV, 2, 1, 0), imm(Opcode::MOV, 3, 0, 99)},
                    {imm(Opcode::LOAD, 2, 0, 32), op(Opcode::HALT)},
                    {imm(Opcode::STORE, 2, 0, -1), op(Opcode::HALT)},
                    {0xe0000000u, op(Opcode::HALT)},
                    {op(Opcode::JMP) | 4, 0xe0000000u, op(Opcode::HALT)},
                    {imm(Opcode::MOV, 1, 0, 7)}
                };
                for (const auto& program : programs) {
                    Cpu plain(variant, 32, encoding), traced(variant, 32, encoding);
                    plain.setForwarding(forwarding);
                    traced.setForwarding(forwarding);
                    plain.loadWords(program);
                    traced.loadWords(program);
                    traced.setMicroTracing(true);

                    const auto snapshot = traced.microSnapshot();
                    CHECK(!snapshot.empty());
                    for (const auto& view : snapshot) {
                        CHECK(view.micro_pc >= 0 &&
                              static_cast<size_t>(view.micro_pc) < traced.controlMemory().size());
                        CHECK(view.stage != nullptr && view.stage[0] != '\0');
                    }

                    plain.run();
                    std::vector<MicroTraceRow> rows;
                    for (int guard = 0; !traced.state().halted && guard < 200; ++guard) {
                        traced.step();
                        rows.insert(rows.end(), traced.microTrace().begin(), traced.microTrace().end());
                    }
                    CHECK(traced.state().halted);
                    CHECK(plain.state().registers == traced.state().registers);
                    CHECK(plain.memory() == traced.memory());
                    CHECK(plain.state().pending_exception == traced.state().pending_exception);
                    CHECK(plain.cycles() == traced.cycles());
                    CHECK(plain.retired() == traced.retired());
                    CHECK(plain.trace().stalls == traced.trace().stalls);
                    CHECK(plain.trace().flush_bubbles == traced.trace().flush_bubbles);
                    CHECK(!rows.empty());
                    ControlUnit cu;
                    CHECK(rows.front().signals.ir_load && rows.front().signals.pc_inc);
                    CHECK(rows[1].signals.instr_decode && rows[1].signals.opcode_dispatch);
                    CHECK(rows[1].next == cu.routineEntry(rows[1].opcode));
                    for (const auto& row : rows) {
                        const auto& rom = cu.controlMemory().at(row.micro_pc);
                        CHECK(row.packed == (encoding == MicrocodeEncoding::Horizontal
                            ? ControlUnit::packHorizontal(rom) : packVertical(rom, row.micro_pc)));
                    }
                    if (program[0] == 0xe0000000u) {
                        CHECK(traced.state().pending_exception == ExceptionCode::ILLEGAL_INSTRUCTION);
                        CHECK(traced.state().fault_info->pc == 0);
                        CHECK(traced.retired() == 0);
                        CHECK(rows.back().signals.trap);
                    }
                    if (program[0] == (op(Opcode::JMP) | 4)) {
                        CHECK(!traced.state().pending_exception);
                        for (const auto& row : rows) CHECK(row.pc != 1);
                    }
                    if (program.size() == 1) {
                        CHECK(traced.state().pending_exception == ExceptionCode::INVALID_MEMORY_ACCESS);
                        CHECK(traced.state().fault_info->pc == 1);
                        CHECK(traced.trace().faults == 1);
                    }
                    if (program.size() == 3 && program[1] == imm(Opcode::DIV, 2, 1, 0)) {
                        CHECK(traced.state().pending_exception == ExceptionCode::DIVIDE_BY_ZERO);
                        CHECK(traced.state().registers[1] == 7);
                        CHECK(traced.state().registers[2] == 0 && traced.state().registers[3] == 0);
                        bool trapped = false;
                        for (const auto& row : rows) {
                            if (row.pc == 1 && row.signals.trap &&
                                row.exception == ExceptionCode::DIVIDE_BY_ZERO) trapped = true;
                        }
                        CHECK(trapped);
                    }
                    if (program.size() == 4) {
                        for (const auto& row : rows) {
                            if (row.condition == BranchCond::Zero) {
                                const auto& rom = cu.controlMemory()[row.micro_pc];
                                CHECK(row.next == (program[0] == imm(Opcode::SUB, 1, 0, 0)
                                    ? rom.branch_target : rom.next));
                            }
                        }
                    }
                    if (!traced.state().pending_exception) {
                        CHECK(traced.cycles() == traced.stageCount() - 1 + traced.retired() + 1 +
                            traced.trace().stalls + traced.trace().flush_bubbles);
                    }
                    traced.loadWords({op(Opcode::HALT)});
                    CHECK(traced.microTrace().empty());
                    CHECK(!traced.state().fault_info);
                }
            }
        }
    }
    return failures ? 1 : 0;
}
