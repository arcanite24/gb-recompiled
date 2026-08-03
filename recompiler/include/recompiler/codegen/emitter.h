/**
 * @file emitter.h
 * @brief Abstract code emitter interface
 * 
 * This abstraction allows swapping between C output (MVP) and
 * LLVM IR (future) without changing the IR layer.
 */

#ifndef RECOMPILER_CODEGEN_EMITTER_H
#define RECOMPILER_CODEGEN_EMITTER_H

#include "../ir/ir.h"
#include "../native_patch.h"
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

namespace gbrecomp {
namespace codegen {

/**
 * @brief Generator options
 */
struct GeneratorOptions {
    struct RamOverlay {
        uint16_t ram_addr = 0;
        std::vector<uint8_t> bytes;
    };

    std::string output_prefix = "rom";
    std::string output_dir = ".";
    
    bool emit_comments = true;           // Include disassembly comments
    bool emit_address_comments = true;   // Include address comments
    bool single_function_mode = false;   // All code in one function
    bool use_prefixed_symbols = false;   // Prefix all symbols (for multi-ROM)
    bool emit_main_entry_point = true;   // Emit a global main() wrapper
    bool emit_cmake = true;              // Emit a standalone CMakeLists.txt
    bool embed_rom_data = true;          // Embed ROM data in output
    bool debug_mode = false;             // Extra debug output
    size_t parallel_codegen_jobs = 0;    // 0 = auto, 1 = disabled
    
    // Cycle counting
    bool emit_cycle_counting = true;
    
    // Bank handling
    bool generate_bank_dispatch = true;  // Generate runtime bank dispatch

    // Known writable-memory overlays copied from ROM and safe to dispatch
    // when the live bytes still match the generated image.
    std::vector<RamOverlay> ram_overlays;

    // Optional exact-ROM native replacement package. Empty by default, so
    // generated execution keeps the ordinary direct wrapper-to-body path.
    NativePatchPackage native_patch;
};

/**
 * @brief Abstract base class for code emission
 * 
 * Implementations generate target-specific code from IR.
 */
class CodeEmitter {
public:
    virtual ~CodeEmitter() = default;
    
    // ========== Program Structure ==========
    
    virtual void begin_program(const std::string& name) = 0;
    virtual void end_program() = 0;
    
    virtual void begin_function(const std::string& name, BankId bank, uint16_t addr) = 0;
    virtual void end_function() = 0;
    
    virtual void emit_label(const std::string& label) = 0;
    
    // ========== Data Movement ==========
    
    // 8-bit register-to-register
    virtual void emit_mov_reg_reg(uint8_t dst, uint8_t src) = 0;
    
    // 8-bit immediate to register
    virtual void emit_mov_reg_imm8(uint8_t dst, uint8_t imm) = 0;
    
    // 16-bit immediate to register pair
    virtual void emit_mov_reg16_imm16(uint8_t dst, uint16_t imm) = 0;
    
    // Memory loads
    virtual void emit_load8_addr(uint8_t dst, uint16_t addr) = 0;
    virtual void emit_load8_reg(uint8_t dst, uint8_t addr_reg) = 0;
    virtual void emit_load16_addr(uint8_t dst, uint16_t addr) = 0;
    
    // Memory stores
    virtual void emit_store8_addr(uint16_t addr, uint8_t src) = 0;
    virtual void emit_store8_reg(uint8_t addr_reg, uint8_t src) = 0;
    virtual void emit_store16_addr(uint16_t addr, uint8_t src) = 0;
    
    // Stack operations
    virtual void emit_push(uint8_t reg16) = 0;
    virtual void emit_pop(uint8_t reg16) = 0;
    
    // ========== ALU Operations ==========
    
    // 8-bit arithmetic (A += src, etc.)
    virtual void emit_add_a_reg(uint8_t src) = 0;
    virtual void emit_add_a_imm(uint8_t imm) = 0;
    virtual void emit_adc_a_reg(uint8_t src) = 0;
    virtual void emit_adc_a_imm(uint8_t imm) = 0;
    virtual void emit_sub_a_reg(uint8_t src) = 0;
    virtual void emit_sub_a_imm(uint8_t imm) = 0;
    virtual void emit_sbc_a_reg(uint8_t src) = 0;
    virtual void emit_sbc_a_imm(uint8_t imm) = 0;
    virtual void emit_and_a_reg(uint8_t src) = 0;
    virtual void emit_and_a_imm(uint8_t imm) = 0;
    virtual void emit_or_a_reg(uint8_t src) = 0;
    virtual void emit_or_a_imm(uint8_t imm) = 0;
    virtual void emit_xor_a_reg(uint8_t src) = 0;
    virtual void emit_xor_a_imm(uint8_t imm) = 0;
    virtual void emit_cp_a_reg(uint8_t src) = 0;
    virtual void emit_cp_a_imm(uint8_t imm) = 0;
    
