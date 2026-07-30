#pragma once

#include "rom.h"
#include <cstdint>
#include <vector>
#include <unordered_map>

namespace gbrecomp {

/**
 * @brief Tracks ROM bank switches for static analysis
 * 
 * Many games use bank switching, and we need to track which
 * banks are accessed from which code locations to properly
 * analyze control flow across bank boundaries.
 */
class BankTracker {
public:
    struct BankSwitch {
        uint32_t addr;          /**< Address where switch occurs (bank:addr) */
        BankId target_bank;     /**< Bank being switched to */
        bool is_dynamic;        /**< True if bank is loaded from register/variable */
    };
    
    struct BankCall {
        uint32_t from_addr;     /**< Caller address */
        uint32_t to_addr;       /**< Callee address */
        BankId from_bank;       /**< Caller bank */
        BankId to_bank;         /**< Callee bank (may be same) */
    };
    
    /**
     * @brief Record a bank switch at the given address
     */
    void record_bank_switch(uint32_t addr, BankId bank, bool dynamic = false);
    
    /**
     * @brief Record a cross-bank call
     */
    void record_cross_bank_call(uint32_t from, uint32_t to, BankId from_bank, BankId to_bank);
    
    /**
     * @brief Get all bank switches
     */
    const std::vector<BankSwitch>& switches() const { return switches_; }
    
    /**
     * @brief Get all cross-bank calls
     */
    const std::vector<BankCall>& calls() const { return calls_; }
    
    /**
     * @brief Get the active bank at a given address (if determinable)
     * @return Bank number, or -1 if unknown
     */
    int get_bank_at(uint32_t addr) const;
    
    /**
     * @brief Check if there are any dynamic bank switches
     */
    bool has_dynamic_switches() const;

private:
    std::vector<BankSwitch> switches_;
    std::vector<BankCall> calls_;
};

} // namespace gbrecomp
