/**
 * @file gbrt.c
 * @brief GameBoy Runtime Library Implementation
 */

#include "gbrt.h"
#include "ppu.h"
#include "gbrt_debug.h"
#include "platform_sdl.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * Memory Map Constants
 * ========================================================================== */

#define ROM_BANK0_START    0x0000
#define ROM_BANK0_END      0x3FFF
#define ROM_BANKN_START    0x4000
#define ROM_BANKN_END      0x7FFF
#define VRAM_START         0x8000
#define VRAM_END           0x9FFF
#define ERAM_START         0xA000
#define ERAM_END           0xBFFF
#define WRAM_BANK0_START   0xC000
#define WRAM_BANK0_END     0xCFFF
#define WRAM_BANKN_START   0xD000
#define WRAM_BANKN_END     0xDFFF
#define ECHO_START         0xE000
#define ECHO_END           0xFDFF
#define OAM_START          0xFE00
#define OAM_END            0xFE9F
#define UNUSABLE_START     0xFEA0
#define UNUSABLE_END       0xFEFF
#define IO_START           0xFF00
#define IO_END             0xFF7F
#define HRAM_START         0xFF80
#define HRAM_END           0xFFFE
#define IE_REG             0xFFFF

#define ROM_BANK_SIZE      0x4000
#define VRAM_SIZE          0x2000
#define ERAM_BANK_SIZE     0x2000
#define WRAM_BANK_SIZE     0x1000
#define OAM_SIZE           0xA0
#define IO_SIZE            0x80
#define HRAM_SIZE          0x7F

/* ============================================================================
 * Context Management
 * ========================================================================== */

GBContext* gb_context_create(const GBConfig* config) {
    GBContext* ctx = (GBContext*)calloc(1, sizeof(GBContext));
    if (!ctx) return NULL;
    
    /* Allocate memory regions */
    ctx->wram = (uint8_t*)calloc(1, WRAM_BANK_SIZE * 8);  /* 8 banks for CGB */
    ctx->vram = (uint8_t*)calloc(1, VRAM_SIZE * 2);       /* 2 banks for CGB */
    ctx->oam = (uint8_t*)calloc(1, OAM_SIZE);
    ctx->hram = (uint8_t*)calloc(1, HRAM_SIZE);
    ctx->io = (uint8_t*)calloc(1, IO_SIZE + 1);  /* +1 for IE register */
    
    if (!ctx->wram || !ctx->vram || !ctx->oam || !ctx->hram || !ctx->io) {
        gb_context_destroy(ctx);
        return NULL;
    }
    
    /* Allocate and initialize PPU */
    GBPPU* ppu = (GBPPU*)calloc(1, sizeof(GBPPU));
    if (!ppu) {
        gb_context_destroy(ctx);
        return NULL;
    }
    ppu_init(ppu);
    ctx->ppu = ppu;
    
    /* Initialize to post-bootrom state */
    gb_context_reset(ctx, true);
    
    (void)config;
    return ctx;
}

void gb_context_destroy(GBContext* ctx) {
    if (!ctx) return;
    
    free(ctx->wram);
    free(ctx->vram);
    free(ctx->oam);
    free(ctx->hram);
    free(ctx->io);
    free(ctx->eram);
    free(ctx->ppu);
    free(ctx);
}

