#include "control_unit/vertical_control_unit.h"
#include "utils/utils.h"

#include <array>
#include <initializer_list>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>

namespace risc201 {
    namespace {

        // ---- decoder: micro-op code -> control lines ------------------------

        struct UopDef {
            MicroOpCode code;
            const char* name;
            bool        takes_func;
            ControlWord ctl;
        };

        ControlWord lines(std::initializer_list<bool ControlWord::*> asserted) {
            ControlWord c;
            for (bool ControlWord::* line : asserted) c.*line = true;
            return c;
        }

        using UopTable = std::array<UopDef, kNumMicroOpCodes>;

        // Decodes MicroOpCode. Throws on unsupported horizontal rows.
        const UopTable& uopTable() {
            static const UopTable table = [] {
                using C = ControlWord;
                UopTable t = {{
                    { MicroOpCode::Idle,         "IDLE",     false, lines({}) },
                    { MicroOpCode::Fetch,        "FETCH",    false, lines({&C::pc_inc}) },
                    { MicroOpCode::Alu,          "ALU",      true,  lines({&C::alu_enable, &C::flag_update}) },
                    { MicroOpCode::AluNoFlags,   "ALU.NF",   true,  lines({&C::alu_enable}) },
                    { MicroOpCode::AluImm,       "ALU.IMM",  true,  lines({&C::alu_enable, &C::alu_src_imm}) },
                    { MicroOpCode::MemRead,      "MEM.RD",   false, lines({&C::mem_read}) },
                    { MicroOpCode::MemWrite,     "MEM.WR",   false, lines({&C::mem_write}) },
                    { MicroOpCode::Writeback,    "WB",       false, lines({&C::reg_write}) },
                    { MicroOpCode::WritebackMem, "WB.MEM",   false, lines({&C::reg_write, &C::mem_to_reg}) },
                    { MicroOpCode::Jump,         "JUMP",     false, lines({&C::pc_write}) },
                    { MicroOpCode::JumpMem,      "JUMP.MEM", false, lines({&C::pc_write, &C::pc_src_mem}) },
                    { MicroOpCode::SpDec,        "SP.DEC",   false, lines({&C::sp_dec}) },
                    { MicroOpCode::SpInc,        "SP.INC",   false, lines({&C::sp_inc}) },
                    { MicroOpCode::PushWrite,    "PUSH.WR",  false, lines({&C::mem_write, &C::mem_addr_sp, &C::stack_check}) },
                    { MicroOpCode::CallWrite,    "CALL.WR",  false, lines({&C::mem_write, &C::mem_addr_sp, &C::mem_data_pc, &C::stack_check}) },
                    { MicroOpCode::PopRead,      "POP.RD",   false, lines({&C::mem_read, &C::mem_addr_sp, &C::stack_check}) },
                    { MicroOpCode::Halt,         "HALT",     false, lines({&C::halt}) },
                }};
                for (size_t i = 0; i < t.size(); ++i) {
                    if (static_cast<size_t>(t[i].code) != i) {
                        throw std::logic_error("vertical decoder table is out of order");
                    }
                }
                return t;
            }();
            return table;
        }

        // Extracts 16 control bits, leveraging packHorizontal for sync.
        uint16_t lineBits(const ControlWord& c) {
            MicroOp m;
            m.ctl = c;
            return static_cast<uint16_t>(ControlUnit::packHorizontal(m) & 0xFFFFu);
        }

        // ---- listing / sequencing helpers -----------------------------------

        uint32_t addrField(int32_t a) {
            return a == kMicroEnd ? kMicroEndField : (static_cast<uint32_t>(a) & 0xFFu);
        }

        bool condHolds(BranchCond c, const Flags& f) {
            switch (c) {
                case BranchCond::None:     return false;
                case BranchCond::Zero:     return f.zero;
                case BranchCond::NotZero:  return !f.zero;
                case BranchCond::Negative: return f.negative;
                case BranchCond::Carry:    return f.carry;
                case BranchCond::Overflow: return f.overflow;
            }
            return false;
        }

        const char* condName(BranchCond c) {
            switch (c) {
                case BranchCond::None:     return "-";
                case BranchCond::Zero:     return "Z";
                case BranchCond::NotZero:  return "!Z";
                case BranchCond::Negative: return "N";
                case BranchCond::Carry:    return "C";
                case BranchCond::Overflow: return "V";
            }
            return "?";
        }

        std::string signalList(const ControlWord& w) {
            std::string s;
            auto add = [&s](bool on, const char* name) {
                if (!on) return;
                if (!s.empty()) s += ' ';
                s += name;
            };
            add(w.alu_enable, "alu_enable");
            if (w.alu_enable) {
                s += ' ';
                s += std::string("alu_op=") + opcodeName(w.alu_op);
            }
            add(w.alu_src_imm,  "alu_src_imm");
            add(w.flag_update,  "flag_update");
            add(w.mem_read,     "mem_read");
            add(w.mem_write,    "mem_write");
            add(w.reg_write,    "reg_write");
            add(w.mem_to_reg,   "mem_to_reg");
            add(w.pc_inc,       "pc_inc");
            add(w.pc_write,     "pc_write");
            add(w.sp_dec,       "sp_dec");
            add(w.sp_inc,       "sp_inc");
            add(w.mem_addr_sp,  "mem_addr_sp");
            add(w.mem_data_pc,  "mem_data_pc");
            add(w.pc_src_mem,   "pc_src_mem");
            add(w.stack_check,  "stack_check");
            add(w.halt,         "halt");
            return s.empty() ? "(idle)" : s;
        }

