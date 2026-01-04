/**
 * @file ir_builder.cpp
 * @brief IR builder implementation (MVP stub)
 */

#include "recompiler/ir/ir_builder.h"
#include <sstream>
#include <iomanip>

namespace gbrecomp {
namespace ir {

/* ============================================================================
 * Operand Factory Methods (from ir.h)
 * ========================================================================== */

Operand Operand::reg8(uint8_t r) {
    Operand op;
    op.type = OperandType::REG8;
    op.value.reg8 = r;
    return op;
}

Operand Operand::reg16(uint8_t r) {
    Operand op;
    op.type = OperandType::REG16;
    op.value.reg16 = r;
    return op;
}

Operand Operand::imm8(uint8_t v) {
    Operand op;
    op.type = OperandType::IMM8;
    op.value.imm8 = v;
    return op;
}

Operand Operand::imm16(uint16_t v) {
    Operand op;
    op.type = OperandType::IMM16;
    op.value.imm16 = v;
    return op;
}

Operand Operand::offset(int8_t o) {
    Operand op;
    op.type = OperandType::OFFSET;
    op.value.offset = o;
    return op;
}

Operand Operand::condition(uint8_t c) {
    Operand op;
    op.type = OperandType::COND;
    op.value.condition = c;
    return op;
}

Operand Operand::bit_idx(uint8_t b) {
    Operand op;
    op.type = OperandType::BIT_IDX;
    op.value.bit_idx = b;
    return op;
}

Operand Operand::mem_reg16(uint8_t r) {
    Operand op;
    op.type = OperandType::MEM_REG16;
    op.value.reg16 = r;
    return op;
}

Operand Operand::mem_imm16(uint16_t addr) {
    Operand op;
    op.type = OperandType::MEM_IMM16;
    op.value.imm16 = addr;
    return op;
}

Operand Operand::io_offset(uint8_t off) {
    Operand op;
    op.type = OperandType::IO_OFFSET;
    op.value.io_offset = off;
    return op;
}

Operand Operand::label(uint32_t id) {
    Operand op;
    op.type = OperandType::LABEL_REF;
    op.value.label_id = id;
    return op;
}

Operand Operand::rst_vec(uint8_t vec) {
    Operand op;
    op.type = OperandType::RST_VEC;
    op.value.rst_vec = vec;
    return op;
}

/* ============================================================================
 * FlagEffects Factory Methods
 * ========================================================================== */

FlagEffects FlagEffects::none() {
    return FlagEffects{};
}

FlagEffects FlagEffects::znhc() {
    FlagEffects f{};
    f.affects_z = f.affects_n = f.affects_h = f.affects_c = true;
    return f;
}

FlagEffects FlagEffects::z0h0() {
    FlagEffects f{};
    f.affects_z = f.affects_n = f.affects_h = f.affects_c = true;
    f.fixed_n = f.fixed_c = true;
    f.n_value = f.c_value = false;
    return f;
}

FlagEffects FlagEffects::z1hc() {
    FlagEffects f{};
    f.affects_z = f.affects_n = f.affects_h = f.affects_c = true;
    f.fixed_n = true;
    f.n_value = true;
    return f;
}

FlagEffects FlagEffects::z0hc() {
    FlagEffects f{};
    f.affects_z = f.affects_n = f.affects_h = f.affects_c = true;
    f.fixed_n = true;
    f.n_value = false;
    return f;
}

FlagEffects FlagEffects::only_c() {
    FlagEffects f{};
    f.affects_c = true;
    return f;
}

/* ============================================================================
 * IRInstruction Factory Methods
 * ========================================================================== */

IRInstruction IRInstruction::make_nop(uint8_t bank, uint16_t addr) {
    IRInstruction instr;
    instr.opcode = Opcode::NOP;
    instr.source_bank = bank;
    instr.source_address = addr;
    instr.cycles = 4;
    return instr;
}

IRInstruction IRInstruction::make_mov_reg_reg(uint8_t dst, uint8_t src, 
                                              uint8_t bank, uint16_t addr) {
    IRInstruction instr;
    instr.opcode = Opcode::MOV_REG_REG;
    instr.dst = Operand::reg8(dst);
    instr.src = Operand::reg8(src);
    instr.source_bank = bank;
    instr.source_address = addr;
    instr.cycles = 4;
    return instr;
}

IRInstruction IRInstruction::make_load8(uint8_t dst_reg, uint16_t addr,
                                        uint8_t bank, uint16_t src_addr) {
    IRInstruction instr;
    instr.opcode = Opcode::LOAD8;
    instr.dst = Operand::reg8(dst_reg);
    instr.src = Operand::imm16(addr);
    instr.source_bank = bank;
    instr.source_address = src_addr;
    instr.cycles = 8;
    return instr;
}

IRInstruction IRInstruction::make_store8(uint16_t addr, uint8_t src_reg,
                                         uint8_t bank, uint16_t src_addr) {
    IRInstruction instr;
    instr.opcode = Opcode::STORE8;
    instr.dst = Operand::imm16(addr);
    instr.src = Operand::reg8(src_reg);
    instr.source_bank = bank;
    instr.source_address = src_addr;
    instr.cycles = 8;
    return instr;
}

IRInstruction IRInstruction::make_add8(uint8_t src, uint8_t bank, uint16_t addr) {
    IRInstruction instr;
    instr.opcode = Opcode::ADD8;
    instr.src = Operand::reg8(src);
    instr.source_bank = bank;
    instr.source_address = addr;
    instr.cycles = 4;
    instr.flags = FlagEffects::z0hc();
    return instr;
}

IRInstruction IRInstruction::make_jump(uint32_t label_id, uint8_t bank, uint16_t addr) {
    IRInstruction instr;
    instr.opcode = Opcode::JUMP;
    instr.dst = Operand::label(label_id);
    instr.source_bank = bank;
    instr.source_address = addr;
    instr.cycles = 16;
    return instr;
}

IRInstruction IRInstruction::make_jump_cc(uint8_t cond, uint32_t label_id,
                                          uint8_t bank, uint16_t addr) {
    IRInstruction instr;
    instr.opcode = Opcode::JUMP_CC;
    instr.dst = Operand::label(label_id);
    instr.src = Operand::condition(cond);
    instr.source_bank = bank;
    instr.source_address = addr;
    instr.cycles = 12;
    instr.cycles_branch_taken = 16;
    return instr;
}

IRInstruction IRInstruction::make_call(uint32_t label_id, uint8_t bank, uint16_t addr) {
    IRInstruction instr;
    instr.opcode = Opcode::CALL;
    instr.dst = Operand::label(label_id);
    instr.source_bank = bank;
    instr.source_address = addr;
    instr.cycles = 24;
    return instr;
}

IRInstruction IRInstruction::make_ret(uint8_t bank, uint16_t addr) {
    IRInstruction instr;
    instr.opcode = Opcode::RET;
    instr.source_bank = bank;
    instr.source_address = addr;
    instr.cycles = 16;
    return instr;
}

IRInstruction IRInstruction::make_label(uint32_t label_id) {
    IRInstruction instr;
    instr.opcode = Opcode::LABEL;
    instr.dst = Operand::label(label_id);
    return instr;
}

IRInstruction IRInstruction::make_comment(const std::string& text) {
    IRInstruction instr;
    instr.opcode = Opcode::NOP;  // Use NOP for comments
    instr.comment = text;
    return instr;
}

/* ============================================================================
 * Program Methods
 * ========================================================================== */

uint32_t Program::create_block(uint8_t bank, uint16_t addr) {
    uint32_t id = next_block_id++;
    BasicBlock block;
    block.id = id;
    block.bank = bank;
    block.start_address = addr;
    block.end_address = addr;
    block.label = make_address_label(bank, addr);
    blocks[id] = block;
    return id;
}

uint32_t Program::create_label(const std::string& name) {
    uint32_t id = next_label_id++;
    labels[id] = name;
    label_by_name[name] = id;
    return id;
}

uint32_t Program::get_or_create_label(const std::string& name) {
    auto it = label_by_name.find(name);
    if (it != label_by_name.end()) {
        return it->second;
    }
    return create_label(name);
}

std::string Program::get_label_name(uint32_t id) const {
    auto it = labels.find(id);
    if (it != labels.end()) {
        return it->second;
    }
    return "unknown_label";
}

std::string Program::make_address_label(uint8_t bank, uint16_t addr) const {
    std::ostringstream ss;
    ss << "loc_";
    if (bank > 0) {
        ss << std::hex << std::setfill('0') << std::setw(2) << (int)bank << "_";
    }
    ss << std::hex << std::setfill('0') << std::setw(4) << addr;
    return ss.str();
}

std::string Program::make_function_name(uint8_t bank, uint16_t addr) const {
    std::ostringstream ss;
    ss << "func_";
    if (bank > 0) {
        ss << std::hex << std::setfill('0') << std::setw(2) << (int)bank << "_";
    }
    ss << std::hex << std::setfill('0') << std::setw(4) << addr;
    return ss.str();
}

/* ============================================================================
 * IRBuilder Implementation
 * ========================================================================== */

IRBuilder::IRBuilder(const BuilderOptions& options) : options_(options) {}

Program IRBuilder::build(const AnalysisResult& analysis, const std::string& rom_name) {
    Program program;
    program.rom_name = rom_name;
    program.main_entry = analysis.entry_point;
    program.interrupt_vectors = analysis.interrupt_vectors;
    
    // For each function in analysis, create IR function
    for (const auto& [addr, func] : analysis.functions) {
        ir::Function ir_func;
        ir_func.name = func.name;
        ir_func.bank = func.bank;
        ir_func.entry_address = func.entry_address;
        ir_func.is_interrupt_handler = func.is_interrupt_handler;
        
        // Create a block for each block address in this function
        for (uint16_t block_addr : func.block_addresses) {
            uint32_t full_addr = (static_cast<uint32_t>(func.bank) << 16) | block_addr;
            auto block_it = analysis.blocks.find(full_addr);
            if (block_it == analysis.blocks.end()) continue;
            
            const gbrecomp::BasicBlock& src_block = block_it->second;
            
            // Create new IR block for this address
            uint32_t block_id = program.create_block(func.bank, block_addr);
            ir_func.block_ids.push_back(block_id);
            ir::BasicBlock& dst_block = program.blocks[block_id];
            
            // Copy end_address from source block for correct fallthrough handling
            dst_block.end_address = src_block.end_address;
            
            // Lower each instruction in the block
            for (size_t idx : src_block.instruction_indices) {
                if (idx < analysis.instructions.size()) {
                    const Instruction& instr = analysis.instructions[idx];
                    lower_instruction(instr, dst_block);
                }
            }
        }
        
        program.functions[func.name] = ir_func;
    }
    
    return program;
}

void IRBuilder::lower_instruction(const Instruction& instr, ir::BasicBlock& block) {
    // Add a comment with the disassembly
    if (options_.emit_comments) {
        IRInstruction comment;
        comment.opcode = Opcode::NOP;
        comment.comment = disassemble(instr);
        comment.source_bank = instr.bank;
        comment.source_address = instr.address;
        block.instructions.push_back(comment);
    }
    
    // Lower based on instruction type
    switch (instr.type) {
        case InstructionType::NOP:
            emit(block, IRInstruction::make_nop(instr.bank, instr.address), instr);
            break;
            
        case InstructionType::LD_R_R:
            lower_load_r_r(instr, block);
            break;
            
        case InstructionType::LD_R_N:
            lower_load_r_imm(instr, block);
            break;
            
        case InstructionType::LD_R_HL:
        case InstructionType::LD_A_BC:
        case InstructionType::LD_A_DE:
        case InstructionType::LD_A_NN:
        case InstructionType::LD_A_HLI:
        case InstructionType::LD_A_HLD:
        case InstructionType::LDH_A_N:
        case InstructionType::LDH_A_C:
            lower_load_mem(instr, block);
            break;
            
        case InstructionType::LD_HL_R:
        case InstructionType::LD_HL_N:
        case InstructionType::LD_BC_A:
        case InstructionType::LD_DE_A:
        case InstructionType::LD_NN_A:
        case InstructionType::LD_HLI_A:
        case InstructionType::LD_HLD_A:
        case InstructionType::LDH_N_A:
        case InstructionType::LDH_C_A:
            lower_store_mem(instr, block);
            break;
            
        case InstructionType::LD_RR_NN:
        case InstructionType::LD_SP_HL:
        case InstructionType::LD_NN_SP:
        case InstructionType::LD_HL_SP_N:
        case InstructionType::PUSH:
        case InstructionType::POP:
            lower_16bit_load(instr, block);
            break;
            
        case InstructionType::ADD_A_R:
        case InstructionType::ADC_A_R:
        case InstructionType::SUB_A_R:
        case InstructionType::SBC_A_R:
        case InstructionType::AND_A_R:
        case InstructionType::OR_A_R:
        case InstructionType::XOR_A_R:
        case InstructionType::CP_A_R:
        case InstructionType::ADD_A_HL:
        case InstructionType::ADC_A_HL:
        case InstructionType::SUB_A_HL:
        case InstructionType::SBC_A_HL:
        case InstructionType::AND_A_HL:
        case InstructionType::OR_A_HL:
        case InstructionType::XOR_A_HL:
        case InstructionType::CP_A_HL:
            lower_alu_r(instr, block);
            break;
            
        case InstructionType::ADD_A_N:
        case InstructionType::ADC_A_N:
        case InstructionType::SUB_A_N:
        case InstructionType::SBC_A_N:
        case InstructionType::AND_A_N:
        case InstructionType::OR_A_N:
        case InstructionType::XOR_A_N:
        case InstructionType::CP_A_N:
            lower_alu_imm(instr, block);
            break;
            
        case InstructionType::INC_R:
        case InstructionType::DEC_R:
        case InstructionType::INC_HL_IND:
        case InstructionType::DEC_HL_IND:
        case InstructionType::INC_RR:
        case InstructionType::DEC_RR:
            lower_inc_dec(instr, block);
            break;
            
        case InstructionType::ADD_HL_RR:
        case InstructionType::ADD_SP_N:
            lower_16bit_alu(instr, block);
            break;
            
        case InstructionType::RLCA:
        case InstructionType::RRCA:
        case InstructionType::RLA:
        case InstructionType::RRA:
        case InstructionType::RLC_R:
        case InstructionType::RRC_R:
        case InstructionType::RL_R:
        case InstructionType::RR_R:
        case InstructionType::SLA_R:
        case InstructionType::SRA_R:
        case InstructionType::SRL_R:
        case InstructionType::SWAP_R:
        case InstructionType::RLC_HL:
        case InstructionType::RRC_HL:
        case InstructionType::RL_HL:
        case InstructionType::RR_HL:
        case InstructionType::SLA_HL:
        case InstructionType::SRA_HL:
        case InstructionType::SRL_HL:
        case InstructionType::SWAP_HL:
            lower_rotate_shift(instr, block);
            break;
            
        case InstructionType::BIT_N_R:
        case InstructionType::BIT_N_HL:
        case InstructionType::SET_N_R:
        case InstructionType::SET_N_HL:
        case InstructionType::RES_N_R:
        case InstructionType::RES_N_HL:
            lower_bit_op(instr, block);
            break;
            
        case InstructionType::JP_NN:
        case InstructionType::JP_CC_NN:
            {
                IRInstruction ir;
                ir.opcode = instr.is_conditional ? Opcode::JUMP_CC : Opcode::JUMP;
                ir.dst = Operand::imm16(instr.imm16);
                if (instr.is_conditional) {
                    ir.src = Operand::condition(static_cast<uint8_t>(instr.condition));
                }
                ir.cycles = instr.cycles;
                ir.cycles_branch_taken = instr.cycles_branch;
                emit(block, ir, instr);
            }
            break;
            
        case InstructionType::JP_HL:
            {
                // JP HL is an indirect jump - target is in HL register
                IRInstruction ir;
                ir.opcode = Opcode::JUMP;
                ir.dst = Operand::reg16(2);  // HL = register index 2
                ir.cycles = instr.cycles;
                emit(block, ir, instr);
            }
            break;
            
        case InstructionType::JR_N:
        case InstructionType::JR_CC_N:
            {
                IRInstruction ir;
                ir.opcode = instr.is_conditional ? Opcode::JUMP_CC : Opcode::JUMP;
                // Calculate absolute target from relative offset
                uint16_t target = instr.address + instr.length + instr.offset;
                ir.dst = Operand::imm16(target);
                if (instr.is_conditional) {
                    ir.src = Operand::condition(static_cast<uint8_t>(instr.condition));
                }
                ir.cycles = instr.cycles;
                ir.cycles_branch_taken = instr.cycles_branch;
                emit(block, ir, instr);
            }
            break;
            
        case InstructionType::CALL_NN:
        case InstructionType::CALL_CC_NN:
        case InstructionType::RST:
            {
                IRInstruction ir;
                if (instr.type == InstructionType::RST) {
                    ir.opcode = Opcode::RST;
                    ir.dst = Operand::rst_vec(instr.rst_vector);
                } else {
                    ir.opcode = instr.is_conditional ? Opcode::CALL_CC : Opcode::CALL;
                    ir.dst = Operand::imm16(instr.imm16);
                    if (instr.is_conditional) {
                        ir.src = Operand::condition(static_cast<uint8_t>(instr.condition));
                    }
                }
                ir.cycles = instr.cycles;
                ir.cycles_branch_taken = instr.cycles_branch;
                emit(block, ir, instr);
            }
            break;
            
        case InstructionType::RET:
        case InstructionType::RET_CC:
        case InstructionType::RETI:
            lower_ret(instr, block);
            break;
            
        case InstructionType::DAA:
        case InstructionType::CPL:
        case InstructionType::SCF:
        case InstructionType::CCF:
        case InstructionType::HALT:
        case InstructionType::STOP:
        case InstructionType::DI:
        case InstructionType::EI:
            lower_misc(instr, block);
            break;
            
        default:
            // Emit NOP for unhandled instructions (stub)
            emit(block, IRInstruction::make_nop(instr.bank, instr.address), instr);
            break;
    }
}

void IRBuilder::emit(ir::BasicBlock& block, IRInstruction ir_instr, 
                     const Instruction& src) {
    if (options_.emit_source_locations) {
        ir_instr.source_bank = src.bank;
        ir_instr.source_address = src.address;
    }
    block.instructions.push_back(ir_instr);
}

// ========== Lowering Stubs ==========

void IRBuilder::lower_load_r_r(const Instruction& instr, ir::BasicBlock& block) {
    uint8_t dst = static_cast<uint8_t>(instr.reg8_dst);
    uint8_t src = static_cast<uint8_t>(instr.reg8_src);
    emit(block, IRInstruction::make_mov_reg_reg(dst, src, instr.bank, instr.address), instr);
}

void IRBuilder::lower_load_r_imm(const Instruction& instr, ir::BasicBlock& block) {
    IRInstruction ir;
    ir.opcode = Opcode::MOV_REG_IMM8;
    ir.dst = Operand::reg8(static_cast<uint8_t>(instr.reg8_dst));
    ir.src = Operand::imm8(instr.imm8);
    ir.cycles = instr.cycles;
    emit(block, ir, instr);
}

void IRBuilder::lower_load_mem(const Instruction& instr, ir::BasicBlock& block) {
    IRInstruction ir;
    ir.opcode = Opcode::LOAD8;
    ir.dst = Operand::reg8(7);  // A register
    
    // Set source address based on instruction type
    switch (instr.type) {
        case InstructionType::LD_A_NN:
            ir.src = Operand::imm16(instr.imm16);
            break;
        case InstructionType::LD_A_BC:
            ir.src = Operand::reg16(static_cast<uint8_t>(Reg16::BC));
            break;
        case InstructionType::LD_A_DE:
            ir.src = Operand::reg16(static_cast<uint8_t>(Reg16::DE));
            break;
        case InstructionType::LD_R_HL:
        case InstructionType::LD_A_HLI:
        case InstructionType::LD_A_HLD:
            ir.src = Operand::reg16(static_cast<uint8_t>(Reg16::HL));
            // For LD r,(HL), set the destination register properly
            if (instr.type == InstructionType::LD_R_HL) {
                ir.dst = Operand::reg8(static_cast<uint8_t>(instr.reg8_dst));
            }
            break;
        case InstructionType::LDH_A_N:
            ir.src = Operand::imm16(0xFF00 + instr.imm8);
            break;
        case InstructionType::LDH_A_C:
            // 0xFF00 + C - use special IO_READ_C opcode
            ir.opcode = Opcode::IO_READ_C;
            ir.dst = Operand::reg8(7);  // A register
            ir.cycles = instr.cycles;
            emit(block, ir, instr);
            return;  // Early return - already emitted
        default:
            break;
    }
    
    ir.cycles = instr.cycles;
    emit(block, ir, instr);
    
    // Handle HL increment/decrement for LDI/LDD instructions
    if (instr.type == InstructionType::LD_A_HLI) {
        // LD A,(HL+) - increment HL after load
        IRInstruction inc_ir;
        inc_ir.opcode = Opcode::INC16;
        inc_ir.dst = Operand::reg16(static_cast<uint8_t>(Reg16::HL));
        inc_ir.cycles = 0;  // Included in the original instruction's cycles
        emit(block, inc_ir, instr);
    } else if (instr.type == InstructionType::LD_A_HLD) {
        // LD A,(HL-) - decrement HL after load
        IRInstruction dec_ir;
        dec_ir.opcode = Opcode::DEC16;
        dec_ir.dst = Operand::reg16(static_cast<uint8_t>(Reg16::HL));
        dec_ir.cycles = 0;  // Included in the original instruction's cycles
        emit(block, dec_ir, instr);
    }
}

void IRBuilder::lower_store_mem(const Instruction& instr, ir::BasicBlock& block) {
    IRInstruction ir;
    ir.opcode = Opcode::STORE8;
    ir.src = Operand::reg8(static_cast<uint8_t>(Reg8::A));  // A register
    
    // Set destination address based on instruction type
    switch (instr.type) {
        case InstructionType::LD_NN_A:
            ir.dst = Operand::imm16(instr.imm16);
            break;
        case InstructionType::LD_BC_A:
            ir.dst = Operand::reg16(static_cast<uint8_t>(Reg16::BC));
            break;
        case InstructionType::LD_DE_A:
            ir.dst = Operand::reg16(static_cast<uint8_t>(Reg16::DE));
            break;
        case InstructionType::LD_HL_R:
        case InstructionType::LD_HL_N:
        case InstructionType::LD_HLI_A:
        case InstructionType::LD_HLD_A:
            ir.dst = Operand::reg16(static_cast<uint8_t>(Reg16::HL));
            if (instr.type == InstructionType::LD_HL_R) {
                ir.src = Operand::reg8(static_cast<uint8_t>(instr.reg8_src));
            } else if (instr.type == InstructionType::LD_HL_N) {
                ir.src = Operand::imm8(instr.imm8);
            }
            break;
        case InstructionType::LDH_N_A:
            ir.dst = Operand::imm16(0xFF00 + instr.imm8);
            break;
        case InstructionType::LDH_C_A:
            // 0xFF00 + C - use special IO_WRITE_C opcode
            ir.opcode = Opcode::IO_WRITE_C;
            ir.src = Operand::reg8(7);  // A register
            ir.cycles = instr.cycles;
            emit(block, ir, instr);
            return;  // Early return - already emitted
        default:
            break;
    }
    
    ir.cycles = instr.cycles;
    emit(block, ir, instr);
    
    // Handle HL increment/decrement for LDI/LDD instructions
    if (instr.type == InstructionType::LD_HLI_A) {
        // LD (HL+),A - increment HL after store
        IRInstruction inc_ir;
        inc_ir.opcode = Opcode::INC16;
        inc_ir.dst = Operand::reg16(static_cast<uint8_t>(Reg16::HL));
        inc_ir.cycles = 0;  // Included in the original instruction's cycles
        emit(block, inc_ir, instr);
    } else if (instr.type == InstructionType::LD_HLD_A) {
        // LD (HL-),A - decrement HL after store
        IRInstruction dec_ir;
        dec_ir.opcode = Opcode::DEC16;
        dec_ir.dst = Operand::reg16(static_cast<uint8_t>(Reg16::HL));
        dec_ir.cycles = 0;  // Included in the original instruction's cycles
        emit(block, dec_ir, instr);
    }
}

void IRBuilder::lower_alu_r(const Instruction& instr, ir::BasicBlock& block) {
    IRInstruction ir;
    switch (instr.type) {
        case InstructionType::ADD_A_R:
        case InstructionType::ADD_A_HL:
            ir.opcode = Opcode::ADD8;
            break;
        case InstructionType::ADC_A_R:
        case InstructionType::ADC_A_HL:
            ir.opcode = Opcode::ADC8;
            break;
        case InstructionType::SUB_A_R:
        case InstructionType::SUB_A_HL:
            ir.opcode = Opcode::SUB8;
            break;
        case InstructionType::SBC_A_R:
        case InstructionType::SBC_A_HL:
            ir.opcode = Opcode::SBC8;
            break;
        case InstructionType::AND_A_R:
        case InstructionType::AND_A_HL:
            ir.opcode = Opcode::AND8;
            break;
        case InstructionType::OR_A_R:
        case InstructionType::OR_A_HL:
            ir.opcode = Opcode::OR8;
            break;
        case InstructionType::XOR_A_R:
        case InstructionType::XOR_A_HL:
            ir.opcode = Opcode::XOR8;
            break;
        case InstructionType::CP_A_R:
        case InstructionType::CP_A_HL:
            ir.opcode = Opcode::CP8;
            break;
        default:
            ir.opcode = Opcode::NOP;
    }
    ir.src = Operand::reg8(static_cast<uint8_t>(instr.reg8_src));
    ir.cycles = instr.cycles;
    emit(block, ir, instr);
}

void IRBuilder::lower_alu_imm(const Instruction& instr, ir::BasicBlock& block) {
    IRInstruction ir;
    switch (instr.type) {
        case InstructionType::ADD_A_N: ir.opcode = Opcode::ADD8; break;
        case InstructionType::ADC_A_N: ir.opcode = Opcode::ADC8; break;
        case InstructionType::SUB_A_N: ir.opcode = Opcode::SUB8; break;
        case InstructionType::SBC_A_N: ir.opcode = Opcode::SBC8; break;
        case InstructionType::AND_A_N: ir.opcode = Opcode::AND8; break;
        case InstructionType::OR_A_N: ir.opcode = Opcode::OR8; break;
        case InstructionType::XOR_A_N: ir.opcode = Opcode::XOR8; break;
        case InstructionType::CP_A_N: ir.opcode = Opcode::CP8; break;
        default: ir.opcode = Opcode::NOP;
    }
    ir.src = Operand::imm8(instr.imm8);
    ir.cycles = instr.cycles;
    emit(block, ir, instr);
}

void IRBuilder::lower_inc_dec(const Instruction& instr, ir::BasicBlock& block) {
    IRInstruction ir;
    switch (instr.type) {
        case InstructionType::INC_R: ir.opcode = Opcode::INC8; break;
        case InstructionType::DEC_R: ir.opcode = Opcode::DEC8; break;
        case InstructionType::INC_HL_IND: ir.opcode = Opcode::INC8; break;  // Handle via memory
        case InstructionType::DEC_HL_IND: ir.opcode = Opcode::DEC8; break;  // Handle via memory
        case InstructionType::INC_RR: ir.opcode = Opcode::INC16; break;
        case InstructionType::DEC_RR: ir.opcode = Opcode::DEC16; break;
        default: ir.opcode = Opcode::NOP;
    }
    
    /* Use correct register type based on instruction */
    if (instr.type == InstructionType::INC_RR || instr.type == InstructionType::DEC_RR) {
        ir.dst = Operand::reg16(static_cast<uint8_t>(instr.reg16));
    } else if (instr.type == InstructionType::INC_HL_IND || instr.type == InstructionType::DEC_HL_IND) {
        // INC (HL) / DEC (HL) - use reg8 index 6 to indicate memory at (HL)
        ir.dst = Operand::reg8(6);  // 6 = HL_IND, signals memory operation to emitter
    } else {
        ir.dst = Operand::reg8(static_cast<uint8_t>(instr.reg8_dst));
    }
    ir.cycles = instr.cycles;
    emit(block, ir, instr);
}

void IRBuilder::lower_rotate_shift(const Instruction& instr, ir::BasicBlock& block) {
    IRInstruction ir;
    
    // Determine opcode based on instruction type
    switch (instr.type) {
        // Non-CB prefixed rotate instructions (always on A register)
        case InstructionType::RLCA:
            ir.opcode = Opcode::RLC;
            ir.dst = Operand::reg8(7);  // A register
            ir.extra = Operand::imm8(1);  // Flag to indicate RLCA variant (Z=0)
            break;
        case InstructionType::RRCA:
            ir.opcode = Opcode::RRC;
            ir.dst = Operand::reg8(7);
            ir.extra = Operand::imm8(1);
            break;
        case InstructionType::RLA:
            ir.opcode = Opcode::RL;
            ir.dst = Operand::reg8(7);
            ir.extra = Operand::imm8(1);
            break;
        case InstructionType::RRA:
            ir.opcode = Opcode::RR;
            ir.dst = Operand::reg8(7);
            ir.extra = Operand::imm8(1);
            break;
            
        // CB-prefixed rotate/shift instructions
        case InstructionType::RLC_R:
        case InstructionType::RLC_HL:
            ir.opcode = Opcode::RLC;
            ir.dst = Operand::reg8(static_cast<uint8_t>(instr.reg8_dst));
            break;
        case InstructionType::RRC_R:
        case InstructionType::RRC_HL:
            ir.opcode = Opcode::RRC;
            ir.dst = Operand::reg8(static_cast<uint8_t>(instr.reg8_dst));
            break;
        case InstructionType::RL_R:
        case InstructionType::RL_HL:
            ir.opcode = Opcode::RL;
            ir.dst = Operand::reg8(static_cast<uint8_t>(instr.reg8_dst));
            break;
        case InstructionType::RR_R:
        case InstructionType::RR_HL:
            ir.opcode = Opcode::RR;
            ir.dst = Operand::reg8(static_cast<uint8_t>(instr.reg8_dst));
            break;
        case InstructionType::SLA_R:
        case InstructionType::SLA_HL:
            ir.opcode = Opcode::SLA;
            ir.dst = Operand::reg8(static_cast<uint8_t>(instr.reg8_dst));
            break;
        case InstructionType::SRA_R:
        case InstructionType::SRA_HL:
            ir.opcode = Opcode::SRA;
            ir.dst = Operand::reg8(static_cast<uint8_t>(instr.reg8_dst));
            break;
        case InstructionType::SRL_R:
        case InstructionType::SRL_HL:
            ir.opcode = Opcode::SRL;
            ir.dst = Operand::reg8(static_cast<uint8_t>(instr.reg8_dst));
            break;
        case InstructionType::SWAP_R:
        case InstructionType::SWAP_HL:
            ir.opcode = Opcode::SWAP;
            ir.dst = Operand::reg8(static_cast<uint8_t>(instr.reg8_dst));
            break;
        default:
            ir.opcode = Opcode::NOP;
            break;
    }
    
    ir.cycles = instr.cycles;
    emit(block, ir, instr);
}

void IRBuilder::lower_bit_op(const Instruction& instr, ir::BasicBlock& block) {
    IRInstruction ir;
    switch (instr.type) {
        case InstructionType::BIT_N_R:
        case InstructionType::BIT_N_HL:
            ir.opcode = Opcode::BIT;
            break;
        case InstructionType::SET_N_R:
        case InstructionType::SET_N_HL:
            ir.opcode = Opcode::SET;
            break;
        case InstructionType::RES_N_R:
        case InstructionType::RES_N_HL:
            ir.opcode = Opcode::RES;
            break;
        default:
            ir.opcode = Opcode::NOP;
    }
    ir.dst = Operand::reg8(static_cast<uint8_t>(instr.reg8_dst));
    ir.src = Operand::bit_idx(instr.bit_index);
    ir.cycles = instr.cycles;
    emit(block, ir, instr);
}

void IRBuilder::lower_jump(const Instruction& instr, ir::BasicBlock& block, Program& prog) {
    IRInstruction ir;
    ir.opcode = instr.is_conditional ? Opcode::JUMP_CC : Opcode::JUMP;
    ir.dst = Operand::imm16(instr.imm16);
    if (instr.is_conditional) {
        ir.src = Operand::condition(static_cast<uint8_t>(instr.condition));
    }
    ir.cycles = instr.cycles;
    ir.cycles_branch_taken = instr.cycles_branch;
    emit(block, ir, instr);
}

void IRBuilder::lower_call(const Instruction& instr, ir::BasicBlock& block, Program& prog) {
    IRInstruction ir;
    if (instr.type == InstructionType::RST) {
        ir.opcode = Opcode::RST;
        ir.dst = Operand::rst_vec(instr.rst_vector);
    } else {
        ir.opcode = instr.is_conditional ? Opcode::CALL_CC : Opcode::CALL;
        ir.dst = Operand::imm16(instr.imm16);
        if (instr.is_conditional) {
            ir.src = Operand::condition(static_cast<uint8_t>(instr.condition));
        }
    }
    ir.cycles = instr.cycles;
    ir.cycles_branch_taken = instr.cycles_branch;
    emit(block, ir, instr);
}

void IRBuilder::lower_ret(const Instruction& instr, ir::BasicBlock& block) {
    IRInstruction ir;
    switch (instr.type) {
        case InstructionType::RET: ir.opcode = Opcode::RET; break;
        case InstructionType::RET_CC: 
            ir.opcode = Opcode::RET_CC;
            ir.src = Operand::condition(static_cast<uint8_t>(instr.condition));
            break;
        case InstructionType::RETI: ir.opcode = Opcode::RETI; break;
        default: ir.opcode = Opcode::NOP;
    }
    ir.cycles = instr.cycles;
    ir.cycles_branch_taken = instr.cycles_branch;
    emit(block, ir, instr);
}

void IRBuilder::lower_misc(const Instruction& instr, ir::BasicBlock& block) {
    IRInstruction ir;
    switch (instr.type) {
        case InstructionType::DAA: ir.opcode = Opcode::DAA; break;
        case InstructionType::CPL: ir.opcode = Opcode::CPL; break;
        case InstructionType::SCF: ir.opcode = Opcode::SCF; break;
        case InstructionType::CCF: ir.opcode = Opcode::CCF; break;
        case InstructionType::HALT: ir.opcode = Opcode::HALT; break;
        case InstructionType::STOP: ir.opcode = Opcode::STOP; break;
        case InstructionType::DI: ir.opcode = Opcode::DI; break;
        case InstructionType::EI: ir.opcode = Opcode::EI; break;
        default: ir.opcode = Opcode::NOP;
    }
    ir.cycles = instr.cycles;
    emit(block, ir, instr);
}

void IRBuilder::lower_io(const Instruction& instr, ir::BasicBlock& block) {
    // Stub
    emit(block, IRInstruction::make_nop(instr.bank, instr.address), instr);
}

void IRBuilder::lower_16bit_load(const Instruction& instr, ir::BasicBlock& block) {
    IRInstruction ir;
    switch (instr.type) {
        case InstructionType::LD_RR_NN:
            ir.opcode = Opcode::MOV_REG_IMM16;
            ir.dst = Operand::reg16(static_cast<uint8_t>(instr.reg16));
            ir.src = Operand::imm16(instr.imm16);
            break;
        case InstructionType::PUSH:
            ir.opcode = Opcode::PUSH16;
            ir.dst = Operand::reg16(static_cast<uint8_t>(instr.reg16));
            break;
        case InstructionType::POP:
            ir.opcode = Opcode::POP16;
            ir.dst = Operand::reg16(static_cast<uint8_t>(instr.reg16));
            break;
        default:
            ir.opcode = Opcode::NOP;
    }
    ir.cycles = instr.cycles;
    emit(block, ir, instr);
}

void IRBuilder::lower_16bit_alu(const Instruction& instr, ir::BasicBlock& block) {
    IRInstruction ir;
    switch (instr.type) {
        case InstructionType::ADD_HL_RR:
            ir.opcode = Opcode::ADD16;
            ir.dst = Operand::reg16(2);  // HL
            ir.src = Operand::reg16(static_cast<uint8_t>(instr.reg16));
            break;
        case InstructionType::ADD_SP_N:
            ir.opcode = Opcode::ADD_SP_IMM8;
            ir.src = Operand::offset(instr.offset);
            break;
        default:
            ir.opcode = Opcode::NOP;
    }
    ir.cycles = instr.cycles;
    emit(block, ir, instr);
}

/* ============================================================================
 * IR Utilities
 * ========================================================================== */

const char* opcode_name(Opcode op) {
    switch (op) {
        case Opcode::NOP: return "NOP";
        case Opcode::MOV_REG_REG: return "MOV_REG_REG";
        case Opcode::MOV_REG_IMM8: return "MOV_REG_IMM8";
        case Opcode::MOV_REG_IMM16: return "MOV_REG_IMM16";
        case Opcode::LOAD8: return "LOAD8";
        case Opcode::LOAD8_REG: return "LOAD8_REG";
        case Opcode::STORE8: return "STORE8";
        case Opcode::STORE8_REG: return "STORE8_REG";
        case Opcode::ADD8: return "ADD8";
        case Opcode::SUB8: return "SUB8";
        case Opcode::AND8: return "AND8";
        case Opcode::OR8: return "OR8";
        case Opcode::XOR8: return "XOR8";
        case Opcode::CP8: return "CP8";
        case Opcode::INC8: return "INC8";
        case Opcode::DEC8: return "DEC8";
        case Opcode::ADD16: return "ADD16";
        case Opcode::INC16: return "INC16";
        case Opcode::DEC16: return "DEC16";
        case Opcode::JUMP: return "JUMP";
        case Opcode::JUMP_CC: return "JUMP_CC";
        case Opcode::CALL: return "CALL";
        case Opcode::CALL_CC: return "CALL_CC";
        case Opcode::RET: return "RET";
        case Opcode::RET_CC: return "RET_CC";
        case Opcode::RETI: return "RETI";
        case Opcode::RST: return "RST";
        case Opcode::PUSH16: return "PUSH16";
        case Opcode::POP16: return "POP16";
        case Opcode::HALT: return "HALT";
        case Opcode::STOP: return "STOP";
        case Opcode::DI: return "DI";
        case Opcode::EI: return "EI";
        case Opcode::DAA: return "DAA";
        case Opcode::CPL: return "CPL";
        case Opcode::SCF: return "SCF";
        case Opcode::CCF: return "CCF";
        case Opcode::BIT: return "BIT";
        case Opcode::SET: return "SET";
        case Opcode::RES: return "RES";
        default: return "???";
    }
}

std::string format_instruction(const IRInstruction& instr) {
    std::ostringstream ss;
    ss << opcode_name(instr.opcode);
    if (!instr.comment.empty()) {
        ss << " ; " << instr.comment;
    }
    return ss.str();
}

void dump_program(const Program& program, std::ostream& out) {
    out << "Program: " << program.rom_name << "\n";
    out << "Functions: " << program.functions.size() << "\n";
    out << "Blocks: " << program.blocks.size() << "\n";
}

} // namespace ir
} // namespace gbrecomp
