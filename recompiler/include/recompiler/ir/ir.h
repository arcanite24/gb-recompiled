/**
 * @file ir.h
 * @brief Intermediate Representation for GameBoy instructions
 * 
 * The IR layer decouples instruction semantics from code generation,
 * enabling optimization passes and future backend support (e.g., LLVM).
 */

#ifndef RECOMPILER_IR_H
#define RECOMPILER_IR_H

#include "../rom.h"
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <memory>

namespace gbrecomp {
namespace ir {

/* ============================================================================
 * IR Opcodes
 * ========================================================================== */

enum class Opcode : uint16_t {
    // === Data Movement ===
    MOV_REG_REG,        // dst = src
    MOV_REG_REG16,      // dst16 = src16 (for LD SP,HL)
    MOV_REG_IMM8,       // dst = imm8
    MOV_REG_IMM16,      // dst16 = imm16
    LD_HL_SP_N,         // HL = SP + signed_imm8, set H/C flags (for LD HL,SP+n)
    LOAD8,              // dst = mem[addr]
    LOAD8_HL_AUTO,      // dst = mem[HL]; HL += signed delta in the data M-cycle
    LOAD8_REG,          // dst = mem[reg16]
    LOAD16,             // dst16 = mem16[addr]
    LOAD16_REG,         // dst16 = mem16[reg16]
    STORE8,             // mem[addr] = src
    STORE8_HL_AUTO,     // mem[HL] = src; HL += signed delta in the data M-cycle
    STORE8_REG,         // mem[reg16] = src
    STORE16,            // mem16[addr] = src16
    STORE16_REG,        // mem16[reg16] = src16
    PUSH16,             // SP -= 2; mem16[SP] = src16
    POP16,              // dst16 = mem16[SP]; SP += 2
    
    // === ALU Operations (8-bit) ===
    ADD8,               // A = A + src, set flags
    ADC8,               // A = A + src + C, set flags
    SUB8,               // A = A - src, set flags
    SBC8,               // A = A - src - C, set flags
    AND8,               // A = A & src, set flags
    OR8,                // A = A | src, set flags
    XOR8,               // A = A ^ src, set flags
    CP8,                // compare A - src, set flags only
    INC8,               // dst++, set Z/N/H flags
    DEC8,               // dst--, set Z/N/H flags
    
    // === ALU Operations (16-bit) ===
    ADD16,              // HL = HL + rr, set N/H/C flags
    ADD_SP_IMM8,        // SP = SP + signed_imm8, set H/C flags
    INC16,              // rr++, no flags
    DEC16,              // rr--, no flags
    
    // === Bit Operations ===
    RLC,                // Rotate left, old bit 7 to carry and bit 0
    RRC,                // Rotate right, old bit 0 to carry and bit 7
    RL,                 // Rotate left through carry
    RR,                 // Rotate right through carry
    SLA,                // Shift left arithmetic (bit 0 = 0)
    SRA,                // Shift right arithmetic (bit 7 preserved)
    SRL,                // Shift right logical (bit 7 = 0)
    SWAP,               // Swap nibbles
    BIT,                // Test bit, set Z flag
    SET,                // Set bit
    RES,                // Reset bit
    
    // === Control Flow ===
    JUMP,               // Unconditional jump
    JUMP_CC,            // Conditional jump
    JUMP_REG,           // Jump to address in register (JP HL)
    JR,                 // Relative jump
    JR_CC,              // Conditional relative jump
    CALL,               // Push PC, jump
    CALL_CC,            // Conditional call
    RET,                // Pop PC
    RET_CC,             // Conditional return
    RETI,               // Return and enable interrupts
    RST,                // Call to fixed vector
    
    // === Special ===
    NOP,
    HALT,
    STOP,
    DI,                 // Disable interrupts
    EI,                 // Enable interrupts
    DAA,                // Decimal adjust A
    CPL,                // Complement A
    CCF,                // Complement carry flag
    SCF,                // Set carry flag
    
    // === Memory-Mapped I/O ===
    IO_READ,            // A = mem[0xFF00 + offset]
    IO_READ_C,          // A = mem[0xFF00 + C]
    IO_WRITE,           // mem[0xFF00 + offset] = A
    IO_WRITE_C,         // mem[0xFF00 + C] = A
    
