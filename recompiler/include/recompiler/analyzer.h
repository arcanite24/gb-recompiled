/**
 * @file analyzer.h
 * @brief Control flow analysis for GameBoy ROMs
 */

#ifndef RECOMPILER_ANALYZER_H
#define RECOMPILER_ANALYZER_H

#include "decoder.h"
#include "rom.h"
#include <map>
#include <set>
#include <vector>
#include <string>

namespace gbrecomp {

/* ============================================================================
 * Basic Block
 * ========================================================================== */

/**
 * @brief A basic block - sequence of instructions without branches
 */
struct BasicBlock {
    uint16_t start_address;
    uint16_t end_address;
    uint8_t bank;                       // ROM bank this block belongs to
    
    std::vector<size_t> instruction_indices;
    
    // Control flow
    std::vector<uint16_t> successors;   // Addresses of successor blocks
    std::vector<uint16_t> predecessors; // Addresses of predecessor blocks
    
    // Cross-bank info
    bool has_cross_bank_successor = false;
    
    // Labels needed within this block
    std::set<uint16_t> internal_labels;
    
    // Is this block the entry to a function?
    bool is_function_entry = false;
    
    // Is this block an interrupt handler entry?
    bool is_interrupt_entry = false;
    
    // Is this block reachable from entry points?
    bool is_reachable = false;
};

/* ============================================================================
 * Function
 * ========================================================================== */

/**
 * @brief A function - collection of basic blocks with single entry
 */
struct Function {
    std::string name;
    uint16_t entry_address;
    uint8_t bank;
    
    std::vector<uint16_t> block_addresses;
    
    bool is_interrupt_handler = false;
    bool crosses_banks = false;         // Calls into other banks
    bool is_called_cross_bank = false;  // Called from other banks
};

/* ============================================================================
 * Analysis Result
 * ========================================================================== */

/**
 * @brief Complete control flow analysis result
 */
struct AnalysisResult {
    // ROM reference
    const ROM* rom = nullptr;
    
    // All decoded instructions
    std::vector<Instruction> instructions;
    
    // Address to instruction index map
    std::map<uint32_t, size_t> addr_to_index;  // (bank << 16 | addr) -> index
    
    // Basic blocks indexed by (bank << 16 | addr)
    std::map<uint32_t, BasicBlock> blocks;
    
    // Functions indexed by (bank << 16 | addr)  
    std::map<uint32_t, Function> functions;
    
    // Labels needed (jump targets)
    std::set<uint32_t> label_addresses;  // (bank << 16 | addr)
    
    // Call targets (function entry points)
    std::set<uint32_t> call_targets;
    
    // Computed jump targets (JP HL, etc.)
    std::set<uint32_t> computed_jump_targets;
    
    // Bank switch points
    std::set<uint16_t> bank_switch_addresses;
    
    // Entry point
    uint16_t entry_point = 0x100;
    
    // Interrupt vectors
    std::vector<uint16_t> interrupt_vectors;
    
    // Statistics
    struct {
        size_t total_instructions = 0;
        size_t total_blocks = 0;
        size_t total_functions = 0;
        size_t unreachable_instructions = 0;
        size_t cross_bank_calls = 0;
    } stats;
    
    // Helper to create combined address
    static uint32_t make_addr(uint8_t bank, uint16_t addr) {
        return (static_cast<uint32_t>(bank) << 16) | addr;
    }
    
    // Get instruction at bank:addr
    const Instruction* get_instruction(uint8_t bank, uint16_t addr) const;
    
    // Get block at bank:addr
    const BasicBlock* get_block(uint8_t bank, uint16_t addr) const;
    
    // Get function at bank:addr
    const Function* get_function(uint8_t bank, uint16_t addr) const;
};

/* ============================================================================
 * Analyzer Interface
 * ========================================================================== */

/**
 * @brief Analysis options
 */
struct AnalyzerOptions {
    bool analyze_all_banks = true;      // Analyze all ROM banks
    bool detect_computed_jumps = true;  // Try to resolve JP HL targets
    bool track_bank_switches = true;    // Track bank switch operations
    bool mark_unreachable = true;       // Mark unreachable code
    
    // Debugging options
    bool trace_log = false;             // Print detailed execution trace
    size_t max_instructions = 0;        // Max instructions to analyze (0 = infinite)
    size_t max_functions = 0;           // Max functions to discover (0 = infinite)
};

/**
 * @brief Analyze a ROM
 * 
 * @param rom Loaded ROM
 * @param options Analysis options
 * @return Analysis result
 */
AnalysisResult analyze(const ROM& rom, const AnalyzerOptions& options = {});

/**
 * @brief Analyze a single bank
 * 
 * @param rom Loaded ROM
 * @param bank Bank number to analyze
 * @param options Analysis options
 * @return Partial analysis for that bank
 */
AnalysisResult analyze_bank(const ROM& rom, uint8_t bank,
                            const AnalyzerOptions& options = {});

/* ============================================================================
 * Utility Functions
 * ========================================================================== */

/**
 * @brief Generate function name for an address
 */
std::string generate_function_name(uint8_t bank, uint16_t address);

/**
 * @brief Generate label name for an address
 */
std::string generate_label_name(uint8_t bank, uint16_t address);

/**
 * @brief Print analysis summary
 */
void print_analysis_summary(const AnalysisResult& result);

/**
 * @brief Check if address is likely data (not code)
 */
bool is_likely_data(const AnalysisResult& result, uint8_t bank, uint16_t address);

} // namespace gbrecomp

#endif // RECOMPILER_ANALYZER_H
