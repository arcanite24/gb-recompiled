/**
 * @file decoder.h
 * @brief SM83 (GameBoy CPU) instruction decoder
 * 
 * Decodes all ~500 SM83 opcodes including CB-prefixed instructions.
 * The SM83 is similar to Z80 but with differences in timing and some opcodes.
 */

#ifndef RECOMPILER_DECODER_H
#define RECOMPILER_DECODER_H

#include "rom.h"
#include <cstdint>
#include <string>
#include <vector>

namespace gbrecomp {

/* ============================================================================
 * Register Definitions
 * ========================================================================== */

// 8-bit registers - values match GameBoy instruction encoding (bits 0-2 or 3-5)
enum class Reg8 : uint8_t {
    B = 0, C = 1, D = 2, E = 3, H = 4, L = 5,
    // Special: memory at HL
    HL_IND = 6,  // (HL) - indirect through HL
    A = 7
};

// 16-bit registers
enum class Reg16 : uint8_t {
    BC = 0, DE = 1, HL = 2, SP = 3,
    AF = 4  // For PUSH/POP AF
};

// Condition codes
enum class Condition : uint8_t {
    NZ = 0,  // Not Zero
    Z = 1,   // Zero
    NC = 2,  // Not Carry
    C = 3,   // Carry
    ALWAYS = 4  // Unconditional (not a real condition)
};

/* ============================================================================
 * Instruction Types
 * ========================================================================== */

enum class InstructionType : uint16_t {
    // === Invalid/Unknown ===
    INVALID = 0,
    
    // === 8-bit Loads ===
    LD_R_R,         // LD r, r'
    LD_R_N,         // LD r, n
    LD_R_HL,        // LD r, (HL)
    LD_HL_R,        // LD (HL), r
    LD_HL_N,        // LD (HL), n
    LD_A_BC,        // LD A, (BC)
    LD_A_DE,        // LD A, (DE)
    LD_BC_A,        // LD (BC), A
    LD_DE_A,        // LD (DE), A
    LD_A_NN,        // LD A, (nn)
    LD_NN_A,        // LD (nn), A
    LDH_A_N,        // LDH A, (n)  - LD A, (0xFF00+n)
    LDH_N_A,        // LDH (n), A  - LD (0xFF00+n), A
    LDH_A_C,        // LDH A, (C)  - LD A, (0xFF00+C)
    LDH_C_A,        // LDH (C), A  - LD (0xFF00+C), A
    LD_A_HLI,       // LD A, (HL+) - LDI A, (HL)
    LD_A_HLD,       // LD A, (HL-) - LDD A, (HL)
    LD_HLI_A,       // LD (HL+), A - LDI (HL), A
    LD_HLD_A,       // LD (HL-), A - LDD (HL), A
    
    // === 16-bit Loads ===
    LD_RR_NN,       // LD rr, nn
    LD_SP_HL,       // LD SP, HL
    LD_NN_SP,       // LD (nn), SP
    LD_HL_SP_N,     // LD HL, SP+n (signed)
    PUSH,           // PUSH rr
    POP,            // POP rr
    
    // === 8-bit Arithmetic ===
    ADD_A_R,        // ADD A, r
    ADD_A_N,        // ADD A, n
    ADD_A_HL,       // ADD A, (HL)
    ADC_A_R,        // ADC A, r
    ADC_A_N,        // ADC A, n
    ADC_A_HL,       // ADC A, (HL)
    SUB_A_R,        // SUB r
    SUB_A_N,        // SUB n
    SUB_A_HL,       // SUB (HL)
    SBC_A_R,        // SBC A, r
    SBC_A_N,        // SBC A, n
    SBC_A_HL,       // SBC A, (HL)
    AND_A_R,        // AND r
    AND_A_N,        // AND n
    AND_A_HL,       // AND (HL)
    OR_A_R,         // OR r
    OR_A_N,         // OR n
    OR_A_HL,        // OR (HL)
    XOR_A_R,        // XOR r
    XOR_A_N,        // XOR n
    XOR_A_HL,       // XOR (HL)
    CP_A_R,         // CP r
    CP_A_N,         // CP n
    CP_A_HL,        // CP (HL)
    INC_R,          // INC r
    DEC_R,          // DEC r
    INC_HL_IND,     // INC (HL)
    DEC_HL_IND,     // DEC (HL)
    
    // === 16-bit Arithmetic ===
    ADD_HL_RR,      // ADD HL, rr
    ADD_SP_N,       // ADD SP, n (signed)
    INC_RR,         // INC rr
    DEC_RR,         // DEC rr
    
    // === Rotates/Shifts (Non-CB) ===
    RLCA,           // RLCA
    RRCA,           // RRCA
    RLA,            // RLA
    RRA,            // RRA
    
    // === CB-Prefixed Rotates/Shifts ===
    RLC_R,          // RLC r
    RLC_HL,         // RLC (HL)
    RRC_R,          // RRC r
    RRC_HL,         // RRC (HL)
    RL_R,           // RL r
    RL_HL,          // RL (HL)
    RR_R,           // RR r
    RR_HL,          // RR (HL)
    SLA_R,          // SLA r
    SLA_HL,         // SLA (HL)
    SRA_R,          // SRA r
    SRA_HL,         // SRA (HL)
    SWAP_R,         // SWAP r
    SWAP_HL,        // SWAP (HL)
    SRL_R,          // SRL r
    SRL_HL,         // SRL (HL)
    
    // === CB-Prefixed Bit Operations ===
    BIT_N_R,        // BIT n, r
    BIT_N_HL,       // BIT n, (HL)
    SET_N_R,        // SET n, r
    SET_N_HL,       // SET n, (HL)
    RES_N_R,        // RES n, r
    RES_N_HL,       // RES n, (HL)
    