    // === Bank Switching (pseudo-ops) ===
    BANK_HINT,          // Hint: bank may have changed
    CROSS_BANK_CALL,    // Call that crosses bank boundary
    CROSS_BANK_JUMP,    // Jump that crosses bank boundary
    
    // === Meta ===
    LABEL,              // Label definition (pseudo-op)
    COMMENT,            // Debugging info (pseudo-op)
    SOURCE_LOC,         // Source location marker (pseudo-op)
};

/* ============================================================================
 * Operand Types
 * ========================================================================== */

enum class OperandType : uint8_t {
    NONE = 0,
    REG8,           // 8-bit register (A, B, C, D, E, H, L)
    REG16,          // 16-bit register (BC, DE, HL, SP, AF)
    IMM8,           // 8-bit immediate
    IMM16,          // 16-bit immediate
    OFFSET,         // Signed 8-bit offset (for JR)
    ADDR,           // 16-bit address
    COND,           // Condition code (Z, NZ, C, NC)
    BIT_IDX,        // Bit index 0-7
    BANK,           // ROM bank number
    MEM_REG16,      // Memory at [reg16] e.g., [HL]
    MEM_IMM16,      // Memory at [imm16]
    IO_OFFSET,      // 0xFF00 + offset
    LABEL_REF,      // Reference to a label
    RST_VEC,        // RST vector (0x00, 0x08, ..., 0x38)
};

/* ============================================================================
 * IR Operand
 * ========================================================================== */

struct Operand {
    OperandType type = OperandType::NONE;
    BankId bank = 0; // ROM bank context for this operand (0 for fixed, 1-N for switched)
    
    union {
        uint8_t reg8;           // Register index (0=A, 1=B, 2=C, 3=D, 4=E, 5=H, 6=L)
        uint8_t reg16;          // Register pair index (0=BC, 1=DE, 2=HL, 3=SP, 4=AF)
        uint8_t imm8;
        uint16_t imm16;
        int8_t offset;
        uint8_t bit_idx;
        BankId bank;
        uint8_t condition;      // 0=NZ, 1=Z, 2=NC, 3=C
        uint8_t io_offset;
        uint8_t rst_vec;
        uint32_t label_id;
    } value = {0};
    
    // Constructors
    static Operand none() { return Operand{}; }
    static Operand reg8(uint8_t r);
    static Operand reg16(uint8_t r);
    static Operand imm8(uint8_t v);
    static Operand imm16(uint16_t v);
    static Operand offset(int8_t o);
    static Operand condition(uint8_t c);
    static Operand bit_idx(uint8_t b);
    static Operand mem_reg16(uint8_t r);
    static Operand mem_imm16(uint16_t addr);
    static Operand io_offset(uint8_t off);
    static Operand label(uint32_t id);
    static Operand rst_vec(uint8_t vec);
};

/* ============================================================================
 * Flag Effects
 * ========================================================================== */

struct FlagEffects {
    // Which flags are affected
    bool affects_z : 1;
    bool affects_n : 1;
    bool affects_h : 1;
    bool affects_c : 1;
    
    // For flags that are set to a fixed value
    bool fixed_z : 1;   // If affects_z && fixed_z, Z = z_value
    bool fixed_n : 1;
    bool fixed_h : 1;
    bool fixed_c : 1;
    
    bool z_value : 1;
    bool n_value : 1;
    bool h_value : 1;
    bool c_value : 1;
    
    static FlagEffects none();
    static FlagEffects znhc();  // All affected, computed
    static FlagEffects z0h0();  // Z=computed, N=0, H=computed, C=0
    static FlagEffects z1hc();  // Z=computed, N=1, H=computed, C=computed
    static FlagEffects z0hc();  // Z=computed, N=0, H=computed, C=computed
    static FlagEffects only_c(); // Only carry affected
};

/* ============================================================================
 * IR Instruction
 * ========================================================================== */

struct IRInstruction {
    Opcode opcode;
    Operand dst;
    Operand src;
    Operand extra;          // For 3-operand ops (e.g., BIT n, r)
    
    // Source location tracking
    BankId source_bank = 0;
    uint16_t source_address = 0;
    bool has_source_location = false;
    
    // Cycle cost
    uint8_t cycles = 0;
    uint8_t cycles_branch_taken = 0;
    
    // Flag effects
    FlagEffects flags = FlagEffects::none();
    
    // Debug info
    std::string comment;
    
