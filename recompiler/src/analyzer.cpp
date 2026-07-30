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
#include <cmath>
#include <set>
#include <map>
#include <fstream>

namespace gbrecomp {

/* ============================================================================
 * Helper Functions
 * ========================================================================== */

static uint32_t make_address(BankId bank, uint16_t addr) {
    return (static_cast<uint32_t>(bank) << 16) | addr;
}

static BankId get_bank(uint32_t addr) {
    return static_cast<BankId>(addr >> 16);
}

static uint16_t get_offset(uint32_t addr) {
    return static_cast<uint16_t>(addr & 0xFFFF);
}

static const char* analysis_memory_space(uint16_t address) {
    if (address < 0x8000) return "physical_rom";
    if (address < 0xA000) return "vram";
    if (address < 0xC000) return "external_ram";
    if (address < 0xD000) return "wram";
    if (address < 0xE000) return "banked_wram";
    if (address < 0xFE00) return "echo_ram";
    if (address < 0xFEA0) return "oam";
    if (address < 0xFF00) return "unusable";
    if (address < 0xFF80 || address == 0xFFFF) return "mmio";
    return "hram";
}

static std::string analysis_site_id(const std::string& kind,
                                    BankId bank,
                                    uint16_t address,
                                    bool has_related_address = false,
                                    BankId related_bank = 0,
                                    uint16_t related_address = 0) {
    std::ostringstream id;
    id << "analysis:v1:" << kind << ':'
       << std::hex << std::setfill('0')
       << std::setw(4) << static_cast<unsigned>(bank) << ':'
       << std::setw(4) << static_cast<unsigned>(address);
    if (has_related_address) {
        id << ':' << std::setw(4) << static_cast<unsigned>(related_bank)
           << ':' << std::setw(4) << static_cast<unsigned>(related_address);
    }
    return id.str();
}

static std::string annotation_address(BankId bank, uint16_t address) {
    std::ostringstream value;
    value << std::hex << std::setfill('0')
          << std::setw(2) << static_cast<unsigned>(bank) << ':'
          << std::setw(4) << static_cast<unsigned>(address);
    return value.str();
}

static void record_analysis_diagnostic(
    AnalysisResult& result,
    AnalysisDiagnostic diagnostic) {
    diagnostic.id = analysis_site_id(
        diagnostic.kind,
        diagnostic.bank,
        diagnostic.address,
        diagnostic.has_related_address,
        diagnostic.related_bank,
        diagnostic.related_address);
    result.analysis_diagnostics.emplace(diagnostic.id, std::move(diagnostic));
}

struct AnnotationDataRange {
    BankId bank;
    uint16_t start;
    uint32_t end;
};

struct AnnotationIndex {
    std::set<uint32_t> function_entries;
    std::vector<AnnotationDataRange> data_ranges;

    bool has_function(BankId bank, uint16_t addr) const {
        return function_entries.count(make_address(bank, addr)) > 0;
    }