    // === Control Flow ===
    JP_NN,          // JP nn
    JP_CC_NN,       // JP cc, nn
    JP_HL,          // JP HL (JP (HL))
    JR_N,           // JR n
    JR_CC_N,        // JR cc, n
    CALL_NN,        // CALL nn
    CALL_CC_NN,     // CALL cc, nn
    RET,            // RET
    RET_CC,         // RET cc
    RETI,           // RETI
    RST,            // RST vec
    
    // === Misc ===
    NOP,
    HALT,
    STOP,
    DI,
    EI,
    CCF,            // Complement Carry Flag
    SCF,            // Set Carry Flag
    DAA,            // Decimal Adjust A
    CPL,            // Complement A
    
    // === Illegal/Undefined ===
    // 0xD3, 0xDB, 0xDD, 0xE3, 0xE4, 0xEB, 0xEC, 0xED, 0xF4, 0xFC, 0xFD
    UNDEFINED,
    
    // Count
    TYPE_COUNT
};

/* ============================================================================
 * Instruction Structure
 * ========================================================================== */

/**
 * @brief Decoded instruction with all operand information
 */
struct Instruction {
    // Location
    uint16_t address;           // Address in ROM
    BankId bank;                // ROM bank (0 for fixed bank)
    BankId resolved_target_bank = UNKNOWN_BANK;
    
    // Raw bytes
    uint8_t opcode;             // Primary opcode
    uint8_t cb_opcode;          // CB-prefixed opcode (if applicable)
    bool is_cb_prefixed;        // Is this a CB-prefixed instruction?
    
    // Decoded type
    InstructionType type;
    
    // Size and timing
    uint8_t length;             // 1, 2, or 3 bytes
    uint8_t cycles;             // Base cycle count
    uint8_t cycles_branch;      // Cycles if branch taken (for conditionals)
    
    // Operands
    Reg8 reg8_dst;              // Destination 8-bit register
    Reg8 reg8_src;              // Source 8-bit register
    Reg16 reg16;                // 16-bit register
    Condition condition;        // Condition code for jumps
    uint8_t bit_index;          // Bit index for BIT/SET/RES (0-7)
    uint8_t imm8;               // 8-bit immediate
    uint16_t imm16;             // 16-bit immediate
    int8_t offset;              // Signed offset for JR
    uint8_t rst_vector;         // RST vector (0x00, 0x08, ..., 0x38)
    
    // Control flow flags
    bool is_jump;               // JP, JR
    bool is_call;               // CALL, RST
    bool is_return;             // RET, RETI
    bool is_conditional;        // Has condition code
    bool is_terminator;         // Ends basic block
    
    // Memory access flags
    bool reads_memory;          // Reads from memory
    bool writes_memory;         // Writes to memory
    bool is_io;                 // Accesses 0xFF00-0xFFFF
    
    // Flag effects
    struct {
        bool affects_z : 1;
        bool affects_n : 1;
        bool affects_h : 1;
        bool affects_c : 1;
    } flag_effects;
    
    /**
     * @brief Get disassembly string
     */
    std::string disassemble() const;
    
    /**
     * @brief Get raw bytes as hex string
     */
    std::string bytes_hex() const;
};

/* ============================================================================
 * Decoder Class
 * ========================================================================== */

/**
 * @brief SM83 instruction decoder
 */
class Decoder {
public:
    explicit Decoder(const ROM& rom);
    
    Instruction decode(uint32_t full_addr) const;
    Instruction decode(uint16_t addr, BankId bank) const;
    
private:
    void decode_main(Instruction& instr, uint8_t opcode,
                     uint16_t addr, BankId bank) const;
    Instruction decode_cb(uint16_t addr, BankId bank) const;
    uint16_t read_u16(uint16_t addr, BankId bank) const;
    
    const ROM& rom_;
};

/* ============================================================================
 * Decoder Interface
 * ========================================================================== */

/**
 * @brief Decode a single instruction at the given address
 * 
 * @param rom ROM data
 * @param rom_size ROM size
 * @param address Address to decode at
 * @param bank Current ROM bank (0 for fixed region)
 * @return Decoded instruction
 */

/**
 * @brief Decode all instructions in a ROM
 * 
 * Performs linear sweep decoding. For accurate results,
 * combine with control flow analysis.
 * 
 * @param rom ROM data
 * @param rom_size ROM size
 * @param bank Bank to decode (0 for bank 0, 1+ for switchable)
 * @return Vector of decoded instructions
 */
std::vector<Instruction> decode_bank(const ROM& rom, BankId bank = 0);


/**
 * @brief Get cycle count for an instruction
 * 
 * @param instr Instruction
 * @param branch_taken Whether branch was taken (for conditionals)
 * @return Cycle count
 */
uint8_t get_cycle_count(const Instruction& instr, bool branch_taken = false);

/**
 * @brief Get instruction length
 * 
 * @param opcode Primary opcode
 * @param is_cb Is CB-prefixed
 * @return Length in bytes
 */
uint8_t get_instruction_length(uint8_t opcode, bool is_cb);

/* ============================================================================
 * Disassembly
 * ========================================================================== */

/**
 * @brief Disassemble instruction to string
 */
std::string disassemble(const Instruction& instr);

/**
 * @brief Get register name
 */
const char* reg8_name(Reg8 reg);
const char* reg16_name(Reg16 reg);
const char* condition_name(Condition cond);

} // namespace gbrecomp

#endif // RECOMPILER_DECODER_H
