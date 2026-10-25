// Tests VerticalControlUnit equivalence against the horizontal ControlUnit.
// Validates ROM format, bit layout, encoding/decoding, and lockstep execution.

#include "assembler/assembler.h"
#include "control_unit/horizontal_control_unit.h"
#include "control_unit/vertical_control_unit.h"
#include "simulator_core/control_decode.h"
#include "simulator_core/cpu.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef EXAMPLES_DIR
#error "EXAMPLES_DIR must point at the examples/ directory (set in tests/CMakeLists.txt)"
#endif

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

// Exact member-wise comparison.
bool sameWord(const ControlWord& a, const ControlWord& b) {
    return a.alu_enable == b.alu_enable && a.mem_read == b.mem_read &&
           a.mem_write == b.mem_write && a.reg_write == b.reg_write &&
           a.pc_write == b.pc_write && a.pc_inc == b.pc_inc &&
           a.alu_src_imm == b.alu_src_imm && a.mem_to_reg == b.mem_to_reg &&
           a.sp_dec == b.sp_dec && a.sp_inc == b.sp_inc &&
           a.flag_update == b.flag_update && a.halt == b.halt &&
           a.mem_addr_sp == b.mem_addr_sp && a.mem_data_pc == b.mem_data_pc &&
           a.pc_src_mem == b.pc_src_mem && a.stack_check == b.stack_check &&
           a.alu_op == b.alu_op;
}

bool sameRow(const MicroOp& a, const MicroOp& b) {
    return sameWord(a.ctl, b.ctl) && a.cond == b.cond && a.next == b.next &&
           a.branch_target == b.branch_target;
}

bool sameControls(const StageControls& a, const StageControls& b) {
    return a.halting == b.halting && a.is_branch == b.is_branch && a.cond == b.cond &&
           a.pc_inc == b.pc_inc && a.alu_enable == b.alu_enable && a.alu_op == b.alu_op &&
           a.alu_src_imm == b.alu_src_imm && a.flag_update == b.flag_update &&
           a.sp_dec == b.sp_dec && a.sp_inc == b.sp_inc &&
           a.pc_from_target == b.pc_from_target && a.mem_read == b.mem_read &&
           a.mem_write == b.mem_write && a.mem_addr_sp == b.mem_addr_sp &&
           a.mem_data_pc == b.mem_data_pc && a.stack_check == b.stack_check &&
           a.pc_from_mem == b.pc_from_mem && a.reg_write == b.reg_write &&
           a.mem_to_reg == b.mem_to_reg;
}

Flags flagsFromBits(int bits) {
    Flags f;
    f.zero     = bits & 1;
    f.negative = bits & 2;
    f.carry    = bits & 4;
    f.overflow = bits & 8;
    return f;
}

MicroOp branchRow(BranchCond c, int32_t next, int32_t target) {
    MicroOp m;
    m.cond = c;
    m.next = next;
    m.branch_target = target;
    return m;
}

