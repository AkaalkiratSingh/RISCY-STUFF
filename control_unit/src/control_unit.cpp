#include "control_unit/control_unit.h"

#include <iomanip>
#include <map>
#include <sstream>

namespace risc201 {
    namespace {

        MicroOp fetchRow() {          // μ1: sequential fetch, PC <- PC + 1
            MicroOp m;
            m.ctl.pc_inc = true;
            return m;
        }

        MicroOp aluRow(Opcode fn) {   // execute, operand B from rs2, latch flags
            MicroOp m;
            m.ctl.alu_enable = true;
            m.ctl.flag_update = true;
            m.ctl.alu_op = fn;
            return m;
        }

        MicroOp aluImmRow(Opcode fn) {  // execute, operand B from immediate — for
            MicroOp m;                  // LOAD/STORE this is the effective-address calc
            m.ctl.alu_enable = true;    // (address arithmetic must NOT disturb the flags)
            m.ctl.alu_src_imm = true;
            m.ctl.alu_op = fn;
            return m;
        }

        MicroOp memReadRow() {
            MicroOp m;
            m.ctl.mem_read = true;
            return m;
        }

        MicroOp memWriteRow() {
            MicroOp m;
            m.ctl.mem_write = true;
            return m;
        }

        MicroOp pushRow() {
            MicroOp m;
            m.ctl.sp_dec = true;
            m.ctl.mem_write = true;
            return m;
        }

        MicroOp popRow() {
            MicroOp m;
            m.ctl.mem_read = true;
            m.ctl.sp_inc = true;
            m.ctl.reg_write = true;
            m.ctl.mem_to_reg = true;
            return m;
        }

        MicroOp writebackRow() {      // WB, source = ALU result
            MicroOp m;
            m.ctl.reg_write = true;
            return m;
        }

        MicroOp writebackMemRow() {   // WB, source = loaded word
            MicroOp m;
            m.ctl.reg_write = true;
            m.ctl.mem_to_reg = true;
            return m;
        }

        MicroOp jumpRow() {           // taken transfer, PC <- instruction target
            MicroOp m;
            m.ctl.pc_write = true;
            return m;
        }

        MicroOp haltRow() {
            MicroOp m;
            m.ctl.halt = true;
            return m;
        }

        MicroOp idleRow() {           // one-cycle bubble
            return MicroOp{};
        }

