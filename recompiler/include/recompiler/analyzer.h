/**
 * @file analyzer.h
 * @brief Control flow analysis for GameBoy ROMs
 */

#ifndef RECOMPILER_ANALYZER_H
#define RECOMPILER_ANALYZER_H

#include "decoder.h"
#include "rom.h"
#include "bank_tracker.h"
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
    BankId bank;                        // ROM bank this block belongs to
    
    std::vector<size_t> instruction_indices;
    
    // Control flow
    std::vector<uint32_t> successors;   // Full (bank << 16 | addr) successors
    std::vector<uint32_t> predecessors; // Full (bank << 16 | addr) predecessors
    
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
    BankId bank;
    
    std::vector<uint16_t> block_addresses;
    
    bool is_interrupt_handler = false;
    bool crosses_banks = false;         // Calls into other banks
    bool is_called_cross_bank = false;  // Called from other banks
    bool may_switch_rom_bank = false;   // Contains a detected MBC bank write
};

/**
 * @brief Symbol metadata associated with a ROM address
 */
struct AddressSymbolMetadata {
    std::string source_name;
    std::vector<std::string> source_names;
    std::string emitted_name;
    std::string kind;
    std::string provenance;
    std::string memory_space;
    uint32_t width = 1;
    std::string comment;
};

struct ConstantSymbolMetadata {
    uint32_t value = 0;
    std::string memory_space = "constant";
    std::string provenance;
    std::string comment;
};

enum class AnalysisAnnotationKind {
    FUNCTION,
    LABEL,
    DATA,
};

struct AnalysisAnnotation {
    uint32_t addr = 0;
    uint32_t size = 1;
    AnalysisAnnotationKind kind = AnalysisAnnotationKind::LABEL;
};

/**
 * @brief Stable, actionable record for a conservative analyzer decision
 */
struct AnalysisDiagnostic {
    std::string id;
    std::string kind;
    BankId bank = 0;
    uint16_t address = 0;
    std::string memory_space;
    std::string status;
    std::string evidence;
    std::string suggested_annotation;
    std::string relationship;
    bool has_related_address = false;
    BankId related_bank = 0;
    uint16_t related_address = 0;
    std::string related_memory_space;
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

    // Imported symbol metadata indexed by (bank << 16 | addr)
    std::map<uint32_t, AddressSymbolMetadata> symbol_metadata;
    std::map<std::string, ConstantSymbolMetadata> constant_metadata;

    // Conservative analysis choices and unresolved sites, keyed by stable ID.
    std::map<std::string, AnalysisDiagnostic> analysis_diagnostics;
    
    // Labels needed (jump targets)
    std::set<uint32_t> label_addresses;  // (bank << 16 | addr)
    
    // Call targets (function entry points)
    std::set<uint32_t> call_targets;

    // Entry points that must remain standalone callable functions.
    // This excludes some synthetic targets used only for control-flow recovery.
    std::set<uint32_t> strong_call_targets;

    // Synthetic entry points recovered from thunk patterns (for example
    // pushed return addresses before JP HL). These must remain dispatchable
    // function entries, but are tracked separately from user-visible entry
    // points so later passes can distinguish them.
    std::set<uint32_t> synthetic_entry_targets;

    // Targets discovered through static/computed jump analysis. These need to
    // remain dispatchable roots if no larger function absorbs them, but they
    // should not automatically force hard function boundaries the way real
    // call targets do.
    std::set<uint32_t> branch_entry_targets;
    
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
    
    // Bank tracker results
    BankTracker bank_tracker;
    
    // Helper to create combined address
    static uint32_t make_addr(BankId bank, uint16_t addr) {
        return (static_cast<uint32_t>(bank) << 16) | addr;
    }
    
    // Get instruction at bank:addr
    const Instruction* get_instruction(BankId bank, uint16_t addr) const;
    
    // Get block at bank:addr
    const BasicBlock* get_block(BankId bank, uint16_t addr) const;
    
    // Get function at bank:addr
    const Function* get_function(BankId bank, uint16_t addr) const;
};

/* ============================================================================
 * Analyzer Interface
 * ========================================================================== */

/**
 * @brief Analysis options
 */
struct AnalyzerOptions {
    // Map of RAM address -> ROM address (source of the code)
    // This allows analyzing code that is copied to RAM (e.g. OAM DMA routines)
    struct RamOverlay {
        uint16_t ram_addr;
        uint32_t rom_addr;
        uint16_t size;
    };
    std::vector<RamOverlay> ram_overlays;
    
    // Explicit list of entry points to analyze (in addition to standard ones)
    std::vector<uint32_t> entry_points;

    bool analyze_all_banks = true;      // Analyze all ROM banks
    bool detect_computed_jumps = true;  // Try to resolve JP HL targets
    bool track_bank_switches = true;    // Track bank switch operations
    bool mark_unreachable = true;       // Mark unreachable code
    
    // Debugging options
    bool trace_log = false;             // Print detailed execution trace
    bool verbose = false;               // Print verbose analysis info
    size_t max_instructions = 0;        // Max instructions to analyze (0 = infinite)
    size_t max_functions = 0;           // Max functions to discover (0 = infinite)
    
    // Feature flags
    bool aggressive_scan = true;        // Scan for unreferenced code (ON by default)
    std::string trace_file_path;        // Path to entry points trace file
    std::vector<AnalysisAnnotation> annotations;
    bool add_builtin_rom_annotations = true;
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
AnalysisResult analyze_bank(const ROM& rom, BankId bank,
                            const AnalyzerOptions& options = {});

/* ============================================================================
 * Utility Functions
 * ========================================================================== */

/**
 * @brief Generate function name for an address
 */
std::string generate_function_name(BankId bank, uint16_t address);

/**
 * @brief Generate label name for an address
 */
std::string generate_label_name(BankId bank, uint16_t address);

/**
 * @brief Print analysis summary
 */
void print_analysis_summary(const AnalysisResult& result);

/**
 * @brief Check if address is likely data (not code)
 */
bool is_likely_data(const AnalysisResult& result, BankId bank, uint16_t address);

} // namespace gbrecomp

#endif // RECOMPILER_ANALYZER_H