    bool contains_data(BankId bank, uint16_t addr) const {
        for (const AnnotationDataRange& range : data_ranges) {
            if (range.bank == bank && addr >= range.start && addr < range.end) {
                return true;
            }
        }
        return false;
    }
};

static void add_annotation_data_range(AnnotationIndex& annotations,
                                      BankId bank,
                                      uint16_t start,
                                      uint16_t size) {
    if (size == 0) {
        return;
    }

    const uint32_t end32 = std::min<uint32_t>(0x10000u, static_cast<uint32_t>(start) + size);
    if (end32 <= start) {
        return;
    }

    annotations.data_ranges.push_back({
        bank,
        start,
        end32,
    });
}

static AnnotationIndex build_annotation_index(const ROM& rom, const AnalyzerOptions& options) {
    AnnotationIndex annotations;

    if (options.add_builtin_rom_annotations && rom.size() >= 0x150) {
        add_annotation_data_range(annotations, 0, 0x0104, 0x30);
        add_annotation_data_range(annotations, 0, 0x0134, 0x1c);
    }

    for (const AnalysisAnnotation& annotation : options.annotations) {
        const BankId bank = get_bank(annotation.addr);
        const uint16_t addr = get_offset(annotation.addr);
        switch (annotation.kind) {
            case AnalysisAnnotationKind::FUNCTION:
                annotations.function_entries.insert(annotation.addr);
                break;
            case AnalysisAnnotationKind::DATA:
                add_annotation_data_range(
                    annotations,
                    bank,
                    addr,
                    static_cast<uint16_t>(std::min<uint32_t>(annotation.size, 0xffffu)));
                break;
            case AnalysisAnnotationKind::LABEL:
            default:
                break;
        }
    }

    return annotations;
}

/* ============================================================================
 * AnalysisResult Implementation
 * ========================================================================== */

const Instruction* AnalysisResult::get_instruction(BankId bank, uint16_t addr) const {
    uint32_t full_addr = make_addr(bank, addr);
    auto it = addr_to_index.find(full_addr);
    if (it != addr_to_index.end() && it->second < instructions.size()) {
        return &instructions[it->second];
    }
    return nullptr;
}

const BasicBlock* AnalysisResult::get_block(BankId bank, uint16_t addr) const {
    uint32_t full_addr = make_addr(bank, addr);
    auto it = blocks.find(full_addr);
    if (it != blocks.end()) {
        return &it->second;
    }
    return nullptr;
}

const Function* AnalysisResult::get_function(BankId bank, uint16_t addr) const {
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
 * @brief Check if RST 00 is a jump table dispatcher
 *
 * Castlevania uses a compact dispatcher at 0x0000:
 *   POP HL
 *   RST 08
 *   JP HL
 *
 * The bytes following an RST 00 call site are 16-bit table entries, just like
 * the more common RST 28 dispatch pattern.
 */
static bool is_rst00_jump_table(const ROM& rom) {
    if (rom.read_banked(0, 0x0000) != 0xE1 ||  // POP HL
        rom.read_banked(0, 0x0001) != 0xCF ||  // RST 08
        rom.read_banked(0, 0x0002) != 0xE9) {  // JP HL
        return false;
    }

    // The helper reached through RST 08 should at least begin by doubling A.
    return rom.read_banked(0, 0x0008) == 0x87; // ADD A,A
}

static bool is_jump_table_rst_vector(const ROM& rom, uint8_t rst_vector) {
    return (rst_vector == 0x00 && is_rst00_jump_table(rom)) ||
           (rst_vector == 0x28 && is_rst28_jump_table(rom));
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
static std::vector<uint16_t> extract_rst_table_entries(const ROM& rom, uint16_t rst_call_addr, BankId bank) {
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
            rom_offset = static_cast<size_t>(bank) * 0x4000 + entry_addr;
        } else {
            rom_offset = static_cast<size_t>(bank) * 0x4000 + (entry_addr - 0x4000);
        }
        
        if (rom_offset + 1 >= rom.size()) {
            break;  // Past end of ROM
        }
        
        // Read 16-bit address (little-endian)
        uint8_t lo = rom.read_banked(bank, entry_addr);
        uint8_t hi = rom.read_banked(bank, entry_addr + 1);
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
 * Internal State Tracking
 * ========================================================================== */

struct MapperAnalysisState {
    int mbc1_low = -1;
    int mbc1_high = -1;
    int mbc1_mode = -1;
    int mbc3_bank = -1;
    int mbc5_low = -1;
    int mbc5_high = -1;
    BankId generic_bank = UNKNOWN_BANK;
};

// Track addresses to explore: (addr, known registers, mapper register state)
struct AnalysisState {
    uint32_t addr;
    int known_a;  // -1 if unknown
    int known_b;
    int known_c;
    int known_d;
    int known_e;
    int known_h;
    int known_l;
    int known_sp;
    MapperAnalysisState mapper;
};

static bool is_mbc1(MBCType type) {
    return type == MBCType::MBC1 ||
           type == MBCType::MBC1_RAM ||
           type == MBCType::MBC1_RAM_BATTERY;
}

static bool is_mbc3(MBCType type) {
    return type >= MBCType::MBC3_TIMER_BATTERY &&
           type <= MBCType::MBC3_RAM_BATTERY;
}

static bool is_mbc5(MBCType type) {
    return type >= MBCType::MBC5 &&
           type <= MBCType::MBC5_RUMBLE_RAM_BATTERY;
}

static MapperAnalysisState mapper_state_for_bank(const ROM& rom, BankId bank) {
    MapperAnalysisState state;
    const MBCType type = rom.header().mbc_type;
    if (bank == UNKNOWN_BANK) {
        return state;
    }

    state.generic_bank = bank;
    if (is_mbc1(type)) {
        state.mbc1_low = bank & 0x1F;
        if (state.mbc1_low == 0) {
            state.mbc1_low = 1;
        }
        state.mbc1_high = (bank >> 5) & 0x03;
        state.mbc1_mode = 0;
    } else if (is_mbc3(type)) {
        state.mbc3_bank = bank & 0x7F;
        if (state.mbc3_bank == 0) {
            state.mbc3_bank = 1;
        }
    } else if (is_mbc5(type)) {
        state.mbc5_low = bank & 0xFF;
        state.mbc5_high = (bank >> 8) & 0x01;
    }
    return state;
}

static BankId physical_bank(const ROM& rom, uint32_t bank) {
    const uint32_t bank_count = rom.bank_count();
    if (bank_count == 0) {
        return UNKNOWN_BANK;
    }
    return static_cast<BankId>(bank % bank_count);
}

static bool is_mbc1_multicart(const ROM& rom) {
    static constexpr uint8_t nintendo_logo[48] = {
        0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B,
        0x03, 0x73, 0x00, 0x83, 0x00, 0x0C, 0x00, 0x0D,
        0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E,
        0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99,
        0xBB, 0xBB, 0x67, 0x63, 0x6E, 0x0E, 0xEC, 0xCC,
        0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E,
    };
    if (rom.bank_count() <= 0x10) {
        return false;
    }
    for (size_t i = 0; i < sizeof(nintendo_logo); ++i) {
        if (rom.read_banked(0x10, static_cast<uint16_t>(0x0104 + i)) !=
            nintendo_logo[i]) {
            return false;
        }
    }
    return true;
}

static BankId mapper_upper_bank(const ROM& rom,
                                const MapperAnalysisState& state) {
    const MBCType type = rom.header().mbc_type;
    if (is_mbc1(type)) {
        if (state.mbc1_low == -1 || state.mbc1_high == -1) {
            return UNKNOWN_BANK;
        }
        const uint32_t raw_low = static_cast<uint32_t>(state.mbc1_low) & 0x1F;
        const uint32_t selected_low = raw_low == 0 ? 1 : raw_low;
        const uint32_t shift = is_mbc1_multicart(rom) ? 4 : 5;
        const uint32_t wired_low = shift == 4 ? (selected_low & 0x0F) : selected_low;
        return physical_bank(
            rom,
            ((static_cast<uint32_t>(state.mbc1_high) & 0x03) << shift) |
                wired_low);
    }
    if (is_mbc3(type)) {
        if (state.mbc3_bank == -1) {
            return UNKNOWN_BANK;
        }
        uint32_t bank = static_cast<uint32_t>(state.mbc3_bank) & 0x7F;
        if (bank == 0) {
            bank = 1;
        }
        return physical_bank(rom, bank);
    }
    if (is_mbc5(type)) {
        if (state.mbc5_low == -1 || state.mbc5_high == -1) {
            return UNKNOWN_BANK;
        }
        return physical_bank(
            rom,
            ((static_cast<uint32_t>(state.mbc5_high) & 0x01) << 8) |
                (static_cast<uint32_t>(state.mbc5_low) & 0xFF));
    }
    if (type == MBCType::NONE) {
        return rom.bank_count() > 1 ? 1 : 0;
    }
    return state.generic_bank;
}

static BankId mapper_lower_bank(const ROM& rom,
                                const MapperAnalysisState& state) {
    if (!is_mbc1(rom.header().mbc_type)) {
        return 0;
    }
    if (state.mbc1_mode == -1 || state.mbc1_high == -1) {
        return UNKNOWN_BANK;
    }
    if (state.mbc1_mode == 0) {
        return 0;
    }
    const uint32_t shift = is_mbc1_multicart(rom) ? 4 : 5;
    return physical_bank(
        rom,
        (static_cast<uint32_t>(state.mbc1_high) & 0x03) << shift);
}

static bool apply_mapper_write(const ROM& rom,
                               MapperAnalysisState& state,
                               uint16_t addr,
                               int value) {
    const MBCType type = rom.header().mbc_type;
    if (is_mbc1(type)) {
        if (addr >= 0x2000 && addr < 0x4000) {
            state.mbc1_low = value == -1 ? -1 : (value & 0x1F);
            return true;
        }
        if (addr >= 0x4000 && addr < 0x6000) {
            state.mbc1_high = value == -1 ? -1 : (value & 0x03);
            return true;
        }
        if (addr >= 0x6000 && addr < 0x8000) {
            state.mbc1_mode = value == -1 ? -1 : (value & 0x01);
        }
        return false;
    }
    if (is_mbc3(type)) {
        if (addr >= 0x2000 && addr < 0x4000) {
            state.mbc3_bank = value == -1 ? -1 : (value & 0x7F);
            return true;
        }
        return false;
    }
    if (is_mbc5(type)) {
        if (addr >= 0x2000 && addr < 0x3000) {
            state.mbc5_low = value == -1 ? -1 : (value & 0xFF);
            return true;
        }
        if (addr >= 0x3000 && addr < 0x4000) {
            state.mbc5_high = value == -1 ? -1 : (value & 0x01);
            return true;
        }
        return false;
    }
    if (addr >= 0x2000 && addr < 0x4000) {
        if (value == -1) {
            state.generic_bank = UNKNOWN_BANK;
        } else {
            uint32_t bank = static_cast<uint32_t>(value) & 0x1F;
            state.generic_bank = physical_bank(rom, bank == 0 ? 1 : bank);
        }
        return true;
    }
    return false;
}

static bool join_analysis_state(AnalysisState& state,
                                const AnalysisState& incoming) {
    bool changed = false;
    auto join_constant = [&](int& current, int next) {
        if (current != next && current != -1) {
            current = -1;
            changed = true;
        }
    };

    join_constant(state.known_a, incoming.known_a);
    join_constant(state.known_b, incoming.known_b);
    join_constant(state.known_c, incoming.known_c);
    join_constant(state.known_d, incoming.known_d);
    join_constant(state.known_e, incoming.known_e);
    join_constant(state.known_h, incoming.known_h);
    join_constant(state.known_l, incoming.known_l);
    join_constant(state.known_sp, incoming.known_sp);
    auto join_mapper_constant = [&](int& current, int next) {
        if (current != next && current != -1) {
            current = -1;
            changed = true;
        }
    };
    join_mapper_constant(state.mapper.mbc1_low, incoming.mapper.mbc1_low);
    join_mapper_constant(state.mapper.mbc1_high, incoming.mapper.mbc1_high);
    join_mapper_constant(state.mapper.mbc1_mode, incoming.mapper.mbc1_mode);
    join_mapper_constant(state.mapper.mbc3_bank, incoming.mapper.mbc3_bank);
    join_mapper_constant(state.mapper.mbc5_low, incoming.mapper.mbc5_low);
    join_mapper_constant(state.mapper.mbc5_high, incoming.mapper.mbc5_high);
    if (state.mapper.generic_bank != incoming.mapper.generic_bank &&
        state.mapper.generic_bank != UNKNOWN_BANK) {
        state.mapper.generic_bank = UNKNOWN_BANK;
        changed = true;
    }
    return changed;
}

enum RegisterWriteMask : uint16_t {
    WRITE_B  = 1u << 0,
    WRITE_C  = 1u << 1,
    WRITE_D  = 1u << 2,
    WRITE_E  = 1u << 3,
    WRITE_H  = 1u << 4,
    WRITE_L  = 1u << 5,
    WRITE_A  = 1u << 6,
    WRITE_SP = 1u << 7,
};

static uint16_t reg8_write_mask(uint8_t encoded_reg) {
    switch (encoded_reg) {
        case 0: return WRITE_B;
        case 1: return WRITE_C;
        case 2: return WRITE_D;
        case 3: return WRITE_E;
        case 4: return WRITE_H;
        case 5: return WRITE_L;
        case 7: return WRITE_A;
        default: return 0; // (HL) writes memory, not a tracked register.
    }
}

static uint16_t instruction_register_writes(const Instruction& instr) {
    if (instr.is_cb_prefixed) {
        const uint8_t group = instr.cb_opcode >> 6;
        return (group == 1) ? 0 : reg8_write_mask(instr.cb_opcode & 7);
    }

    const uint8_t opcode = instr.opcode;
    if (opcode >= 0x40 && opcode <= 0x7F && opcode != 0x76) {
        return reg8_write_mask((opcode >> 3) & 7);
    }
    if ((opcode & 0xC7) == 0x04 ||
        (opcode & 0xC7) == 0x05 ||
        (opcode & 0xC7) == 0x06) {
        return reg8_write_mask((opcode >> 3) & 7);
    }
    if (opcode >= 0x80 && opcode <= 0xBF) {
        return WRITE_A;
    }

    switch (opcode) {
        case 0x01: return WRITE_B | WRITE_C;
        case 0x11: return WRITE_D | WRITE_E;
        case 0x21: return WRITE_H | WRITE_L;
        case 0x31: return WRITE_SP;
        case 0x03: case 0x0B: return WRITE_B | WRITE_C;
        case 0x13: case 0x1B: return WRITE_D | WRITE_E;
        case 0x23: case 0x2B: return WRITE_H | WRITE_L;
        case 0x33: case 0x3B: return WRITE_SP;
        case 0x09: case 0x19: case 0x29: case 0x39:
            return WRITE_H | WRITE_L;
        case 0x0A: case 0x1A: case 0xFA: case 0xF0: case 0xF2:
            return WRITE_A;
        case 0x2A: case 0x3A:
            return WRITE_A | WRITE_H | WRITE_L;
        case 0x22: case 0x32:
            return WRITE_H | WRITE_L;
        case 0x07: case 0x0F: case 0x17: case 0x1F:
        case 0x27: case 0x2F:
            return WRITE_A;
        case 0xC6: case 0xCE: case 0xD6: case 0xDE:
        case 0xE6: case 0xEE: case 0xF6: case 0xFE:
            return WRITE_A;
        case 0xC1: return WRITE_B | WRITE_C | WRITE_SP;
        case 0xD1: return WRITE_D | WRITE_E | WRITE_SP;
        case 0xE1: return WRITE_H | WRITE_L | WRITE_SP;
        case 0xF1: return WRITE_A | WRITE_SP;
        case 0xC5: case 0xD5: case 0xE5: case 0xF5:
        case 0xC4: case 0xCC: case 0xD4: case 0xDC: case 0xCD:
        case 0xC0: case 0xC8: case 0xD0: case 0xD8: case 0xC9: case 0xD9:
        case 0xC7: case 0xCF: case 0xD7: case 0xDF:
        case 0xE7: case 0xEF: case 0xF7: case 0xFF:
            return WRITE_SP;
        case 0xE8: return WRITE_SP;
        case 0xF8: return WRITE_H | WRITE_L;
        case 0xF9: return WRITE_SP;
        default: return 0;
    }
}

static void invalidate_register_writes(uint16_t writes,
                                       int& known_a,
                                       int& known_b,
                                       int& known_c,
                                       int& known_d,
                                       int& known_e,
                                       int& known_h,
                                       int& known_l,
                                       int& known_sp) {
    if (writes & WRITE_A) known_a = -1;
    if (writes & WRITE_B) known_b = -1;
    if (writes & WRITE_C) known_c = -1;
    if (writes & WRITE_D) known_d = -1;
    if (writes & WRITE_E) known_e = -1;
    if (writes & WRITE_H) known_h = -1;
    if (writes & WRITE_L) known_l = -1;
    if (writes & WRITE_SP) known_sp = -1;
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
static std::set<BankId> detect_bank_values(const ROM& rom) {
    std::set<BankId> banks;
    banks.insert(0);  // Bank 0 is always present
    banks.insert(1);  // Bank 1 is the default switchable bank
    
    // Use ROM header to know how many banks exist
    uint16_t bank_count = rom.header().rom_banks;
    for (BankId i = 0; i < bank_count; i++) {
        banks.insert(i);
    }
    
    return banks;
}

/**
 * @brief Calculate Shannon entropy of a memory region
 */
static double calculate_entropy(const ROM& rom, BankId bank, uint16_t addr, size_t len) {
    if (addr + len > 0x8000) return 0.0;
    
    uint32_t counts[256] = {0};
    for (size_t i = 0; i < len; i++) {
        counts[rom.read_banked(bank, addr + i)]++;
    }
    
    double entropy = 0;
    for (int i = 0; i < 256; i++) {
        if (counts[i] > 0) {
            double p = (double)counts[i] / len;
            entropy -= p * std::log2(p);
        }
    }
    return entropy;
}

/**
 * @brief Heuristic check if an address looks like valid code start
 * 
 * Checks for:
 * 1. Shannon Entropy (filtering tile data / PCM)
 * 2. Repetitive byte patterns
 * 3. Illegal address access/jumps
 * 4. Instruction density (loads vs math/control flow)
 */
static int is_likely_valid_code(const ROM& rom, BankId bank, uint16_t addr) {
    // 1. Shannon Entropy Check
    // Typical code has moderate entropy (3.0-6.0). 
    // Data like tilemaps is very low (< 2.0). PCM is very high (> 7.5).
    double entropy = calculate_entropy(rom, bank, addr, 48);
    if (entropy < 1.8 || entropy > 7.6) return 0;

    // 2. Check for repetitive patterns (e.g. tile data)
    const int PATTERN_CHECK_LEN = 128;
    if (addr + PATTERN_CHECK_LEN < 0x8000) {
        for (int period = 1; period <= 8; period++) {
            const int REQUIRED_REPEATS = 16;
            const int REQUIRED_LEN = period * REQUIRED_REPEATS;
            
            bool matches = true;
            for (int i = 0; i < REQUIRED_LEN; i++) {
                if (rom.read_banked(bank, addr + i) != rom.read_banked(bank, addr + i + period)) {
                    matches = false;
                    break;
                }
            }
            if (matches) return 0;
        }
    }

    // 3. Decode instructions
    Decoder decoder(rom);
    uint16_t curr = addr;
    int instructions_checked = 0;
    const int MAX_CHECK = 64;
    int nop_count = 0;
    int ld_count = 0;
    int control_flow_count = 0;
    int math_count = 0;

    while (instructions_checked < MAX_CHECK) {
        Instruction instr = decoder.decode(curr, bank);
        
        if (instr.type == InstructionType::UNDEFINED || instr.type == InstructionType::INVALID) return 0;
        
        if (instr.type == InstructionType::NOP) {
            nop_count++;
            if (nop_count > 4) return 0; // Too many NOPs
        }
        
        // 4. Illegal address check
        if (instr.type == InstructionType::LD_A_NN || instr.type == InstructionType::LD_NN_A ||
            instr.type == InstructionType::LD_NN_SP || instr.type == InstructionType::LD_RR_NN ||
            instr.is_call || instr.is_jump) {
            
            uint16_t imm = (instr.type == InstructionType::JR_N || instr.type == InstructionType::JR_CC_N) ? 0 : instr.imm16;
            if (imm != 0) {
                // Prohibited memory areas
                if (imm >= 0xFEA0 && imm <= 0xFEFF) return 0;
                // Echo RAM (usually not used by real code)
                if (imm >= 0xE000 && imm <= 0xFDFF) return 0;
            }
        }

        if (instr.reads_memory || instr.writes_memory) ld_count++;
        if (instr.is_call || instr.is_jump || instr.is_return) control_flow_count++;
        
        // Math/Logic
        if (instr.opcode >= 0x80 && instr.opcode <= 0xBF) math_count++;

        // Reject rare/data-like opcodes if too frequent at start
        if (instr.opcode == 0x27 || instr.opcode == 0x2F || instr.opcode == 0x37 || instr.opcode == 0x3F) {
            if (instructions_checked < 4) return 0;
        }
        
        // RST instructions in data are suspicious (0x00 or 0xFF)
        if (instr.type == InstructionType::RST) {
            if (is_jump_table_rst_vector(rom, instr.rst_vector)) {
                if (instructions_checked >= 1) {
                    return (curr + instr.length - addr);
                }
                return 0;
            }

            if (instr.opcode == 0xC7 || instr.opcode == 0xFF) {
                if (instructions_checked < 2) return 0;
            }
        }

        // Terminator Check
        if (instr.is_return && !instr.is_conditional) {
            if (instructions_checked < 2) return 0; 
            // Avoid load-only functions discovered via scanning
            if (ld_count >= instructions_checked && instructions_checked > 2) return 0;
            return (curr + instr.length - addr);
        }
        
        if (instr.is_jump && !instr.is_conditional) {
             if (instructions_checked >= 3) return (curr + instr.length - addr);
             return 0;
        }
        
        curr += instr.length;
        if (curr >= 0x8000) return 0;
        instructions_checked++;
        
        // High density of loads (indicative of data or large tables)
        if (instructions_checked >= 15 && ld_count == instructions_checked) return 0;
    }

    return 0;
}

static bool is_uniform_padding(const ROM& rom, BankId bank, uint16_t addr, uint8_t value, size_t len) {
    if (addr + len > 0x8000) return false;
    for (size_t i = 0; i < len; i++) {
        if (rom.read_banked(bank, addr + i) != value) return false;
    }
    return true;
}

static bool has_plausible_branch_prefix(const ROM& rom, BankId bank, uint16_t addr) {
    Decoder decoder(rom);
    uint16_t curr = addr;
    int instructions_checked = 0;
    int nop_count = 0;
    bool saw_memory_or_control = false;

    while (instructions_checked < 8 && curr < 0x8000) {
        Instruction instr = decoder.decode(curr, bank);
        if (instr.type == InstructionType::UNDEFINED || instr.type == InstructionType::INVALID) {
            return false;
        }

        if (instr.type == InstructionType::NOP && ++nop_count > 4) {
            return false;
        }

        if (instr.type == InstructionType::LD_A_NN || instr.type == InstructionType::LD_NN_A ||
            instr.type == InstructionType::LD_NN_SP || instr.type == InstructionType::LD_RR_NN ||
            instr.is_call || instr.is_jump) {
            uint16_t imm = (instr.type == InstructionType::JR_N || instr.type == InstructionType::JR_CC_N)
                               ? 0
                               : instr.imm16;
            if (imm != 0) {
                if (imm >= 0xFEA0 && imm <= 0xFEFF) return false;
                if (imm >= 0xE000 && imm <= 0xFDFF) return false;
            }
        }

        if (instr.reads_memory || instr.writes_memory || instr.is_call || instr.is_jump || instr.is_return) {
            saw_memory_or_control = true;
        }

        instructions_checked++;

        if (instr.is_return || instr.type == InstructionType::JP_HL) {
            return saw_memory_or_control;
        }

        if (instr.type == InstructionType::RST && is_jump_table_rst_vector(rom, instr.rst_vector)) {
            return saw_memory_or_control;
        }

        if (instr.is_jump && !instr.is_conditional) {
            return saw_memory_or_control;
        }

        curr += instr.length;
    }

    return instructions_checked >= 5 && saw_memory_or_control;
}

static bool is_likely_direct_branch_target(const ROM& rom, BankId bank, uint16_t addr) {
    if (addr >= 0x8000) return false;

    if (is_uniform_padding(rom, bank, addr, 0x00, 8) ||
        is_uniform_padding(rom, bank, addr, 0xFF, 8)) {
        return false;
    }

    Decoder decoder(rom);
    Instruction instr = decoder.decode(addr, bank);
    if (instr.type == InstructionType::UNDEFINED || instr.type == InstructionType::INVALID) {
        return false;
    }

    // Direct branch targets are often tiny stubs, especially bank-end trampolines.
    if (instr.is_jump || instr.is_call || instr.is_return || instr.type == InstructionType::JP_HL) {
        return true;
    }

    if (has_plausible_branch_prefix(rom, bank, addr)) {
        return true;
    }

    return is_likely_valid_code(rom, bank, addr) != 0;
}

static bool is_bank_entry_stub(const ROM& rom, BankId bank, uint16_t addr) {
    Decoder decoder(rom);
    Instruction instr = decoder.decode(addr, bank);
    if (instr.type == InstructionType::UNDEFINED || instr.type == InstructionType::INVALID) {
        return false;
    }

    if (!(instr.is_jump || instr.is_call || instr.is_return || instr.type == InstructionType::JP_HL)) {
        return false;
    }

    return is_likely_direct_branch_target(rom, bank, addr);
}

/**
 * @brief Scan for 16-bit pointers that likely lead to code
 */
static void find_pointer_entry_points(const ROM& rom,
                                      AnalysisResult& result,
                                      std::queue<AnalysisState>& work_queue,
                                      const AnnotationIndex& annotations) {
    auto seed_pointer_target = [&](BankId target_bank, uint16_t target) {
        if (annotations.contains_data(target_bank, target)) {
            return;
        }
        if (!annotations.has_function(target_bank, target) &&
            !is_likely_direct_branch_target(rom, target_bank, target)) {
            return;
        }

        uint32_t full_addr = make_address(target_bank, target);
        if (result.call_targets.find(full_addr) != result.call_targets.end()) {
            return;
        }

        result.call_targets.insert(full_addr);
        result.strong_call_targets.insert(full_addr);
        work_queue.push({full_addr, -1, -1, -1, -1, -1, -1, -1, -1,
                         mapper_state_for_bank(
                             rom,
                             target_bank > 0 ? target_bank : static_cast<BankId>(1))});
    };

    for (BankId bank = 0; bank < rom.bank_count(); ++bank) {
        uint16_t scan_start = (bank == 0) ? 0x0150 : 0x4000;
        uint16_t scan_end = 0x3FFE;
        if (bank > 0) {
            scan_end = 0x7FFE;
        }

        for (uint16_t addr = scan_start; addr < scan_end; addr++) {
            uint8_t lo = rom.read_banked(bank, addr);
            uint8_t hi = rom.read_banked(bank, addr + 1);
            uint16_t target = lo | (hi << 8);

            // Target must be in ROM
            if (target < 0x0150 || target >= 0x8000) {
                continue;
            }

            if (target < 0x4000) {
                seed_pointer_target(0, target);
                continue;
            }

            if (bank == 0) {
                // Bank 0 tables commonly point into switchable banks. Probe every
                // switchable bank and keep only targets that look like real code.
                for (BankId target_bank = 1; target_bank < rom.bank_count(); ++target_bank) {
                    seed_pointer_target(target_bank, target);
                }
            } else {
                seed_pointer_target(bank, target);
            }
        }
    }
}

/* ============================================================================
 * Analysis Implementation
 * ========================================================================== */

/**
 * @brief Load entry points from a runtime trace file
 */
static void load_trace_entry_points(const std::string& path,
                                    std::set<uint32_t>& call_targets,
                                    std::set<uint32_t>& strong_call_targets) {
    if (path.empty()) return;

    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Warning: Could not open trace file: " << path << "\n";
        return;
    }

    std::string line;
    int count = 0;
    while (std::getline(file, line)) {
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            try {
                int bank = std::stoi(line.substr(0, colon));
                int addr = std::stoi(line.substr(colon + 1), nullptr, 16);
                uint32_t target = make_address(bank, addr);
                call_targets.insert(target);
                strong_call_targets.insert(target);
                count++;
            } catch (...) {
                continue;
            }
        }
    }
    std::cout << "Loaded " << count << " entry points from trace file: " << path << "\n";
}

AnalysisResult analyze(const ROM& rom, const AnalyzerOptions& options) {
    AnalysisResult result;
    result.rom = &rom;
    result.entry_point = 0x100;
    const AnnotationIndex annotations = build_annotation_index(rom, options);
    
    // Add standard GameBoy entry points
    result.interrupt_vectors = {0x40, 0x48, 0x50, 0x58, 0x60};  // Interrupt vectors
    
    Decoder decoder(rom);
    
    // Detect which banks are used
    std::set<BankId> known_banks = detect_bank_values(rom);
    
    std::queue<AnalysisState> work_queue;
    std::set<uint32_t> visited;
    // Pointer scanning pass
    find_pointer_entry_points(rom, result, work_queue, annotations);
    
    // Entry point is always a function (bank 0)
    result.call_targets.insert(make_address(0, 0x100));
    result.strong_call_targets.insert(make_address(0, 0x100));
    
    // RST vectors
    bool skip_rst30 = rst28_uses_rst30(rom);
    for (uint16_t vec : {0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38}) {
        if (is_rst_padding(rom, vec)) continue;
        if (vec == 0x30 && skip_rst30) continue;
        result.call_targets.insert(make_address(0, vec));
        result.strong_call_targets.insert(make_address(0, vec));
    }
    
    // Interrupt vectors
    for (uint16_t vec : result.interrupt_vectors) {
        result.call_targets.insert(make_address(0, vec));
        result.strong_call_targets.insert(make_address(0, vec));
    }

    // Load from trace if provided
    load_trace_entry_points(options.trace_file_path, result.call_targets, result.strong_call_targets);

    for (const AnalysisAnnotation& annotation : options.annotations) {
        if (annotation.kind != AnalysisAnnotationKind::FUNCTION) {
            continue;
        }
        result.call_targets.insert(annotation.addr);
        result.strong_call_targets.insert(annotation.addr);
    }

    // Initial work queue seeding
    for (uint32_t target : result.call_targets) {
        BankId bank = get_bank(target);
        work_queue.push({target, -1, -1, -1, -1, -1, -1, -1, -1,
                         mapper_state_for_bank(
                             rom,
                             bank > 0 ? bank : static_cast<BankId>(1))});
    }
    
    // Manual entry points
    for (uint32_t target : options.entry_points) {
        if (result.call_targets.find(target) == result.call_targets.end()) {
            result.call_targets.insert(target);
            result.strong_call_targets.insert(target);
            BankId bank = get_bank(target);
            work_queue.push({target, -1, -1, -1, -1, -1, -1, -1, -1,
                             mapper_state_for_bank(
                                 rom,
                                 bank > 0 ? bank : static_cast<BankId>(1))});
        }
    }
    
    // For MBC games
    if (rom.header().mbc_type != MBCType::NONE && options.analyze_all_banks) {
        std::cerr << "Analyzing all " << known_banks.size() << " banks\n";
        for (BankId bank : known_banks) {
            if (bank == 0) continue;

            auto seed_bank_target = [&](uint16_t addr) {
                uint32_t target = make_address(bank, addr);
                if (result.call_targets.insert(target).second) {
                    result.strong_call_targets.insert(target);
                    work_queue.push({target, -1, -1, -1, -1, -1, -1, -1, -1,
                                     mapper_state_for_bank(rom, bank)});
                } else {
                    result.strong_call_targets.insert(target);
                }
            };

            bool seeded_bank = false;
            // Do not assume every switchable bank starts with code. Many games
            // place tables or compressed assets right at 0x4000, and merely
            // being decodable as opcodes is far too permissive.
            if (annotations.has_function(bank, 0x4000) ||
                (!annotations.contains_data(bank, 0x4000) &&
                 (is_likely_direct_branch_target(rom, bank, 0x4000) ||
                  is_likely_valid_code(rom, bank, 0x4000) > 0))) {
                seed_bank_target(0x4000);
                seeded_bank = true;
            }

            for (uint16_t addr = 0x4000; addr < 0x4008; addr++) {
                if (annotations.contains_data(bank, addr) || !is_bank_entry_stub(rom, bank, addr)) continue;

                seed_bank_target(addr);
                seeded_bank = true;
            }

            if (!seeded_bank &&
                !annotations.contains_data(bank, 0x4001) &&
                is_likely_valid_code(rom, bank, 0x4001)) {
                seed_bank_target(0x4001);
            }
        }
    }
    
    // Add overlay entry points
    for (const auto& ov : options.ram_overlays) {
        uint32_t addr = make_address(0, ov.ram_addr);
        AnalysisDiagnostic diagnostic;
        diagnostic.kind = "ram_overlay";
        diagnostic.bank = 0;
        diagnostic.address = ov.ram_addr;
        diagnostic.memory_space = analysis_memory_space(ov.ram_addr);
        diagnostic.status = "configured";
        diagnostic.evidence =
            "configured copied-RAM overlay of " + std::to_string(ov.size) +
            " byte(s)";
        diagnostic.suggested_annotation =
            "data " +
            annotation_address(get_bank(ov.rom_addr), get_offset(ov.rom_addr)) +
            " " + std::to_string(ov.size);
        diagnostic.relationship = "copied_from_physical_rom";
        diagnostic.has_related_address = true;
        diagnostic.related_bank = get_bank(ov.rom_addr);
        diagnostic.related_address = get_offset(ov.rom_addr);
        diagnostic.related_memory_space = "physical_rom";
        record_analysis_diagnostic(result, std::move(diagnostic));
        result.call_targets.insert(addr);
        result.strong_call_targets.insert(addr);
        work_queue.push({addr, -1, -1, -1, -1, -1, -1, -1, -1,
                         mapper_state_for_bank(rom, 1)});
    }

    // Add manual entry points
    for (uint32_t addr : options.entry_points) {
        AnalysisDiagnostic diagnostic;
        diagnostic.kind = "manual_entry_point";
        diagnostic.bank = get_bank(addr);
        diagnostic.address = get_offset(addr);
        diagnostic.memory_space = analysis_memory_space(get_offset(addr));
        diagnostic.status = "configured";
        diagnostic.evidence =
            "explicit analyzer entry point supplied by the generator";
        diagnostic.suggested_annotation =
            "function " +
            annotation_address(get_bank(addr), get_offset(addr));
        diagnostic.relationship = "resolved_fallback_entry_point";
        record_analysis_diagnostic(result, std::move(diagnostic));
        result.call_targets.insert(addr);
        result.strong_call_targets.insert(addr);
        BankId bank = get_bank(addr);
        BankId context = (bank > 0) ? bank : 1;
        work_queue.push({addr, -1, -1, -1, -1, -1, -1, -1, -1,
                         mapper_state_for_bank(rom, context)});
    }
    
    // Multi-pass analysis
    bool scanning_pass = false;
    // Aggressive-scan coverage is meaningful only within this ROM analysis.
    // Keeping it process-global makes later ROMs inherit addresses from the
    // first ROM and races when multi-ROM workers analyze in parallel.
    std::set<uint32_t> aggressive_regions;
    // Abstract input state for each normalized instruction address. A later
    // predecessor can only make a constant less precise, so reprocessing
    // reaches a finite fixed point instead of depending on queue order.
    std::map<uint32_t, AnalysisState> input_states;

    // Explore all reachable code
    while (true) {
        // Drain work queue
        while (!work_queue.empty()) {
            auto item = work_queue.front();
        work_queue.pop();
        
        uint32_t addr = item.addr;
        int known_a = item.known_a;
        int known_b = item.known_b;
        int known_c = item.known_c;
        int known_d = item.known_d;
        int known_e = item.known_e;
        int known_h = item.known_h;
        int known_l = item.known_l;
        int known_sp = item.known_sp;
        MapperAnalysisState mapper_state = item.mapper;
        
        BankId bank = get_bank(addr);
        uint16_t offset = get_offset(addr);
        
        // Check if inside any RAM overlay
        const AnalyzerOptions::RamOverlay* overlay = nullptr;
        for (const auto& ov : options.ram_overlays) {
            if (offset >= ov.ram_addr && offset < ov.ram_addr + ov.size) {
                overlay = &ov;
                break;
            }
        }
        
        // Only analyze ROM space or RAM overlays
        if (offset >= 0x8000 && !overlay) continue;
        
        // Bank mapping rules
        if (offset < 0x4000 && bank == 0) {
            bank = 0;  // Force bank 0 for this region
            addr = make_address(0, offset);
        } else if (offset < 0x8000 && bank == 0) {
            bank = 1;  // Default to bank 1
            addr = make_address(1, offset);
        }

        if (!overlay &&
            annotations.contains_data(bank, offset) &&
            !annotations.has_function(bank, offset)) {
            continue;
        }
        
        AnalysisState incoming_state{
            addr,
            known_a,
            known_b,
            known_c,
            known_d,
            known_e,
            known_h,
            known_l,
            known_sp,
            mapper_state,
        };
        auto [state_it, inserted] = input_states.emplace(addr, incoming_state);
        if (!inserted && !join_analysis_state(state_it->second, incoming_state)) {
            continue;
        }
        const AnalysisState& joined_state = state_it->second;
        known_a = joined_state.known_a;
        known_b = joined_state.known_b;
        known_c = joined_state.known_c;
        known_d = joined_state.known_d;
        known_e = joined_state.known_e;
        known_h = joined_state.known_h;
        known_l = joined_state.known_l;
        known_sp = joined_state.known_sp;
        mapper_state = joined_state.mapper;
        
        // Calculate ROM offset
        size_t rom_offset;
        if (overlay) {
            BankId src_bank = get_bank(overlay->rom_addr);
            uint16_t src_addr = get_offset(overlay->rom_addr);
            if (src_addr < 0x4000) rom_offset = src_addr;
            else rom_offset = static_cast<size_t>(src_bank) * 0x4000 + (src_addr - 0x4000);
            rom_offset += (offset - overlay->ram_addr);
        } else if (offset < 0x4000) {
            rom_offset = static_cast<size_t>(bank) * 0x4000 + offset;
        } else {
            rom_offset = static_cast<size_t>(bank) * 0x4000 + (offset - 0x4000);
        }
        if (rom_offset >= rom.size()) continue;
        
        visited.insert(addr);
        
        // Decode instruction
        Instruction instr;
        if (overlay) {
             BankId src_bank = get_bank(overlay->rom_addr);
             uint16_t src_addr = get_offset(overlay->rom_addr) + (offset - overlay->ram_addr);
             instr = decoder.decode(src_addr, src_bank);
             instr.address = offset; 
             instr.bank = 0; // RAM is bank 0
        } else {
            instr = decoder.decode(offset, bank);
        }

        /* -------------------------------------------------------------
         * Constant Propagation (A and HL)
         * ------------------------------------------------------------- */
         
        const int before_a = known_a;
        const int before_b = known_b;
        const int before_c = known_c;
        const int before_d = known_d;
        const int before_e = known_e;
        const int before_h = known_h;
        const int before_l = known_l;
        const int before_sp = known_sp;
        invalidate_register_writes(instruction_register_writes(instr),
                                   known_a, known_b, known_c, known_d,
                                   known_e, known_h, known_l, known_sp);

        // All transfer functions read the pre-instruction state and write the
        // post-instruction state. Any decoded register write not explicitly
        // modeled above remains conservatively unknown.
        auto get_before_hl = [&]() -> int {
            if (before_h != -1 && before_l != -1) return (before_h << 8) | before_l;
            return -1;
        };
        auto get_known_hl = [&]() -> int {
            if (known_h != -1 && known_l != -1) return (known_h << 8) | known_l;
            return -1;
        };
        auto set_known_hl = [&](int value) {
            known_h = (value >> 8) & 0xFF;
            known_l = value & 0xFF;
        };

        // Helper to get combined registers
        auto get_before_bc = [&]() -> int { if (before_b != -1 && before_c != -1) return (before_b << 8) | before_c; return -1; };
        auto get_before_de = [&]() -> int { if (before_d != -1 && before_e != -1) return (before_d << 8) | before_e; return -1; };
        auto get_known_bc = [&]() -> int { if (known_b != -1 && known_c != -1) return (known_b << 8) | known_c; return -1; };
        auto get_known_de = [&]() -> int { if (known_d != -1 && known_e != -1) return (known_d << 8) | known_e; return -1; };

        // 8-bit Loads
        if (instr.opcode == 0x06) known_b = instr.imm8;
        else if (instr.opcode == 0x0E) known_c = instr.imm8;
        else if (instr.opcode == 0x16) known_d = instr.imm8;
        else if (instr.opcode == 0x1E) known_e = instr.imm8;
        else if (instr.opcode == 0x26) known_h = instr.imm8;
        else if (instr.opcode == 0x2E) known_l = instr.imm8;
        else if (instr.opcode == 0x3E) known_a = instr.imm8;
        // 16-bit Loads
        else if (instr.opcode == 0x01) { known_b = (instr.imm16 >> 8); known_c = instr.imm16 & 0xFF; }
        else if (instr.opcode == 0x11) { known_d = (instr.imm16 >> 8); known_e = instr.imm16 & 0xFF; }
        else if (instr.opcode == 0x21) { known_h = (instr.imm16 >> 8); known_l = instr.imm16 & 0xFF; }
        else if (instr.opcode == 0x31) { known_sp = instr.imm16; }
        // LD r, r'
        else if (instr.opcode >= 0x40 && instr.opcode <= 0x7F && instr.opcode != 0x76) {
            int* regs[] = {&known_b, &known_c, &known_d, &known_e, &known_h, &known_l, nullptr, &known_a};
            const int before_regs[] = {before_b, before_c, before_d, before_e, before_h, before_l, -1, before_a};
            int dst = (instr.opcode >> 3) & 7;
            int src = instr.opcode & 7;
            if (regs[dst]) {
                if (src == 6) { // LD r, (HL)
                    int mhl = get_before_hl();
                    BankId mapped_bank = (mhl < 0x4000)
                        ? mapper_lower_bank(rom, mapper_state)
                        : mapper_upper_bank(rom, mapper_state);
                    if (mhl != -1 && mhl < 0x8000 && mapped_bank != UNKNOWN_BANK) {
                        *regs[dst] = rom.read_banked(mapped_bank, mhl);
                    }
                    else *regs[dst] = -1;
                } else if (regs[src]) *regs[dst] = before_regs[src];
                else *regs[dst] = -1;
            } else if (dst == 6) { // LD (HL), r
                 // Memory write - conceptually invalidates ROM values if we were tracking them, 
                 // but we only track constant ROM.
            }
        }
        else if (instr.opcode == 0x2A || instr.opcode == 0x3A) {
            int mhl = get_before_hl();
            BankId mapped_bank = (mhl < 0x4000)
                ? mapper_lower_bank(rom, mapper_state)
                : mapper_upper_bank(rom, mapper_state);
            if (mhl != -1 && mhl < 0x8000 && mapped_bank != UNKNOWN_BANK) {
                known_a = rom.read_banked(mapped_bank, (uint16_t)mhl);
                set_known_hl((instr.opcode == 0x2A) ? ((mhl + 1) & 0xFFFF) : ((mhl - 1) & 0xFFFF));
            } else {
                known_a = -1;
                known_h = -1;
                known_l = -1;
            }
        }
        else if (instr.opcode == 0x22 || instr.opcode == 0x32) {
            int mhl = get_before_hl();
            if (mhl != -1) {
                set_known_hl((instr.opcode == 0x22) ? ((mhl + 1) & 0xFFFF) : ((mhl - 1) & 0xFFFF));
            } else {
                known_h = -1;
                known_l = -1;
            }
        }
        else if (instr.opcode == 0xAF) known_a = 0; // XOR A
        else if (instr.opcode == 0x03 || instr.opcode == 0x13 || instr.opcode == 0x23 || instr.opcode == 0x33) {
            int value = -1;
            if (instr.opcode == 0x03) value = get_before_bc();
            else if (instr.opcode == 0x13) value = get_before_de();
            else if (instr.opcode == 0x23) value = get_before_hl();
            else value = before_sp;

            if (value != -1) {
                value = (value + 1) & 0xFFFF;
                if (instr.opcode == 0x03) { known_b = value >> 8; known_c = value & 0xFF; }
                else if (instr.opcode == 0x13) { known_d = value >> 8; known_e = value & 0xFF; }
                else if (instr.opcode == 0x23) set_known_hl(value);
                else known_sp = value;
            } else if (instr.opcode == 0x23) {
                known_h = -1;
                known_l = -1;
            } else if (instr.opcode == 0x33) {
                known_sp = -1;
            }
        }
        else if (instr.opcode == 0x0B || instr.opcode == 0x1B || instr.opcode == 0x2B || instr.opcode == 0x3B) {
            int value = -1;
            if (instr.opcode == 0x0B) value = get_before_bc();
            else if (instr.opcode == 0x1B) value = get_before_de();
            else if (instr.opcode == 0x2B) value = get_before_hl();
            else value = before_sp;

            if (value != -1) {
                value = (value - 1) & 0xFFFF;
                if (instr.opcode == 0x0B) { known_b = value >> 8; known_c = value & 0xFF; }
                else if (instr.opcode == 0x1B) { known_d = value >> 8; known_e = value & 0xFF; }
                else if (instr.opcode == 0x2B) set_known_hl(value);
                else known_sp = value;
            } else if (instr.opcode == 0x2B) {
                known_h = -1;
                known_l = -1;
            } else if (instr.opcode == 0x3B) {
                known_sp = -1;
            }
        }
        // ADD HL, rr
        else if (instr.opcode == 0x09 || instr.opcode == 0x19 || instr.opcode == 0x29 || instr.opcode == 0x39) {
            int val = -1;
            if (instr.opcode == 0x09) val = get_before_bc();
            if (instr.opcode == 0x19) val = get_before_de();
            if (instr.opcode == 0x29) val = get_before_hl();
            if (instr.opcode == 0x39) val = before_sp;
            int mhl = get_before_hl();
            if (mhl != -1 && val != -1) {
                int res = (mhl + val) & 0xFFFF;
                set_known_hl(res);
            } else { known_h = -1; known_l = -1; }
        }
        else if (instr.opcode == 0xE8) {
            if (before_sp != -1) known_sp = (before_sp + instr.offset) & 0xFFFF;
        }
        else if (instr.opcode == 0xF8) {
            if (before_sp != -1) set_known_hl((before_sp + instr.offset) & 0xFFFF);
            else { known_h = -1; known_l = -1; }
        }
        else if (instr.opcode == 0xF9) {
            known_sp = get_before_hl();
        }
        // Invalidate A on ALU
        else if ((instr.opcode >= 0x80 && instr.opcode <= 0xBF) || (instr.opcode & 0xC7) == 0x06 || instr.opcode == 0x3C || instr.opcode == 0x3D) {
            known_a = -1;
        }
        // POPs
        else if (instr.opcode == 0xC1) { known_b = -1; known_c = -1; known_sp = -1; }
        else if (instr.opcode == 0xD1) { known_d = -1; known_e = -1; known_sp = -1; }
        else if (instr.opcode == 0xE1) { known_h = -1; known_l = -1; known_sp = -1; }
        else if (instr.opcode == 0xF1) { known_a = -1; known_sp = -1; }
        else if (instr.opcode == 0xC5 || instr.opcode == 0xD5 || instr.opcode == 0xE5 || instr.opcode == 0xF5 ||
                 instr.opcode == 0xC9 || instr.opcode == 0xD9 || instr.opcode == 0xCD || instr.opcode == 0xC4 ||
                 instr.opcode == 0xCC || instr.opcode == 0xD4 || instr.opcode == 0xDC ||
                 (instr.opcode & 0xC7) == 0xC7) {
            known_sp = -1;
        }

        /* -------------------------------------------------------------
         * Bank Switching Detection
         * ------------------------------------------------------------- */
        int mapper_addr = -1;
        int mapper_value = -1;
        if (instr.opcode == 0xEA) { // LD (nn), A
            mapper_addr = instr.imm16;
            mapper_value = before_a;
        } else if (instr.opcode == 0x02) { // LD (BC), A
            mapper_addr = get_before_bc();
            mapper_value = before_a;
        } else if (instr.opcode == 0x12) { // LD (DE), A
            mapper_addr = get_before_de();
            mapper_value = before_a;
        } else if (instr.opcode == 0x22 || instr.opcode == 0x32) { // LDI/LDD (HL), A
            mapper_addr = get_before_hl();
            mapper_value = before_a;
        } else if (instr.opcode >= 0x70 && instr.opcode <= 0x75) {
            const int before_regs[] = {
                before_b, before_c, before_d, before_e, before_h, before_l,
            };
            mapper_addr = get_before_hl();
            mapper_value = before_regs[instr.opcode & 7];
        } else if (instr.opcode == 0x77) { // LD (HL), A
            mapper_addr = get_before_hl();
            mapper_value = before_a;
        }

        if (mapper_addr >= 0 && mapper_addr < 0x8000 &&
            apply_mapper_write(rom,
                               mapper_state,
                               static_cast<uint16_t>(mapper_addr),
                               mapper_value)) {
            const BankId target_b = mapper_upper_bank(rom, mapper_state);
            result.bank_tracker.record_bank_switch(
                addr,
                target_b,
                target_b == UNKNOWN_BANK);
        }

        // Trace logging
        if (options.trace_log) {
            std::cout << "[TRACE] " << std::hex << std::setfill('0') << std::setw(2) << (int)bank
                      << ":" << std::setw(4) << offset << " " << instr.disassemble() << std::dec << "\n";
        }
        
        // Check padding
        if (bank > 0 && instr.opcode == 0xFF) {
            bool is_padding = true;
            for (int i = 1; i < 16; i++) {
                if (rom.read_banked(bank, offset + i) != 0xFF) { is_padding = false; break; }
            }
            if (is_padding) continue;
        }

        if (instr.type == InstructionType::UNDEFINED) {
             AnalysisDiagnostic diagnostic;
             diagnostic.kind = "undefined_instruction";
             diagnostic.bank = bank;
             diagnostic.address = offset;
             diagnostic.memory_space = "physical_rom";
             diagnostic.status = "unresolved";
             std::ostringstream evidence;
             evidence << "decoder rejected opcode 0x"
                      << std::hex << std::setfill('0') << std::setw(2)
                      << static_cast<unsigned>(instr.opcode);
             diagnostic.evidence = evidence.str();
             diagnostic.suggested_annotation =
                 "data " + annotation_address(bank, offset) + " 1";
             diagnostic.relationship = "potential_false_code";
             record_analysis_diagnostic(result, std::move(diagnostic));
             std::cout << "[ERROR] Undefined instruction at " << std::hex << (int)bank << ":" << offset << "\n";
             continue;
        }

        auto instruction_it = result.addr_to_index.find(addr);
        const bool is_new_instruction = instruction_it == result.addr_to_index.end();
        if (is_new_instruction &&
            options.max_instructions > 0 &&
            result.instructions.size() >= options.max_instructions) {
            break;
        }

        size_t idx = 0;
        if (is_new_instruction) {
            idx = result.instructions.size();
            result.instructions.push_back(instr);
            result.addr_to_index[addr] = idx;
        } else {
            idx = instruction_it->second;
            result.instructions[idx] = instr;
        }
        
        auto target_bank = [&](uint16_t target) -> BankId {
            if (target < 0x4000) return mapper_lower_bank(rom, mapper_state);
            return mapper_upper_bank(rom, mapper_state);
        };
        
        if (instr.type == InstructionType::RST) {
            if (is_rst_padding(rom, instr.rst_vector)) continue;
            
            const BankId rst_bank = mapper_lower_bank(rom, mapper_state);
            if (rst_bank == UNKNOWN_BANK) {
                result.instructions[idx] = instr;
                continue;
            }
            result.call_targets.insert(make_address(rst_bank, instr.rst_vector));
            result.strong_call_targets.insert(make_address(rst_bank, instr.rst_vector));
            work_queue.push({make_address(rst_bank, instr.rst_vector), -1, -1, -1, -1, -1, -1, -1, -1, mapper_state});
            
            bool is_jump_table_rst = is_jump_table_rst_vector(rom, instr.rst_vector);
            if (is_jump_table_rst) {
                std::vector<uint16_t> table_targets = extract_rst_table_entries(rom, offset, bank);
                for (uint16_t target : table_targets) {
                    BankId tbank = target_bank(target);
                    if (tbank == UNKNOWN_BANK) {
                        continue;
                    }

                    if (!annotations.contains_data(tbank, target) &&
                        (annotations.has_function(tbank, target) ||
                         is_likely_direct_branch_target(rom, tbank, target))) {
                        uint32_t full_target = make_address(tbank, target);
                        result.call_targets.insert(full_target);
                        result.branch_entry_targets.insert(full_target);
                        work_queue.push({full_target, -1, -1, -1, -1, -1, -1, -1, -1,
                                         mapper_state_for_bank(rom, tbank)});
                        result.label_addresses.insert(full_target);
                    }
                }
            } else {
                uint32_t fall_through = make_address(bank, offset + instr.length);
                result.label_addresses.insert(fall_through);
                work_queue.push({fall_through, known_a, known_b, known_c, known_d, known_e, known_h, known_l, known_sp, mapper_state});
            }
        } else if (instr.is_call) {
            uint16_t target = instr.imm16;
            BankId tbank = target_bank(target);
            instr.resolved_target_bank = tbank;

            bool target_valid = tbank != UNKNOWN_BANK;
            if (target_valid && annotations.contains_data(tbank, target)) {
                target_valid = false;
            } else if (target_valid && annotations.has_function(tbank, target)) {
                target_valid = true;
            } else if (target_valid && tbank > 0 && tbank != bank) {
                target_valid = is_likely_direct_branch_target(rom, tbank, target);
            }

            if (!target_valid) {
                AnalysisDiagnostic diagnostic;
                diagnostic.kind = "unresolved_direct_target";
                diagnostic.bank = bank;
                diagnostic.address = offset;
                diagnostic.memory_space = "physical_rom";
                diagnostic.status = "unresolved";
                diagnostic.evidence =
                    tbank == UNKNOWN_BANK
                        ? "CALL target bank is unknown under the joined mapper state"
                        : "CALL target was rejected by data boundaries or code heuristics";
                diagnostic.suggested_annotation =
                    tbank == UNKNOWN_BANK
                        ? "add-entry-point <bank>:" +
                              annotation_address(0, target).substr(3)
                        : "function " + annotation_address(tbank, target);
                diagnostic.relationship = "potential_dispatch_fallback";
                if (tbank != UNKNOWN_BANK) {
                    diagnostic.has_related_address = true;
                    diagnostic.related_bank = tbank;
                    diagnostic.related_address = target;
                    diagnostic.related_memory_space =
                        analysis_memory_space(target);
                }
                record_analysis_diagnostic(result, std::move(diagnostic));
            }

            if (target_valid) {
                result.call_targets.insert(make_address(tbank, target));
                result.strong_call_targets.insert(make_address(tbank, target));
                work_queue.push({make_address(tbank, target), -1, -1, -1, -1, -1, -1, -1, -1, mapper_state});

                if (tbank != bank) {
                    result.stats.cross_bank_calls++;
                    result.bank_tracker.record_cross_bank_call(offset, target, bank, tbank);
                }
            }

            uint32_t fall_through = make_address(bank, offset + instr.length);
            result.label_addresses.insert(fall_through);
            // SM83 has no callee-saved register convention. Until a callee
            // summary proves otherwise, every register constant is unknown on
            // return; preserving them can invent mapper writes and JP HL
            // targets that never occur at runtime.
            work_queue.push({fall_through, -1, -1, -1, -1, -1, -1, -1, -1, mapper_state});
        } else if (instr.is_jump) {
            if (instr.type == InstructionType::JP_NN || instr.type == InstructionType::JP_CC_NN) {
                uint16_t target = instr.imm16;
                BankId tbank = target_bank(target);
                instr.resolved_target_bank = tbank;

                bool target_valid = tbank != UNKNOWN_BANK;
                if (target_valid && annotations.contains_data(tbank, target)) {
                    target_valid = false;
                } else if (target_valid && annotations.has_function(tbank, target)) {
                    target_valid = true;
                } else if (target_valid && target >= 0x4000 && target <= 0x7FFF &&
                           tbank > 0 && tbank != bank) {
                    target_valid = is_likely_direct_branch_target(rom, tbank, target);
                }

                if (!target_valid) {
                    AnalysisDiagnostic diagnostic;
                    diagnostic.kind = "unresolved_direct_target";
                    diagnostic.bank = bank;
                    diagnostic.address = offset;
                    diagnostic.memory_space = "physical_rom";
                    diagnostic.status = "unresolved";
                    diagnostic.evidence =
                        tbank == UNKNOWN_BANK
                            ? "JP target bank is unknown under the joined mapper state"
                            : "JP target was rejected by data boundaries or code heuristics";
                    diagnostic.suggested_annotation =
                        tbank == UNKNOWN_BANK
                            ? "add-entry-point <bank>:" +
                                  annotation_address(0, target).substr(3)
                            : "function " + annotation_address(tbank, target);
                    diagnostic.relationship = "potential_dispatch_fallback";
                    if (tbank != UNKNOWN_BANK) {
                        diagnostic.has_related_address = true;
                        diagnostic.related_bank = tbank;
                        diagnostic.related_address = target;
                        diagnostic.related_memory_space =
                            analysis_memory_space(target);
                    }
                    record_analysis_diagnostic(result, std::move(diagnostic));
                }

                if (target_valid) {
                    uint32_t full_target = make_address(tbank, target);
                    if (tbank != bank) {
                        result.call_targets.insert(full_target);
                        result.branch_entry_targets.insert(full_target);
                    }
                    result.label_addresses.insert(full_target);
                    work_queue.push({full_target, known_a, known_b, known_c, known_d, known_e, known_h, known_l, known_sp, mapper_state});
                }
            } else if (instr.type == InstructionType::JR_N || instr.type == InstructionType::JR_CC_N) {
                uint16_t target = offset + instr.length + instr.offset;
                result.label_addresses.insert(make_address(bank, target));
                work_queue.push({make_address(bank, target), known_a, known_b, known_c, known_d, known_e, known_h, known_l, known_sp, mapper_state});
            } else if (instr.type == InstructionType::JP_HL) {
                int synthetic_return_target = -1;
                if (!overlay && offset > 0) {
                    uint8_t prev_opcode = rom.read_banked(bank, static_cast<uint16_t>(offset - 1));
                    switch (prev_opcode) {
                        case 0xC5: synthetic_return_target = get_known_bc(); break; /* PUSH BC */
                        case 0xD5: synthetic_return_target = get_known_de(); break; /* PUSH DE */
                        case 0xE5: synthetic_return_target = get_known_hl(); break; /* PUSH HL */
                        default: break;
                    }
                } else if (overlay && offset > overlay->ram_addr) {
                    BankId src_bank = get_bank(overlay->rom_addr);
                    uint16_t prev_src_addr = static_cast<uint16_t>(
                        get_offset(overlay->rom_addr) + (offset - overlay->ram_addr) - 1);
                    uint8_t prev_opcode = rom.read_banked(src_bank, prev_src_addr);
                    switch (prev_opcode) {
                        case 0xC5: synthetic_return_target = get_known_bc(); break; /* PUSH BC */
                        case 0xD5: synthetic_return_target = get_known_de(); break; /* PUSH DE */
                        case 0xE5: synthetic_return_target = get_known_hl(); break; /* PUSH HL */
                        default: break;
                    }
                }

                if (synthetic_return_target != -1) {
                    uint16_t return_target = static_cast<uint16_t>(synthetic_return_target);
                    BankId return_bank = target_bank(return_target);
                    bool target_valid = false;

                    if (return_target < 0x8000 && return_bank != UNKNOWN_BANK) {
                        if (annotations.contains_data(return_bank, return_target)) {
                            target_valid = false;
                        } else if (annotations.has_function(return_bank, return_target)) {
                            target_valid = true;
                        } else {
                            target_valid = is_likely_direct_branch_target(rom, return_bank, return_target);
                        }
                    } else {
                        for (const auto& ov : options.ram_overlays) {
                            if (return_target >= ov.ram_addr &&
                                return_target < ov.ram_addr + ov.size) {
                                return_bank = 0;
                                target_valid = true;
                                break;
                            }
                        }
                    }

                    if (target_valid) {
                        uint32_t full_return = make_address(return_bank, return_target);
                        result.call_targets.insert(full_return);
                        result.synthetic_entry_targets.insert(full_return);
                        result.label_addresses.insert(full_return);
                        result.computed_jump_targets.insert(full_return);
                        work_queue.push({full_return, -1, -1, -1, -1, -1, -1, -1, -1, mapper_state});
                        if (options.verbose) {
                            std::cout << "[ANALYSIS] Recovered synthetic JP HL return target "
                                      << std::hex << (int)return_bank << ":" << return_target
                                      << " from " << (int)bank << ":" << offset << std::dec << "\n";
                        }
                    }
                }

                int combined_hl = get_known_hl();
                if (combined_hl != -1) {
                    uint16_t target = (uint16_t)combined_hl;
                    BankId tbank = target_bank(target);
                    std::cout << "[ANALYSIS] Resolved static JP HL at " << std::hex << (int)bank << ":" << offset << " -> " << (int)tbank << ":" << target << std::dec << "\n";
                    if (tbank != UNKNOWN_BANK &&
                        !annotations.contains_data(tbank, target)) {
                        uint32_t full_target = make_address(tbank, target);
                        result.call_targets.insert(full_target);
                        result.branch_entry_targets.insert(full_target);
                        result.computed_jump_targets.insert(full_target);
                        result.label_addresses.insert(full_target);
                        work_queue.push({full_target, known_a, known_b, known_c, known_d, known_e, known_h, known_l, known_sp, mapper_state});
                    }
                } else {
                    // Backtracking Jump Table Heuristic
                    bool found_table = false;
                    // Scan back for 'LD H, imm' pattern (very common for tables)
                    for (int back = 1; back < 10; back++) {
                        if (offset < back) break;
                        uint8_t op = rom.read_banked(bank, offset - back);
                        if (op == 0x26) { // LD H, imm
                            uint8_t table_h = rom.read_banked(bank, offset - back + 1);
                            std::cout << "[ANALYSIS] Heuristic: Found potential jump table at " << std::hex << (int)table_h << "00 near " << (int)bank << ":" << offset << std::dec << "\n";
                            // Scan the page for addresses that lead to code
                            for (int i = 0; i < 256; i += 2) {
                                uint16_t entry_addr = (table_h << 8) | i;
                                uint8_t lo = rom.read(entry_addr);
                                uint8_t hi = rom.read(entry_addr + 1);
                                uint16_t target = lo | (hi << 8);
                                if (target >= 0x0100 && target < 0x8000) {
                                    BankId tbank = target_bank(target);
                                    if (tbank != UNKNOWN_BANK &&
                                        !annotations.contains_data(tbank, target) &&
                                        (annotations.has_function(tbank, target) ||
                                         is_likely_valid_code(rom, tbank, target))) {
                                        uint32_t full_target = make_address(tbank, target);
                                        result.call_targets.insert(full_target);
                                        result.branch_entry_targets.insert(full_target);
                                        result.computed_jump_targets.insert(full_target);
                                        result.label_addresses.insert(full_target);
                                        work_queue.push({full_target, -1, -1, -1, -1, -1, -1, -1, -1, mapper_state});
                                        found_table = true;
                                    }
                                }
                            }
                            if (found_table) break;
                        }
                    }
                    if (!found_table) {
                        AnalysisDiagnostic diagnostic;
                        diagnostic.kind = "unresolved_indirect_jump";
                        diagnostic.bank = bank;
                        diagnostic.address = offset;
                        diagnostic.memory_space = "physical_rom";
                        diagnostic.status = "unresolved";
                        diagnostic.evidence =
                            "JP HL target remained unknown after constant "
                            "propagation and jump-table heuristics";
                        diagnostic.suggested_annotation =
                            "inspect dispatch at " +
                            annotation_address(bank, offset) +
                            "; annotate each confirmed target as function "
                            "<bank>:<address>";
                        diagnostic.relationship = "potential_dispatch_fallback";
                        record_analysis_diagnostic(result, std::move(diagnostic));
                        std::cout << "[ANALYSIS] Unresolved JP HL at " << std::hex << (int)bank << ":" << offset << std::dec << "\n";
                    }
                }
            }
            
            if (instr.is_conditional) {
                uint32_t fall_through = make_address(bank, offset + instr.length);
                result.label_addresses.insert(fall_through);
                work_queue.push({fall_through, known_a, known_b, known_c, known_d, known_e, known_h, known_l, known_sp, mapper_state});
            }
        } else if (instr.is_return) {
            if (instr.is_conditional) {
                uint32_t fall_through = make_address(bank, offset + instr.length);
                result.label_addresses.insert(fall_through);
                work_queue.push({fall_through, known_a, known_b, known_c, known_d, known_e, known_h, known_l, known_sp, mapper_state});
            }
        } else {
            work_queue.push({make_address(bank, offset + instr.length), known_a, known_b, known_c, known_d, known_e, known_h, known_l, known_sp, mapper_state});
        }
        // Target-bank resolution depends on the joined input state and is
        // therefore persisted only after control-flow processing completes.
        result.instructions[idx] = instr;
    } // End work_queue loop

    // Aggressive Code Scanning
    if (options.aggressive_scan && !scanning_pass) {
        scanning_pass = true; // prevent infinite loops if we find nothing new
        
        if (options.verbose) std::cout << "[ANALYSIS] Starting aggressive scan for missing code..." << std::endl;
        
        size_t found_count = 0;

        // Iterate through all known banks (and bank 0)
        std::vector<BankId> banks_to_scan;
        banks_to_scan.push_back(0);
        for (BankId b : known_banks) if (b > 0) banks_to_scan.push_back(b);

        for (BankId bank : banks_to_scan) {
            uint16_t start_addr = (bank == 0) ? 0x0000 : 0x4000;
            uint16_t end_addr = (bank == 0) ? 0x3FFF : 0x7FFF;
            
            for (uint32_t addr = start_addr; addr <= end_addr; ) {
                uint32_t full_addr = make_address(bank, addr);
                
                // If already visited by ANY means, skip
                if (visited.count(full_addr) || aggressive_regions.count(full_addr)) {
                    addr++; 
                    continue;
                }
                
                // Alignment heuristic: most functions start on some boundary? No.
                // But we can skip obvious padding (0xFF or 0x00)
                if (annotations.contains_data(bank, static_cast<uint16_t>(addr)) &&
                    !annotations.has_function(bank, static_cast<uint16_t>(addr))) {
                    addr++;
                    continue;
                }

                uint8_t byte = rom.read_banked(bank, addr);
                if (byte == 0xFF || byte == 0x00) {
                    addr++;
                    continue;
                }

                // Check if this looks like valid code
                int code_len = is_likely_valid_code(rom, bank, addr);
                if (code_len > 0) {
                    AnalysisDiagnostic diagnostic;
                    diagnostic.kind = "data_as_code_candidate";
                    diagnostic.bank = bank;
                    diagnostic.address = static_cast<uint16_t>(addr);
                    diagnostic.memory_space = "physical_rom";
                    diagnostic.status = "candidate";
                    diagnostic.evidence =
                        "aggressive scan accepted " +
                        std::to_string(code_len) +
                        " byte(s) of unreferenced ROM as plausible code";
                    diagnostic.suggested_annotation =
                        "data " +
                        annotation_address(bank, static_cast<uint16_t>(addr)) +
                        " " + std::to_string(code_len);
                    diagnostic.relationship = "potential_false_code";
                    record_analysis_diagnostic(result, std::move(diagnostic));
                    if (options.verbose) {
                        std::cout << "[ANALYSIS] Detected potential function at " 
                                  << std::hex << (int)bank << ":" << addr << std::dec << "\n";
                    }
                    
                    // Add as a new entry point
                    uint32_t entry = make_address(bank, addr);
                    result.call_targets.insert(entry);
                    result.strong_call_targets.insert(entry);
                    
                    // Add to queue
                    BankId context = (bank > 0) ? bank : 1;
                    work_queue.push({entry, -1, -1, -1, -1, -1, -1, -1, -1,
                                     mapper_state_for_bank(rom, context)});
                    found_count++;
                    
                    // Mark region as scanned
                    for (int i = 0; i < code_len; i++) {
                        aggressive_regions.insert(make_address(bank, addr + i));
                    }
                    
                    // Skip the block we just found to avoid overlapping detection
                    addr += code_len;
                    continue;
                } else {
                    // Not valid code, skip ahead.
                    addr++;
                }
            }
        }
        
        if (found_count > 0) {
            if (options.verbose) std::cout << "[ANALYSIS] Found " << found_count << " new entry points. Restarting analysis." << std::endl;
            scanning_pass = false; // Reset pass flag to allow further scanning after this batch is analyzed
            continue; // Go back to work_queue processing
        }
    }
    
    // If we get here, we are done
    break; 
    } // End while(true)
    
    // Build basic blocks from instruction boundaries
    std::set<uint32_t> block_starts;
    
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
                    BankId succ_bank = instr.resolved_target_bank;
                    if (succ_bank == UNKNOWN_BANK) {
                        succ_bank = block.bank;
                    }
                    uint32_t succ_addr = make_address(succ_bank, instr.imm16);
                    block.successors.push_back(succ_addr);
                    if (succ_bank != block.bank) {
                        block.has_cross_bank_successor = true;
                    }
                } else if (instr.type == InstructionType::JR_N || instr.type == InstructionType::JR_CC_N) {
                    uint16_t target = get_offset(curr) + instr.length + instr.offset;
                    block.successors.push_back(make_address(block.bank, target));
                }
                // Conditional jumps also fall through
                if (instr.is_conditional) {
                    block.successors.push_back(make_address(block.bank, get_offset(curr) + instr.length));
                }
            } else if (instr.is_return && instr.is_conditional) {
                // Conditional returns fall through if condition is false
                block.successors.push_back(make_address(block.bank, get_offset(curr) + instr.length));
            }
            
