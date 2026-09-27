#include "control_unit/control_unit.h"

namespace risc201 {
    namespace {

        MicroOp fetchRow() {          // μ1: sequential fetch, PC <- PC + 1
            MicroOp m;
            m.ctl.pc_inc = true;
            return m;
        }

        MicroOp aluRow(Opcode fn) {   // execute, operand B from rs2
            MicroOp m;
            m.ctl.alu_enable = true;
            m.ctl.alu_op = fn;
            return m;
        }

        MicroOp aluImmRow(Opcode fn) {  // execute, operand B from immediate — for
            MicroOp m;                  // LOAD/STORE this is the effective-address calc
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
        appendRoutine(Opcode::NOP, { idleRow() });

        // TODO: rows for BEQ, CALL, RET, PUSH, POP. BEQ
    }

    // Appends `routine` contiguously at the end of CM and returns its base address, rewriting each row's `next` to chain through the block. 
    int ControlUnit::appendRaw(std::vector<MicroOp> routine) {
        const int base = cm_.size();

        for (int i = 0; i < routine.size(); ++i) {
            routine[i].next = (i + 1 < routine.size())
                ? base + i + 1
                : kMicroEnd;
            cm_.push_back(routine[i]);
        }
        return base;
    }

    void ControlUnit::appendRoutine(Opcode op, std::vector<MicroOp> routine) {
        entries_[(uint8_t)op] = appendRaw(std::move(routine));
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

    ControlWord ControlUnit::step(Opcode op, const Flags& /*flags*/) {
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

        if (cur.next == kMicroEnd) {
            done_ = true;
            micro_pc_ = 0;   
        }
        else {
            micro_pc_ = static_cast<uint32_t>(cur.next);
        }
        return out;
    }

} 