void gb_context_reset(GBContext* ctx, bool skip_bootrom) {
    if (skip_bootrom) {
        /* DMG post-bootrom state */
        ctx->af = 0x01B0;  /* A=0x01 indicates DMG */
        ctx->bc = 0x0013;
        ctx->de = 0x00D8;
        ctx->hl = 0x014D;
        ctx->sp = 0xFFFE;
        ctx->pc = 0x0100;
        
        gb_unpack_flags(ctx);
        
        ctx->ime = 0;
        ctx->halted = 0;
        ctx->stopped = 0;
        
        /* Initialize I/O registers */
        ctx->io[0x00] = 0xCF;  /* P1/JOYP */
        ctx->io[0x01] = 0x00;  /* SB */
        ctx->io[0x02] = 0x7E;  /* SC */
        ctx->io[0x04] = 0xAB;  /* DIV */
        ctx->io[0x05] = 0x00;  /* TIMA */
        ctx->io[0x06] = 0x00;  /* TMA */
        ctx->io[0x07] = 0xF8;  /* TAC */
        ctx->io[0x0F] = 0xE1;  /* IF */
        ctx->io[0x10] = 0x80;  /* NR10 */
        ctx->io[0x11] = 0xBF;  /* NR11 */
        ctx->io[0x12] = 0xF3;  /* NR12 */
        ctx->io[0x14] = 0xBF;  /* NR14 */
        ctx->io[0x16] = 0x3F;  /* NR21 */
        ctx->io[0x17] = 0x00;  /* NR22 */
        ctx->io[0x19] = 0xBF;  /* NR24 */
        ctx->io[0x1A] = 0x7F;  /* NR30 */
        ctx->io[0x1B] = 0xFF;  /* NR31 */
        ctx->io[0x1C] = 0x9F;  /* NR32 */
        ctx->io[0x1E] = 0xBF;  /* NR34 */
        ctx->io[0x20] = 0xFF;  /* NR41 */
        ctx->io[0x21] = 0x00;  /* NR42 */
        ctx->io[0x22] = 0x00;  /* NR43 */
        ctx->io[0x23] = 0xBF;  /* NR44 */
        ctx->io[0x24] = 0x77;  /* NR50 */
        ctx->io[0x25] = 0xF3;  /* NR51 */
        ctx->io[0x26] = 0xF1;  /* NR52 */
        ctx->io[0x40] = 0x91;  /* LCDC */
        ctx->io[0x41] = 0x85;  /* STAT */
        ctx->io[0x42] = 0x00;  /* SCY */
        ctx->io[0x43] = 0x00;  /* SCX */
        ctx->io[0x44] = 0x00;  /* LY */
        ctx->io[0x45] = 0x00;  /* LYC */
        ctx->io[0x47] = 0xFC;  /* BGP */
        ctx->io[0x48] = 0xFF;  /* OBP0 */
        ctx->io[0x49] = 0xFF;  /* OBP1 */
        ctx->io[0x4A] = 0x00;  /* WY */
        ctx->io[0x4B] = 0x00;  /* WX */
        ctx->io[0x80] = 0x00;  /* IE - at offset 0x80 in our io array */
    } else {
        /* Start at bootrom */
        ctx->pc = 0x0000;
    }
    
    ctx->rom_bank = 1;
    ctx->ram_bank = 0;
    ctx->wram_bank = 1;
    ctx->vram_bank = 0;
    ctx->ram_enabled = 0;
    ctx->mbc_mode = 0;
    
    ctx->cycles = 0;
    ctx->frame_cycles = 0;
}

bool gb_context_load_rom(GBContext* ctx, const uint8_t* data, size_t size) {
    ctx->rom = (uint8_t*)malloc(size);
    if (!ctx->rom) return false;
    
    memcpy(ctx->rom, data, size);
    ctx->rom_size = size;
    
    /* Detect MBC type from header */
    if (size >= 0x148) {
        ctx->mbc_type = data[0x147];
        
        /* Allocate external RAM based on header */
        uint8_t ram_size_code = data[0x149];
        size_t ram_size = 0;
        switch (ram_size_code) {
            case 0x00: ram_size = 0; break;
            case 0x01: ram_size = 2 * 1024; break;
            case 0x02: ram_size = 8 * 1024; break;
            case 0x03: ram_size = 32 * 1024; break;
            case 0x04: ram_size = 128 * 1024; break;
            case 0x05: ram_size = 64 * 1024; break;
        }
        
        /* MBC2 has built-in RAM */
        if (ctx->mbc_type == 0x05 || ctx->mbc_type == 0x06) {
            ram_size = 512;
        }
        
        if (ram_size > 0) {
            ctx->eram = (uint8_t*)calloc(1, ram_size);
            ctx->eram_size = ram_size;
        }
    }
    
    DBG_GENERAL("ROM loaded: size=%zu, MBC=0x%02X, RAM size=%zu",
                size, ctx->mbc_type, ctx->eram_size);
    
    /* Debug: dump first few bytes of ROM at offset 0x1000 (common tile data location) */
    if (size > 0x1050) {
        DBG_GENERAL("ROM[0x1000..0x1010]: %02X %02X %02X %02X %02X %02X %02X %02X...",
                    data[0x1000], data[0x1001], data[0x1002], data[0x1003],
                    data[0x1004], data[0x1005], data[0x1006], data[0x1007]);
    }
    
    return true;
}

/* ============================================================================
 * Memory Access
 * ========================================================================== */