    // Increment/decrement
    virtual void emit_inc_reg8(uint8_t reg) = 0;
    virtual void emit_dec_reg8(uint8_t reg) = 0;
    virtual void emit_inc_reg16(uint8_t reg) = 0;
    virtual void emit_dec_reg16(uint8_t reg) = 0;
    virtual void emit_inc_mem_hl() = 0;
    virtual void emit_dec_mem_hl() = 0;
    
    // 16-bit arithmetic
    virtual void emit_add_hl_reg16(uint8_t src) = 0;
    virtual void emit_add_sp_imm8(int8_t offset) = 0;
    
    // ========== Bit Operations ==========
    
    virtual void emit_rlc_reg(uint8_t reg) = 0;
    virtual void emit_rrc_reg(uint8_t reg) = 0;
    virtual void emit_rl_reg(uint8_t reg) = 0;
    virtual void emit_rr_reg(uint8_t reg) = 0;
    virtual void emit_sla_reg(uint8_t reg) = 0;
    virtual void emit_sra_reg(uint8_t reg) = 0;
    virtual void emit_srl_reg(uint8_t reg) = 0;
    virtual void emit_swap_reg(uint8_t reg) = 0;
    
    virtual void emit_bit_n_reg(uint8_t bit, uint8_t reg) = 0;
    virtual void emit_set_n_reg(uint8_t bit, uint8_t reg) = 0;
    virtual void emit_res_n_reg(uint8_t bit, uint8_t reg) = 0;
    
    // Memory versions
    virtual void emit_rlc_hl() = 0;
    virtual void emit_rrc_hl() = 0;
    virtual void emit_rl_hl() = 0;
    virtual void emit_rr_hl() = 0;
    virtual void emit_sla_hl() = 0;
    virtual void emit_sra_hl() = 0;
    virtual void emit_srl_hl() = 0;
    virtual void emit_swap_hl() = 0;
    virtual void emit_bit_n_hl(uint8_t bit) = 0;
    virtual void emit_set_n_hl(uint8_t bit) = 0;
    virtual void emit_res_n_hl(uint8_t bit) = 0;
    
    // Non-CB rotates (affect only A, different flag behavior)
    virtual void emit_rlca() = 0;
    virtual void emit_rrca() = 0;
    virtual void emit_rla() = 0;
    virtual void emit_rra() = 0;
    
    // ========== Control Flow ==========
    
    virtual void emit_jump(const std::string& label) = 0;
    virtual void emit_jump_cc(uint8_t cc, const std::string& label, 
                              const std::string& fallthrough_label) = 0;
    virtual void emit_jump_hl() = 0;
    
    virtual void emit_jr(int8_t offset, const std::string& label) = 0;
    virtual void emit_jr_cc(uint8_t cc, int8_t offset, 
                            const std::string& label,
                            const std::string& fallthrough_label) = 0;
    
    virtual void emit_call(const std::string& func_name) = 0;
    virtual void emit_call_cc(uint8_t cc, const std::string& func_name,
                              const std::string& fallthrough_label) = 0;
    virtual void emit_rst(uint8_t vector) = 0;
    
    virtual void emit_ret() = 0;
    virtual void emit_ret_cc(uint8_t cc, const std::string& fallthrough_label) = 0;
    virtual void emit_reti() = 0;
    
    // Bank-aware calls
    virtual void emit_bank_call(BankId target_bank, const std::string& func_name) = 0;
    virtual void emit_bank_dispatch(uint16_t addr) = 0;
    
    // ========== Special ==========
    
    virtual void emit_nop() = 0;
    virtual void emit_halt(uint16_t next_pc) = 0;
    virtual void emit_stop() = 0;
    virtual void emit_di() = 0;
    virtual void emit_ei() = 0;
    virtual void emit_daa() = 0;
    virtual void emit_cpl() = 0;
    virtual void emit_ccf() = 0;
    virtual void emit_scf() = 0;
    
    // ========== I/O ==========
    
    virtual void emit_ldh_a_n(uint8_t offset) = 0;
    virtual void emit_ldh_n_a(uint8_t offset) = 0;
    virtual void emit_ldh_a_c() = 0;
    virtual void emit_ldh_c_a() = 0;
    
    // ========== Memory with increment/decrement ==========
    
    virtual void emit_ldi_a_hl() = 0;  // LD A, (HL+)
    virtual void emit_ldd_a_hl() = 0;  // LD A, (HL-)
    virtual void emit_ldi_hl_a() = 0;  // LD (HL+), A
    virtual void emit_ldd_hl_a() = 0;  // LD (HL-), A
    
    // ========== Debug/Comments ==========
    
    virtual void emit_comment(const std::string& comment) = 0;
    virtual void emit_source_location(BankId bank, uint16_t addr) = 0;
    
    // ========== Cycle Counting ==========
    
    virtual void emit_add_cycles(uint8_t cycles) = 0;
    virtual void emit_yield_check() = 0;
};

} // namespace codegen
} // namespace gbrecomp

#endif // RECOMPILER_CODEGEN_EMITTER_H