        // ---- the encoder ----------------------------------------------------

        // Packs row into 16-bit word. Returns error string if invalid.
        std::string packVerticalWord(const MicroOp& row, int addr, uint16_t& word) {
            const bool end = (row.next == kMicroEnd);
            if (!end && row.next != addr + 1) {
                return "its successor is " + std::to_string(row.next) +
                       ", but a vertical row can only continue to END or to addr + 1";
            }

            if (row.cond != BranchCond::None) {                    // branch row
                const uint32_t cond = static_cast<uint32_t>(row.cond);
                if (cond > static_cast<uint32_t>(BranchCond::Overflow)) {
                    return "undefined branch condition";
                }
                if (lineBits(row.ctl) != 0 || row.ctl.alu_op != Opcode::NOP) {
                    return "a branch row cannot also drive the datapath";
                }
                if (row.branch_target != kMicroEnd &&
                    (row.branch_target < 0 ||
                     static_cast<uint32_t>(row.branch_target) >= kMicroEndField)) {
                    return "branch target " + std::to_string(row.branch_target) +
                           " does not fit the 8-bit target field";
                }
                word = static_cast<uint16_t>(0x8000u | (cond << 12) |
                                             (addrField(row.branch_target) << 4) |
                                             (end ? 1u : 0u));
                return {};
            }

            if (row.branch_target != kMicroEnd) {
                return "it has a branch target but no branch condition";
            }

            const uint16_t want = lineBits(row.ctl);
            for (const UopDef& u : uopTable()) {
                if (lineBits(u.ctl) != want) continue;

                uint32_t func = 0;
                if (u.takes_func) {
                    func = static_cast<uint32_t>(row.ctl.alu_op);
                    if (func > 0x1Fu) return "its ALU function does not fit the 5-bit func field";
                } else if (row.ctl.alu_op != Opcode::NOP) {
                    return "it selects an ALU function without enabling the ALU";
                }
                word = static_cast<uint16_t>((static_cast<uint32_t>(u.code) << 10) |
                                             (func << 5) | (end ? 1u : 0u));
                return {};
            }
            return "its control lines match no micro-operation (" + signalList(row.ctl) + ")";
        }

        // Generates ROM listing mnemonics.
        std::string verticalMnemonic(uint16_t word) {
            if (word & 0x8000u) {
                return std::string("BRANCH ") + condName(static_cast<BranchCond>((word >> 12) & 0x7u));
            }
            const uint32_t code = (word >> 10) & 0x1Fu;
            if (code >= kNumMicroOpCodes) return "?";
            const UopDef& u = uopTable()[code];
            std::string m = u.name;
            if (u.takes_func) m += std::string("(") + opcodeName(static_cast<Opcode>((word >> 5) & 0x1Fu)) + ")";
            return m;
        }

    }

    // ---- word codec ---------------------------------------------------------

    bool canPackVertical(const MicroOp& row, int addr) {
        uint16_t word = 0;
        return packVerticalWord(row, addr, word).empty();
    }

    uint16_t packVertical(const MicroOp& row, int addr) {
        uint16_t word = 0;
        const std::string why = packVerticalWord(row, addr, word);
        if (!why.empty()) {
            throw std::invalid_argument("control memory row " + std::to_string(addr) +
                                        " is not vertically encodable: " + why);
        }
        return word;
    }

    MicroOp unpackVertical(uint16_t word, int addr) {
        MicroOp row;
        const int32_t not_taken = (word & 1u) ? kMicroEnd : addr + 1;

        const auto failSafe = [] {              // undefined encoding fails safe
            MicroOp h;
            h.ctl.halt = true;
            return h;
        };
        const auto decodeAddr = [](uint32_t f) {
            return f == kMicroEndField ? kMicroEnd : static_cast<int32_t>(f);
        };

        if (word & 0x8000u) {                                      // branch row
            const uint32_t cond = (word >> 12) & 0x7u;
            if (cond == 0 || cond > static_cast<uint32_t>(BranchCond::Overflow)) return failSafe();
            row.cond          = static_cast<BranchCond>(cond);
            row.branch_target = decodeAddr((word >> 4) & 0xFFu);
        } else {                                                   // operate row
            const uint32_t code = (word >> 10) & 0x1Fu;
            if (code >= kNumMicroOpCodes) return failSafe();
            const UopDef& u = uopTable()[code];
            row.ctl = u.ctl;
            if (u.takes_func) row.ctl.alu_op = static_cast<Opcode>((word >> 5) & 0x1Fu);
        }
        row.next = not_taken;
        return row;
    }

