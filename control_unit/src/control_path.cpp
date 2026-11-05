#include "control_unit/control_path.h"

#include "control_unit/vertical_control_unit.h"

#include <algorithm>
#include <set>
#include <stdexcept>

namespace risc201 {

namespace {

constexpr size_t kPathGuard = kMaxControlMemoryRows + 1;

template <class Unit>
std::vector<int> frontendRowsOf(const Unit& cu) {
    std::vector<int> rows;
    const auto& cm = cu.controlMemory();
    if (cm.empty()) return rows;

    int address = cu.frontendEntry();
    while (address != kMicroEnd) {
        if (address < 0 || static_cast<size_t>(address) >= cm.size()) {
            throw std::logic_error("control path: frontend row out of range");
        }
        if (std::find(rows.begin(), rows.end(), address) != rows.end()) {
            throw std::logic_error("control path: frontend contains a loop");
        }
        rows.push_back(address);
        address = cm[static_cast<size_t>(address)].next;
    }
    return rows;
}

template <class Unit>
std::vector<int> routineRowsOf(const Unit& cu, Opcode op) {
    std::vector<int> rows;
    const auto& cm = cu.controlMemory();
    if (cm.empty()) return rows;

    std::vector<bool> seen(cm.size(), false);
    std::vector<int> pending{ cu.routineEntry(op) };
    while (!pending.empty()) {
        const int address = pending.back();
        pending.pop_back();

        if (address < 0 || static_cast<size_t>(address) >= cm.size()) continue;
        if (seen[static_cast<size_t>(address)]) continue;
        seen[static_cast<size_t>(address)] = true;
        rows.push_back(address);

        const MicroOp& row = cm[static_cast<size_t>(address)];
        if (row.next != kMicroEnd) pending.push_back(row.next);
        if (row.cond != BranchCond::None && row.branch_target != kMicroEnd) {
            pending.push_back(row.branch_target);
        }
    }

    std::sort(rows.begin(), rows.end());
    return rows;
}

template <class Unit>
std::vector<int> routineEndsOf(const Unit& cu) {
    std::set<int> ends;
    for (int i = 0; i < 256; ++i) {
        const std::vector<int> rows = routineRowsOf(cu, static_cast<Opcode>(i));
        if (!rows.empty()) ends.insert(rows.back());
    }
    return std::vector<int>(ends.begin(), ends.end());
}

template <class Unit>
std::vector<ControlStep> pathOf(const Unit& cu, Opcode op,
                                const Flags& flags, ExceptionCode fault) {
    std::vector<ControlStep> steps;
    const auto& cm = cu.controlMemory();
    if (cm.empty()) return steps;

    for (int address : frontendRowsOf(cu)) {
        const MicroOp& row = cm[static_cast<size_t>(address)];
        const int next = row.ctl.opcode_dispatch ? cu.routineEntry(op) : row.next;
        steps.push_back({address, row.ctl, row.cond, next, ExceptionCode::NONE});
    }

    const int trap = cu.trapEntry();
    int address = cu.routineEntry(op);
    for (size_t guard = 0; address != kMicroEnd; ++guard) {
        if (guard > kPathGuard) throw std::logic_error("control path: routine contains a loop");
        if (address < 0 || static_cast<size_t>(address) >= cm.size()) {
            throw std::logic_error("control path: routine row out of range");
        }

        const MicroOp& row = cm[static_cast<size_t>(address)];
        if (fault != ExceptionCode::NONE && faultApplies(row.ctl, fault)) {
            steps.push_back({address, ControlWord{}, row.cond, trap, fault});
            address = trap;
            continue;
        }

        const int next = (row.cond != BranchCond::None && condHolds(row.cond, flags))
                             ? row.branch_target
                             : row.next;
        const ExceptionCode exception =
            (address == trap && fault != ExceptionCode::NONE) ? fault : ExceptionCode::NONE;
        steps.push_back({address, row.ctl, row.cond, next, exception});
        address = next;
    }
    return steps;
}

} // namespace

std::vector<int> frontendRows(const ControlUnit& cu) { return frontendRowsOf(cu); }
std::vector<int> frontendRows(const VerticalControlUnit& cu) { return frontendRowsOf(cu); }

std::vector<int> routineRows(const ControlUnit& cu, Opcode op) {
    return routineRowsOf(cu, op);
}
std::vector<int> routineRows(const VerticalControlUnit& cu, Opcode op) {
    return routineRowsOf(cu, op);
}

std::vector<int> routineEnds(const ControlUnit& cu) { return routineEndsOf(cu); }
std::vector<int> routineEnds(const VerticalControlUnit& cu) { return routineEndsOf(cu); }

std::vector<ControlStep> controlPath(const ControlUnit& cu, Opcode op,
                                     const Flags& flags, ExceptionCode fault) {
    return pathOf(cu, op, flags, fault);
}
std::vector<ControlStep> controlPath(const VerticalControlUnit& cu, Opcode op,
                                     const Flags& flags, ExceptionCode fault) {
    return pathOf(cu, op, flags, fault);
}

} // namespace risc201