uint8_t gb_read8(GBContext* ctx, uint16_t addr) {
    if (addr <= ROM_BANK0_END) {
        /* ROM Bank 0 */
        return ctx->rom ? ctx->rom[addr] : 0xFF;
    }
    else if (addr <= ROM_BANKN_END) {
        /* ROM Bank N */
        size_t offset = (size_t)(ctx->rom_bank) * ROM_BANK_SIZE + (addr - ROM_BANKN_START);
        if (addr == 0x4A07) {
            DBG_GENERAL("READ 0x4A07! Bank=%d, Offset=0x%zX, Value=0x%02X", 
                        ctx->rom_bank, offset, (ctx->rom && offset < ctx->rom_size) ? ctx->rom[offset] : 0xFF);
        }
        return (ctx->rom && offset < ctx->rom_size) ? ctx->rom[offset] : 0xFF;
    }
    else if (addr <= VRAM_END) {
        /* VRAM */
        return ctx->vram[addr - VRAM_START + ctx->vram_bank * VRAM_SIZE];
    }
    else if (addr <= ERAM_END) {
        /* External RAM */
        if (ctx->ram_enabled && ctx->eram) {
            size_t offset = (size_t)(ctx->ram_bank) * ERAM_BANK_SIZE + (addr - ERAM_START);
            return (offset < ctx->eram_size) ? ctx->eram[offset] : 0xFF;
        }
        return 0xFF;
    }
    else if (addr <= WRAM_BANK0_END) {
        /* WRAM Bank 0 */
        return ctx->wram[addr - WRAM_BANK0_START];
    }
    else if (addr <= WRAM_BANKN_END) {
        /* WRAM Bank N */
        return ctx->wram[(addr - WRAM_BANKN_START) + ctx->wram_bank * WRAM_BANK_SIZE];
    }
    else if (addr <= ECHO_END) {
        /* Echo RAM */
        return gb_read8(ctx, addr - 0x2000);
    }
    else if (addr <= OAM_END) {
        /* OAM */
        return ctx->oam[addr - OAM_START];
    }
    else if (addr <= UNUSABLE_END) {
        /* Unusable */
        return 0xFF;
    }
    else if (addr <= IO_END) {
        /* I/O Registers */
        /* LCD registers 0xFF40-0xFF4B are handled by PPU */
        if (addr >= 0xFF40 && addr <= 0xFF4B && ctx->ppu) {
            return ppu_read_register((GBPPU*)ctx->ppu, addr);
        }
        /* Joypad register - return SELECT bits plus input state */
        if (addr == 0xFF00) {
            uint8_t joyp = ctx->io[0x00];
            uint8_t result = joyp | 0x0F;  /* Start with all buttons released */
            
#ifdef GB_HAS_SDL2
            /* Get actual joypad state from platform */
            uint8_t dpad = 0x0F;
            uint8_t buttons = 0x0F;
            extern uint8_t g_joypad_dpad;
            extern uint8_t g_joypad_buttons;
            dpad = g_joypad_dpad;
            buttons = g_joypad_buttons;
            
            /* P14 (bit 4) = select direction keys */
            /* P15 (bit 5) = select button keys */
            if (!(joyp & 0x10)) {
                /* Direction keys selected */
                result = (result & 0xF0) | (dpad & 0x0F);
            }
            if (!(joyp & 0x20)) {
                /* Button keys selected */
                result = (result & 0xF0) | (buttons & 0x0F);
            }
#endif
            return result;
        }
        return ctx->io[addr - IO_START];
    }
    else if (addr <= HRAM_END) {
        /* High RAM */
        return ctx->hram[addr - HRAM_START];
    }
    else {
        /* IE Register */
        return ctx->io[IO_SIZE];  /* IE at end of io array */
    }
}

void gb_write8(GBContext* ctx, uint16_t addr, uint8_t value) {
    if (addr <= ROM_BANKN_END) {
        /* ROM area - MBC register writes */
        /* TODO: Implement full MBC handling */
        if (addr >= 0x2000 && addr <= 0x3FFF) {
            /* ROM bank select (simplified) */
            ctx->rom_bank = value;
            if (ctx->rom_bank == 0) ctx->rom_bank = 1;
        }
        else if (addr <= 0x1FFF) {
            /* RAM enable */
            ctx->ram_enabled = ((value & 0x0F) == 0x0A);
        }
        else if (addr >= 0x4000 && addr <= 0x5FFF) {
            /* RAM bank or upper ROM bank bits */
            ctx->ram_bank = value & 0x03;
        }
        return;
    }
    else if (addr <= VRAM_END) {
        /* VRAM */
        uint16_t offset = addr - VRAM_START + ctx->vram_bank * VRAM_SIZE;
        ctx->vram[offset] = value;
        
        /* Debug: log significant VRAM writes (TEMPORARY: log all VRAM writes) */
        //static int vram_write_count = 0;
        //vram_write_count++;
        //if (vram_write_count <= 10 || (addr == 0x8000) || (addr == 0x9800)) {
            DBG_VRAM("Write 0x%04X = 0x%02X (offset=0x%04X, A=0x%02X)", 
                     addr, value, offset, ctx->a);
        //}
    }
    else if (addr <= ERAM_END) {
        /* External RAM */
        if (ctx->ram_enabled && ctx->eram) {
            size_t offset = (size_t)(ctx->ram_bank) * ERAM_BANK_SIZE + (addr - ERAM_START);
            if (offset < ctx->eram_size) {
                ctx->eram[offset] = value;
            }
        }
    }
    else if (addr <= WRAM_BANK0_END) {
        /* WRAM Bank 0 */
        ctx->wram[addr - WRAM_BANK0_START] = value;
    }
    else if (addr <= WRAM_BANKN_END) {
        /* WRAM Bank N */
        ctx->wram[(addr - WRAM_BANKN_START) + ctx->wram_bank * WRAM_BANK_SIZE] = value;
    }
    else if (addr <= ECHO_END) {
        /* Echo RAM */
        gb_write8(ctx, addr - 0x2000, value);
    }
    else if (addr <= OAM_END) {
        /* OAM */
        ctx->oam[addr - OAM_START] = value;
    }
    else if (addr <= UNUSABLE_END) {
        /* Unusable - ignore */
    }
    else if (addr <= IO_END) {
        /* I/O Registers */
        /* LCD registers 0xFF40-0xFF4B are handled by PPU */
        if (addr >= 0xFF40 && addr <= 0xFF4B && ctx->ppu) {
            ppu_write_register((GBPPU*)ctx->ppu, ctx, addr, value);
            return;
        }
        /* Also store in io array for other code to read */
        ctx->io[addr - IO_START] = value;
    }
    else if (addr <= HRAM_END) {
        /* High RAM */
        ctx->hram[addr - HRAM_START] = value;
    }
    else {
        /* IE Register */
        ctx->io[IO_SIZE] = value;
    }
}