    const char* microOpName(MicroOpCode code) {
        const size_t i = static_cast<size_t>(code);
        return i < kNumMicroOpCodes ? uopTable()[i].name : "?";
    }

    // ---- the unit -----------------------------------------------------------

    // Compiles horizontal microprogram to vertical ROM and decodes it back.
    VerticalControlUnit::VerticalControlUnit() {
        const ControlUnit source;                    
        const std::vector<MicroOp>& rows = source.controlMemory();

        for (int op = 0; op < 256; ++op) {
            entries_[static_cast<size_t>(op)] = source.routineEntry(static_cast<Opcode>(op));
        }
        illegal_entry_ = source.routineEntry(static_cast<Opcode>(0xFF));   

        vrom_.reserve(rows.size());
        for (size_t a = 0; a < rows.size(); ++a) {
            vrom_.push_back(packVertical(rows[a], static_cast<int>(a)));
        }

        cm_.reserve(vrom_.size());
        for (size_t a = 0; a < vrom_.size(); ++a) {
            cm_.push_back(unpackVertical(vrom_[a], static_cast<int>(a)));
        }

        // Ensure implicit successors stay in bounds.
        const auto inside = [this](int32_t a) {
            return a == kMicroEnd || (a >= 0 && static_cast<size_t>(a) < cm_.size());
        };
        for (const MicroOp& r : cm_) {
            if (!inside(r.next) || !inside(r.branch_target)) {
                throw std::logic_error("vertical ROM: a row continues past the end of the control memory");
            }
        }
    }

    int VerticalControlUnit::routineEntry(Opcode op) const {
        return entries_[static_cast<uint8_t>(op)];
    }

    void VerticalControlUnit::reset() {
        micro_pc_ = 0;
        done_ = false;
        latched_ = false;
    }

    // Reads and decodes a ROM word.
    MicroOp VerticalControlUnit::readRow(uint32_t addr) const {
        return unpackVertical(vrom_[addr], static_cast<int>(addr));
    }

    ControlWord VerticalControlUnit::step(Opcode op, const Flags& flags) {
        if (vrom_.empty()) {
            // Defensive check for empty ROM.
            done_ = true;
            ControlWord w;
            w.halt = true;
            return w;
        }

        if (done_) {
            // Auto-reset for new macro instruction.
            reset();
        }

        if (!latched_) {
            micro_pc_ = static_cast<uint32_t>(routineEntry(op));
            latched_ = true;
        }

        const MicroOp cur = readRow(micro_pc_);
        const ControlWord out = cur.ctl;

        // Determine successor address (branch vs fall-through).
        const int32_t successor =
            (cur.cond != BranchCond::None && condHolds(cur.cond, flags))
                ? cur.branch_target
                : cur.next;

        if (successor == kMicroEnd) {
            done_ = true;
            micro_pc_ = 0;
        }
        else {
            micro_pc_ = static_cast<uint32_t>(successor);
        }
        return out;
    }

    std::string VerticalControlUnit::dumpControlMemory() const {
        // Label routine entry points.
        std::map<int, std::string> labels;
        if (!cm_.empty()) labels[illegal_entry_] = "ILLEGAL";
        for (int op = 0; op < 256; ++op) {
            const int entry = entries_[static_cast<size_t>(op)];
            if (entry != illegal_entry_) labels[entry] = opcodeName(static_cast<Opcode>(op));
        }

        std::ostringstream os;
        os << "addr  routine  cond  next  taken  microword(hex)  micro-op        signals\n";
        for (size_t a = 0; a < cm_.size(); ++a) {
            const MicroOp& r = cm_[a];
            const auto lbl = labels.find(static_cast<int>(a));

            os << std::right << std::setw(4) << std::setfill('0') << a << "  "
               << std::setfill(' ') << std::left << std::setw(7)
               << (lbl == labels.end() ? "" : lbl->second) << "  "
               << std::setw(4) << condName(r.cond) << "  ";

            if (r.next == kMicroEnd) os << std::setw(4) << "END";
            else                     os << std::setw(4) << r.next;
            os << "  ";
            if (r.cond == BranchCond::None || r.branch_target == kMicroEnd) os << std::setw(5) << "-";
            else os << std::setw(5) << r.branch_target;

            os << "  0x" << std::right << std::hex << std::uppercase
               << std::setfill('0') << std::setw(4) << vrom_[a]
               << std::dec << std::setfill(' ') << std::left << "          "
               << std::setw(14) << verticalMnemonic(vrom_[a]) << "  "
               << signalList(r.ctl) << "\n";
        }

        const size_t rows = cm_.size();
        const size_t v_bits = rows * kVerticalWordBits;
        const size_t h_bits = rows * kHorizontalWordBits;
        os << "\n" << rows << " rows x " << kVerticalWordBits << " bits = " << v_bits
           << " bits  (horizontal: " << rows << " x " << kHorizontalWordBits << " = " << h_bits
           << " bits, " << (100 - 100 * kVerticalWordBits / kHorizontalWordBits)
           << "% narrower)\n"
           << "decoder: " << kNumMicroOpCodes << " micro-ops -> 16 control lines\n";
        return os.str();
    }

}
