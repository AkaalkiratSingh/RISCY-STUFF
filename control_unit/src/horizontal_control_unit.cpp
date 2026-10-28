#include "control_unit/horizontal_control_unit.h"
#include "utils/utils.h"

#include <iomanip>
#include <map>
#include <sstream>

namespace risc201 {
    namespace {

        MicroOp fetchRow() {          
            MicroOp m;
            m.ctl.pc_inc = true;
            return m;
        }

        MicroOp aluRow(Opcode fn) {   
            MicroOp m;
            m.ctl.alu_enable = true;
            m.ctl.flag_update = true;
            m.ctl.alu_op = fn;
            return m;
        }

        MicroOp movRow() {
            MicroOp m;
            m.ctl.alu_enable = true;
            m.ctl.alu_op = Opcode::MOV;
            return m;
        }

        MicroOp aluImmRow(Opcode fn) {
            MicroOp m;
            m.ctl.alu_enable = true;
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

        MicroOp pushWriteRow() {
            MicroOp m;
            m.ctl.mem_write = true;
            m.ctl.mem_addr_sp = true;
            m.ctl.stack_check = true;
            return m;
        }

        MicroOp writebackRow() {
            MicroOp m;
            m.ctl.reg_write = true;
            return m;
        }

        MicroOp writebackMemRow() {
            MicroOp m;
            m.ctl.reg_write = true;
            m.ctl.mem_to_reg = true;
            return m;
        }

        MicroOp jumpRow() {
            MicroOp m;
            m.ctl.pc_write = true;
            return m;
        }

        MicroOp spDecRow() {
            MicroOp m;
            m.ctl.sp_dec = true;
            return m;
        }

        MicroOp spIncRow() {
            MicroOp m;
            m.ctl.sp_inc = true;
            return m;
        }

        MicroOp stackPushWriteRow() {
            MicroOp m;
            m.ctl.mem_write = true;
            m.ctl.mem_addr_sp = true;
            m.ctl.mem_data_pc = true;
            m.ctl.stack_check = true;
            return m;
        }

        MicroOp stackPopReadRow() {
            MicroOp m;
            m.ctl.mem_read = true;
            m.ctl.mem_addr_sp = true;
            m.ctl.stack_check = true;
            return m;
        }

        MicroOp pcFromMemRow() {
            MicroOp m;
            m.ctl.pc_write = true;
            m.ctl.pc_src_mem = true;
            return m;
        }

        MicroOp haltRow() {
            MicroOp m;
            m.ctl.halt = true;
            return m;
        }

        MicroOp trapRow() {
            MicroOp m;
            m.ctl.trap = true;
            m.ctl.halt = true;
            return m;
        }

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
            add(w.trap,         "trap");
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

        appendRoutine(Opcode::LOAD,
            { fetchRow(), aluImmRow(Opcode::ADD), memReadRow(),
             writebackMemRow() });

        appendRoutine(Opcode::STORE,
            { fetchRow(), aluImmRow(Opcode::ADD), memWriteRow() });

        appendRoutine(Opcode::JMP, { jumpRow() });
        appendRoutine(Opcode::HALT, { haltRow() });
        appendRoutine(Opcode::NOP, { fetchRow() });

        appendBranchRoutine(Opcode::BEQ, BranchCond::Zero, jumpRow(), fetchRow());

        appendRoutine(Opcode::MOV, { fetchRow(), movRow(), writebackRow() });

        appendRoutine(Opcode::CALL,
            { fetchRow(), spDecRow(), stackPushWriteRow(), jumpRow() });

        appendRoutine(Opcode::RET,
            { stackPopReadRow(), pcFromMemRow(), spIncRow() });

        appendRoutine(Opcode::PUSH, { fetchRow(), spDecRow(), pushWriteRow() });

        appendRoutine(Opcode::POP,
            { fetchRow(), stackPopReadRow(), writebackMemRow(), spIncRow() });

        appendRoutine(Opcode::NOT, aluRoutine(Opcode::NOT));
        appendRoutine(Opcode::SAR, aluRoutine(Opcode::SAR));
        appendRoutine(Opcode::MUL, aluRoutine(Opcode::MUL));

        appendRoutine(Opcode::CMP, { fetchRow(), aluRow(Opcode::CMP) });

        trap_entry_ = appendRaw({ trapRow() });

        appendRoutine(Opcode::DIV, aluRoutine(Opcode::DIV));
        appendRoutine(Opcode::REM, aluRoutine(Opcode::REM));
    }

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
        MicroOp test;
        test.cond = cond;

        const int base = appendRaw({ test, not_taken_row, taken_row });

        cm_[base].next          = base + 1;
        cm_[base].branch_target = base + 2;
        cm_[base + 1].next      = kMicroEnd;

        entries_[static_cast<uint8_t>(op)] = base;
    }

    int ControlUnit::routineEntry(Opcode op) const {
        const auto it = entries_.find(static_cast<uint8_t>(op));
        return it == entries_.end() ? illegal_entry_ : it->second;
    }

    void ControlUnit::restart() {
        micro_pc_ = 0;
        done_ = false;
        latched_ = false;
    }

    void ControlUnit::reset() {
        restart();
        pending_exception_ = ExceptionCode::NONE;
    }

    ControlWord ControlUnit::step(Opcode op, const Flags& flags, ExceptionCode alu_fault) {
        if (cm_.empty()) {
            done_ = true;
            ControlWord w;
            w.halt = true;
            return w;
        }

        if (done_) {
            restart();
        }

        if (!latched_) {
            micro_pc_ = static_cast<uint32_t>(routineEntry(op));
            latched_ = true;
        }

        const MicroOp& cur = cm_[micro_pc_];
        ControlWord out = cur.ctl;

        if (alu_fault != ExceptionCode::NONE && cur.ctl.alu_enable) {
            pending_exception_ = alu_fault;

            out = ControlWord{};

            micro_pc_ = static_cast<uint32_t>(trap_entry_);
            return out;
        }

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
        w |= static_cast<uint64_t>(c.mem_addr_sp) << 12;
        w |= static_cast<uint64_t>(c.mem_data_pc) << 13;
        w |= static_cast<uint64_t>(c.pc_src_mem)  << 14;
        w |= static_cast<uint64_t>(c.stack_check) << 15;
        w |= (static_cast<uint64_t>(c.alu_op) & 0x1Fu) << 16;
        w |= (static_cast<uint64_t>(row.cond) & 0x7u) << 21;
        w |= static_cast<uint64_t>(addrField(row.next))          << 24;
        w |= static_cast<uint64_t>(addrField(row.branch_target)) << 32;
        w |= static_cast<uint64_t>(c.trap)        << 40;
        return w;
    }

    std::string ControlUnit::dumpControlMemory() const {
        std::map<int, std::string> labels;
        if (!cm_.empty()) {
            labels[illegal_entry_] = "ILLEGAL";
            labels[trap_entry_]    = "TRAP";
        }
        for (const auto& [opc, addr] : entries_) {
            labels[addr] = opcodeName(static_cast<Opcode>(opc));
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
               << std::setfill('0') << std::setw(11) << packHorizontal(r)
               << std::dec << std::setfill(' ') << std::left << "     "
               << signalList(r.ctl) << "\n";
        }
        return os.str();
    }

}