uint16_t gb_read16(GBContext* ctx, uint16_t addr) {
    uint8_t lo = gb_read8(ctx, addr);
    uint8_t hi = gb_read8(ctx, addr + 1);
    return (uint16_t)lo | ((uint16_t)hi << 8);
}

void gb_write16(GBContext* ctx, uint16_t addr, uint16_t value) {
    gb_write8(ctx, addr, (uint8_t)(value & 0xFF));
    gb_write8(ctx, addr + 1, (uint8_t)(value >> 8));
}

/* ============================================================================
 * Stack Operations
 * ========================================================================== */

void gb_push16(GBContext* ctx, uint16_t value) {
    ctx->sp -= 2;
    gb_write16(ctx, ctx->sp, value);
}

uint16_t gb_pop16(GBContext* ctx) {
    uint16_t value = gb_read16(ctx, ctx->sp);
    ctx->sp += 2;
    return value;
}

/* ============================================================================
 * ALU Operations
 * ========================================================================== */

void gb_add8(GBContext* ctx, uint8_t value) {
    uint16_t result = ctx->a + value;
    
    ctx->f_z = ((result & 0xFF) == 0);
    ctx->f_n = 0;
    ctx->f_h = ((ctx->a & 0x0F) + (value & 0x0F)) > 0x0F;
    ctx->f_c = result > 0xFF;
    
    ctx->a = (uint8_t)result;
}

void gb_adc8(GBContext* ctx, uint8_t value) {
    uint8_t carry = ctx->f_c ? 1 : 0;
    uint16_t result = ctx->a + value + carry;
    
    ctx->f_z = ((result & 0xFF) == 0);
    ctx->f_n = 0;
    ctx->f_h = ((ctx->a & 0x0F) + (value & 0x0F) + carry) > 0x0F;
    ctx->f_c = result > 0xFF;
    
    ctx->a = (uint8_t)result;
}

void gb_sub8(GBContext* ctx, uint8_t value) {
    uint16_t result = ctx->a - value;
    
    ctx->f_z = ((result & 0xFF) == 0);
    ctx->f_n = 1;
    ctx->f_h = (ctx->a & 0x0F) < (value & 0x0F);
    ctx->f_c = ctx->a < value;
    
    ctx->a = (uint8_t)result;
}

void gb_sbc8(GBContext* ctx, uint8_t value) {
    uint8_t carry = ctx->f_c ? 1 : 0;
    uint16_t result = ctx->a - value - carry;
    
    ctx->f_z = ((result & 0xFF) == 0);
    ctx->f_n = 1;
    ctx->f_h = (ctx->a & 0x0F) < ((value & 0x0F) + carry);
    ctx->f_c = ctx->a < (value + carry);
    
    ctx->a = (uint8_t)result;
}

void gb_and8(GBContext* ctx, uint8_t value) {
    ctx->a &= value;
    
    ctx->f_z = (ctx->a == 0);
    ctx->f_n = 0;
    ctx->f_h = 1;
    ctx->f_c = 0;
}

void gb_or8(GBContext* ctx, uint8_t value) {
    ctx->a |= value;
    
    ctx->f_z = (ctx->a == 0);
    ctx->f_n = 0;
    ctx->f_h = 0;
    ctx->f_c = 0;
}

