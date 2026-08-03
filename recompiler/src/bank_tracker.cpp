/**
 * @file bank_tracker.cpp
 * @brief Bank tracking for multi-ROM-bank games
 */

#include "recompiler/bank_tracker.h"

namespace gbrecomp {

void BankTracker::record_bank_switch(uint32_t addr, BankId bank, bool dynamic) {
    for (auto& existing : switches_) {
        if (existing.addr != addr) {
            continue;
        }
        if (existing.is_dynamic || dynamic || existing.target_bank != bank) {
            existing.target_bank = UNKNOWN_BANK;
            existing.is_dynamic = true;
        }
        return;
    }
    switches_.push_back({addr, bank, dynamic});
}

void BankTracker::record_cross_bank_call(uint32_t from, uint32_t to, BankId from_bank, BankId to_bank) {
    calls_.push_back({from, to, from_bank, to_bank});
}

int BankTracker::get_bank_at(uint32_t addr) const {
    // Simple heuristic: use the most recent switch before this address
    // This is naive and assumes linear execution order, which isn't true for branching code.
    // Real analysis needs control flow awareness.
    int bank = -1; // Unknown by default
    
    // This method is likely not very useful without CFG, but we keep it for now
    // as a best-effort lookup if we assume the switches list is naturally ordered
    // by discovery in the analyzer.
    for (const auto& sw : switches_) {
        if (sw.addr < addr && !sw.is_dynamic) {
            bank = sw.target_bank;
        }
    }
    return bank;
}

bool BankTracker::has_dynamic_switches() const {
    for (const auto& sw : switches_) {
        if (sw.is_dynamic) return true;
    }
    return false;
}

} // namespace gbrecomp
