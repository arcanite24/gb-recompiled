/**
 * @file c_emitter.h
 * @brief C code generation backend (MVP)
 */

#ifndef RECOMPILER_CODEGEN_C_EMITTER_H
#define RECOMPILER_CODEGEN_C_EMITTER_H

#include "emitter.h"
#include <sstream>
#include <ostream>
#include <vector>

namespace gbrecomp {
namespace codegen {

/**
 * @brief C code emitter implementation
 * 
 * Generates portable C code that uses the gbrt runtime library.
 */
class CEmitter : public CodeEmitter {
public:
    explicit CEmitter(std::ostream& output, const GeneratorOptions& options = {});
    
    // ========== Program Structure ==========
    
    void begin_program(const std::string& name) override;
    void end_program() override;
    void begin_function(const std::string& name, BankId bank, uint16_t addr) override;
    void end_function() override;
    void emit_label(const std::string& label) override;
    
    // ========== Data Movement ==========
    
    void emit_mov_reg_reg(uint8_t dst, uint8_t src) override;
    void emit_mov_reg_imm8(uint8_t dst, uint8_t imm) override;
    void emit_mov_reg16_imm16(uint8_t dst, uint16_t imm) override;
    void emit_load8_addr(uint8_t dst, uint16_t addr) override;
    void emit_load8_reg(uint8_t dst, uint8_t addr_reg) override;
    void emit_load16_addr(uint8_t dst, uint16_t addr) override;
    void emit_store8_addr(uint16_t addr, uint8_t src) override;
    void emit_store8_reg(uint8_t addr_reg, uint8_t src) override;
    void emit_store16_addr(uint16_t addr, uint8_t src) override;
    void emit_push(uint8_t reg16) override;
    void emit_pop(uint8_t reg16) override;
    
    // ========== ALU Operations ==========
    
    void emit_add_a_reg(uint8_t src) override;
    void emit_add_a_imm(uint8_t imm) override;
    void emit_adc_a_reg(uint8_t src) override;
    void emit_adc_a_imm(uint8_t imm) override;
    void emit_sub_a_reg(uint8_t src) override;
    void emit_sub_a_imm(uint8_t imm) override;
    void emit_sbc_a_reg(uint8_t src) override;
    void emit_sbc_a_imm(uint8_t imm) override;
    void emit_and_a_reg(uint8_t src) override;
    void emit_and_a_imm(uint8_t imm) override;
    void emit_or_a_reg(uint8_t src) override;
    void emit_or_a_imm(uint8_t imm) override;
    void emit_xor_a_reg(uint8_t src) override;
    void emit_xor_a_imm(uint8_t imm) override;
    void emit_cp_a_reg(uint8_t src) override;
    void emit_cp_a_imm(uint8_t imm) override;
    void emit_inc_reg8(uint8_t reg) override;
    void emit_dec_reg8(uint8_t reg) override;
    void emit_inc_reg16(uint8_t reg) override;
    void emit_dec_reg16(uint8_t reg) override;
    void emit_inc_mem_hl() override;
    void emit_dec_mem_hl() override;
    void emit_add_hl_reg16(uint8_t src) override;
    void emit_add_sp_imm8(int8_t offset) override;
    
    // ========== Bit Operations ==========
    
    void emit_rlc_reg(uint8_t reg) override;
    void emit_rrc_reg(uint8_t reg) override;
    void emit_rl_reg(uint8_t reg) override;
    void emit_rr_reg(uint8_t reg) override;
    void emit_sla_reg(uint8_t reg) override;
    void emit_sra_reg(uint8_t reg) override;
    void emit_srl_reg(uint8_t reg) override;
    void emit_swap_reg(uint8_t reg) override;
    void emit_bit_n_reg(uint8_t bit, uint8_t reg) override;
    void emit_set_n_reg(uint8_t bit, uint8_t reg) override;
    void emit_res_n_reg(uint8_t bit, uint8_t reg) override;
    void emit_rlc_hl() override;
    void emit_rrc_hl() override;
    void emit_rl_hl() override;
    void emit_rr_hl() override;
    void emit_sla_hl() override;
    void emit_sra_hl() override;
    void emit_srl_hl() override;
    void emit_swap_hl() override;
    void emit_bit_n_hl(uint8_t bit) override;
    void emit_set_n_hl(uint8_t bit) override;
    void emit_res_n_hl(uint8_t bit) override;
    void emit_rlca() override;
    void emit_rrca() override;
    void emit_rla() override;
    void emit_rra() override;
    