void gb_xor8(GBContext* ctx, uint8_t value) {
    ctx->a ^= value;
    
    ctx->f_z = (ctx->a == 0);
    ctx->f_n = 0;
    ctx->f_h = 0;
    ctx->f_c = 0;
}

void gb_cp8(GBContext* ctx, uint8_t value) {
    uint16_t result = ctx->a - value;
    
    ctx->f_z = ((result & 0xFF) == 0);
    ctx->f_n = 1;
    ctx->f_h = (ctx->a & 0x0F) < (value & 0x0F);
    ctx->f_c = ctx->a < value;
}

uint8_t gb_inc8(GBContext* ctx, uint8_t value) {
    uint8_t result = value + 1;
    
    ctx->f_z = (result == 0);
    ctx->f_n = 0;
    ctx->f_h = (value & 0x0F) == 0x0F;
    /* C not affected */
    
    return result;
}

uint8_t gb_dec8(GBContext* ctx, uint8_t value) {
    uint8_t result = value - 1;
    
    ctx->f_z = (result == 0);
    ctx->f_n = 1;
    ctx->f_h = (value & 0x0F) == 0x00;
    /* C not affected */
    
    return result;
}

void gb_add16(GBContext* ctx, uint16_t value) {
    uint32_t result = ctx->hl + value;
    
    /* Z not affected */
    ctx->f_n = 0;
    ctx->f_h = ((ctx->hl & 0x0FFF) + (value & 0x0FFF)) > 0x0FFF;
    ctx->f_c = result > 0xFFFF;
    
    ctx->hl = (uint16_t)result;
}

void gb_add_sp(GBContext* ctx, int8_t offset) {
    uint32_t result = ctx->sp + offset;
    
    ctx->f_z = 0;
    ctx->f_n = 0;
    ctx->f_h = ((ctx->sp & 0x0F) + (offset & 0x0F)) > 0x0F;
    ctx->f_c = ((ctx->sp & 0xFF) + (offset & 0xFF)) > 0xFF;
    
    ctx->sp = (uint16_t)result;
}

/* ============================================================================
 * Rotate/Shift Operations
 * ========================================================================== */

uint8_t gb_rlc(GBContext* ctx, uint8_t value) {
    uint8_t carry = (value >> 7) & 1;
    uint8_t result = (value << 1) | carry;
    
    ctx->f_z = (result == 0);
    ctx->f_n = 0;
    ctx->f_h = 0;
    ctx->f_c = carry;
    
    return result;
}

uint8_t gb_rrc(GBContext* ctx, uint8_t value) {
    uint8_t carry = value & 1;
    uint8_t result = (value >> 1) | (carry << 7);
    
    ctx->f_z = (result == 0);
    ctx->f_n = 0;
    ctx->f_h = 0;
    ctx->f_c = carry;
    
    return result;
}

uint8_t gb_rl(GBContext* ctx, uint8_t value) {
    uint8_t old_carry = ctx->f_c ? 1 : 0;
    uint8_t new_carry = (value >> 7) & 1;
    uint8_t result = (value << 1) | old_carry;
    
    ctx->f_z = (result == 0);
    ctx->f_n = 0;
    ctx->f_h = 0;
    ctx->f_c = new_carry;
    
    return result;
}

uint8_t gb_rr(GBContext* ctx, uint8_t value) {
    uint8_t old_carry = ctx->f_c ? 1 : 0;
    uint8_t new_carry = value & 1;
    uint8_t result = (value >> 1) | (old_carry << 7);
    
    ctx->f_z = (result == 0);
    ctx->f_n = 0;
    ctx->f_h = 0;
    ctx->f_c = new_carry;
    
    return result;
}

uint8_t gb_sla(GBContext* ctx, uint8_t value) {
    uint8_t carry = (value >> 7) & 1;
    uint8_t result = value << 1;
    
    ctx->f_z = (result == 0);
    ctx->f_n = 0;
    ctx->f_h = 0;
    ctx->f_c = carry;
    
    return result;
}

uint8_t gb_sra(GBContext* ctx, uint8_t value) {
    uint8_t carry = value & 1;
    uint8_t result = (value >> 1) | (value & 0x80);  /* Preserve sign bit */
    
    ctx->f_z = (result == 0);
    ctx->f_n = 0;
    ctx->f_h = 0;
    ctx->f_c = carry;
    
    return result;
}

uint8_t gb_srl(GBContext* ctx, uint8_t value) {
    uint8_t carry = value & 1;
    uint8_t result = value >> 1;
    
    ctx->f_z = (result == 0);
    ctx->f_n = 0;
    ctx->f_h = 0;
    ctx->f_c = carry;
    
    return result;
}