            // Check if this ends the block
            if (instr.is_jump || instr.is_return || instr.is_call) {
                // CALLs fall through to next instruction after return
                if (instr.is_call) {
                    block.successors.push_back(make_address(block.bank, get_offset(curr) + instr.length));
                }
                break;
            }
            
            curr = make_address(block.bank, get_offset(curr) + instr.length);
            
            // Check if next instruction starts a new block
            if (block_starts.count(curr)) {
                // Fall through to the new block - add as successor
                block.successors.push_back(curr);
                break;
            }
        }
        
        result.blocks[start] = block;
    }

    // Populate predecessor lists now that the block graph is complete.
    for (const auto& [block_addr, block] : result.blocks) {
        for (uint32_t succ_addr : block.successors) {
            auto succ_it = result.blocks.find(succ_addr);
            if (succ_it == result.blocks.end()) {
                continue;
            }
            succ_it->second.predecessors.push_back(block_addr);
        }
    }
    
    // Create functions from call targets with better merging logic
    std::set<uint32_t> processed_targets;
    std::set<uint32_t> bank_switch_instruction_addrs;
    for (const auto& sw : result.bank_tracker.switches()) {
        bank_switch_instruction_addrs.insert(sw.addr);
    }
    
    // Function size threshold for merging (in instructions)
    const int MIN_FUNCTION_SIZE = 3;
    
    for (uint32_t target : result.call_targets) {
        if (processed_targets.count(target)) continue;
        
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
                // Strong call targets (interrupt vectors, explicit entry points) are
                // always function boundaries — never absorb them into another function.
                if (result.strong_call_targets.count(block_addr) ||
                    result.synthetic_entry_targets.count(block_addr)) {
                    // Don't include this block or its successors in the current function.
                    // Leave it unprocessed so it gets its own function entry later.
                    continue;
                }
                func.block_addresses.push_back(get_offset(block_addr));
            }

            for (size_t instr_index : blk->second.instruction_indices) {
                if (instr_index >= result.instructions.size()) {
                    continue;
                }
                const Instruction& block_instr = result.instructions[instr_index];
                uint32_t full_instr_addr = make_address(block_instr.bank, block_instr.address);
                if (bank_switch_instruction_addrs.count(full_instr_addr)) {
                    func.may_switch_rom_bank = true;
                    break;
                }
            }
            
            // Mark all reachable (non-strong) call targets as processed to avoid
            // creating redundant separate functions for inlined helpers.
            if (result.call_targets.count(block_addr) &&
                !result.strong_call_targets.count(block_addr) &&
                !result.synthetic_entry_targets.count(block_addr) &&
                block_addr != target) {
                processed_targets.insert(block_addr);
            }
            
            // Follow successors
            for (uint32_t succ_addr : blk->second.successors) {
                if (get_bank(succ_addr) != func.bank) {
                    func.crosses_banks = true;
                    continue;
                }
                if (!func_visited.count(succ_addr)) {
                    func_queue.push(succ_addr);
                }
            }
        }
        
        result.functions[target] = func;
        processed_targets.insert(target);
    }
    
    // Post-process: Merge very small functions into their callers
    // This reduces the number of single-instruction functions
    std::map<uint32_t, Function> merged_functions = result.functions;
    std::set<uint32_t> functions_to_remove;
    
    for (const auto& [func_addr, func] : result.functions) {
        if (functions_to_remove.count(func_addr)) continue;
        
        // Calculate total number of instructions in function
        int total_instrs = 0;
        for (uint16_t block_addr : func.block_addresses) {
            uint32_t full_blk_addr = make_address(func.bank, block_addr);
            auto blk_it = result.blocks.find(full_blk_addr);
            if (blk_it != result.blocks.end()) {
                total_instrs += blk_it->second.instruction_indices.size();
            }
        }
        
        // If function is too small and not a special entry point, consider merging
        bool is_special_entry = (func.bank == 0 && (
            func.entry_address == 0x100 || // main entry
            (func.entry_address >= 0x00 && func.entry_address <= 0x38) || // RST vectors
            (func.entry_address >= 0x40 && func.entry_address <= 0x60) // interrupt vectors
        ));
        
        if (total_instrs < MIN_FUNCTION_SIZE &&
            !is_special_entry &&
            !result.strong_call_targets.count(func_addr) &&
            !result.synthetic_entry_targets.count(func_addr) &&
            !result.branch_entry_targets.count(func_addr)) {
            functions_to_remove.insert(func_addr);
        }
    }
    
    // Remove small functions
    for (uint32_t func_addr : functions_to_remove) {
        merged_functions.erase(func_addr);
        // Also remove from call_targets so they don't get re-created
        result.call_targets.erase(func_addr);
    }
    
    result.functions = merged_functions;

    for (uint32_t target : result.branch_entry_targets) {
        AnalysisDiagnostic diagnostic;
        diagnostic.kind = "uncertain_entry_point";
        diagnostic.bank = get_bank(target);
        diagnostic.address = get_offset(target);
        diagnostic.memory_space =
            analysis_memory_space(diagnostic.address);
        diagnostic.status = "inferred";
        diagnostic.evidence =
            result.computed_jump_targets.count(target)
                ? "entry inferred from static or heuristic computed-jump analysis"
                : "entry inferred from a cross-bank branch";
        diagnostic.suggested_annotation =
            "function " +
            annotation_address(diagnostic.bank, diagnostic.address);
        diagnostic.relationship = "potential_dispatch_entry";
        record_analysis_diagnostic(result, std::move(diagnostic));
    }
    
    // Update stats
    result.stats.total_instructions = result.instructions.size();
    result.stats.total_blocks = result.blocks.size();
    result.stats.total_functions = result.functions.size();
    
    return result;
}

