/**
 * @file decoder.cpp
 * @brief SM83 instruction decoder implementation
 * 
 * Decodes all ~500 SM83 opcodes including CB-prefixed instructions.
 * The SM83 is a modified Z80/8080 hybrid used in the GameBoy.
 */

#include "recompiler/decoder.h"
#include "recompiler/rom.h"
#include <stdexcept>
#include <sstream>
#include <iomanip>

namespace gbrecomp {

/* ============================================================================
 * Helper Tables
 * ========================================================================== */

// Register lookup for bits 0-2 or 3-5 of opcodes
static constexpr Reg8 REG8_TABLE[8] = {
    Reg8::B, Reg8::C, Reg8::D, Reg8::E, 
    Reg8::H, Reg8::L, Reg8::HL_IND, Reg8::A
};

// 16-bit register pairs (for most instructions)
static constexpr Reg16 REG16_TABLE[4] = {
    Reg16::BC, Reg16::DE, Reg16::HL, Reg16::SP
};

// 16-bit register pairs (for PUSH/POP)
static constexpr Reg16 REG16_STACK_TABLE[4] = {
    Reg16::BC, Reg16::DE, Reg16::HL, Reg16::AF
};

// Condition lookup
static constexpr Condition COND_TABLE[4] = {
    Condition::NZ, Condition::Z, Condition::NC, Condition::C
};

/* ============================================================================
 * Decoder Implementation
 * ========================================================================== */

Decoder::Decoder(const ROM& rom) : rom_(rom) {}

Instruction Decoder::decode(uint32_t full_addr) const {
    uint8_t bank = static_cast<uint8_t>(full_addr >> 16);
    uint16_t addr = static_cast<uint16_t>(full_addr & 0xFFFF);
    return decode(addr, bank);
}

Instruction Decoder::decode(uint16_t addr, uint8_t bank) const {
    Instruction instr = {};
    instr.address = addr;
    instr.bank = bank;
    instr.length = 1;
    instr.cycles = 4;  // Default
    instr.is_cb_prefixed = false;
    
    // Read opcode
    uint8_t opcode = rom_.read_banked(bank, addr);
    instr.opcode = opcode;
    
    // Check for CB prefix
    if (opcode == 0xCB) {
        return decode_cb(addr, bank);
    }
    
    // Decode main instruction
    decode_main(instr, opcode, addr, bank);
    
    return instr;
}

void Decoder::decode_main(Instruction& instr, uint8_t opcode, 
                          uint16_t addr, uint8_t bank) const {
    switch (opcode) {
        // =========== 0x00-0x0F ===========
        case 0x00: // NOP
            instr.type = InstructionType::NOP;
            break;
            
        case 0x01: // LD BC,u16
            instr.type = InstructionType::LD_RR_NN;
            instr.reg16 = Reg16::BC;
            instr.imm16 = read_u16(addr + 1, bank);
            instr.length = 3;
            instr.cycles = 12;
            break;
            
        case 0x11: // LD DE,u16
            instr.type = InstructionType::LD_RR_NN;
            instr.reg16 = Reg16::DE;
            instr.imm16 = read_u16(addr + 1, bank);
            instr.length = 3;
            instr.cycles = 12;
            break;
            
        case 0x21: // LD HL,u16
            instr.type = InstructionType::LD_RR_NN;
            instr.reg16 = Reg16::HL;
            instr.imm16 = read_u16(addr + 1, bank);
            instr.length = 3;
            instr.cycles = 12;
            break;
            
        case 0x31: // LD SP,u16
            instr.type = InstructionType::LD_RR_NN;
            instr.reg16 = Reg16::SP;
            instr.imm16 = read_u16(addr + 1, bank);
            instr.length = 3;
            instr.cycles = 12;
            break;
            
        case 0x02: // LD (BC),A
            instr.type = InstructionType::LD_BC_A;
            instr.cycles = 8;
            break;
            
        case 0x12: // LD (DE),A
            instr.type = InstructionType::LD_DE_A;
            instr.cycles = 8;
            break;
            
        case 0x22: // LD (HL+),A
            instr.type = InstructionType::LD_HLI_A;
            instr.cycles = 8;
            break;
            
        case 0x32: // LD (HL-),A
            instr.type = InstructionType::LD_HLD_A;
            instr.cycles = 8;
            break;
            
        case 0x03: // INC BC
            instr.type = InstructionType::INC_RR;
            instr.reg16 = Reg16::BC;
            instr.cycles = 8;
            break;
            
        case 0x13: // INC DE
            instr.type = InstructionType::INC_RR;
            instr.reg16 = Reg16::DE;
            instr.cycles = 8;
            break;
            
        case 0x23: // INC HL
            instr.type = InstructionType::INC_RR;
            instr.reg16 = Reg16::HL;
            instr.cycles = 8;
            break;
            
        case 0x33: // INC SP
            instr.type = InstructionType::INC_RR;
            instr.reg16 = Reg16::SP;
            instr.cycles = 8;
            break;
            
        case 0x04: case 0x0C: case 0x14: case 0x1C:
        case 0x24: case 0x2C: case 0x3C: // INC r8
            instr.type = InstructionType::INC_R;
            instr.reg8_dst = REG8_TABLE[(opcode >> 3) & 0x07];
            break;
            
        case 0x34: // INC (HL)
            instr.type = InstructionType::INC_HL_IND;
            instr.cycles = 12;
            break;
            
        case 0x05: case 0x0D: case 0x15: case 0x1D:
        case 0x25: case 0x2D: case 0x3D: // DEC r8
            instr.type = InstructionType::DEC_R;
            instr.reg8_dst = REG8_TABLE[(opcode >> 3) & 0x07];
            break;
            
        case 0x35: // DEC (HL)
            instr.type = InstructionType::DEC_HL_IND;
            instr.cycles = 12;
            break;
            
        case 0x06: case 0x0E: case 0x16: case 0x1E:
        case 0x26: case 0x2E: case 0x3E: // LD r8, n
            instr.type = InstructionType::LD_R_N;
            instr.reg8_dst = REG8_TABLE[(opcode >> 3) & 0x07];
            instr.imm8 = rom_.read_banked(bank, addr + 1);
            instr.length = 2;
            instr.cycles = 8;
            break;
            
        case 0x36: // LD (HL), n
            instr.type = InstructionType::LD_HL_N;
            instr.imm8 = rom_.read_banked(bank, addr + 1);
            instr.length = 2;
            instr.cycles = 12;
            break;
            
        case 0x07: // RLCA
            instr.type = InstructionType::RLCA;
            break;
            
        case 0x17: // RLA
            instr.type = InstructionType::RLA;
            break;
            
        case 0x27: // DAA
            instr.type = InstructionType::DAA;
            break;
            
        case 0x37: // SCF
            instr.type = InstructionType::SCF;
            break;
            
        case 0x08: // LD (u16),SP
            instr.type = InstructionType::LD_NN_SP;
            instr.imm16 = read_u16(addr + 1, bank);
            instr.length = 3;
            instr.cycles = 20;
            break;
            
        case 0x18: // JR i8
            instr.type = InstructionType::JR_N;
            instr.offset = static_cast<int8_t>(rom_.read_banked(bank, addr + 1));
            instr.length = 2;
            instr.cycles = 12;
            instr.is_jump = true;
            break;
            
        case 0x28: // JR Z,i8
            instr.type = InstructionType::JR_CC_N;
            instr.condition = Condition::Z;
            instr.offset = static_cast<int8_t>(rom_.read_banked(bank, addr + 1));
            instr.length = 2;
            instr.cycles = 8;
            instr.cycles_branch = 12;
            instr.is_conditional = true;
            instr.is_jump = true;
            break;
            
        case 0x38: // JR C,i8
            instr.type = InstructionType::JR_CC_N;
            instr.condition = Condition::C;
            instr.offset = static_cast<int8_t>(rom_.read_banked(bank, addr + 1));
            instr.length = 2;
            instr.cycles = 8;
            instr.cycles_branch = 12;
            instr.is_conditional = true;
            instr.is_jump = true;
            break;
            
        case 0x20: // JR NZ,i8
            instr.type = InstructionType::JR_CC_N;
            instr.condition = Condition::NZ;
            instr.offset = static_cast<int8_t>(rom_.read_banked(bank, addr + 1));
            instr.length = 2;
            instr.cycles = 8;
            instr.cycles_branch = 12;
            instr.is_conditional = true;
            instr.is_jump = true;
            break;
            
        case 0x30: // JR NC,i8
            instr.type = InstructionType::JR_CC_N;
            instr.condition = Condition::NC;
            instr.offset = static_cast<int8_t>(rom_.read_banked(bank, addr + 1));
            instr.length = 2;
            instr.cycles = 8;
            instr.cycles_branch = 12;
            instr.is_conditional = true;
            instr.is_jump = true;
            break;
            
        case 0x09: // ADD HL,BC
            instr.type = InstructionType::ADD_HL_RR;
            instr.reg16 = Reg16::BC;
            instr.cycles = 8;
            break;
            
        case 0x19: // ADD HL,DE
            instr.type = InstructionType::ADD_HL_RR;
            instr.reg16 = Reg16::DE;
            instr.cycles = 8;
            break;
            
        case 0x29: // ADD HL,HL
            instr.type = InstructionType::ADD_HL_RR;
            instr.reg16 = Reg16::HL;
            instr.cycles = 8;
            break;
            
        case 0x39: // ADD HL,SP
            instr.type = InstructionType::ADD_HL_RR;
            instr.reg16 = Reg16::SP;
            instr.cycles = 8;
            break;
            
        case 0x0A: // LD A,(BC)
            instr.type = InstructionType::LD_A_BC;
            instr.cycles = 8;
            break;
            
        case 0x1A: // LD A,(DE)
            instr.type = InstructionType::LD_A_DE;
            instr.cycles = 8;
            break;
            
        case 0x2A: // LD A,(HL+)
            instr.type = InstructionType::LD_A_HLI;
            instr.cycles = 8;
            break;
            
        case 0x3A: // LD A,(HL-)
            instr.type = InstructionType::LD_A_HLD;
            instr.cycles = 8;
            break;
            
        case 0x0B: // DEC BC
            instr.type = InstructionType::DEC_RR;
            instr.reg16 = Reg16::BC;
            instr.cycles = 8;
            break;
            
        case 0x1B: // DEC DE
            instr.type = InstructionType::DEC_RR;
            instr.reg16 = Reg16::DE;
            instr.cycles = 8;
            break;
            
        case 0x2B: // DEC HL
            instr.type = InstructionType::DEC_RR;
            instr.reg16 = Reg16::HL;
            instr.cycles = 8;
            break;
            
        case 0x3B: // DEC SP
            instr.type = InstructionType::DEC_RR;
            instr.reg16 = Reg16::SP;
            instr.cycles = 8;
            break;
            
        case 0x0F: // RRCA
            instr.type = InstructionType::RRCA;
            break;
            
        case 0x1F: // RRA
            instr.type = InstructionType::RRA;
            break;
            
        case 0x2F: // CPL
            instr.type = InstructionType::CPL;
            break;
            
        case 0x3F: // CCF
            instr.type = InstructionType::CCF;
            break;
            
        // =========== 0x40-0x7F: LD r,r' (except HALT) ===========
        case 0x76: // HALT
            instr.type = InstructionType::HALT;
            break;
            
        case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x47:
        case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4F:
        case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x57:
        case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5F:
        case 0x60: case 0x61: case 0x62: case 0x63: case 0x64: case 0x65: case 0x67:
        case 0x68: case 0x69: case 0x6A: case 0x6B: case 0x6C: case 0x6D: case 0x6F:
        case 0x78: case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7D: case 0x7F:
            // LD r, r'
            instr.type = InstructionType::LD_R_R;
            instr.reg8_dst = REG8_TABLE[(opcode >> 3) & 0x07];
            instr.reg8_src = REG8_TABLE[opcode & 0x07];
            break;
            
        case 0x46: case 0x4E: case 0x56: case 0x5E:
        case 0x66: case 0x6E: case 0x7E: // LD r, (HL)
            instr.type = InstructionType::LD_R_HL;
            instr.reg8_dst = REG8_TABLE[(opcode >> 3) & 0x07];
            instr.cycles = 8;
            break;
            
        case 0x70: case 0x71: case 0x72: case 0x73:
        case 0x74: case 0x75: case 0x77: // LD (HL), r
            instr.type = InstructionType::LD_HL_R;
            instr.reg8_src = REG8_TABLE[opcode & 0x07];
            instr.cycles = 8;
            break;
            
        // =========== 0x80-0x87: ADD A,r ===========
        case 0x80: case 0x81: case 0x82: case 0x83:
        case 0x84: case 0x85: case 0x87:
            instr.type = InstructionType::ADD_A_R;
            instr.reg8_src = REG8_TABLE[opcode & 0x07];
            break;
            
        case 0x86: // ADD A,(HL)
            instr.type = InstructionType::ADD_A_HL;
            instr.cycles = 8;
            break;
            
        // =========== 0x88-0x8F: ADC A,r ===========
        case 0x88: case 0x89: case 0x8A: case 0x8B:
        case 0x8C: case 0x8D: case 0x8F:
            instr.type = InstructionType::ADC_A_R;
            instr.reg8_src = REG8_TABLE[opcode & 0x07];
            break;
            
        case 0x8E: // ADC A,(HL)
            instr.type = InstructionType::ADC_A_HL;
            instr.cycles = 8;
            break;
            
        // =========== 0x90-0x97: SUB r ===========
        case 0x90: case 0x91: case 0x92: case 0x93:
        case 0x94: case 0x95: case 0x97:
            instr.type = InstructionType::SUB_A_R;
            instr.reg8_src = REG8_TABLE[opcode & 0x07];
            break;
            
        case 0x96: // SUB (HL)
            instr.type = InstructionType::SUB_A_HL;
            instr.cycles = 8;
            break;
            
        // =========== 0x98-0x9F: SBC A,r ===========
        case 0x98: case 0x99: case 0x9A: case 0x9B:
        case 0x9C: case 0x9D: case 0x9F:
            instr.type = InstructionType::SBC_A_R;
            instr.reg8_src = REG8_TABLE[opcode & 0x07];
            break;
            
        case 0x9E: // SBC A,(HL)
            instr.type = InstructionType::SBC_A_HL;
            instr.cycles = 8;
            break;
            
        // =========== 0xA0-0xA7: AND r ===========
        case 0xA0: case 0xA1: case 0xA2: case 0xA3:
        case 0xA4: case 0xA5: case 0xA7:
            instr.type = InstructionType::AND_A_R;
            instr.reg8_src = REG8_TABLE[opcode & 0x07];
            break;
            
        case 0xA6: // AND (HL)
            instr.type = InstructionType::AND_A_HL;
            instr.cycles = 8;
            break;
            
        // =========== 0xA8-0xAF: XOR r ===========
        case 0xA8: case 0xA9: case 0xAA: case 0xAB:
        case 0xAC: case 0xAD: case 0xAF:
            instr.type = InstructionType::XOR_A_R;
            instr.reg8_src = REG8_TABLE[opcode & 0x07];
            break;
            
        case 0xAE: // XOR (HL)
            instr.type = InstructionType::XOR_A_HL;
            instr.cycles = 8;
            break;
            
        // =========== 0xB0-0xB7: OR r ===========
        case 0xB0: case 0xB1: case 0xB2: case 0xB3:
        case 0xB4: case 0xB5: case 0xB7:
            instr.type = InstructionType::OR_A_R;
            instr.reg8_src = REG8_TABLE[opcode & 0x07];
            break;
            
        case 0xB6: // OR (HL)
            instr.type = InstructionType::OR_A_HL;
            instr.cycles = 8;
            break;
            
        // =========== 0xB8-0xBF: CP r ===========
        case 0xB8: case 0xB9: case 0xBA: case 0xBB:
        case 0xBC: case 0xBD: case 0xBF:
            instr.type = InstructionType::CP_A_R;
            instr.reg8_src = REG8_TABLE[opcode & 0x07];
            break;
            
        case 0xBE: // CP (HL)
            instr.type = InstructionType::CP_A_HL;
            instr.cycles = 8;
            break;
            
        // =========== 0xC0-0xFF: Control flow, stack, misc ===========
        case 0xC0: // RET NZ
            instr.type = InstructionType::RET_CC;
            instr.condition = Condition::NZ;
            instr.cycles = 8;
            instr.cycles_branch = 20;
            instr.is_conditional = true;
            instr.is_return = true;
            break;
            
        case 0xC8: // RET Z
            instr.type = InstructionType::RET_CC;
            instr.condition = Condition::Z;
            instr.cycles = 8;
            instr.cycles_branch = 20;
            instr.is_conditional = true;
            instr.is_return = true;
            break;
            
        case 0xD0: // RET NC
            instr.type = InstructionType::RET_CC;
            instr.condition = Condition::NC;
            instr.cycles = 8;
            instr.cycles_branch = 20;
            instr.is_conditional = true;
            instr.is_return = true;
            break;
            
        case 0xD8: // RET C
            instr.type = InstructionType::RET_CC;
            instr.condition = Condition::C;
            instr.cycles = 8;
            instr.cycles_branch = 20;
            instr.is_conditional = true;
            instr.is_return = true;
            break;
            
        case 0xC9: // RET
            instr.type = InstructionType::RET;
            instr.cycles = 16;
            instr.is_return = true;
            break;
            
        case 0xD9: // RETI
            instr.type = InstructionType::RETI;
            instr.cycles = 16;
            instr.is_return = true;
            break;
            
        case 0xC1: // POP BC
            instr.type = InstructionType::POP;
            instr.reg16 = Reg16::BC;
            instr.cycles = 12;
            break;
            
        case 0xD1: // POP DE
            instr.type = InstructionType::POP;
            instr.reg16 = Reg16::DE;
            instr.cycles = 12;
            break;
            
        case 0xE1: // POP HL
            instr.type = InstructionType::POP;
            instr.reg16 = Reg16::HL;
            instr.cycles = 12;
            break;
            
        case 0xF1: // POP AF
            instr.type = InstructionType::POP;
            instr.reg16 = Reg16::AF;
            instr.cycles = 12;
            break;
            
        case 0xC5: // PUSH BC
            instr.type = InstructionType::PUSH;
            instr.reg16 = Reg16::BC;
            instr.cycles = 16;
            break;
            
        case 0xD5: // PUSH DE
            instr.type = InstructionType::PUSH;
            instr.reg16 = Reg16::DE;
            instr.cycles = 16;
            break;
            
        case 0xE5: // PUSH HL
            instr.type = InstructionType::PUSH;
            instr.reg16 = Reg16::HL;
            instr.cycles = 16;
            break;
            
        case 0xF5: // PUSH AF
            instr.type = InstructionType::PUSH;
            instr.reg16 = Reg16::AF;
            instr.cycles = 16;
            break;
            
        case 0xC2: // JP NZ,u16
            instr.type = InstructionType::JP_CC_NN;
            instr.condition = Condition::NZ;
            instr.imm16 = read_u16(addr + 1, bank);
            instr.length = 3;
            instr.cycles = 12;
            instr.cycles_branch = 16;
            instr.is_conditional = true;
            instr.is_jump = true;
            break;
            
        case 0xCA: // JP Z,u16
            instr.type = InstructionType::JP_CC_NN;
            instr.condition = Condition::Z;
            instr.imm16 = read_u16(addr + 1, bank);
            instr.length = 3;
            instr.cycles = 12;
            instr.cycles_branch = 16;
            instr.is_conditional = true;
            instr.is_jump = true;
            break;
            
        case 0xD2: // JP NC,u16
            instr.type = InstructionType::JP_CC_NN;
            instr.condition = Condition::NC;
            instr.imm16 = read_u16(addr + 1, bank);
            instr.length = 3;
            instr.cycles = 12;
            instr.cycles_branch = 16;
            instr.is_conditional = true;
            instr.is_jump = true;
            break;
            
        case 0xDA: // JP C,u16
            instr.type = InstructionType::JP_CC_NN;
            instr.condition = Condition::C;
            instr.imm16 = read_u16(addr + 1, bank);
            instr.length = 3;
            instr.cycles = 12;
            instr.cycles_branch = 16;
            instr.is_conditional = true;
            instr.is_jump = true;
            break;
            
        case 0xC3: // JP u16
            instr.type = InstructionType::JP_NN;
            instr.imm16 = read_u16(addr + 1, bank);
            instr.length = 3;
            instr.cycles = 16;
            instr.is_jump = true;
            break;
            
        case 0xE9: // JP HL
            instr.type = InstructionType::JP_HL;
            instr.is_jump = true;
            break;
            
        case 0xC4: // CALL NZ,u16
            instr.type = InstructionType::CALL_CC_NN;
            instr.condition = Condition::NZ;
            instr.imm16 = read_u16(addr + 1, bank);
            instr.length = 3;
            instr.cycles = 12;
            instr.cycles_branch = 24;
            instr.is_conditional = true;
            instr.is_call = true;
            break;
            
        case 0xCC: // CALL Z,u16
            instr.type = InstructionType::CALL_CC_NN;
            instr.condition = Condition::Z;
            instr.imm16 = read_u16(addr + 1, bank);
            instr.length = 3;
            instr.cycles = 12;
            instr.cycles_branch = 24;
            instr.is_conditional = true;
            instr.is_call = true;
            break;
            
        case 0xD4: // CALL NC,u16
            instr.type = InstructionType::CALL_CC_NN;
            instr.condition = Condition::NC;
            instr.imm16 = read_u16(addr + 1, bank);
            instr.length = 3;
            instr.cycles = 12;
            instr.cycles_branch = 24;
            instr.is_conditional = true;
            instr.is_call = true;
            break;
            
        case 0xDC: // CALL C,u16
            instr.type = InstructionType::CALL_CC_NN;
            instr.condition = Condition::C;
            instr.imm16 = read_u16(addr + 1, bank);
            instr.length = 3;
            instr.cycles = 12;
            instr.cycles_branch = 24;
            instr.is_conditional = true;
            instr.is_call = true;
            break;
            
        case 0xCD: // CALL u16
            instr.type = InstructionType::CALL_NN;
            instr.imm16 = read_u16(addr + 1, bank);
            instr.length = 3;
            instr.cycles = 24;
            instr.is_call = true;
            break;
            
        case 0xC6: // ADD A,u8
            instr.type = InstructionType::ADD_A_N;
            instr.imm8 = rom_.read_banked(bank, addr + 1);
            instr.length = 2;
            instr.cycles = 8;
            break;
            
        case 0xCE: // ADC A,u8
            instr.type = InstructionType::ADC_A_N;
            instr.imm8 = rom_.read_banked(bank, addr + 1);
            instr.length = 2;
            instr.cycles = 8;
            break;
            
        case 0xD6: // SUB u8
            instr.type = InstructionType::SUB_A_N;
            instr.imm8 = rom_.read_banked(bank, addr + 1);
            instr.length = 2;
            instr.cycles = 8;
            break;
            
        case 0xDE: // SBC A,u8
            instr.type = InstructionType::SBC_A_N;
            instr.imm8 = rom_.read_banked(bank, addr + 1);
            instr.length = 2;
            instr.cycles = 8;
            break;
            
        case 0xE6: // AND u8
            instr.type = InstructionType::AND_A_N;
            instr.imm8 = rom_.read_banked(bank, addr + 1);
            instr.length = 2;
            instr.cycles = 8;
            break;
            
        case 0xEE: // XOR u8
            instr.type = InstructionType::XOR_A_N;
            instr.imm8 = rom_.read_banked(bank, addr + 1);
            instr.length = 2;
            instr.cycles = 8;
            break;
            
        case 0xF6: // OR u8
            instr.type = InstructionType::OR_A_N;
            instr.imm8 = rom_.read_banked(bank, addr + 1);
            instr.length = 2;
            instr.cycles = 8;
            break;
            
        case 0xFE: // CP u8
            instr.type = InstructionType::CP_A_N;
            instr.imm8 = rom_.read_banked(bank, addr + 1);
            instr.length = 2;
            instr.cycles = 8;
            break;
            
        case 0xC7: case 0xCF: case 0xD7: case 0xDF:
        case 0xE7: case 0xEF: case 0xF7: case 0xFF: // RST vec
            instr.type = InstructionType::RST;
            instr.rst_vector = opcode & 0x38;
            instr.cycles = 16;
            instr.is_call = true;
            break;
            
        case 0xE0: // LDH (u8),A
            instr.type = InstructionType::LDH_N_A;
            instr.imm8 = rom_.read_banked(bank, addr + 1);
            instr.length = 2;
            instr.cycles = 12;
            break;
            
        case 0xF0: // LDH A,(u8)
            instr.type = InstructionType::LDH_A_N;
            instr.imm8 = rom_.read_banked(bank, addr + 1);
            instr.length = 2;
            instr.cycles = 12;
            break;
            
        case 0xE2: // LDH (C),A
            instr.type = InstructionType::LDH_C_A;
            instr.cycles = 8;
            break;
            
        case 0xF2: // LDH A,(C)
            instr.type = InstructionType::LDH_A_C;
            instr.cycles = 8;
            break;
            
        case 0xEA: // LD (u16),A
            instr.type = InstructionType::LD_NN_A;
            instr.imm16 = read_u16(addr + 1, bank);
            instr.length = 3;
            instr.cycles = 16;
            break;
            
        case 0xFA: // LD A,(u16)
            instr.type = InstructionType::LD_A_NN;
            instr.imm16 = read_u16(addr + 1, bank);
            instr.length = 3;
            instr.cycles = 16;
            break;
            
        case 0xE8: // ADD SP,i8
            instr.type = InstructionType::ADD_SP_N;
            instr.offset = static_cast<int8_t>(rom_.read_banked(bank, addr + 1));
            instr.length = 2;
            instr.cycles = 16;
            break;
            
        case 0xF8: // LD HL,SP+i8
            instr.type = InstructionType::LD_HL_SP_N;
            instr.offset = static_cast<int8_t>(rom_.read_banked(bank, addr + 1));
            instr.length = 2;
            instr.cycles = 12;
            break;
            
        case 0xF9: // LD SP,HL
            instr.type = InstructionType::LD_SP_HL;
            instr.cycles = 8;
            break;
            
        case 0xF3: // DI
            instr.type = InstructionType::DI;
            break;
            
        case 0xFB: // EI
            instr.type = InstructionType::EI;
            break;
            
        case 0x10: // STOP
            instr.type = InstructionType::STOP;
            instr.length = 2;  // STOP consumes the next byte
            break;
            
        // Undefined/illegal opcodes
        case 0xD3: case 0xDB: case 0xDD: case 0xE3: case 0xE4:
        case 0xEB: case 0xEC: case 0xED: case 0xF4: case 0xFC: case 0xFD:
            instr.type = InstructionType::UNDEFINED;
            break;
            
        default:
            instr.type = InstructionType::UNDEFINED;
            break;
    }
}

Instruction Decoder::decode_cb(uint16_t addr, uint8_t bank) const {
    Instruction instr = {};
    instr.address = addr;
    instr.bank = bank;
    instr.length = 2;
    instr.is_cb_prefixed = true;
    instr.opcode = 0xCB;
    
    uint8_t opcode = rom_.read_banked(bank, addr + 1);
    instr.cb_opcode = opcode;
    
    Reg8 reg = REG8_TABLE[opcode & 0x07];
    bool is_hl = (reg == Reg8::HL_IND);
    
    uint8_t op_type = (opcode >> 6) & 0x03;
    uint8_t bit = (opcode >> 3) & 0x07;
    
    instr.reg8_dst = reg;
    instr.reg8_src = reg;
    instr.bit_index = bit;
    instr.cycles = is_hl ? 16 : 8;
    
    switch (op_type) {
        case 0x00: // Rotates/shifts
            switch (bit) {
                case 0: instr.type = is_hl ? InstructionType::RLC_HL : InstructionType::RLC_R; break;
                case 1: instr.type = is_hl ? InstructionType::RRC_HL : InstructionType::RRC_R; break;
                case 2: instr.type = is_hl ? InstructionType::RL_HL : InstructionType::RL_R; break;
                case 3: instr.type = is_hl ? InstructionType::RR_HL : InstructionType::RR_R; break;
                case 4: instr.type = is_hl ? InstructionType::SLA_HL : InstructionType::SLA_R; break;
                case 5: instr.type = is_hl ? InstructionType::SRA_HL : InstructionType::SRA_R; break;
                case 6: instr.type = is_hl ? InstructionType::SWAP_HL : InstructionType::SWAP_R; break;
                case 7: instr.type = is_hl ? InstructionType::SRL_HL : InstructionType::SRL_R; break;
            }
            break;
            
        case 0x01: // BIT b,r
            instr.type = is_hl ? InstructionType::BIT_N_HL : InstructionType::BIT_N_R;
            instr.cycles = is_hl ? 12 : 8;  // BIT is faster
            break;
            
        case 0x02: // RES b,r
            instr.type = is_hl ? InstructionType::RES_N_HL : InstructionType::RES_N_R;
            break;
            
        case 0x03: // SET b,r
            instr.type = is_hl ? InstructionType::SET_N_HL : InstructionType::SET_N_R;
            break;
    }
    
    return instr;
}

uint16_t Decoder::read_u16(uint16_t addr, uint8_t bank) const {
    uint8_t lo = rom_.read_banked(bank, addr);
    uint8_t hi = rom_.read_banked(bank, addr + 1);
    return static_cast<uint16_t>(lo) | (static_cast<uint16_t>(hi) << 8);
}

/* ============================================================================
 * Instruction String Formatting
 * ========================================================================== */

const char* reg8_name(Reg8 r) {
    switch (r) {
        case Reg8::B: return "B";
        case Reg8::C: return "C";
        case Reg8::D: return "D";
        case Reg8::E: return "E";
        case Reg8::H: return "H";
        case Reg8::L: return "L";
        case Reg8::HL_IND: return "(HL)";
        case Reg8::A: return "A";
        default: return "?";
    }
}

const char* reg16_name(Reg16 r) {
    switch (r) {
        case Reg16::BC: return "BC";
        case Reg16::DE: return "DE";
        case Reg16::HL: return "HL";
        case Reg16::SP: return "SP";
        case Reg16::AF: return "AF";
        default: return "?";
    }
}

const char* condition_name(Condition c) {
    switch (c) {
        case Condition::NZ: return "NZ";
        case Condition::Z: return "Z";
        case Condition::NC: return "NC";
        case Condition::C: return "C";
        default: return "?";
    }
}

const char* instruction_type_name(InstructionType type) {
    switch (type) {
        case InstructionType::NOP: return "NOP";
        // Load instructions
        case InstructionType::LD_R_R: return "LD r,r";
        case InstructionType::LD_R_N: return "LD r,n";
        case InstructionType::LD_R_HL: return "LD r,(HL)";
        case InstructionType::LD_HL_R: return "LD (HL),r";
        case InstructionType::LD_HL_N: return "LD (HL),n";
        case InstructionType::LD_A_BC: return "LD A,(BC)";
        case InstructionType::LD_A_DE: return "LD A,(DE)";
        case InstructionType::LD_BC_A: return "LD (BC),A";
        case InstructionType::LD_DE_A: return "LD (DE),A";
        case InstructionType::LD_A_NN: return "LD A,(nn)";
        case InstructionType::LD_NN_A: return "LD (nn),A";
        case InstructionType::LDH_A_N: return "LDH A,(n)";
        case InstructionType::LDH_N_A: return "LDH (n),A";
        case InstructionType::LDH_A_C: return "LDH A,(C)";
        case InstructionType::LDH_C_A: return "LDH (C),A";
        case InstructionType::LD_A_HLI: return "LD A,(HL+)";
        case InstructionType::LD_A_HLD: return "LD A,(HL-)";
        case InstructionType::LD_HLI_A: return "LD (HL+),A";
        case InstructionType::LD_HLD_A: return "LD (HL-),A";
        case InstructionType::LD_RR_NN: return "LD rr,nn";
        case InstructionType::LD_SP_HL: return "LD SP,HL";
        case InstructionType::LD_NN_SP: return "LD (nn),SP";
        case InstructionType::LD_HL_SP_N: return "LD HL,SP+n";
        case InstructionType::PUSH: return "PUSH";
        case InstructionType::POP: return "POP";
        // ALU instructions
        case InstructionType::ADD_A_R: return "ADD A,r";
        case InstructionType::ADD_A_N: return "ADD A,n";
        case InstructionType::ADD_A_HL: return "ADD A,(HL)";
        case InstructionType::ADC_A_R: return "ADC A,r";
        case InstructionType::ADC_A_N: return "ADC A,n";
        case InstructionType::ADC_A_HL: return "ADC A,(HL)";
        case InstructionType::SUB_A_R: return "SUB r";
        case InstructionType::SUB_A_N: return "SUB n";
        case InstructionType::SUB_A_HL: return "SUB (HL)";
        case InstructionType::SBC_A_R: return "SBC A,r";
        case InstructionType::SBC_A_N: return "SBC A,n";
        case InstructionType::SBC_A_HL: return "SBC A,(HL)";
        case InstructionType::AND_A_R: return "AND r";
        case InstructionType::AND_A_N: return "AND n";
        case InstructionType::AND_A_HL: return "AND (HL)";
        case InstructionType::OR_A_R: return "OR r";
        case InstructionType::OR_A_N: return "OR n";
        case InstructionType::OR_A_HL: return "OR (HL)";
        case InstructionType::XOR_A_R: return "XOR r";
        case InstructionType::XOR_A_N: return "XOR n";
        case InstructionType::XOR_A_HL: return "XOR (HL)";
        case InstructionType::CP_A_R: return "CP r";
        case InstructionType::CP_A_N: return "CP n";
        case InstructionType::CP_A_HL: return "CP (HL)";
        case InstructionType::INC_R: return "INC r";
        case InstructionType::INC_HL_IND: return "INC (HL)";
        case InstructionType::DEC_R: return "DEC r";
        case InstructionType::DEC_HL_IND: return "DEC (HL)";
        case InstructionType::ADD_HL_RR: return "ADD HL,rr";
        case InstructionType::ADD_SP_N: return "ADD SP,n";
        case InstructionType::INC_RR: return "INC rr";
        case InstructionType::DEC_RR: return "DEC rr";
        // Rotate/shift instructions
        case InstructionType::RLCA: return "RLCA";
        case InstructionType::RRCA: return "RRCA";
        case InstructionType::RLA: return "RLA";
        case InstructionType::RRA: return "RRA";
        case InstructionType::RLC_R: return "RLC r";
        case InstructionType::RLC_HL: return "RLC (HL)";
        case InstructionType::RRC_R: return "RRC r";
        case InstructionType::RRC_HL: return "RRC (HL)";
        case InstructionType::RL_R: return "RL r";
        case InstructionType::RL_HL: return "RL (HL)";
        case InstructionType::RR_R: return "RR r";
        case InstructionType::RR_HL: return "RR (HL)";
        case InstructionType::SLA_R: return "SLA r";
        case InstructionType::SLA_HL: return "SLA (HL)";
        case InstructionType::SRA_R: return "SRA r";
        case InstructionType::SRA_HL: return "SRA (HL)";
        case InstructionType::SRL_R: return "SRL r";
        case InstructionType::SRL_HL: return "SRL (HL)";
        case InstructionType::SWAP_R: return "SWAP r";
        case InstructionType::SWAP_HL: return "SWAP (HL)";
        // Bit operations
        case InstructionType::BIT_N_R: return "BIT n,r";
        case InstructionType::BIT_N_HL: return "BIT n,(HL)";
        case InstructionType::SET_N_R: return "SET n,r";
        case InstructionType::SET_N_HL: return "SET n,(HL)";
        case InstructionType::RES_N_R: return "RES n,r";
        case InstructionType::RES_N_HL: return "RES n,(HL)";
        // Control flow
        case InstructionType::JP_NN: return "JP nn";
        case InstructionType::JP_CC_NN: return "JP cc,nn";
        case InstructionType::JP_HL: return "JP HL";
        case InstructionType::JR_N: return "JR n";
        case InstructionType::JR_CC_N: return "JR cc,n";
        case InstructionType::CALL_NN: return "CALL nn";
        case InstructionType::CALL_CC_NN: return "CALL cc,nn";
        case InstructionType::RET: return "RET";
        case InstructionType::RET_CC: return "RET cc";
        case InstructionType::RETI: return "RETI";
        case InstructionType::RST: return "RST";
        // Misc
        case InstructionType::HALT: return "HALT";
        case InstructionType::STOP: return "STOP";
        case InstructionType::DI: return "DI";
        case InstructionType::EI: return "EI";
        case InstructionType::DAA: return "DAA";
        case InstructionType::CPL: return "CPL";
        case InstructionType::CCF: return "CCF";
        case InstructionType::SCF: return "SCF";
        default: return "???";
    }
}

std::string disassemble(const Instruction& instr) {
    std::ostringstream ss;
    
    // Helper for formatting hex
    auto hex8 = [](uint8_t v) { 
        std::ostringstream s; s << "$" << std::hex << std::setfill('0') << std::setw(2) << (int)v; return s.str(); 
    };
    auto hex16 = [](uint16_t v) { 
        std::ostringstream s; s << "$" << std::hex << std::setfill('0') << std::setw(4) << v; return s.str(); 
    };
    
    switch (instr.type) {
        case InstructionType::NOP: return "NOP";
        case InstructionType::HALT: return "HALT";
        case InstructionType::STOP: return "STOP";
        case InstructionType::DI: return "DI";
        case InstructionType::EI: return "EI";
        case InstructionType::RET: return "RET";
        case InstructionType::RETI: return "RETI";
        case InstructionType::RLCA: return "RLCA";
        case InstructionType::RRCA: return "RRCA";
        case InstructionType::RLA: return "RLA";
        case InstructionType::RRA: return "RRA";
        case InstructionType::DAA: return "DAA";
        case InstructionType::CPL: return "CPL";
        case InstructionType::SCF: return "SCF";
        case InstructionType::CCF: return "CCF";
        case InstructionType::JP_HL: return "JP HL";
        case InstructionType::LD_SP_HL: return "LD SP,HL";
        
        // Jumps / Calls
        case InstructionType::JP_NN: return "JP " + hex16(instr.imm16);
        case InstructionType::CALL_NN: return "CALL " + hex16(instr.imm16);
        case InstructionType::JP_CC_NN: return "JP " + std::string(condition_name(instr.condition)) + "," + hex16(instr.imm16);
        case InstructionType::CALL_CC_NN: return "CALL " + std::string(condition_name(instr.condition)) + "," + hex16(instr.imm16);
        case InstructionType::RET_CC: return "RET " + std::string(condition_name(instr.condition));
        case InstructionType::RST: return "RST " + hex8(instr.rst_vector);
        
        // Relative Jumps
        case InstructionType::JR_N: return "JR " + std::to_string((int)instr.offset);
        case InstructionType::JR_CC_N: return "JR " + std::string(condition_name(instr.condition)) + "," + std::to_string((int)instr.offset);
        
        // 8-bit Loads
        case InstructionType::LD_R_N: return "LD " + std::string(reg8_name(instr.reg8_dst)) + "," + hex8(instr.imm8);
        case InstructionType::LD_R_R: return "LD " + std::string(reg8_name(instr.reg8_dst)) + "," + std::string(reg8_name(instr.reg8_src));
        case InstructionType::LD_R_HL: return "LD " + std::string(reg8_name(instr.reg8_dst)) + ",(HL)";
        case InstructionType::LD_HL_R: return "LD (HL)," + std::string(reg8_name(instr.reg8_src));
        case InstructionType::LD_HL_N: return "LD (HL)," + hex8(instr.imm8);
        case InstructionType::LD_A_BC: return "LD A,(BC)";
        case InstructionType::LD_A_DE: return "LD A,(DE)";
        case InstructionType::LD_BC_A: return "LD (BC),A";
        case InstructionType::LD_DE_A: return "LD (DE),A";
        case InstructionType::LD_A_NN: return "LD A,(" + hex16(instr.imm16) + ")";
        case InstructionType::LD_NN_A: return "LD (" + hex16(instr.imm16) + "),A";
        case InstructionType::LDH_A_N: return "LDH A,($FF00+" + hex8(instr.imm8) + ")";
        case InstructionType::LDH_N_A: return "LDH ($FF00+" + hex8(instr.imm8) + "),A";
        case InstructionType::LDH_A_C: return "LDH A,(C)";
        case InstructionType::LDH_C_A: return "LDH (C),A";
        case InstructionType::LD_A_HLI: return "LD A,(HL+)";
        case InstructionType::LD_A_HLD: return "LD A,(HL-)";
        case InstructionType::LD_HLI_A: return "LD (HL+),A";
        case InstructionType::LD_HLD_A: return "LD (HL-),A";
        
        // 16-bit Loads
        case InstructionType::LD_RR_NN: return "LD " + std::string(reg16_name(instr.reg16)) + "," + hex16(instr.imm16);
        case InstructionType::LD_NN_SP: return "LD (" + hex16(instr.imm16) + "),SP";
        case InstructionType::LD_HL_SP_N: return "LD HL,SP+" + std::to_string((int)instr.offset);
        case InstructionType::PUSH: return "PUSH " + std::string(reg16_name(instr.reg16));
        case InstructionType::POP: return "POP " + std::string(reg16_name(instr.reg16));
        
        // ALU
        case InstructionType::ADD_A_N: return "ADD A," + hex8(instr.imm8);
        case InstructionType::ADC_A_N: return "ADC A," + hex8(instr.imm8);
        case InstructionType::SUB_A_N: return "SUB " + hex8(instr.imm8);
        case InstructionType::SBC_A_N: return "SBC A," + hex8(instr.imm8);
        case InstructionType::AND_A_N: return "AND " + hex8(instr.imm8);
        case InstructionType::XOR_A_N: return "XOR " + hex8(instr.imm8);
        case InstructionType::OR_A_N: return "OR " + hex8(instr.imm8);
        case InstructionType::CP_A_N: return "CP " + hex8(instr.imm8);
        
        case InstructionType::ADD_A_R: return "ADD A," + std::string(reg8_name(instr.reg8_src));
        case InstructionType::ADC_A_R: return "ADC A," + std::string(reg8_name(instr.reg8_src));
        case InstructionType::SUB_A_R: return "SUB " + std::string(reg8_name(instr.reg8_src));
        case InstructionType::SBC_A_R: return "SBC A," + std::string(reg8_name(instr.reg8_src));
        case InstructionType::AND_A_R: return "AND " + std::string(reg8_name(instr.reg8_src));
        case InstructionType::XOR_A_R: return "XOR " + std::string(reg8_name(instr.reg8_src));
        case InstructionType::OR_A_R: return "OR " + std::string(reg8_name(instr.reg8_src));
        case InstructionType::CP_A_R: return "CP " + std::string(reg8_name(instr.reg8_src));
        
        case InstructionType::ADD_A_HL: return "ADD A,(HL)";
        case InstructionType::ADC_A_HL: return "ADC A,(HL)";
        case InstructionType::SUB_A_HL: return "SUB (HL)";
        case InstructionType::SBC_A_HL: return "SBC A,(HL)";
        case InstructionType::AND_A_HL: return "AND (HL)";
        case InstructionType::XOR_A_HL: return "XOR (HL)";
        case InstructionType::OR_A_HL: return "OR (HL)";
        case InstructionType::CP_A_HL: return "CP (HL)";
        
        case InstructionType::INC_R: return "INC " + std::string(reg8_name(instr.reg8_dst));
        case InstructionType::DEC_R: return "DEC " + std::string(reg8_name(instr.reg8_dst));
        case InstructionType::INC_HL_IND: return "INC (HL)";
        case InstructionType::DEC_HL_IND: return "DEC (HL)";
        case InstructionType::INC_RR: return "INC " + std::string(reg16_name(instr.reg16));
        case InstructionType::DEC_RR: return "DEC " + std::string(reg16_name(instr.reg16));
        
        case InstructionType::ADD_HL_RR: return "ADD HL," + std::string(reg16_name(instr.reg16));
        case InstructionType::ADD_SP_N: return "ADD SP," + std::to_string((int)instr.offset);
        
        // CB Prefix - simplified
        case InstructionType::BIT_N_R: return "BIT " + std::to_string((int)instr.bit_index) + "," + std::string(reg8_name(instr.reg8_dst));
        case InstructionType::BIT_N_HL: return "BIT " + std::to_string((int)instr.bit_index) + ",(HL)";
        // ... (others similar, using defaults for now if not explicit)
        
        default: break;
    }
    
    // Fallback to type name if not handled above
    ss << instruction_type_name(instr.type);
    
    return ss.str();
}

std::string Instruction::disassemble() const {
    return gbrecomp::disassemble(*this);
}

std::vector<Instruction> decode_bank(const uint8_t* rom, size_t rom_size, 
                                     uint8_t bank) {
    std::vector<Instruction> instructions;
    
    // Determine bank boundaries
    const size_t BANK_SIZE = 0x4000;  // 16KB
    size_t start_offset, end_offset;
    
    if (bank == 0) {
        start_offset = 0;
        end_offset = std::min(BANK_SIZE, rom_size);
    } else {
        start_offset = bank * BANK_SIZE;
        end_offset = std::min(start_offset + BANK_SIZE, rom_size);
    }
    
    if (start_offset >= rom_size) {
        return instructions;
    }
    
    // Decode instructions sequentially
    uint16_t addr = (bank == 0) ? 0x0000 : 0x4000;
    size_t offset = start_offset;
    
    while (offset < end_offset) {
        Instruction instr = {};
        instr.address = addr;
        instr.bank = bank;
        instr.length = 1;
        instr.cycles = 4;
        instr.is_cb_prefixed = false;
        
        uint8_t opcode = rom[offset];
        instr.opcode = opcode;
        
        // Handle CB prefix
        if (opcode == 0xCB && offset + 1 < end_offset) {
            instr.is_cb_prefixed = true;
            instr.opcode = rom[offset + 1];
            instr.length = 2;
            instr.type = InstructionType::NOP;  // Simplified for now
        } else {
            // Decode instruction length
            switch (opcode) {
                // 3-byte instructions
                case 0x01: case 0x11: case 0x21: case 0x31:  // LD rr,nn
                case 0x08:  // LD (nn),SP
                case 0xC2: case 0xC3: case 0xCA: case 0xD2: case 0xDA:  // JP
                case 0xC4: case 0xCC: case 0xCD: case 0xD4: case 0xDC:  // CALL
                case 0xEA: case 0xFA:  // LD (nn),A and LD A,(nn)
                    instr.length = 3;
                    instr.cycles = 12;
                    if (offset + 2 < end_offset) {
                        instr.imm16 = rom[offset + 1] | (rom[offset + 2] << 8);
                    }
                    break;
                    
                // 2-byte instructions
                case 0x06: case 0x0E: case 0x16: case 0x1E:  // LD r,n
                case 0x26: case 0x2E: case 0x36: case 0x3E:
                case 0x18: case 0x20: case 0x28: case 0x30: case 0x38:  // JR
                case 0xC6: case 0xCE: case 0xD6: case 0xDE:  // ALU A,n
                case 0xE6: case 0xEE: case 0xF6: case 0xFE:
                case 0xE0: case 0xF0: case 0xE8: case 0xF8:  // LDH, ADD SP,n
                    instr.length = 2;
                    if (offset + 1 < end_offset) {
                        instr.imm8 = rom[offset + 1];
                    }
                    break;
                    
                default:
                    instr.length = 1;
                    break;
            }
            
            // Simplified type assignment
            instr.type = InstructionType::NOP;  // Will be properly decoded later
        }
        
        instructions.push_back(instr);
        
        offset += instr.length;
        addr += instr.length;
    }
    
    return instructions;
}

} // namespace gbrecomp