uint8_t gb_swap(GBContext* ctx, uint8_t value) {
    uint8_t result = ((value & 0x0F) << 4) | ((value & 0xF0) >> 4);
    
    ctx->f_z = (result == 0);
    ctx->f_n = 0;
    ctx->f_h = 0;
    ctx->f_c = 0;
    
    return result;
}

void gb_rlca(GBContext* ctx) {
    uint8_t carry = (ctx->a >> 7) & 1;
    ctx->a = (ctx->a << 1) | carry;
    
    ctx->f_z = 0;  /* RLCA always clears Z */
    ctx->f_n = 0;
    ctx->f_h = 0;
    ctx->f_c = carry;
}

void gb_rrca(GBContext* ctx) {
    uint8_t carry = ctx->a & 1;
    ctx->a = (ctx->a >> 1) | (carry << 7);
    
    ctx->f_z = 0;
    ctx->f_n = 0;
    ctx->f_h = 0;
    ctx->f_c = carry;
}

void gb_rla(GBContext* ctx) {
    uint8_t old_carry = ctx->f_c ? 1 : 0;
    uint8_t new_carry = (ctx->a >> 7) & 1;
    ctx->a = (ctx->a << 1) | old_carry;
    
    ctx->f_z = 0;
    ctx->f_n = 0;
    ctx->f_h = 0;
    ctx->f_c = new_carry;
}

void gb_rra(GBContext* ctx) {
    uint8_t old_carry = ctx->f_c ? 1 : 0;
    uint8_t new_carry = ctx->a & 1;
    ctx->a = (ctx->a >> 1) | (old_carry << 7);
    
    ctx->f_z = 0;
    ctx->f_n = 0;
    ctx->f_h = 0;
    ctx->f_c = new_carry;
}

/* ============================================================================
 * Bit Operations
 * ========================================================================== */

void gb_bit(GBContext* ctx, uint8_t bit, uint8_t value) {
    ctx->f_z = ((value >> bit) & 1) == 0;
    ctx->f_n = 0;
    ctx->f_h = 1;
    /* C not affected */
}

/* ============================================================================
 * Misc Operations
 * ========================================================================== */

void gb_daa(GBContext* ctx) {
    uint16_t result = ctx->a;
    
    if (ctx->f_n) {
        /* After subtraction */
        if (ctx->f_h) result = (result - 0x06) & 0xFF;
        if (ctx->f_c) result -= 0x60;
    } else {
        /* After addition */
        if (ctx->f_h || (result & 0x0F) > 9) result += 0x06;
        if (ctx->f_c || result > 0x9F) result += 0x60;
    }
    
    ctx->a = (uint8_t)(result & 0xFF);
    ctx->f_z = (ctx->a == 0);
    ctx->f_h = 0;
    if (result > 0xFF) ctx->f_c = 1;
}

/* ============================================================================
 * Control Flow
 * ========================================================================== */

void gb_call(GBContext* ctx, uint16_t addr) {
    /* Push return address and dispatch */
    /* Note: PC should point to instruction after CALL */
    gb_push16(ctx, ctx->pc);
    gb_dispatch(ctx, addr);
}

void gb_ret(GBContext* ctx) {
    ctx->pc = gb_pop16(ctx);
    /* Dispatch continues from caller */
}

void gb_rst(GBContext* ctx, uint8_t vector) {
    gb_push16(ctx, ctx->pc);
    gb_dispatch(ctx, vector);
}

/* These are weak symbols - overridden by the generated dispatch table */
__attribute__((weak))
void gb_dispatch(GBContext* ctx, uint16_t addr) {
    /* This will be overridden by the generated dispatch table */
    ctx->pc = addr;
    gb_interpret(ctx, addr);
}

__attribute__((weak))
void gb_dispatch_call(GBContext* ctx, uint16_t addr) {
    /* For calls to unanalyzed code (e.g., HRAM routines) */
    /* The return address should already be pushed by the caller's recompiled code */
    /* or we push it here if called directly */
    gb_push16(ctx, ctx->pc);
    ctx->pc = addr;
    gb_interpret(ctx, addr);
}