bool packThrows(const MicroOp& row, int addr) {
    try {
        packVertical(row, addr);
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Whole-CPU comparison
// ---------------------------------------------------------------------------

constexpr uint64_t kCycleCap = 5'000'000;

struct RunResult {
    CpuState state;
    std::vector<int32_t> memory;
    uint64_t cycles = 0, retired = 0;
    uint64_t stalls = 0, flushes = 0, bubbles = 0, squashed = 0, faults = 0;
};

RunResult runProgram(const std::vector<uint32_t>& words, PipelineVariant variant,
                     bool forwarding, MicrocodeEncoding encoding) {
    Cpu cpu(variant, kDefaultMemoryWords, encoding);
    cpu.setForwarding(forwarding);
    cpu.loadWords(words);
    // Bounded CPU run to prevent hangs on broken ROMs.
    while (!cpu.state().halted && cpu.cycles() < kCycleCap) cpu.step();
    const PipelineTrace& t = cpu.trace();
    return { cpu.state(), cpu.memory(), cpu.cycles(), cpu.retired(),
             t.stalls, t.flushes, t.flush_bubbles, t.squashed, t.faults };
}

bool sameRun(const RunResult& a, const RunResult& b) {
    return a.state.registers == b.state.registers && a.state.pc == b.state.pc &&
           a.state.sp == b.state.sp && a.state.halted == b.state.halted &&
           a.state.flags.zero == b.state.flags.zero &&
           a.state.flags.negative == b.state.flags.negative &&
           a.state.flags.carry == b.state.flags.carry &&
           a.state.flags.overflow == b.state.flags.overflow &&
           a.state.pending_exception == b.state.pending_exception &&
           a.memory == b.memory && a.cycles == b.cycles && a.retired == b.retired &&
           a.stalls == b.stalls && a.flushes == b.flushes && a.bubbles == b.bubbles &&
           a.squashed == b.squashed && a.faults == b.faults;
}

} // namespace

int main() {
    const ControlUnit hcu;                                   // horizontal reference
    const VerticalControlUnit vcu;
    const auto& hcm = hcu.controlMemory();
    const auto& vcm = vcu.controlMemory();
    const auto& vrom = vcu.verticalRom();

    // -----------------------------------------------------------------------
    section("Vertical ROM: shape");
    CHECK(vcu.encoding() == MicrocodeEncoding::Vertical);
    CHECK_EQ(vrom.size(), hcm.size());                       
    CHECK_EQ(vcm.size(), hcm.size());
    CHECK(vrom.size() * kVerticalWordBits < hcm.size() * kHorizontalWordBits);
    CHECK_EQ(kVerticalWordBits, 16);
    CHECK_EQ(sizeof(vrom[0]) * 8, static_cast<size_t>(kVerticalWordBits));

    // -----------------------------------------------------------------------
    section("Vertical ROM: bit layout, pinned with hand-computed words");
    // [15]=0: operate row. [15]=1: branch row.
    const auto word = [&](Opcode op, int offset) {
        return vrom[static_cast<size_t>(hcu.routineEntry(op) + offset)];
    };
    CHECK_EQ(word(Opcode::ADD, 0), 0x0400);        
    CHECK_EQ(word(Opcode::ADD, 1), 0x0820);        
    CHECK_EQ(word(Opcode::SUB, 1), 0x0840);        
    CHECK_EQ(word(Opcode::ADD, 2), 0x1C01);        
    CHECK_EQ(word(Opcode::LOAD, 1), 0x1020);       
    CHECK_EQ(word(Opcode::LOAD, 3), 0x2001);       
    CHECK_EQ(word(Opcode::MOV, 1), 0x0E60);        
    CHECK_EQ(word(Opcode::CALL, 2), 0x3800);       
    CHECK_EQ(word(Opcode::RET, 1), 0x2800);        
    CHECK_EQ(word(Opcode::HALT, 0), 0x4001);       
    CHECK_EQ(word(Opcode::BEQ, 0), 0x92B0);        
    CHECK_EQ(word(Opcode::BEQ, 1), 0x0401);        
    CHECK_EQ(word(Opcode::BEQ, 2), 0x2401);        
    CHECK_EQ(packVertical(MicroOp{}, 0), 0x0001);   
    {
        MicroOp idle;
        idle.next = 6;
        CHECK_EQ(packVertical(idle, 5), 0x0000);    
    }

    // -----------------------------------------------------------------------
    section("Vertical ROM: every word is canonical and round-trips");
    for (size_t a = 0; a < vrom.size(); ++a) {
        const uint16_t w = vrom[a];
        const MicroOp row = unpackVertical(w, static_cast<int>(a));
        CHECK_EQ(packVertical(row, static_cast<int>(a)), w);   
        if (w & 0x8000u) CHECK_EQ(w & 0x000Eu, 0u);          
        else             CHECK_EQ(w & 0x001Eu, 0u);          
        if (!(w & 0x8000u)) {
            const unsigned uop = (w >> 10) & 0x1Fu;
            CHECK(uop < kNumMicroOpCodes);
            const bool takes_func = (uop == static_cast<unsigned>(MicroOpCode::Alu) ||
                                     uop == static_cast<unsigned>(MicroOpCode::AluNoFlags) ||
                                     uop == static_cast<unsigned>(MicroOpCode::AluImm));
            if (!takes_func) CHECK_EQ((w >> 5) & 0x1Fu, 0u); 
        }
    }
    CHECK_EQ(vrom.back() & 1u, 1u);                          

    // -----------------------------------------------------------------------
    section("Vertical == horizontal: control memory, row by row");
    for (size_t a = 0; a < hcm.size(); ++a) {
        if (!sameRow(vcm[a], hcm[a])) {
            std::cerr << "  FAIL row " << a << " differs between encodings\n";
            ++failures;
        }
        CHECK_EQ(ControlUnit::packHorizontal(vcm[a]), ControlUnit::packHorizontal(hcm[a]));
    }
    for (int op = 0; op < 256; ++op) {
        CHECK_EQ(vcu.routineEntry(static_cast<Opcode>(op)), hcu.routineEntry(static_cast<Opcode>(op)));
    }

    // -----------------------------------------------------------------------
    section("Vertical == horizontal: per-opcode stage controls the CPU consumes");
    {
        const StageControlTable ht = buildStageControls(hcu);
        const StageControlTable vt = buildStageControls(vcu);
        int diff = 0;
        for (size_t op = 0; op < ht.size(); ++op) diff += !sameControls(ht[op], vt[op]);
        CHECK_EQ(diff, 0);
    }

    // -----------------------------------------------------------------------
    section("Vertical == horizontal: lockstep, every opcode x every flag combination");
    {
        int mismatches = 0;
        for (int opv = 0; opv < 256; ++opv) {
            const Opcode op = static_cast<Opcode>(opv);
            for (int fl = 0; fl < 16; ++fl) {
                const Flags f = flagsFromBits(fl);
                ControlUnit h;
                VerticalControlUnit v;
                for (int cycle = 0;; ++cycle) {
                    const ControlWord hw = h.step(op, f);
                    const ControlWord vw = v.step(op, f);
                    if (!sameWord(hw, vw) || h.microPC() != v.microPC() ||
                        h.routineDone() != v.routineDone() || cycle > 32) {
                        ++mismatches;
                        break;
                    }
                    if (h.routineDone()) break;
                }
            }
        }
        CHECK_EQ(mismatches, 0);
    }

    section("Vertical == horizontal: lockstep across back-to-back instructions (no reset)");
    {
        ControlUnit h;
        VerticalControlUnit v;
        uint32_t seed = 0x2545F491u;
        const auto next = [&seed] { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
        int mismatches = 0;
        for (int i = 0; i < 20000; ++i) {
            const Opcode op = static_cast<Opcode>(next() & 0xFF);
            const Flags f = flagsFromBits(static_cast<int>(next() & 0xF));
            if (!sameWord(h.step(op, f), v.step(op, f)) || h.microPC() != v.microPC() ||
                h.routineDone() != v.routineDone()) {
                ++mismatches;
            }
        }
        CHECK_EQ(mismatches, 0);
    }

    // -----------------------------------------------------------------------
    section("Decoder: micro-op codes");
    {
        std::set<uint16_t> line_sets;
        for (unsigned code = 0; code < kNumMicroOpCodes; ++code) {
            const MicroOp row = unpackVertical(static_cast<uint16_t>(code << 10), 0);
            CHECK(row.cond == BranchCond::None);
            CHECK(microOpName(static_cast<MicroOpCode>(code))[0] != '?');
            MicroOp lines = row;
            lines.next = kMicroEnd;
            line_sets.insert(static_cast<uint16_t>(ControlUnit::packHorizontal(lines) & 0xFFFFu));
        }
        CHECK_EQ(line_sets.size(), kNumMicroOpCodes);        
        CHECK(std::string(microOpName(static_cast<MicroOpCode>(31))) == "?");
    }

    section("Decoder: undefined encodings fail safe to a halt row");
    {
        for (unsigned code = kNumMicroOpCodes; code < 32; ++code) {
            const MicroOp m = unpackVertical(static_cast<uint16_t>(code << 10), 9);
            CHECK(m.ctl.halt && m.next == kMicroEnd && m.cond == BranchCond::None);
        }
        for (unsigned cond : {0u, 6u, 7u}) {                 
            const MicroOp m = unpackVertical(static_cast<uint16_t>(0x8000u | (cond << 12)), 9);
            CHECK(m.ctl.halt && m.next == kMicroEnd && m.cond == BranchCond::None);
        }
    }

    // -----------------------------------------------------------------------
    section("Encoder: branch rows (all five conditions, END and explicit targets)");
    for (BranchCond c : {BranchCond::Zero, BranchCond::NotZero, BranchCond::Negative,
                         BranchCond::Carry, BranchCond::Overflow}) {
        for (int32_t next : {int32_t{8}, kMicroEnd}) {       
            for (int32_t target : {int32_t{0}, int32_t{34}, int32_t{254}, kMicroEnd}) {
                const MicroOp in = branchRow(c, next, target);
                CHECK(canPackVertical(in, 7));
                const uint16_t w = packVertical(in, 7);
                CHECK((w & 0x8000u) != 0);
                CHECK(sameRow(unpackVertical(w, 7), in));
            }
        }
    }

    section("Encoder: rejects rows the vertical format cannot express");
    {
        MicroOp m;
        m.next = 5;
        CHECK(!canPackVertical(m, 3));          
        CHECK(packThrows(m, 3));
        m.next = 4;
        CHECK(canPackVertical(m, 3));           

        MicroOp mixed = branchRow(BranchCond::Zero, 4, 9);   
        mixed.ctl.pc_inc = true;
        CHECK(!canPackVertical(mixed, 3));
        CHECK(packThrows(mixed, 3));

        MicroOp both;                                        
        both.ctl.mem_read = both.ctl.mem_write = true;
        CHECK(!canPackVertical(both, 0));
        CHECK(packThrows(both, 0));

        MicroOp imm_flags;                                   
        imm_flags.ctl.alu_enable = imm_flags.ctl.alu_src_imm = imm_flags.ctl.flag_update = true;
        CHECK(!canPackVertical(imm_flags, 0));

        MicroOp stray_func;                                  
        stray_func.ctl.alu_op = Opcode::ADD;
        CHECK(!canPackVertical(stray_func, 0));

        MicroOp wide_func;                                   
        wide_func.ctl.alu_enable = wide_func.ctl.flag_update = true;
        wide_func.ctl.alu_op = static_cast<Opcode>(0x20);
        CHECK(!canPackVertical(wide_func, 0));

        MicroOp stray_target;                                
        stray_target.branch_target = 3;
        CHECK(!canPackVertical(stray_target, 0));

        CHECK(!canPackVertical(branchRow(BranchCond::Zero, 1, 255), 0));   
        CHECK(!canPackVertical(branchRow(BranchCond::Zero, 1, 300), 0));
        CHECK(!canPackVertical(branchRow(BranchCond::Zero, 1, -2), 0));
        CHECK(!canPackVertical(branchRow(static_cast<BranchCond>(6), 1, 0), 0));

        MicroOp skips;
        skips.next = 9;                                      
        try {
            packVertical(skips, 12);
            CHECK(false);
        } catch (const std::invalid_argument& e) {
            CHECK(std::string(e.what()).find("row 12") != std::string::npos);
        }
    }

    // -----------------------------------------------------------------------
    section("Listing");
    {
        const std::string h = hcu.dumpControlMemory();
        const std::string v = vcu.dumpControlMemory();
        CHECK(h.rfind("addr  routine  cond  next  taken  microword(hex)  signals\n", 0) == 0);  
        CHECK(h.find("micro-op") == std::string::npos);
        CHECK(v.find("micro-op") != std::string::npos);
        CHECK(v.find("0x0820") != std::string::npos && v.find("ALU(ADD)") != std::string::npos);
        CHECK(v.find("BRANCH Z") != std::string::npos);
        CHECK(v.find(" rows x 16 bits = ") != std::string::npos);
        CHECK(v.find("60% narrower") != std::string::npos);
    }

    // -----------------------------------------------------------------------
    section("CPU: encoding selection");
    {
        CHECK(Cpu(PipelineVariant::FourStage).encoding() == MicrocodeEncoding::Horizontal);
        CHECK(Cpu(PipelineVariant::FourStage, kDefaultMemoryWords,
                  MicrocodeEncoding::Vertical).encoding() == MicrocodeEncoding::Vertical);
    }

    // -----------------------------------------------------------------------
    section("CPU: every example program behaves identically on both ROMs");
    {
        std::vector<std::filesystem::path> programs;
        for (const auto& e : std::filesystem::directory_iterator(EXAMPLES_DIR)) {
            if (e.path().extension() == ".asm") programs.push_back(e.path());
        }
        std::sort(programs.begin(), programs.end());
        CHECK(!programs.empty());

        for (const auto& path : programs) {
            std::vector<std::string> lines;
            std::ifstream in(path);
            for (std::string l; std::getline(in, l);) lines.push_back(l);

            Assembler as;
            CHECK(as.assemblePass1(lines));
            std::vector<uint32_t> words;
            for (const auto& instr : as.assemblePass2()) words.push_back(as.encodeInstruction(instr));
            CHECK(!words.empty());

            int bad = 0;
            for (PipelineVariant pv : {PipelineVariant::FourStage, PipelineVariant::SixStage}) {
                for (bool fwd : {true, false}) {
                    const RunResult h = runProgram(words, pv, fwd, MicrocodeEncoding::Horizontal);
                    const RunResult v = runProgram(words, pv, fwd, MicrocodeEncoding::Vertical);
                    CHECK(h.state.halted);                   
                    bad += !sameRun(h, v);
                }
            }
            if (bad == 0) {
                std::cout << "  ok    " << path.filename().string() << "\n";
            } else {
                std::cerr << "  FAIL  " << path.filename().string() << ": " << bad
                          << " of 4 configurations differ\n";
                ++failures;
            }
        }
    }

    if (failures == 0) {
        std::cout << "\ncontrol_unit_vertical_test: all checks passed\n";
        return 0;
    }
    std::cerr << "\ncontrol_unit_vertical_test: " << failures << " check(s) failed\n";
    return 1;
}