    // Factory methods
    static IRInstruction make_nop(BankId bank, uint16_t addr);
    static IRInstruction make_mov_reg_reg(uint8_t dst, uint8_t src, BankId bank, uint16_t addr);
    static IRInstruction make_load8(uint8_t dst_reg, uint16_t addr, BankId bank, uint16_t src_addr);
    static IRInstruction make_store8(uint16_t addr, uint8_t src_reg, BankId bank, uint16_t src_addr);
    static IRInstruction make_add8(uint8_t src, BankId bank, uint16_t addr);
    static IRInstruction make_jump(uint32_t label_id, BankId bank, uint16_t addr);
    static IRInstruction make_jump_cc(uint8_t cond, uint32_t label_id, BankId bank, uint16_t addr);
    static IRInstruction make_call(uint32_t label_id, BankId bank, uint16_t addr);
    static IRInstruction make_ret(BankId bank, uint16_t addr);
    static IRInstruction make_label(uint32_t label_id);
    static IRInstruction make_comment(const std::string& text);
};

/* ============================================================================
 * Basic Block
 * ========================================================================== */

struct BasicBlock {
    uint32_t id;
    std::string label;
    std::vector<IRInstruction> instructions;
    
    // Control flow
    std::vector<uint32_t> successors;
    std::vector<uint32_t> predecessors;
    
    // Bank info
    BankId bank = 0;
    uint16_t start_address = 0;
    uint16_t end_address = 0;
    
    // Flags
    bool is_entry = false;
    bool is_interrupt_handler = false;
    bool is_reachable = false;
};

/* ============================================================================
 * Function
 * ========================================================================== */

struct Function {
    std::string name;
    BankId bank = 0;
    uint16_t entry_address = 0;
    
    std::vector<uint32_t> block_ids;
    
    bool is_interrupt_handler = false;
    bool is_entry_point = false;
    bool crosses_banks = false;
    bool may_switch_rom_bank = false;
};

struct AddressSymbol {
    BankId bank = 0;
    uint16_t address = 0;
    std::string source_name;
    std::vector<std::string> source_names;
    std::string emitted_name;
    std::string kind;
    std::string provenance;
    std::string memory_space;
    uint32_t width = 1;
    std::string comment;
};

struct ConstantSymbol {
    std::string name;
    uint32_t value = 0;
    std::string memory_space;
    std::string provenance;
    std::string comment;
};

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
 * IR Program
 * ========================================================================== */

struct Program {
    std::string rom_name;
    std::map<std::string, ConstantSymbol> constants;
    
    // All basic blocks
    std::map<uint32_t, BasicBlock> blocks;
    uint32_t next_block_id = 0;
    
    // Functions
    std::map<std::string, Function> functions;

    // Address-level symbol metadata carried through to code generation
    std::map<uint32_t, AddressSymbol> address_symbols;

    // Actionable analyzer decisions carried through to generated metadata.
    std::map<std::string, AnalysisDiagnostic> analysis_diagnostics;
    
    // Labels (for cross-referencing)
    std::map<uint32_t, std::string> labels;         // id -> name
    std::map<std::string, uint32_t> label_by_name;  // name -> id
    uint32_t next_label_id = 0;
    
    // ROM info
    uint8_t mbc_type = 0;
    uint16_t rom_bank_count = 0;
    
    // Entry points
    uint16_t main_entry = 0x100;
    std::vector<uint16_t> interrupt_vectors;
    
    // Create a new block
    uint32_t create_block(BankId bank, uint16_t addr);
    
    // Create/lookup labels
    uint32_t create_label(const std::string& name);
    uint32_t get_or_create_label(const std::string& name);
    std::string get_label_name(uint32_t id) const;
    
    // Generate unique label for address
    std::string make_address_label(BankId bank, uint16_t addr) const;
    std::string make_function_name(BankId bank, uint16_t addr) const;
};

/* ============================================================================
 * IR Utilities
 * ========================================================================== */

/**
 * @brief Get opcode name for debugging
 */
const char* opcode_name(Opcode op);

/**
 * @brief Print IR instruction for debugging
 */
std::string format_instruction(const IRInstruction& instr);

/**
 * @brief Print entire program for debugging
 */
void dump_program(const Program& program, std::ostream& out);

} // namespace ir
} // namespace gbrecomp

#endif // RECOMPILER_IR_H