void gb_interpret(GBContext* ctx, uint16_t addr) {
    /* Set PC to the address we want to execute */
    ctx->pc = addr;

    /* Handle HRAM OAM DMA routine specifically */
    /* Tetris calls a routine in HRAM (usually around 0xFFB6) to start DMA */
    /* Routine starts with: LDH (0xFF46), A */
    if (addr >= 0xFF80 && addr <= 0xFFFE) {
        uint8_t opcode = ctx->hram[addr - 0xFF80];
        
        /* Check for LDH (n),A instruction (0xE0) */
        if (opcode == 0xE0) {
            uint8_t operand = ctx->hram[addr - 0xFF80 + 1];
            if (operand == 0x46) {
                /* Generic OAM DMA: LDH (0xFF46), A */
                DBG_GENERAL("Intercepted HRAM DMA routine at 0x%04X (Generic)", addr);
                gb_write8(ctx, 0xFF46, ctx->a);
                gb_ret(ctx);
                return;
            }
        }
        /* Check for Tetris pattern: LD A, n (0x3E) then LDH (0xFF46), A */
        else if (opcode == 0x3E) {
            uint8_t op2 = ctx->hram[addr - 0xFF80 + 2];
            uint8_t operand2 = ctx->hram[addr - 0xFF80 + 3];
            if (op2 == 0xE0 && operand2 == 0x46) {
                uint8_t dma_src = ctx->hram[addr - 0xFF80 + 1];
                /* Tetris OAM DMA: LD A, n; LDH (0xFF46), A */
                /* DBG_GENERAL("Intercepted HRAM DMA routine at 0x%04X (Tetris variant, src=0x%02X)", addr, dma_src); */
                ctx->a = dma_src; /* Execute LD A, n */
                gb_write8(ctx, 0xFF46, ctx->a);
                gb_ret(ctx);
                return;
            }
        }
    }

    /* Fallback: trace execution of uncompiled code (basic logging) */
    static int missing_log_count = 0;
    if (missing_log_count < 20) {
        missing_log_count++;
        DBG_GENERAL("Executing uncompiled code at 0x%04X (Bank %d) - Implementation missing using interpreter stub", 
                    addr, ctx->rom_bank);
    }
}

/* ============================================================================
 * CPU State
 * ========================================================================== */

void gb_halt(GBContext* ctx) {
    ctx->halted = 1;
    
    /* Spin until an interrupt is pending */
    /* This runs the PPU to advance time without CPU execution */
    int max_cycles = 70224;  /* One full frame maximum */
    while (ctx->halted && max_cycles > 0) {
        /* Tick PPU for 4 cycles (one M-cycle) */
        gb_tick(ctx, 4);
        max_cycles -= 4;
        
        /* Check for interrupts (IE & IF) */
        uint8_t ie = ctx->io[IO_SIZE];  /* IE register at 0xFFFF stored in io[0x80] */
        uint8_t if_reg = ctx->io[0x0F];
        if (ie & if_reg) {
            /* An interrupt is pending, wake up */
            ctx->halted = 0;
            break;
        }
        
        /* If frame is ready, render it (inline to avoid longjmp issues) */
        if (ctx->ppu && ppu_frame_ready((GBPPU*)ctx->ppu)) {
#ifdef GB_HAS_SDL2
            /* Poll events to keep system responsive */
            if (!gb_platform_poll_events(ctx)) {
                /* User requested quit */
                ctx->stopped = 1;
                ctx->halted = 0;
                break;
            }
            
            /* Render the frame */
            const uint32_t* fb = ppu_get_framebuffer((GBPPU*)ctx->ppu);
            if (fb) {
                gb_platform_render_frame(fb);
            }
            gb_platform_vsync();
#endif
            ppu_clear_frame_ready((GBPPU*)ctx->ppu);
        }
    }
}

void gb_stop(GBContext* ctx) {
    ctx->stopped = 1;
    /* CPU will wake on joypad press */
}

/* ============================================================================
 * Timing
 * ========================================================================== */

#define CYCLES_PER_FRAME  70224  /* 154 scanlines * 456 dots */

void gb_add_cycles(GBContext* ctx, uint32_t cycles) {
    ctx->cycles += cycles;
    ctx->frame_cycles += cycles;
}

bool gb_frame_complete(GBContext* ctx) {
    if (ctx->ppu && ppu_frame_ready((GBPPU*)ctx->ppu)) {
        return true;
    }
    return false;
}

const uint32_t* gb_get_framebuffer(GBContext* ctx) {
    if (ctx->ppu) {
        return ppu_get_framebuffer((GBPPU*)ctx->ppu);
    }
    return NULL;
}

void gb_reset_frame(GBContext* ctx) {
    if (ctx->ppu) {
        ppu_clear_frame_ready((GBPPU*)ctx->ppu);
    }
}

