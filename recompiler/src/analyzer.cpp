/**
 * @file analyzer.cpp
 * @brief Control flow analyzer implementation (stub for MVP)
 */

#include "recompiler/analyzer.h"
#include <algorithm>
#include <queue>
#include <iostream>
#include <iomanip>
#include <sstream>

namespace gbrecomp {

/* ============================================================================
 * Helper Functions
 * ========================================================================== */

static uint32_t make_address(uint8_t bank, uint16_t addr) {
    return (static_cast<uint32_t>(bank) << 16) | addr;
}

static uint8_t get_bank(uint32_t addr) {
    return static_cast<uint8_t>(addr >> 16);
}

static uint16_t get_offset(uint32_t addr) {
    return static_cast<uint16_t>(addr & 0xFFFF);
}

/* ============================================================================
 * AnalysisResult Implementation
 * ========================================================================== */

const Instruction* AnalysisResult::get_instruction(uint8_t bank, uint16_t addr) const {
    uint32_t full_addr = make_addr(bank, addr);
    auto it = addr_to_index.find(full_addr);
    if (it != addr_to_index.end() && it->second < instructions.size()) {
        return &instructions[it->second];
    }
    return nullptr;
}

const BasicBlock* AnalysisResult::get_block(uint8_t bank, uint16_t addr) const {
    uint32_t full_addr = make_addr(bank, addr);
    auto it = blocks.find(full_addr);
    if (it != blocks.end()) {
        return &it->second;
    }
    return nullptr;
}

const Function* AnalysisResult::get_function(uint8_t bank, uint16_t addr) const {
    uint32_t full_addr = make_addr(bank, addr);
    auto it = functions.find(full_addr);
    if (it != functions.end()) {
        return &it->second;
    }
    return nullptr;
}

/* ============================================================================
 * RST Pattern Detection
 * ========================================================================== */

/**
 * @brief Check if a RST vector contains only 0xFF padding (not real code)
 * 
 * Many ROMs have 0xFF padding at unused RST vector locations.
 * This prevents infinite recursion when analyzing these vectors.
 * 
 * @param rom The ROM to check
 * @param vector The RST vector address (0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38)
 * @return true if the vector contains only 0xFF bytes
 */
static bool is_rst_padding(const ROM& rom, uint16_t vector) {
    // RST vectors are 8 bytes apart, check all bytes up to the next vector
    uint16_t end = vector + 8;
    if (end > 0x40) end = 0x40;  // Don't go past RST 38 region
    
    for (uint16_t addr = vector; addr < end; addr++) {
        if (rom.read_banked(0, addr) != 0xFF) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Check if RST 28 is a jump table dispatcher
 * 
 * Tetris and many other GB games use RST 28 as a computed jump table:
 *   ADD A,A       ; Double A (table entries are 2 bytes)
 *   POP HL        ; Get return address (points to table)
 *   ...
 *   JP (HL)       ; Jump to looked-up address
 * 
 * The bytes following RST 28 calls are table data, NOT code.
 */
static bool is_rst28_jump_table(const ROM& rom) {
    // Check for the pattern starting at 0x28:
    // 87 E1 ... E9 (ADD A,A; POP HL; ...; JP (HL))
    if (rom.read_banked(0, 0x28) == 0x87 &&  // ADD A,A
        rom.read_banked(0, 0x29) == 0xE1) {  // POP HL
        // Look for JP (HL) = 0xE9 somewhere in 0x28-0x3F region
        for (uint16_t addr = 0x2A; addr < 0x40; addr++) {
            if (rom.read_banked(0, addr) == 0xE9) {
                return true;
            }
        }
    }
    return false;
}

/**
 * @brief Check if RST 28 falls through into RST 30
 * 
 * When RST 28 is a jump table dispatcher, it typically continues through
 * RST 30's space to reach JP (HL). In this case, RST 30 should NOT be
 * marked as a separate function entry since it's part of RST 28's routine.
 * 
 * Pattern: RST 28 at 0x28-0x2F falls through to code at 0x30-0x33 ending with JP (HL)
 */
static bool rst28_uses_rst30(const ROM& rom) {
    if (!is_rst28_jump_table(rom)) {
        return false;
    }
    
    // Check if JP (HL) (0xE9) is in the 0x30-0x37 range (RST 30 region)
    for (uint16_t addr = 0x30; addr < 0x38; addr++) {
        if (rom.read_banked(0, addr) == 0xE9) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Extract jump table entries following an RST 28 call site
 * 
 * When RST 28 is a jump table dispatcher, the bytes immediately following
 * the RST 28 opcode are 16-bit addresses (in little-endian format).
 * 
 * Pattern from Tetris:
 *   ldh  a,(0cdh)     ; Load index value
 *   rst  28h          ; Call jump table dispatcher (opcode 0xEF)
 *   .dw  l0078        ; Entry 0
 *   .dw  l009f        ; Entry 1
 *   ...
 * 
 * @param rom The ROM to read from
 * @param rst_call_addr Address of the RST 28 opcode (0xEF)
 * @param bank Bank number for the call site
 * @return Vector of extracted jump table target addresses
 */
static std::vector<uint16_t> extract_rst28_table_entries(const ROM& rom, uint16_t rst_call_addr, uint8_t bank) {
    std::vector<uint16_t> targets;
    
    // Table starts immediately after the RST 28 opcode (1 byte)
    uint16_t table_start = rst_call_addr + 1;
    
    // We don't know the table size statically.
    // Heuristic: Read addresses until we hit:
    // 1. An address that's clearly not code (below 0x0100 except for RST vectors)
    // 2. An address that overlaps with known code
    // 3. An unreasonably large number of entries (e.g., > 64)
    // 4. An address at or past 0x8000 (not ROM)
    
    const int MAX_TABLE_ENTRIES = 64;  // Tetris has up to 44 entries in its main state machine
    
    for (int i = 0; i < MAX_TABLE_ENTRIES; i++) {
        uint16_t entry_addr = table_start + i * 2;
        
        // Make sure we can read 2 bytes
        size_t rom_offset;
        if (entry_addr < 0x4000) {
            rom_offset = entry_addr;
        } else {
            rom_offset = static_cast<size_t>(bank) * 0x4000 + (entry_addr - 0x4000);
        }
        
        if (rom_offset + 1 >= rom.size()) {
            break;  // Past end of ROM
        }
        
        // Read 16-bit address (little-endian)
        // For addresses in bank 0 region (< 0x4000), always use bank 0
        uint8_t read_bank = (entry_addr < 0x4000) ? 0 : bank;
        uint8_t lo = rom.read_banked(read_bank, entry_addr);
        uint8_t hi = rom.read_banked(read_bank, entry_addr + 1);
        uint16_t target = static_cast<uint16_t>(lo) | (static_cast<uint16_t>(hi) << 8);
        
        // Validate the target address
        if (target >= 0x8000) {
            // Not ROM - likely end of table
            break;
        }
        
        // Address should be aligned to reasonable code
        // Very low addresses (0x00-0x3F) are RST/INT vectors, which is OK
        // Addresses 0x40-0xFF should be valid only for known interrupt handlers
        // Core code typically starts at 0x100+
        if (target == 0x0000 || target == 0xFFFF) {
            // Invalid entry, likely end of table
            break;
        }
        
        // Add the target if it looks valid
        targets.push_back(target);
    }
    
    return targets;
}

/* ============================================================================
 * Bank Switch Detection
 * ========================================================================== */

/**
 * @brief Detect immediate bank values from common patterns
 * 
 * Looks for patterns like:
 *   LD A, n      ; n is bank number
 *   LD (2000), A ; or LD (2100), A, etc.
 */
static std::set<uint8_t> detect_bank_values(const ROM& rom) {
    std::set<uint8_t> banks;
    banks.insert(0);  // Bank 0 is always present
    banks.insert(1);  // Bank 1 is the default switchable bank
    
    // Use ROM header to know how many banks exist
    uint16_t bank_count = rom.header().rom_banks;
    for (uint16_t i = 0; i < bank_count && i < 256; i++) {
        banks.insert(static_cast<uint8_t>(i));
    }
    
    return banks;
}

/* ============================================================================
 * Analysis Implementation
 * ========================================================================== */

AnalysisResult analyze(const ROM& rom, const AnalyzerOptions& options) {
    AnalysisResult result;
    result.rom = &rom;
    result.entry_point = 0x100;
    
    // Add standard GameBoy entry points
    result.interrupt_vectors = {0x40, 0x48, 0x50, 0x58, 0x60};  // Interrupt vectors
    
    Decoder decoder(rom);
    
    // Detect which banks are used
    std::set<uint8_t> known_banks = detect_bank_values(rom);
    
    // Track addresses to explore: (bank, addr, known_bank_state)
    // We explore each bank separately for code in 0x4000-0x7FFF
    std::queue<uint32_t> work_queue;
    std::set<uint32_t> visited;
    
    // Entry point is always a function (bank 0)
    result.call_targets.insert(make_address(0, 0x100));
    
    // RST vectors are implicit functions (always bank 0)
    // Skip any RST vector that contains only 0xFF padding (common in many ROMs)
    // Also skip RST 30 when RST 28 uses it as part of its jump table implementation
    bool skip_rst30 = rst28_uses_rst30(rom);
    for (uint16_t vec : {0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38}) {
        if (is_rst_padding(rom, vec)) {
            continue;  // Skip this RST vector - contains only padding
        }
        if (vec == 0x30 && skip_rst30) {
            continue;  // Skip RST 30 - it's part of RST 28's jump table implementation
        }
        result.call_targets.insert(make_address(0, vec));
        work_queue.push(make_address(0, vec));
    }
    
    // Interrupt vectors are functions (bank 0)
    for (uint16_t vec : result.interrupt_vectors) {
        result.call_targets.insert(make_address(0, vec));
        work_queue.push(make_address(0, vec));
    }
    
    // Start from entry point
    work_queue.push(make_address(0, 0x100));
    
    // For MBC games, also analyze code at entry point in each bank
    // This catches trampoline code that jumps from bank 0 to banked code
    if (rom.header().mbc_type != MBCType::NONE && options.analyze_all_banks) {
        std::cerr << "Analyzing all " << known_banks.size() << " banks\n";
        for (uint8_t bank : known_banks) {
            if (bank > 0) {
                // Check for code at common bank entry points
                // Many games have jump tables or trampolines at 0x4000
                work_queue.push(make_address(bank, 0x4000));
                result.call_targets.insert(make_address(bank, 0x4000));
            }
        }
    }
    
    // Explore all reachable code
    while (!work_queue.empty()) {
        uint32_t addr = work_queue.front();
        work_queue.pop();
        
        if (visited.count(addr)) continue;
        
        uint8_t bank = get_bank(addr);
        uint16_t offset = get_offset(addr);
        
        // Only analyze ROM space
        if (offset >= 0x8000) continue;
        
        // Bank mapping rules:
        // 0x0000-0x3FFF: Always bank 0
        // 0x4000-0x7FFF: Switchable bank (1-N)
        if (offset < 0x4000) {
            bank = 0;  // Force bank 0 for this region
            addr = make_address(0, offset);
            if (visited.count(addr)) continue;
        } else if (bank == 0) {
            bank = 1;  // Default to bank 1 for switchable region
            addr = make_address(1, offset);
            if (visited.count(addr)) continue;
        }
        
        // Skip if outside ROM bounds
        size_t rom_offset;
        if (offset < 0x4000) {
            rom_offset = offset;
        } else {
            rom_offset = static_cast<size_t>(bank) * 0x4000 + (offset - 0x4000);
        }
        if (rom_offset >= rom.size()) continue;
        
        visited.insert(addr);
        
        // Decode instruction
        Instruction instr = decoder.decode(offset, bank);

        // Trace logging
        if (options.trace_log) {
            std::cout << "[TRACE] " << std::hex << std::setfill('0') << std::setw(2) << (int)bank
                      << ":" << std::setw(4) << offset << " " << instr.disassemble() << std::dec << "\n";
        }

        // Check for undefined instructions
        if (instr.type == InstructionType::UNDEFINED) {
             std::cout << "[ERROR] Undefined instruction at " 
                       << std::hex << std::setfill('0') << std::setw(2) << (int)bank << ":" << std::setw(4) << offset 
                       << " Opcode: " << std::setw(2) << (int)instr.opcode << std::dec << "\n";
        }

        // Limit check
        if (options.max_instructions > 0 && result.instructions.size() >= options.max_instructions) {
            std::cerr << "Reached instruction limit (" << options.max_instructions << ")\n";
            break;
        }
        
        // Store instruction
        size_t idx = result.instructions.size();
        result.instructions.push_back(instr);
        result.addr_to_index[addr] = idx;
        
        // Calculate target bank for jumps/calls
        auto target_bank = [&](uint16_t target) -> uint8_t {
            if (target < 0x4000) return 0;  // Bank 0 region
            // For ROM ONLY (no MBC), switchable region is always bank 1
            if (rom.header().mbc_type == MBCType::NONE) return 1;
            return bank;  // Same bank for switchable region (MBC games)
        };
        
        // Track control flow
        // NOTE: RST instructions have is_call=true but need special handling,
        // so we check for RST type FIRST before the general is_call check
        if (instr.type == InstructionType::RST) {
            // RST with 0xFF padding should not be analyzed - it's not real code
            if (is_rst_padding(rom, instr.rst_vector)) {
                // Don't push this RST or fallthrough - target contains only padding
                continue;
            }
            
            result.call_targets.insert(make_address(0, instr.rst_vector));
            work_queue.push(make_address(0, instr.rst_vector));
            
            // RST 28 jump table pattern: the bytes after RST 28 are table data, NOT code
            // Only push fallthrough if this is NOT a RST 28 jump table
            bool is_rst28_jt = (instr.rst_vector == 0x28 && is_rst28_jump_table(rom));
            if (is_rst28_jt) {
                // Extract jump table entries and add them as call targets
                std::vector<uint16_t> table_targets = extract_rst28_table_entries(rom, offset, bank);
                for (uint16_t target : table_targets) {
                    uint8_t tbank = (target < 0x4000) ? 0 : bank;
                    result.call_targets.insert(make_address(tbank, target));
                    work_queue.push(make_address(tbank, target));
                    // Mark these as labels too for proper block generation
                    result.label_addresses.insert(make_address(tbank, target));
                }
                std::cerr << "  RST 28 jump table at 0x" << std::hex << offset << std::dec 
                          << " with " << table_targets.size() << " entries\n";
                
                // DON'T push fallthrough - the bytes after RST 28 are table data
            } else {
                // Normal RST call - push fallthrough and mark as label
                uint32_t fall_through = make_address(bank, offset + instr.length);
                result.label_addresses.insert(fall_through);
                work_queue.push(fall_through);
            }
        } else if (instr.is_call) {
            uint16_t target = instr.imm16;
            uint8_t tbank = target_bank(target);
            result.call_targets.insert(make_address(tbank, target));
            work_queue.push(make_address(tbank, target));
            
            // Track cross-bank calls
            if (tbank != bank) {
                result.stats.cross_bank_calls++;
            }
            
            // Calls fall through - mark as label so a new block starts
            uint32_t fall_through = make_address(bank, offset + instr.length);
            result.label_addresses.insert(fall_through);
            work_queue.push(fall_through);
        } else if (instr.is_jump) {
            if (instr.type == InstructionType::JP_NN || instr.type == InstructionType::JP_CC_NN) {
                uint16_t target = instr.imm16;
                uint8_t tbank = target_bank(target);
                if (target >= 0x4000 && target <= 0x7FFF) {
                    result.call_targets.insert(make_address(tbank, target));
                }
                result.label_addresses.insert(make_address(tbank, target));
                work_queue.push(make_address(tbank, target));
            } else if (instr.type == InstructionType::JR_N || instr.type == InstructionType::JR_CC_N) {
                uint16_t target = offset + instr.length + instr.offset;
                result.label_addresses.insert(make_address(bank, target));
                work_queue.push(make_address(bank, target));
            }
            
            if (instr.is_conditional) {
                // Conditional jumps fall through - this is also a block start
                uint32_t fall_through = make_address(bank, offset + instr.length);
                result.label_addresses.insert(fall_through);
                work_queue.push(fall_through);
            }
        } else if (instr.is_return) {
            // Returns end the block
            // But conditional returns fall through if condition is false
            if (instr.is_conditional) {
                uint32_t fall_through = make_address(bank, offset + instr.length);
                result.label_addresses.insert(fall_through);
                work_queue.push(fall_through);
            }
        } else {
            // Continue to next instruction
            work_queue.push(make_address(bank, offset + instr.length));
        }
    }
    
    // Build basic blocks from instruction boundaries
    std::set<uint32_t> block_starts;
    block_starts.insert(make_address(0, 0x100));  // Entry point
    
    for (uint32_t target : result.call_targets) {
        block_starts.insert(target);
    }
    for (uint32_t target : result.label_addresses) {
        block_starts.insert(target);
    }
    
    // Create blocks
    for (uint32_t start : block_starts) {
        if (!visited.count(start)) continue;
        
        BasicBlock block;
        block.start_address = get_offset(start);
        block.bank = get_bank(start);
        block.is_reachable = true;
        
        if (result.call_targets.count(start)) {
            block.is_function_entry = true;
        }
        
        // Find instructions in this block
        uint32_t curr = start;
        while (visited.count(curr)) {
            auto it = result.addr_to_index.find(curr);
            if (it == result.addr_to_index.end()) break;
            
            block.instruction_indices.push_back(it->second);
            const Instruction& instr = result.instructions[it->second];
            
            block.end_address = get_offset(curr) + instr.length;
            
            // Track successors for control flow
            if (instr.is_jump) {
                if (instr.type == InstructionType::JP_NN || instr.type == InstructionType::JP_CC_NN) {
                    block.successors.push_back(instr.imm16);
                } else if (instr.type == InstructionType::JR_N || instr.type == InstructionType::JR_CC_N) {
                    uint16_t target = get_offset(curr) + instr.length + instr.offset;
                    block.successors.push_back(target);
                }
                // Conditional jumps also fall through
                if (instr.is_conditional) {
                    block.successors.push_back(get_offset(curr) + instr.length);
                }
            } else if (instr.is_return && instr.is_conditional) {
                // Conditional returns fall through if condition is false
                block.successors.push_back(get_offset(curr) + instr.length);
            }
            
            // Check if this ends the block
            if (instr.is_jump || instr.is_return || instr.is_call) {
                // CALLs fall through to next instruction after return
                if (instr.is_call) {
                    block.successors.push_back(get_offset(curr) + instr.length);
                }
                break;
            }
            
            curr = make_address(block.bank, get_offset(curr) + instr.length);
            
            // Check if next instruction starts a new block
            if (block_starts.count(curr)) {
                // Fall through to the new block - add as successor
                block.successors.push_back(get_offset(curr));
                break;
            }
        }
        
        result.blocks[start] = block;
    }
    
    // Create functions from call targets
    for (uint32_t target : result.call_targets) {
        auto block_it = result.blocks.find(target);
        if (block_it == result.blocks.end()) continue;
        
        Function func;
        func.name = generate_function_name(get_bank(target), get_offset(target));
        func.entry_address = get_offset(target);
        func.bank = get_bank(target);
        func.block_addresses.push_back(get_offset(target));
        
        // Add all blocks reachable from this function (simple DFS)
        std::queue<uint32_t> func_queue;
        std::set<uint32_t> func_visited;
        func_queue.push(target);
        
        while (!func_queue.empty()) {
            uint32_t block_addr = func_queue.front();
            func_queue.pop();
            
            if (func_visited.count(block_addr)) continue;
            func_visited.insert(block_addr);
            
            auto blk = result.blocks.find(block_addr);
            if (blk == result.blocks.end()) continue;
            
            // Add this block to function if not already there
            if (block_addr != target) {
                func.block_addresses.push_back(get_offset(block_addr));
            }
            
            // Follow successors (but not into other functions)
            for (uint16_t succ : blk->second.successors) {
                uint32_t succ_addr = make_address(blk->second.bank, succ);
                // Don't follow into other function entry points
                if (result.call_targets.count(succ_addr) && succ_addr != target) continue;
                if (!func_visited.count(succ_addr)) {
                    func_queue.push(succ_addr);
                }
            }
        }
        
        result.functions[target] = func;
    }
    
    // Update stats
    result.stats.total_instructions = result.instructions.size();
    result.stats.total_blocks = result.blocks.size();
    result.stats.total_functions = result.functions.size();
    
    return result;
}

AnalysisResult analyze_bank(const ROM& rom, uint8_t bank, const AnalyzerOptions& options) {
    // For now, just analyze the whole ROM
    // TODO: Filter to specific bank
    return analyze(rom, options);
}

/* ============================================================================
 * Utility Functions
 * ========================================================================== */

std::string generate_function_name(uint8_t bank, uint16_t address) {
    std::ostringstream ss;
    
    // Check for known GameBoy entry points
    if (bank == 0) {
        switch (address) {
            case 0x0000: return "rst_00";
            case 0x0008: return "rst_08";
            case 0x0010: return "rst_10";
            case 0x0018: return "rst_18";
            case 0x0020: return "rst_20";
            case 0x0028: return "rst_28";
            case 0x0030: return "rst_30";
            case 0x0038: return "rst_38";
            case 0x0040: return "int_vblank";
            case 0x0048: return "int_lcd_stat";
            case 0x0050: return "int_timer";
            case 0x0058: return "int_serial";
            case 0x0060: return "int_joypad";
            case 0x0100: return "gb_main";  // Avoid shadowing C main()
        }
    }
    
    ss << "func_";
    if (bank > 0) {
        ss << std::hex << std::setfill('0') << std::setw(2) << (int)bank << "_";
    }
    ss << std::hex << std::setfill('0') << std::setw(4) << address;
    return ss.str();
}

std::string generate_label_name(uint8_t bank, uint16_t address) {
    std::ostringstream ss;
    ss << "loc_";
    if (bank > 0) {
        ss << std::hex << std::setfill('0') << std::setw(2) << (int)bank << "_";
    }
    ss << std::hex << std::setfill('0') << std::setw(4) << address;
    return ss.str();
}

void print_analysis_summary(const AnalysisResult& result) {
    std::cout << "=== Analysis Summary ===" << std::endl;
    std::cout << "Total instructions: " << result.stats.total_instructions << std::endl;
    std::cout << "Total basic blocks: " << result.stats.total_blocks << std::endl;
    std::cout << "Total functions: " << result.stats.total_functions << std::endl;
    std::cout << "Call targets: " << result.call_targets.size() << std::endl;
    std::cout << "Label addresses: " << result.label_addresses.size() << std::endl;
    
    std::cout << "\nFunctions found:" << std::endl;
    for (const auto& [addr, func] : result.functions) {
        std::cout << "  " << func.name << " @ ";
        if (func.bank > 0) {
            std::cout << std::hex << std::setfill('0') << std::setw(2) << (int)func.bank << ":";
        }
        std::cout << std::hex << std::setfill('0') << std::setw(4) << func.entry_address << std::endl;
    }
}

bool is_likely_data(const AnalysisResult& result, uint8_t bank, uint16_t address) {
    uint32_t full_addr = AnalysisResult::make_addr(bank, address);
    return result.addr_to_index.find(full_addr) == result.addr_to_index.end();
}

} // namespace gbrecomp
