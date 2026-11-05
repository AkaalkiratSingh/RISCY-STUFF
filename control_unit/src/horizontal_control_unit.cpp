#include "control_unit/horizontal_control_unit.h"

#include "control_unit/control_path.h"
#include "utils/utils.h"

#include <iomanip>
#include <map>
#include <sstream>

namespace risc201 {
    namespace {

        MicroOp operandRow() {          
            MicroOp m;
            m.ctl.operand_read = true;
            return m;
        }

        MicroOp aluRow(Opcode fn) {   
            MicroOp m;
            m.ctl.alu_enable = true;
            m.ctl.flag_update = true;
            m.ctl.alu_op = fn;
            return m;
        }

        MicroOp aluRowKeepFlags(Opcode fn) {   // execute, FLAGS left alone: the ALU produces no
            MicroOp m;                         // flags for FADD/FSUB/FMUL/FDIV, so latching would
            m.ctl.alu_enable = true;           // overwrite them with all-false
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

        MicroOp memReadRow() {        // MDR <- MEM[ALU result]; bounds-checked before the read
            MicroOp m;
            m.ctl.mem_read = true;
            m.ctl.addr_check = true;
            return m;
        }

        MicroOp memWriteRow() {       // MEM[ALU result] <- rd; bounds-checked before the write
            MicroOp m;
            m.ctl.mem_write = true;
            m.ctl.addr_check = true;
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

        MicroOp raWriteRow() {
            MicroOp m;
            m.ctl.ra_write = true;
            return m;
        }

        MicroOp jumpRaRow() {
            MicroOp m;
            m.ctl.pc_write = true;
            m.ctl.pc_src_ra = true;
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
            return { operandRow(), aluRow(fn), writebackRow() };
        }

        const char* condName(BranchCond c) {
            switch (c) {
                case BranchCond::None:     return "-";
                case BranchCond::Zero:     return "Z";
                case BranchCond::NotZero:  return "!Z";
                case BranchCond::Negative: return "N";
                case BranchCond::Carry:    return "C";
                case BranchCond::Overflow: return "V";
                case BranchCond::GreaterThan: return "GT";
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
            add(w.ir_load, "ir_load");
            add(w.instr_decode, "instr_decode");
            add(w.operand_read, "operand_read");
            add(w.opcode_dispatch, "opcode_dispatch");
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
            add(w.pc_src_ra,    "pc_src_ra");
            add(w.stack_check,  "stack_check");
            add(w.addr_check,   "addr_check");
            add(w.ra_write,     "ra_write");
            add(w.halt,         "halt");
            add(w.trap,         "trap");
            return s.empty() ? "(idle)" : s;
        }

        uint32_t addrField(int32_t a) {
            return a == kMicroEnd ? kMicroEndField : (static_cast<uint32_t>(a) & 0xFFu);
        }

    }

    std::string controlSignalNames(const ControlWord& word) { return signalList(word); }

    ControlUnit::ControlUnit(MicrocodeEncoding encoding) : encoding_(encoding) {
        if (encoding_ == MicrocodeEncoding::Horizontal) {
            buildHorizontalRom();
        }
    }

    void ControlUnit::buildHorizontalRom() {
        illegal_entry_ = appendRaw({ trapRow() });

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
            { operandRow(), aluImmRow(Opcode::ADD), memReadRow(),
             writebackMemRow() });

        appendRoutine(Opcode::STORE,
            { operandRow(), aluImmRow(Opcode::ADD), memWriteRow() });

        appendRoutine(Opcode::JMP, { jumpRow() });
        appendRoutine(Opcode::HALT, { haltRow() });
        appendRoutine(Opcode::NOP, { MicroOp{} });

        appendBranchRoutine(Opcode::BEQ, BranchCond::Zero, jumpRow(), MicroOp{});

        appendRoutine(Opcode::MOV, { operandRow(), movRow(), writebackRow() });

        appendRoutine(Opcode::CALL,
            { operandRow(), jumpRow(), raWriteRow() });

        appendRoutine(Opcode::RET,
            { operandRow(), jumpRaRow() });

        // appendRoutine(Opcode::PUSH, { operandRow(), spDecRow(), pushWriteRow() });

        // appendRoutine(Opcode::POP,
        //     { operandRow(), stackPopReadRow(), writebackMemRow(), spIncRow() });

        appendRoutine(Opcode::NOT, aluRoutine(Opcode::NOT));
        appendRoutine(Opcode::SAR, aluRoutine(Opcode::SAR));
        appendRoutine(Opcode::MUL, aluRoutine(Opcode::MUL));

        appendRoutine(Opcode::CMP, { operandRow(), aluRow(Opcode::CMP) });

        trap_entry_ = appendRaw({ trapRow() });

        appendRoutine(Opcode::DIV, aluRoutine(Opcode::DIV));
        appendRoutine(Opcode::REM, aluRoutine(Opcode::REM));

        // BGT target: conditional micro-branch on flags.greaterThan (set by CMP), built exactly
        // like BEQ. Appended last so no existing routine moves.
        appendBranchRoutine(Opcode::BGT, BranchCond::GreaterThan, jumpRow(), MicroOp{});

        // Floating point (IEEE-754 single, held in the ordinary 32-bit registers; one ALU cycle each).
        // FADD/FSUB/FMUL/FDIV: fetch -> execute -> writeback, FLAGS untouched (see aluRowKeepFlags).
        for (Opcode op : { Opcode::FADD, Opcode::FSUB, Opcode::FMUL, Opcode::FDIV }) {
            appendRoutine(op, { operandRow(), aluRowKeepFlags(op), writebackRow() });
        }
        // FCMP rs1, rs2: sets zero/equal/greaterThan only, no writeback (like CMP), so BGT/BEQ can follow.
        appendRoutine(Opcode::FCMP, { operandRow(), aluRow(Opcode::FCMP) });

        MicroOp fetch, decode;
        fetch.ctl.ir_load = true;
        fetch.ctl.pc_inc = true;
        decode.ctl.instr_decode = true;
        decode.ctl.opcode_dispatch = true;
        frontend_entry_ = appendRaw({fetch, decode});
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

    ControlWord ControlUnit::step(Opcode op, const Flags& flags, ExceptionCode fault) {
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
            if (static_cast<int>(micro_pc_) == illegal_entry_) {
                pending_exception_ = ExceptionCode::ILLEGAL_INSTRUCTION;
            }
        }

        const MicroOp& cur = cm_[micro_pc_];
        ControlWord out = cur.ctl;

        if (faultApplies(cur.ctl, fault)) {
            pending_exception_ = fault;

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
        w |= static_cast<uint64_t>(c.addr_check)  << 41;
        w |= static_cast<uint64_t>(c.ra_write)    << 42;
        w |= static_cast<uint64_t>(c.pc_src_ra)   << 43;
        w |= static_cast<uint64_t>(c.ir_load) << 44;
        w |= static_cast<uint64_t>(c.instr_decode) << 45;
        w |= static_cast<uint64_t>(c.operand_read) << 46;
        w |= static_cast<uint64_t>(c.opcode_dispatch) << 47;
        return w;
    }

    std::string ControlUnit::dumpControlMemory() const {
        std::map<int, std::string> labels;
        if (!cm_.empty()) {
            labels[illegal_entry_] = "ILLEGAL";
            labels[trap_entry_]    = "TRAP";
            const std::vector<int> frontend = frontendRows(*this);
            if (frontend.size() > 0) labels[frontend[0]] = "FETCH";
            if (frontend.size() > 1) labels[frontend[1]] = "DECODE";
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
               << std::setfill('0') << std::setw(12) << packHorizontal(r)
               << std::dec << std::setfill(' ') << std::left << "     "
               << signalList(r.ctl) << "\n";
        }
        return os.str();
    }

}