void gb_tick(GBContext* ctx, uint32_t cycles) {
    static uint32_t poll_counter = 0;
    static int frame_count = 0;
    static uint32_t int_check_count = 0;
    
    gb_add_cycles(ctx, cycles);
    
    /* Handle pending IME enable (from EI instruction) */
    if (ctx->ime_pending) {
        DBG_GENERAL("[INT] IME enabled via EI instruction");
        ctx->ime = 1;
        ctx->ime_pending = 0;
    }
    
    /* Debug: periodically check interrupt state */
    int_check_count++;
    if (int_check_count % 10000 == 1) {
        uint8_t if_reg = ctx->io[0x0F];
        uint8_t ie_reg = ctx->io[0x80];
        DBG_GENERAL("[INT] Check #%u: IME=%d IF=0x%02X IE=0x%02X pending=0x%02X",
                    int_check_count, ctx->ime, if_reg, ie_reg, if_reg & ie_reg & 0x1F);
    }
    
    /* Check and dispatch interrupts */
    if (ctx->ime) {
        uint8_t if_reg = ctx->io[0x0F];  /* Interrupt Flag */
        uint8_t ie_reg = ctx->io[0x80];  /* Interrupt Enable (stored at offset 0x80) */
        uint8_t pending = if_reg & ie_reg & 0x1F;
        
        if (pending) {
            ctx->ime = 0;  /* Disable further interrupts */
            ctx->halted = 0;  /* Wake from HALT */
            
            /* Priority: VBlank > LCD STAT > Timer > Serial > Joypad */
            uint16_t vector = 0;
            uint8_t bit = 0;
            
            if (pending & 0x01) { vector = 0x0040; bit = 0x01; }      /* VBlank */
            else if (pending & 0x02) { vector = 0x0048; bit = 0x02; } /* LCD STAT */
            else if (pending & 0x04) { vector = 0x0050; bit = 0x04; } /* Timer */
            else if (pending & 0x08) { vector = 0x0058; bit = 0x08; } /* Serial */
            else if (pending & 0x10) { vector = 0x0060; bit = 0x10; } /* Joypad */
            
            if (vector) {
                DBG_GENERAL("[INT] Dispatching interrupt to 0x%04X (IF=0x%02X, bit=0x%02X)", 
                            vector, if_reg, bit);
                
                /* Clear the interrupt flag */
                ctx->io[0x0F] &= ~bit;
                
                /* Push PC and jump to handler */
                /* Note: For recompiled code, we call the dispatch function */
                gb_dispatch(ctx, vector);
            }
        }
    }
    
    /* Update PPU */
    if (ctx->ppu) {
        ppu_tick((GBPPU*)ctx->ppu, ctx, cycles);
        
        /* If frame is ready, render it */
        if (ppu_frame_ready((GBPPU*)ctx->ppu)) {
            frame_count++;
            
            if (frame_count <= 3 || (frame_count % 60 == 0)) {
                DBG_FRAME("Frame %d ready, total_cycles=%u", frame_count, ctx->cycles);
                
                /* Check if VRAM has tile data */
                if (ctx->vram) {
                    bool has_tiles = dbg_has_tile_data(ctx->vram, 0x1800);
                    DBG_FRAME("VRAM has tile data: %s", has_tiles ? "YES" : "NO");
                }
            }
            
#ifdef GB_HAS_SDL2
            /* Render the frame */
            const uint32_t* fb = ppu_get_framebuffer((GBPPU*)ctx->ppu);
            if (fb) {
                gb_platform_render_frame(fb);
            }
            gb_platform_vsync();
#endif
            ppu_clear_frame_ready((GBPPU*)ctx->ppu);
            poll_counter = 0;
        }
    }
    
#ifdef GB_HAS_SDL2
    /* Poll events frequently to keep system responsive */
    poll_counter += cycles;
    if (poll_counter >= 4096) {  /* Every ~1ms of emulated time */
        poll_counter = 0;
        if (!gb_platform_poll_events(ctx)) {
            ctx->stopped = 1;
        }
    }
#endif
    
    /* TODO: Update timer, APU, etc */
}

/* ============================================================================
 * Platform Interface
 * ========================================================================== */

static GBPlatformCallbacks g_callbacks = {0};

void gb_set_platform_callbacks(GBContext* ctx, const GBPlatformCallbacks* callbacks) {
    (void)ctx;
    if (callbacks) {
        g_callbacks = *callbacks;
    }
}

/* ============================================================================
 * Execution
 * ========================================================================== */

uint32_t gb_run_frame(GBContext* ctx) {
    uint32_t start_cycles = ctx->cycles;
    
    while (!gb_frame_complete(ctx)) {
        if (ctx->halted) {
            /* Still need to tick hardware when halted */
            gb_tick(ctx, 4);
        } else {
            gb_step(ctx);
        }
    }
    
    return ctx->cycles - start_cycles;
}

uint32_t gb_step(GBContext* ctx) {
    /* Dispatch to recompiled code */
    uint32_t start = ctx->cycles;
    gb_dispatch(ctx, ctx->pc);
    
    /* Handle pending EI */
    if (ctx->ime_pending) {
        ctx->ime = 1;
        ctx->ime_pending = 0;
    }
    
    return ctx->cycles - start;
}