    // ========== Control Flow ==========
    
    void emit_jump(const std::string& label) override;
    void emit_jump_cc(uint8_t cc, const std::string& label,
                      const std::string& fallthrough_label) override;
    void emit_jump_hl() override;
    void emit_jr(int8_t offset, const std::string& label) override;
    void emit_jr_cc(uint8_t cc, int8_t offset,
                    const std::string& label,
                    const std::string& fallthrough_label) override;
    void emit_call(const std::string& func_name) override;
    void emit_call_cc(uint8_t cc, const std::string& func_name,
                      const std::string& fallthrough_label) override;
    void emit_rst(uint8_t vector) override;
    void emit_ret() override;
    void emit_ret_cc(uint8_t cc, const std::string& fallthrough_label) override;
    void emit_reti() override;
    void emit_bank_call(BankId target_bank, const std::string& func_name) override;
    void emit_bank_dispatch(uint16_t addr) override;
    
    // ========== Special ==========
    
    void emit_nop() override;
    void emit_halt(uint16_t next_pc) override;
    void emit_stop() override;
    void emit_di() override;
    void emit_ei() override;
    void emit_daa() override;
    void emit_cpl() override;
    void emit_ccf() override;
    void emit_scf() override;
    
    // ========== I/O ==========
    
    void emit_ldh_a_n(uint8_t offset) override;
    void emit_ldh_n_a(uint8_t offset) override;
    void emit_ldh_a_c() override;
    void emit_ldh_c_a() override;
    
    // ========== Memory with increment/decrement ==========
    
    void emit_ldi_a_hl() override;
    void emit_ldd_a_hl() override;
    void emit_ldi_hl_a() override;
    void emit_ldd_hl_a() override;
    
    // ========== Debug/Comments ==========
    
    void emit_comment(const std::string& comment) override;
    void emit_source_location(BankId bank, uint16_t addr) override;
    
    // ========== Cycle Counting ==========
    
    void emit_add_cycles(uint8_t cycles) override;
    void emit_yield_check() override;

private:
    std::ostream& out_;
    GeneratorOptions options_;
    int indent_level_ = 0;
    std::string current_function_;
    
    void emit_indent();
    void emit_line(const std::string& line);
    
    static const char* reg8_name(uint8_t reg);
    static const char* reg16_name(uint8_t reg);
    static const char* condition_code(uint8_t cc);
    static const char* condition_expr(uint8_t cc);
};

/**
 * @brief Generate complete output from IR program
 */
struct GeneratedOutput {
    struct ExtraFile {
        std::string filename;
        std::string content;
        bool is_source = false;
    };

    std::string header_content;
    std::string source_content;
    std::string rom_data_content;
    std::string main_content;
    std::string cmake_content;
    std::vector<ExtraFile> extra_files;
    
    std::string header_file;
    std::string source_file;
    std::string rom_data_file;
    std::string main_file;
    std::string cmake_file;
    bool has_native_patch = false;
};

/**
 * @brief Generate all output files from IR program
 */
GeneratedOutput generate_output(const ir::Program& program,
                                const uint8_t* rom_data,
                                size_t rom_size,
                                const GeneratorOptions& options);

/**
 * @brief Write generated output to files
 */
bool write_output(const GeneratedOutput& output,
                  const std::string& output_dir);

} // namespace codegen
} // namespace gbrecomp

#endif // RECOMPILER_CODEGEN_C_EMITTER_H