AnalysisResult analyze_bank(const ROM& rom, BankId bank, const AnalyzerOptions& options) {
    (void)bank; // Unused parameter
    // For now, just analyze the whole ROM
    // TODO: Filter to specific bank
    return analyze(rom, options);
}

/* ============================================================================
 * Utility Functions
 * ========================================================================== */

std::string generate_function_name(BankId bank, uint16_t address) {
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

std::string generate_label_name(BankId bank, uint16_t address) {
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
    std::cout << "Bank switches detected: " << result.bank_tracker.switches().size() << std::endl;
    std::cout << "Cross-bank calls tracked: " << result.bank_tracker.calls().size() << std::endl;
    
    std::cout << "\nFunctions found:" << std::endl;
    for (const auto& [addr, func] : result.functions) {
        std::cout << "  " << func.name << " @ ";
        if (func.bank > 0) {
            std::cout << std::hex << std::setfill('0') << std::setw(2) << (int)func.bank << ":";
        }
        std::cout << std::hex << std::setfill('0') << std::setw(4) << func.entry_address << std::endl;
    }
}

bool is_likely_data(const AnalysisResult& result, BankId bank, uint16_t address) {
    uint32_t full_addr = AnalysisResult::make_addr(bank, address);
    return result.addr_to_index.find(full_addr) == result.addr_to_index.end();
}

} // namespace gbrecomp