        // fetch -> execute -> writeback.
        std::vector<MicroOp> aluRoutine(Opcode fn) {
            return { fetchRow(), aluRow(fn), writebackRow() };
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

        const char* opName(Opcode op) {
            switch (op) {
                case Opcode::NOP:   return "NOP";
                case Opcode::ADD:   return "ADD";
                case Opcode::SUB:   return "SUB";
                case Opcode::AND:   return "AND";
                case Opcode::OR:    return "OR";
                case Opcode::XOR:   return "XOR";
                case Opcode::SHL:   return "SHL";
                case Opcode::SHR:   return "SHR";
                case Opcode::LOAD:  return "LOAD";
                case Opcode::STORE: return "STORE";
                case Opcode::JMP:   return "JMP";
                case Opcode::BEQ:   return "BEQ";
                case Opcode::CALL:  return "CALL";
                case Opcode::RET:   return "RET";
                case Opcode::PUSH:  return "PUSH";
                case Opcode::POP:   return "POP";
                case Opcode::HALT:  return "HALT";
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
                s += std::string("alu_op=") + opName(w.alu_op);
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
            add(w.halt,         "halt");
            return s.empty() ? "(idle)" : s;
        }

        uint32_t addrField(int32_t a) {
            return a == kMicroEnd ? kMicroEndField : (static_cast<uint32_t>(a) & 0xFFu);
        }

    }

    ControlUnit::ControlUnit(MicrocodeEncoding encoding) : encoding_(encoding) {
        if (encoding_ == MicrocodeEncoding::Horizontal) {
            buildHorizontalRom();
        }
    }

    void ControlUnit::buildHorizontalRom() {
        illegal_entry_ = appendRaw({ haltRow() });

        // R-type arithmetic / logic — same three-row shape, but each opcode gets
        // its own copy so the execute row carries that opcode's ALU function.

        Opcode logicInstruction[] = {
            Opcode::ADD,
            Opcode::SUB,
            Opcode::AND,
            Opcode::OR,
            Opcode::XOR,
            Opcode::SHL,
            Opcode::SHR
        };
        for (Opcode op : logicInstruction) {
            appendRoutine(op, aluRoutine(op));
        }

        // LOAD: rs1 + imm
        appendRoutine(Opcode::LOAD,
            { fetchRow(), aluImmRow(Opcode::ADD), memReadRow(),
             writebackMemRow() });

        // STORE: same address calc, then write. No writeback.
        appendRoutine(Opcode::STORE,
            { fetchRow(), aluImmRow(Opcode::ADD), memWriteRow() });

        // J-type unconditional transfer. The target is latched from the decoded
        // instruction, so there is no fetch row — pc_write replaces pc_inc.
        appendRoutine(Opcode::JMP, { jumpRow() });
        appendRoutine(Opcode::HALT, { haltRow() });
        // NOP still fetches the next word — only the datapath does nothing.
        appendRoutine(Opcode::NOP, { fetchRow() });

        // BEQ: conditional micro-branch on Z.
        //   Z set   -> taken row     (PC <- target)
        //   Z clear -> not-taken row (PC <- PC + 1)
        appendBranchRoutine(Opcode::BEQ, BranchCond::Zero, jumpRow(), fetchRow());

        // TODO (Oct 14): rows for CALL, RET, PUSH, POP (sp_dec / sp_inc signals
        // are already in ControlWord for these).
        // PUSH: decrease SP, then write the selected register to the stack.
        appendRoutine(Opcode::PUSH, { fetchRow(), pushRow() });

        // POP: read the stack word, increase SP, then write it to the selected register.
        appendRoutine(Opcode::POP, { fetchRow(), popRow() });
    }

    // Appends `routine` contiguously at the end of CM and returns its base address, rewriting each row's `next` to chain through the block.
    int ControlUnit::appendRaw(std::vector<MicroOp> routine) {
        const int base = static_cast<int>(cm_.size());

        for (size_t i = 0; i < routine.size(); ++i) {
            routine[i].next = (i + 1 < routine.size())
                ? base + static_cast<int>(i) + 1
                : kMicroEnd;
            cm_.push_back(routine[i]);
        }
        return base;
    }

    void ControlUnit::appendRoutine(Opcode op, std::vector<MicroOp> routine) {
        entries_[static_cast<uint8_t>(op)] = appendRaw(std::move(routine));
    }

    void ControlUnit::appendBranchRoutine(Opcode op, BranchCond cond,
                                          MicroOp taken_row, MicroOp not_taken_row) {
        MicroOp test;              // decision row: no datapath signals, only the test
        test.cond = cond;

        const int base = appendRaw({ test, not_taken_row, taken_row });

        // appendRaw chained the rows linearly; rewire them as a diamond:
        //   base --(cond false)--> base+1 (not taken) --> END
        //   base --(cond true )--> base+2 (taken)     --> END
        cm_[base].next          = base + 1;
        cm_[base].branch_target = base + 2;
        cm_[base + 1].next      = kMicroEnd;
        // cm_[base + 2].next is already kMicroEnd (last row of the block).

        entries_[static_cast<uint8_t>(op)] = base;
    }

    int ControlUnit::routineEntry(Opcode op) const {
        const auto it = entries_.find(static_cast<uint8_t>(op));
        return it == entries_.end() ? illegal_entry_ : it->second;
    }

    void ControlUnit::reset() {
        micro_pc_ = 0;
        done_ = false;
        latched_ = false;
    }

    ControlWord ControlUnit::step(Opcode op, const Flags& flags) {
        if (cm_.empty()) {
            // Vertical ROM not built yet
            done_ = true;
            ControlWord w;
            w.halt = true;
            return w;
        }

        if (done_) {
            // Caller did not reset() between macro instructions. Start a fresh
            // routine rather than re-emitting the previous terminal row forever.
            reset();
        }

        if (!latched_) {
            micro_pc_ = static_cast<uint32_t>(routineEntry(op));
            latched_ = true;
        }

        const MicroOp& cur = cm_[micro_pc_];
        const ControlWord out = cur.ctl;

        // Micro-sequencing: a branch row picks its successor from FLAGS,
        // every other row just follows `next`.
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

    // Horizontal microword layout (35 bits, LSB first):
    //   [0] alu_enable   [1] mem_read   [2] mem_write  [3] reg_write
    //   [4] pc_write     [5] pc_inc     [6] alu_src_imm [7] mem_to_reg
    //   [8] sp_dec       [9] sp_inc     [10] flag_update [11] halt
    //   [15:12] alu_op   [18:16] cond   [26:19] next    [34:27] branch_target
    // Address fields hold 0xFF for "end of routine".
    uint64_t ControlUnit::packHorizontal(const MicroOp& row) {
        const ControlWord& c = row.ctl;
        uint64_t w = 0;
        w |= static_cast<uint64_t>(c.alu_enable)  << 0;
        w |= static_cast<uint64_t>(c.mem_read)    << 1;
        w |= static_cast<uint64_t>(c.mem_write)   << 2;
        w |= static_cast<uint64_t>(c.reg_write)   << 3;
        w |= static_cast<uint64_t>(c.pc_write)    << 4;
        w |= static_cast<uint64_t>(c.pc_inc)      << 5;
        w |= static_cast<uint64_t>(c.alu_src_imm) << 6;
        w |= static_cast<uint64_t>(c.mem_to_reg)  << 7;
        w |= static_cast<uint64_t>(c.sp_dec)      << 8;
        w |= static_cast<uint64_t>(c.sp_inc)      << 9;
        w |= static_cast<uint64_t>(c.flag_update) << 10;
        w |= static_cast<uint64_t>(c.halt)        << 11;
        w |= (static_cast<uint64_t>(c.alu_op) & 0xFu) << 12;
        w |= (static_cast<uint64_t>(row.cond) & 0x7u) << 16;
        w |= static_cast<uint64_t>(addrField(row.next))          << 19;
        w |= static_cast<uint64_t>(addrField(row.branch_target)) << 27;
        return w;
    }

    std::string ControlUnit::dumpControlMemory() const {
        // Label the first row of each routine with its mnemonic.
        std::map<int, std::string> labels;
        if (!cm_.empty()) labels[illegal_entry_] = "ILLEGAL";
        for (const auto& [opc, addr] : entries_) {
            labels[addr] = opName(static_cast<Opcode>(opc));
        }

        std::ostringstream os;
        os << "addr  routine  cond  next  taken  microword(hex)  signals\n";
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
               << std::setfill('0') << std::setw(9) << packHorizontal(r)
               << std::dec << std::setfill(' ') << std::left << "     "
               << signalList(r.ctl) << "\n";
        }
        return os.str();
    }

}
