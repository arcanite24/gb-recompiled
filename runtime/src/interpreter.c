#include "gbrt.h"
#include "gbrt_debug.h"
#include "ppu.h"
#include <stdlib.h>

/* ============================================================================
 * SM83 Opcode Cycle Timing Tables
 * Based on Pan Docs: https://gbdev.io/pandocs/CPU_Instruction_Set.html
 * Values are in T-cycles (1 M-cycle = 4 T-cycles)
 * For conditional branches, this is the NOT-TAKEN timing; taken branches add extra cycles
 * ========================================================================== */

/* Main opcode cycles (0x00-0xFF) */
static const uint8_t OPCODE_CYCLES[256] = {
    /* 0x00-0x0F */   4, 12,  8,  8,  4,  4,  8,  4, 20,  8,  8,  8,  4,  4,  8,  4,
    /* 0x10-0x1F */   4, 12,  8,  8,  4,  4,  8,  4, 12,  8,  8,  8,  4,  4,  8,  4,
    /* 0x20-0x2F */   8, 12,  8,  8,  4,  4,  8,  4,  8,  8,  8,  8,  4,  4,  8,  4,
    /* 0x30-0x3F */   8, 12,  8,  8, 12, 12, 12,  4,  8,  8,  8,  8,  4,  4,  8,  4,
    /* 0x40-0x4F */   4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4,
    /* 0x50-0x5F */   4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4,
    /* 0x60-0x6F */   4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4,
    /* 0x70-0x7F */   8,  8,  8,  8,  8,  8,  4,  8,  4,  4,  4,  4,  4,  4,  8,  4,
    /* 0x80-0x8F */   4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4,
    /* 0x90-0x9F */   4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4,
    /* 0xA0-0xAF */   4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4,
    /* 0xB0-0xBF */   4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4,
    /* 0xC0-0xCF */   8, 12, 12, 16, 12, 16,  8, 16,  8, 16, 12,  4, 12, 24,  8, 16,
    /* 0xD0-0xDF */   8, 12, 12,  4, 12, 16,  8, 16,  8, 16, 12,  4, 12,  4,  8, 16,
    /* 0xE0-0xEF */  12, 12,  8,  4,  4, 16,  8, 16, 16,  4, 16,  4,  4,  4,  8, 16,
    /* 0xF0-0xFF */  12, 12,  8,  4,  4, 16,  8, 16, 12,  8, 16,  4,  4,  4,  8, 16
};

/* CB-prefix opcode cycles - all are 8 cycles except (HL) operations which are 16 */
static const uint8_t CB_OPCODE_CYCLES[256] = {
    /* 0x00-0x0F */   8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8,
    /* 0x10-0x1F */   8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8,
    /* 0x20-0x2F */   8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8,
    /* 0x30-0x3F */   8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8,
    /* 0x40-0x4F */   8,  8,  8,  8,  8,  8, 12,  8,  8,  8,  8,  8,  8,  8, 12,  8,
    /* 0x50-0x5F */   8,  8,  8,  8,  8,  8, 12,  8,  8,  8,  8,  8,  8,  8, 12,  8,
    /* 0x60-0x6F */   8,  8,  8,  8,  8,  8, 12,  8,  8,  8,  8,  8,  8,  8, 12,  8,
    /* 0x70-0x7F */   8,  8,  8,  8,  8,  8, 12,  8,  8,  8,  8,  8,  8,  8, 12,  8,
    /* 0x80-0x8F */   8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8,
    /* 0x90-0x9F */   8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8,
    /* 0xA0-0xAF */   8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8,
    /* 0xB0-0xBF */   8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8,
    /* 0xC0-0xCF */   8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8,
    /* 0xD0-0xDF */   8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8,
    /* 0xE0-0xEF */   8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8,
    /* 0xF0-0xFF */   8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8
};

/* Extra cycles for conditional branches when taken */
#define BRANCH_TAKEN_EXTRA 4   /* JR cc, JP cc add 4 cycles when taken */
#define CALL_TAKEN_EXTRA 12    /* CALL cc adds 12 cycles when taken */
#define RET_TAKEN_EXTRA 12     /* RET cc adds 12 cycles when taken */

/* Helper macros for instruction arguments */
#define READ8(ctx) gb_read8(ctx, ctx->pc++)
#define READ16(ctx) (ctx->pc += 2, gb_read16(ctx, ctx->pc - 2))

/* Read a 16-bit immediate on the operand-fetch M-cycles. The opcode has
 * already been sampled at M0; this retires M0, samples/retire M1 and M2, and
 * leaves control-flow helpers to execute only their remaining internal and
 * stack phases. */
static uint16_t interpreter_read_imm16_timed(GBContext* ctx) {
    gb_tick(ctx, 7);
    const uint8_t low = gb_read8(ctx, ctx->pc++);
    gb_tick(ctx, 1);
    gb_tick(ctx, 3);
    const uint8_t high = gb_read8(ctx, ctx->pc++);
    gb_tick(ctx, 1);
    return (uint16_t)(low | ((uint16_t)high << 8));
}

static uint8_t interpreter_final_read8(GBContext* ctx,
                                       uint16_t addr,
                                       uint8_t* cycles) {
    const uint8_t value = gbrt_timed_bus_read8(ctx,
                                                addr,
                                                (uint8_t)(*cycles - 1u));
    *cycles = 0;
    return value;
}

static void interpreter_final_write8(GBContext* ctx,
                                     uint16_t addr,
                                     uint8_t value,
                                     uint8_t* cycles) {
    gbrt_timed_bus_write8(ctx,
                          addr,
                          value,
                          (uint8_t)(*cycles - 1u));
    *cycles = 0;
}

static uint8_t get_reg8(GBContext* ctx, uint8_t idx) {
    switch (idx) {
        case 0: return ctx->b;
        case 1: return ctx->c;
        case 2: return ctx->d;
        case 3: return ctx->e;
        case 4: return ctx->h;
        case 5: return ctx->l;
        case 6: return gb_read8(ctx, ctx->hl);
        case 7: return ctx->a;
    }
    return 0;
}

static void set_reg8(GBContext* ctx, uint8_t idx, uint8_t val) {
    switch (idx) {
        case 0: ctx->b = val; break;
        case 1: ctx->c = val; break;
        case 2: ctx->d = val; break;
        case 3: ctx->e = val; break;
        case 4: ctx->h = val; break;
        case 5: ctx->l = val; break;
        case 6: gb_write8(ctx, ctx->hl, val); break;
        case 7: ctx->a = val; break;
    }
}

static void gbrt_finish_interpreter_session(GBContext* ctx,
                                            uint16_t entry_bank,
                                            uint16_t entry_addr,
                                            uint32_t instructions_executed,
                                            uint32_t entry_cycles) {
    if (!ctx) {
        return;
    }

    gbrt_note_interpreter_session(ctx,
                                  entry_bank,
                                  entry_addr,
                                  instructions_executed,
                                  ctx->cycles - entry_cycles);
}

void gb_interpret(GBContext* ctx, uint16_t addr) {
    /* Set PC to the address we want to execute */
    ctx->pc = addr;
    uint16_t entry_bank = gb_resolve_rom_bank(ctx, addr);
    uint32_t entry_cycles = ctx->cycles;
    
    /* Interpreter entry logging */
#ifdef GB_DEBUG_REGS
    static int entry_count = 0;
    entry_count++;
    if (entry_count <= 100) {
        fprintf(stderr, "[INTERP] Enter interpreter at 0x%04X (entry #%d)\n", addr, entry_count);
    }
#endif
    gbrt_log_trace(ctx, gb_resolve_rom_bank(ctx, addr), addr);

    uint32_t instructions_executed = 0;
#define GBRT_INTERPRETER_RETURN()                                                \
    do {                                                                         \
        gbrt_finish_interpreter_session(                                          \
            ctx, entry_bank, addr, instructions_executed, entry_cycles);          \
        return;                                                                  \
    } while (0)

    while (!ctx->stopped) {
        instructions_executed++;
        
        /* Debug logging */
        (void)instructions_executed; /* Avoid unused warning */
        
        /* Check instruction limit */
        if (gbrt_instruction_limit > 0 && gbrt_instruction_count >= gbrt_instruction_limit) {
            fprintf(stderr, "[LIMIT] Reached instruction limit %llu\n", (unsigned long long)gbrt_instruction_limit);
            if (gbrt_instruction_limit_callback != NULL) {
                gbrt_instruction_limit_callback();
            }
            exit(0);
        }
        gbrt_instruction_count++;

        /* Debug logging */
        if (gbrt_trace_enabled) {
             DBG_GENERAL("Interpreter (0x%04X): Regs A=%02X B=%02X C=%02X D=%02X E=%02X H=%02X L=%02X SP=%04X HL=%04X",
                        ctx->pc, ctx->a, ctx->b, ctx->c, ctx->d, ctx->e, ctx->h, ctx->l, ctx->sp, ctx->hl);
        }

#ifdef GB_DEBUG_REGS
        if (1) { /* Always log if REGS is enabled */
            DBG_GENERAL("Interpreter (0x%04X): Regs A=%02X B=%02X C=%02X D=%02X E=%02X H=%02X L=%02X SP=%04X HL=%04X",
                        ctx->pc, ctx->a, ctx->b, ctx->c, ctx->d, ctx->e, ctx->h, ctx->l, ctx->sp, ctx->hl);
            
            if (ctx->pc >= 0x8000) {
                static uint16_t last_dump_pc = 0;
                if (ctx->pc != last_dump_pc) {
                    DBG_GENERAL("Code at 0x%04X: %02X %02X %02X %02X %02X %02X %02X %02X",
                                ctx->pc, gb_read8(ctx, ctx->pc), gb_read8(ctx, ctx->pc+1), 
                                gb_read8(ctx, ctx->pc+2), gb_read8(ctx, ctx->pc+3),
                                gb_read8(ctx, ctx->pc+4), gb_read8(ctx, ctx->pc+5),
                                gb_read8(ctx, ctx->pc+6), gb_read8(ctx, ctx->pc+7));
                    last_dump_pc = ctx->pc;
                }
            }
        }
#endif
        
        /* NOTE: Previously we returned immediately when pc < 0x8000 (ROM area),
         * expecting the dispatcher to have compiled code for all ROM addresses.
         * However, static analysis may miss code paths (like cpu_instrs.gb's 
         * callback tables), so we MUST interpret ROM code too when called.
         * The interpreter is now a universal fallback for ANY uncompiled code.
         */

        if (!ctx->halt_bug &&
            ((ctx->pc >= 0xFF00 && ctx->pc < 0xFF80 && gbrt_try_execute_highmem_stub(ctx, ctx->pc)) ||
             (ctx->pc >= 0xFF80 && ctx->pc <= 0xFFFE && gbrt_try_execute_hram_stub(ctx, ctx->pc)) ||
             (ctx->pc >= 0xC000 && ctx->pc < 0xFFFF && gbrt_try_execute_ram_stub(ctx, ctx->pc)))) {
            if (ctx->single_step_mode) {
                gbrt_finish_interpreter_session(ctx, entry_bank, addr, instructions_executed, entry_cycles);
                return;
            }
            continue;
        }

        uint8_t opcode;
        if (ctx->halt_bug) {
            /* HALT bug: Read opcode without incrementing PC (PC fails to advance) */
            opcode = gb_read8(ctx, ctx->pc);
            ctx->halt_bug = 0;
        } else {
            opcode = READ8(ctx);
        }
        
        /* Track cycles for this instruction - will be adjusted for branches/CB prefix */
        uint8_t cycles = OPCODE_CYCLES[opcode];
        uint8_t extra_cycles = 0;  /* For conditional branches when taken */
        
        switch (opcode) {
            case 0x00: /* NOP */ break;
            
            case 0x07: gb_rlca(ctx); break;
            case 0x0F: gb_rrca(ctx); break;
            case 0x17: gb_rla(ctx); break;
            case 0x1F: gb_rra(ctx); break;
            case 0x27: gb_daa(ctx); break;
            case 0x2F: ctx->a = ~ctx->a; ctx->f_n = 1; ctx->f_h = 1; break; /* CPL */
            case 0x37: ctx->f_n = 0; ctx->f_h = 0; ctx->f_c = 1; break; /* SCF */
            case 0x3F: ctx->f_n = 0; ctx->f_h = 0; ctx->f_c = !ctx->f_c; break; /* CCF */
            
            case 0x10: gb_stop(ctx); ctx->pc++; break; /* STOP 0 */
            
            /* 8-bit Loads */
            case 0x06: ctx->b = READ8(ctx); break; /* LD B,n */
            case 0x0E: ctx->c = READ8(ctx); break; /* LD C,n */
            case 0x16: ctx->d = READ8(ctx); break; /* LD D,n */
            case 0x1E: ctx->e = READ8(ctx); break; /* LD E,n */
            case 0x26: ctx->h = READ8(ctx); break; /* LD H,n */
            case 0x2E: ctx->l = READ8(ctx); break; /* LD L,n */
            case 0x3E: ctx->a = READ8(ctx); break; /* LD A,n */
            
            /* Complete LD r, r' instructions (0x40-0x7F) */
            /* LD B, r */
            case 0x40: /* LD B,B; also Mooneye's opt-in magic breakpoint */
                ctx->b = ctx->b;
                if (gbrt_test_breakpoint_enabled) ctx->stopped = 1;
                break;
            case 0x41: ctx->b = ctx->c; break; /* LD B,C */
            case 0x42: ctx->b = ctx->d; break; /* LD B,D */
            case 0x43: ctx->b = ctx->e; break; /* LD B,E */
            case 0x44: ctx->b = ctx->h; break; /* LD B,H */
            case 0x45: ctx->b = ctx->l; break; /* LD B,L */
            case 0x46: ctx->b = interpreter_final_read8(ctx, ctx->hl, &cycles); break; /* LD B,(HL) */
            case 0x47: ctx->b = ctx->a; break; /* LD B,A */
            
            /* LD C, r */
            case 0x48: ctx->c = ctx->b; break; /* LD C,B */
            case 0x49: ctx->c = ctx->c; break; /* LD C,C */
            case 0x4A: ctx->c = ctx->d; break; /* LD C,D */
            case 0x4B: ctx->c = ctx->e; break; /* LD C,E */
            case 0x4C: ctx->c = ctx->h; break; /* LD C,H */
            case 0x4D: ctx->c = ctx->l; break; /* LD C,L */
            case 0x4E: ctx->c = interpreter_final_read8(ctx, ctx->hl, &cycles); break; /* LD C,(HL) */
            case 0x4F: ctx->c = ctx->a; break; /* LD C,A */
            
            /* LD D, r */
            case 0x50: ctx->d = ctx->b; break; /* LD D,B */
            case 0x51: ctx->d = ctx->c; break; /* LD D,C */
            case 0x52: ctx->d = ctx->d; break; /* LD D,D */
            case 0x53: ctx->d = ctx->e; break; /* LD D,E */
            case 0x54: ctx->d = ctx->h; break; /* LD D,H */
            case 0x55: ctx->d = ctx->l; break; /* LD D,L */
            case 0x56: ctx->d = interpreter_final_read8(ctx, ctx->hl, &cycles); break; /* LD D,(HL) */
            case 0x57: ctx->d = ctx->a; break; /* LD D,A */
            
            /* LD E, r */
            case 0x58: ctx->e = ctx->b; break; /* LD E,B */
            case 0x59: ctx->e = ctx->c; break; /* LD E,C */
            case 0x5A: ctx->e = ctx->d; break; /* LD E,D */
            case 0x5B: ctx->e = ctx->e; break; /* LD E,E */
            case 0x5C: ctx->e = ctx->h; break; /* LD E,H */
            case 0x5D: ctx->e = ctx->l; break; /* LD E,L */
            case 0x5E: ctx->e = interpreter_final_read8(ctx, ctx->hl, &cycles); break; /* LD E,(HL) */
            case 0x5F: ctx->e = ctx->a; break; /* LD E,A */
            
            /* LD H, r */
            case 0x60: ctx->h = ctx->b; break; /* LD H,B */
            case 0x61: ctx->h = ctx->c; break; /* LD H,C */
            case 0x62: ctx->h = ctx->d; break; /* LD H,D */
            case 0x63: ctx->h = ctx->e; break; /* LD H,E */
            case 0x64: ctx->h = ctx->h; break; /* LD H,H */
            case 0x65: ctx->h = ctx->l; break; /* LD H,L */
            case 0x66: ctx->h = interpreter_final_read8(ctx, ctx->hl, &cycles); break; /* LD H,(HL) */
            case 0x67: ctx->h = ctx->a; break; /* LD H,A */
            
            /* LD L, r */
            case 0x68: ctx->l = ctx->b; break; /* LD L,B */
            case 0x69: ctx->l = ctx->c; break; /* LD L,C */
            case 0x6A: ctx->l = ctx->d; break; /* LD L,D */
            case 0x6B: ctx->l = ctx->e; break; /* LD L,E */
            case 0x6C: ctx->l = ctx->h; break; /* LD L,H */
            case 0x6D: ctx->l = ctx->l; break; /* LD L,L */
            case 0x6E: ctx->l = interpreter_final_read8(ctx, ctx->hl, &cycles); break; /* LD L,(HL) */
            case 0x6F: ctx->l = ctx->a; break; /* LD L,A */
            
            /* LD (HL), r */
            case 0x70: interpreter_final_write8(ctx, ctx->hl, ctx->b, &cycles); break; /* LD (HL), B */
            case 0x71: interpreter_final_write8(ctx, ctx->hl, ctx->c, &cycles); break; /* LD (HL), C */
            case 0x72: interpreter_final_write8(ctx, ctx->hl, ctx->d, &cycles); break; /* LD (HL), D */
            case 0x73: interpreter_final_write8(ctx, ctx->hl, ctx->e, &cycles); break; /* LD (HL), E */
            case 0x74: interpreter_final_write8(ctx, ctx->hl, ctx->h, &cycles); break; /* LD (HL), H */
            case 0x75: interpreter_final_write8(ctx, ctx->hl, ctx->l, &cycles); break; /* LD (HL), L */
            case 0x76: /* HALT */
                gbrt_execute_halt(ctx, ctx->pc, cycles);
                GBRT_INTERPRETER_RETURN();
            case 0x77: interpreter_final_write8(ctx, ctx->hl, ctx->a, &cycles); break; /* LD (HL), A */
            
            /* LD A, r */
            case 0x78: ctx->a = ctx->b; break; /* LD A,B */
            case 0x79: ctx->a = ctx->c; break; /* LD A,C */
            case 0x7A: ctx->a = ctx->d; break; /* LD A,D */
            case 0x7B: ctx->a = ctx->e; break; /* LD A,E */
            case 0x7C: ctx->a = ctx->h; break; /* LD A,H */
            case 0x7D: ctx->a = ctx->l; break; /* LD A,L */
            case 0x7E: ctx->a = interpreter_final_read8(ctx, ctx->hl, &cycles); break; /* LD A,(HL) */
            case 0x7F: ctx->a = ctx->a; break; /* LD A,A */
            
            case 0xEA: { uint16_t target = READ16(ctx); interpreter_final_write8(ctx, target, ctx->a, &cycles); break; } /* LD (nn), A */
            case 0xFA: { uint16_t target = READ16(ctx); ctx->a = interpreter_final_read8(ctx, target, &cycles); break; } /* LD A, (nn) */
            
            case 0xE0: { uint16_t target = (uint16_t)(0xFF00u + READ8(ctx)); interpreter_final_write8(ctx, target, ctx->a, &cycles); break; } /* LDH (n), A */
            case 0xF0: { uint16_t target = (uint16_t)(0xFF00u + READ8(ctx)); ctx->a = interpreter_final_read8(ctx, target, &cycles); break; } /* LDH A, (n) */
            
            case 0xE2: interpreter_final_write8(ctx, (uint16_t)(0xFF00u + ctx->c), ctx->a, &cycles); break; /* LD (C), A */
            case 0xF2: ctx->a = interpreter_final_read8(ctx, (uint16_t)(0xFF00u + ctx->c), &cycles); break; /* LD A, (C) */
            
            case 0x0A: ctx->a = interpreter_final_read8(ctx, ctx->bc, &cycles); break; /* LD A, (BC) */
            case 0x1A: ctx->a = interpreter_final_read8(ctx, ctx->de, &cycles); break; /* LD A, (DE) */
            case 0x02: interpreter_final_write8(ctx, ctx->bc, ctx->a, &cycles); break; /* LD (BC), A */
            case 0x12: interpreter_final_write8(ctx, ctx->de, ctx->a, &cycles); break; /* LD (DE), A */

            case 0x22: gbrt_timed_hl_write_auto(ctx, ctx->a, 1); cycles = 0; break; /* LD (HL+), A */
            case 0x2A: ctx->a = gbrt_timed_hl_read_auto(ctx, 1); cycles = 0; break; /* LD A, (HL+) */
            case 0x32: gbrt_timed_hl_write_auto(ctx, ctx->a, -1); cycles = 0; break; /* LD (HL-), A */
            case 0x3A: ctx->a = gbrt_timed_hl_read_auto(ctx, -1); cycles = 0; break; /* LD A, (HL-) */
            case 0x08: { /* LD (nn), SP */
                uint16_t addr = READ16(ctx);
                gb_write16(ctx, addr, ctx->sp);
                break;
            }
            /* 16-bit Loads */
            case 0x01: ctx->bc = READ16(ctx); break; /* LD BC, nn */
            case 0x11: ctx->de = READ16(ctx); break; /* LD DE, nn */
            case 0x21: ctx->hl = READ16(ctx); break; /* LD HL, nn */
            case 0x31: ctx->sp = READ16(ctx); break; /* LD SP, nn */
            case 0xF9: ctx->sp = ctx->hl; break; /* LD SP, HL */
            
            /* Stack */
            case 0xC5: gbrt_timed_push16(ctx, ctx->bc); cycles = 0; break; /* PUSH BC */
            case 0xD5: gbrt_timed_push16(ctx, ctx->de); cycles = 0; break; /* PUSH DE */
            case 0xE5: gbrt_timed_push16(ctx, ctx->hl); cycles = 0; break; /* PUSH HL */
            case 0xF5: gb_pack_flags(ctx); gbrt_timed_push16(ctx, ctx->af & 0xFFF0); cycles = 0; break; /* PUSH AF */
            
            case 0xC1: ctx->bc = gbrt_timed_pop16(ctx); cycles = 0; break; /* POP BC */
            case 0xD1: ctx->de = gbrt_timed_pop16(ctx); cycles = 0; break; /* POP DE */
            case 0xE1: ctx->hl = gbrt_timed_pop16(ctx); cycles = 0; break; /* POP HL */
            case 0xF1: {
                uint16_t af = gbrt_timed_pop16(ctx);
                ctx->af = af & 0xFFF0; /* Lower 4 bits of F are always 0 */
                gb_unpack_flags(ctx);
                cycles = 0;
                break; 
            }
            
            /* ALU 8-bit */
            case 0x04: ctx->b = gb_inc8(ctx, ctx->b); break; /* INC B */
            case 0x05: ctx->b = gb_dec8(ctx, ctx->b); break; /* DEC B */
            case 0x0C: ctx->c = gb_inc8(ctx, ctx->c); break; /* INC C */
            case 0x0D: ctx->c = gb_dec8(ctx, ctx->c); break; /* DEC C */
            case 0x14: ctx->d = gb_inc8(ctx, ctx->d); break; /* INC D */
            case 0x15: ctx->d = gb_dec8(ctx, ctx->d); break; /* DEC D */
            case 0x1C: ctx->e = gb_inc8(ctx, ctx->e); break; /* INC E */
            case 0x1D: ctx->e = gb_dec8(ctx, ctx->e); break; /* DEC E */
            case 0x24: ctx->h = gb_inc8(ctx, ctx->h); break; /* INC H */
            case 0x25: ctx->h = gb_dec8(ctx, ctx->h); break; /* DEC H */
            case 0x2C: ctx->l = gb_inc8(ctx, ctx->l); break; /* INC L */
            case 0x2D: ctx->l = gb_dec8(ctx, ctx->l); break; /* DEC L */
            case 0x3C: ctx->a = gb_inc8(ctx, ctx->a); break; /* INC A */
            case 0x3D: ctx->a = gb_dec8(ctx, ctx->a); break; /* DEC A */
            case 0x34: { /* INC (HL) */
                const uint16_t target = ctx->hl;
                uint8_t value = gbrt_timed_bus_read8(ctx,
                                                     target,
                                                     (uint8_t)(cycles - 5u));
                value = gb_inc8(ctx, value);
                gbrt_timed_bus_rmw_write8(ctx, target, value);
                cycles = 0;
                break;
            }
            case 0x35: { /* DEC (HL) */
                const uint16_t target = ctx->hl;
                uint8_t value = gbrt_timed_bus_read8(ctx,
                                                     target,
                                                     (uint8_t)(cycles - 5u));
                value = gb_dec8(ctx, value);
                gbrt_timed_bus_rmw_write8(ctx, target, value);
                cycles = 0;
                break;
            }
            case 0x36: { uint8_t value = READ8(ctx); interpreter_final_write8(ctx, ctx->hl, value, &cycles); break; } /* LD (HL), n */

            case 0x80: gb_add8(ctx, ctx->b); break; /* ADD A, B */
            case 0x81: gb_add8(ctx, ctx->c); break; /* ADD A, C */
            case 0x82: gb_add8(ctx, ctx->d); break; /* ADD A, D */
            case 0x83: gb_add8(ctx, ctx->e); break; /* ADD A, E */
            case 0x84: gb_add8(ctx, ctx->h); break; /* ADD A, H */
            case 0x85: gb_add8(ctx, ctx->l); break; /* ADD A, L */
            case 0x86: gb_add8(ctx, interpreter_final_read8(ctx, ctx->hl, &cycles)); break; /* ADD A, (HL) */
            case 0x87: gb_add8(ctx, ctx->a); break; /* ADD A, A */
            case 0xC6: gb_add8(ctx, READ8(ctx)); break; /* ADD A, n */

            case 0x88: gb_adc8(ctx, ctx->b); break; /* ADC A, B */
            case 0x89: gb_adc8(ctx, ctx->c); break; /* ADC A, C */
            case 0x8A: gb_adc8(ctx, ctx->d); break; /* ADC A, D */
            case 0x8B: gb_adc8(ctx, ctx->e); break; /* ADC A, E */
            case 0x8C: gb_adc8(ctx, ctx->h); break; /* ADC A, H */
            case 0x8D: gb_adc8(ctx, ctx->l); break; /* ADC A, L */
            case 0x8E: gb_adc8(ctx, interpreter_final_read8(ctx, ctx->hl, &cycles)); break; /* ADC A, (HL) */
            case 0x8F: gb_adc8(ctx, ctx->a); break; /* ADC A, A */
            case 0xCE: gb_adc8(ctx, READ8(ctx)); break; /* ADC A, n */

            case 0x90: gb_sub8(ctx, ctx->b); break; /* SUB B */
            case 0x91: gb_sub8(ctx, ctx->c); break; /* SUB C */
            case 0x92: gb_sub8(ctx, ctx->d); break; /* SUB D */
            case 0x93: gb_sub8(ctx, ctx->e); break; /* SUB E */
            case 0x94: gb_sub8(ctx, ctx->h); break; /* SUB H */
            case 0x95: gb_sub8(ctx, ctx->l); break; /* SUB L */
            case 0x96: gb_sub8(ctx, interpreter_final_read8(ctx, ctx->hl, &cycles)); break; /* SUB (HL) */
            case 0x97: gb_sub8(ctx, ctx->a); break; /* SUB A */
            case 0xD6: gb_sub8(ctx, READ8(ctx)); break; /* SUB n */

            case 0x98: gb_sbc8(ctx, ctx->b); break; /* SBC A, B */
            case 0x99: gb_sbc8(ctx, ctx->c); break; /* SBC A, C */
            case 0x9A: gb_sbc8(ctx, ctx->d); break; /* SBC A, D */
            case 0x9B: gb_sbc8(ctx, ctx->e); break; /* SBC A, E */
            case 0x9C: gb_sbc8(ctx, ctx->h); break; /* SBC A, H */
            case 0x9D: gb_sbc8(ctx, ctx->l); break; /* SBC A, L */
            case 0x9E: gb_sbc8(ctx, interpreter_final_read8(ctx, ctx->hl, &cycles)); break; /* SBC A, (HL) */
            case 0x9F: gb_sbc8(ctx, ctx->a); break; /* SBC A, A */
            case 0xDE: gb_sbc8(ctx, READ8(ctx)); break; /* SBC A, n */

            case 0xA0: gb_and8(ctx, ctx->b); break; /* AND B */
            case 0xA1: gb_and8(ctx, ctx->c); break; /* AND C */
            case 0xA2: gb_and8(ctx, ctx->d); break; /* AND D */
            case 0xA3: gb_and8(ctx, ctx->e); break; /* AND E */
            case 0xA4: gb_and8(ctx, ctx->h); break; /* AND H */
            case 0xA5: gb_and8(ctx, ctx->l); break; /* AND L */
            case 0xA6: gb_and8(ctx, interpreter_final_read8(ctx, ctx->hl, &cycles)); break; /* AND (HL) */
            case 0xA7: gb_and8(ctx, ctx->a); break; /* AND A */
            case 0xE6: gb_and8(ctx, READ8(ctx)); break; /* AND n */

            case 0xA8: gb_xor8(ctx, ctx->b); break; /* XOR B */
            case 0xA9: gb_xor8(ctx, ctx->c); break; /* XOR C */
            case 0xAA: gb_xor8(ctx, ctx->d); break; /* XOR D */
            case 0xAB: gb_xor8(ctx, ctx->e); break; /* XOR E */
            case 0xAC: gb_xor8(ctx, ctx->h); break; /* XOR H */
            case 0xAD: gb_xor8(ctx, ctx->l); break; /* XOR L */
            case 0xAE: gb_xor8(ctx, interpreter_final_read8(ctx, ctx->hl, &cycles)); break; /* XOR (HL) */
            case 0xAF: gb_xor8(ctx, ctx->a); break; /* XOR A */
            case 0xEE: gb_xor8(ctx, READ8(ctx)); break; /* XOR n */
            
            case 0xB0: gb_or8(ctx, ctx->b); break; /* OR B */
            case 0xB1: gb_or8(ctx, ctx->c); break; /* OR C */
            case 0xB2: gb_or8(ctx, ctx->d); break; /* OR D */
            case 0xB3: gb_or8(ctx, ctx->e); break; /* OR E */
            case 0xB4: gb_or8(ctx, ctx->h); break; /* OR H */
            case 0xB5: gb_or8(ctx, ctx->l); break; /* OR L */
            case 0xB6: gb_or8(ctx, interpreter_final_read8(ctx, ctx->hl, &cycles)); break; /* OR (HL) */
            case 0xB7: gb_or8(ctx, ctx->a); break; /* OR A */
            case 0xF6: gb_or8(ctx, READ8(ctx)); break; /* OR n */

            case 0xB8: gb_cp8(ctx, ctx->b); break; /* CP B */
            case 0xB9: gb_cp8(ctx, ctx->c); break; /* CP C */
            case 0xBA: gb_cp8(ctx, ctx->d); break; /* CP D */
            case 0xBB: gb_cp8(ctx, ctx->e); break; /* CP E */
            case 0xBC: gb_cp8(ctx, ctx->h); break; /* CP H */
            case 0xBD: gb_cp8(ctx, ctx->l); break; /* CP L */
            case 0xBE: gb_cp8(ctx, interpreter_final_read8(ctx, ctx->hl, &cycles)); break; /* CP (HL) */
            case 0xBF: gb_cp8(ctx, ctx->a); break; /* CP A */
            case 0xFE: gb_cp8(ctx, READ8(ctx)); break; /* CP n */


            
            /* ALU 16-bit */
            case 0x03: gbrt_timed_inc16(ctx, &ctx->bc); cycles = 0; break; /* INC BC */
            case 0x13: gbrt_timed_inc16(ctx, &ctx->de); cycles = 0; break; /* INC DE */
            case 0x23: gbrt_timed_inc16(ctx, &ctx->hl); cycles = 0; break; /* INC HL */
            case 0x33: gbrt_timed_inc16(ctx, &ctx->sp); cycles = 0; break; /* INC SP */
            
            case 0x0B: gbrt_timed_dec16(ctx, &ctx->bc); cycles = 0; break; /* DEC BC */
            case 0x1B: gbrt_timed_dec16(ctx, &ctx->de); cycles = 0; break; /* DEC DE */
            case 0x2B: gbrt_timed_dec16(ctx, &ctx->hl); cycles = 0; break; /* DEC HL */
            case 0x3B: gbrt_timed_dec16(ctx, &ctx->sp); cycles = 0; break; /* DEC SP */

            case 0x09: gb_add16(ctx, ctx->bc); break; /* ADD HL, BC */
            case 0x19: gb_add16(ctx, ctx->de); break; /* ADD HL, DE */
            case 0x29: gb_add16(ctx, ctx->hl); break; /* ADD HL, HL */
            case 0x39: gb_add16(ctx, ctx->sp); break; /* ADD HL, SP */
            
            case 0xE8: /* ADD SP, n */
                gbrt_timed_add_sp(ctx, ctx->pc++);
                cycles = 0;
                break;
            case 0xF8: { /* LD HL, SP+n */
                gbrt_timed_ld_hl_sp_n(ctx, ctx->pc++);
                cycles = 0;
                break;
            }

            /* Control Flow */
            case 0xC3: { /* JP nn */
                uint16_t dest = interpreter_read_imm16_timed(ctx);
                gbrt_log_trace(ctx, (dest < 0x4000) ? 0 : ctx->rom_bank, dest);
                gbrt_timed_jump(ctx, dest, 4);
                GBRT_INTERPRETER_RETURN();
            }
            case 0xE9: { /* JP HL */
                gbrt_log_trace(ctx, (ctx->hl < 0x4000) ? 0 : ctx->rom_bank, ctx->hl);
                gbrt_timed_jump(ctx, ctx->hl, cycles);
                GBRT_INTERPRETER_RETURN();
            }
            
            case 0xC2: { /* JP NZ, nn */
                uint16_t dest = interpreter_read_imm16_timed(ctx);
                if (!ctx->f_z) { 
                    gbrt_log_trace(ctx, (dest < 0x4000) ? 0 : ctx->rom_bank, dest);
                    gbrt_timed_jump(ctx, dest, 4);
                    GBRT_INTERPRETER_RETURN();
                }
                cycles = 0;
                break;
            }
            case 0xCA: { /* JP Z, nn */
                uint16_t dest = interpreter_read_imm16_timed(ctx);
                if (ctx->f_z) { 
                    gbrt_log_trace(ctx, (dest < 0x4000) ? 0 : ctx->rom_bank, dest);
                    gbrt_timed_jump(ctx, dest, 4);
                    GBRT_INTERPRETER_RETURN();
                }
                cycles = 0;
                break;
            }
            case 0xD2: { /* JP NC, nn */
                uint16_t dest = interpreter_read_imm16_timed(ctx);
                if (!ctx->f_c) { 
                    gbrt_log_trace(ctx, (dest < 0x4000) ? 0 : ctx->rom_bank, dest);
                    gbrt_timed_jump(ctx, dest, 4);
                    GBRT_INTERPRETER_RETURN();
                }
                cycles = 0;
                break;
            }
            case 0xDA: { /* JP C, nn */
                uint16_t dest = interpreter_read_imm16_timed(ctx);
                if (ctx->f_c) { 
                    gbrt_log_trace(ctx, (dest < 0x4000) ? 0 : ctx->rom_bank, dest);
                    gbrt_timed_jump(ctx, dest, 4);
                    GBRT_INTERPRETER_RETURN();
                }
                cycles = 0;
                break;
            }
            
            case 0x18: { /* JR n */
                int8_t off = (int8_t)READ8(ctx);
                uint16_t dest = ctx->pc + off;
                gbrt_log_trace(ctx, (dest < 0x4000) ? 0 : ctx->rom_bank, dest);
                ctx->pc = dest;
                gb_tick(ctx, cycles);
                GBRT_INTERPRETER_RETURN();
            }
            case 0x20: { /* JR NZ, n */
                int8_t off = (int8_t)READ8(ctx);
                if (!ctx->f_z) { 
                    uint16_t dest = ctx->pc + off;
                    gbrt_log_trace(ctx, (dest < 0x4000) ? 0 : ctx->rom_bank, dest);
                    ctx->pc = dest; 
                    gb_tick(ctx, cycles + BRANCH_TAKEN_EXTRA); 
                    GBRT_INTERPRETER_RETURN();
                }
                break;
            }
            case 0x28: { /* JR Z, n */
                int8_t off = (int8_t)READ8(ctx);
                if (ctx->f_z) { 
                    uint16_t dest = ctx->pc + off;
                    gbrt_log_trace(ctx, (dest < 0x4000) ? 0 : ctx->rom_bank, dest);
                    ctx->pc = dest; 
                    gb_tick(ctx, cycles + BRANCH_TAKEN_EXTRA); 
                    GBRT_INTERPRETER_RETURN();
                }
                break;
            }
            case 0x30: { /* JR NC, n */
                int8_t off = (int8_t)READ8(ctx);
                if (!ctx->f_c) { 
                    uint16_t dest = ctx->pc + off;
                    gbrt_log_trace(ctx, (dest < 0x4000) ? 0 : ctx->rom_bank, dest);
                    ctx->pc = dest; 
                    gb_tick(ctx, cycles + BRANCH_TAKEN_EXTRA); 
                    GBRT_INTERPRETER_RETURN();
                }
                break;
            }
            case 0x38: { /* JR C, n */
                int8_t off = (int8_t)READ8(ctx);
                if (ctx->f_c) { 
                    uint16_t dest = ctx->pc + off;
                    gbrt_log_trace(ctx, (dest < 0x4000) ? 0 : ctx->rom_bank, dest);
                    ctx->pc = dest; 
                    gb_tick(ctx, cycles + BRANCH_TAKEN_EXTRA); 
                    GBRT_INTERPRETER_RETURN();
                }
                break;
            }
            
            case 0xCD: { /* CALL nn */
                uint16_t dest = interpreter_read_imm16_timed(ctx);
                gbrt_log_trace(ctx, (dest < 0x4000) ? 0 : ctx->rom_bank, dest);
                gbrt_timed_call_after_imm16(ctx, dest, ctx->pc);
                GBRT_INTERPRETER_RETURN();
            }
            case 0xC4: { /* CALL NZ, nn */
                uint16_t dest = interpreter_read_imm16_timed(ctx);
                if (!ctx->f_z) {
                    gbrt_log_trace(ctx, (dest < 0x4000) ? 0 : ctx->rom_bank, dest);
                    gbrt_timed_call_after_imm16(ctx, dest, ctx->pc);
                    GBRT_INTERPRETER_RETURN();
                }
                cycles = 0;
                break;
            }
            case 0xCC: { /* CALL Z, nn */
                uint16_t dest = interpreter_read_imm16_timed(ctx);
                if (ctx->f_z) {
                    gbrt_log_trace(ctx, (dest < 0x4000) ? 0 : ctx->rom_bank, dest);
                    gbrt_timed_call_after_imm16(ctx, dest, ctx->pc);
                    GBRT_INTERPRETER_RETURN();
                }
                cycles = 0;
                break;
            }
            case 0xD4: { /* CALL NC, nn */
                uint16_t dest = interpreter_read_imm16_timed(ctx);
                if (!ctx->f_c) {
                    gbrt_log_trace(ctx, (dest < 0x4000) ? 0 : ctx->rom_bank, dest);
                    gbrt_timed_call_after_imm16(ctx, dest, ctx->pc);
                    GBRT_INTERPRETER_RETURN();
                }
                cycles = 0;
                break;
            }
            case 0xDC: { /* CALL C, nn */
                uint16_t dest = interpreter_read_imm16_timed(ctx);
                if (ctx->f_c) {
                    gbrt_log_trace(ctx, (dest < 0x4000) ? 0 : ctx->rom_bank, dest);
                    gbrt_timed_call_after_imm16(ctx, dest, ctx->pc);
                    GBRT_INTERPRETER_RETURN();
                }
                cycles = 0;
                break;
            }
            
            case 0xC9: /* RET */
                gbrt_timed_ret(ctx);
                GBRT_INTERPRETER_RETURN();
            case 0xC0: /* RET NZ */
                if (!ctx->f_z) { gbrt_timed_ret_cc(ctx); GBRT_INTERPRETER_RETURN(); }
                break;
            case 0xC8: /* RET Z */
                if (ctx->f_z) { gbrt_timed_ret_cc(ctx); GBRT_INTERPRETER_RETURN(); }
                break;
            case 0xD0: /* RET NC */
                if (!ctx->f_c) { gbrt_timed_ret_cc(ctx); GBRT_INTERPRETER_RETURN(); }
                break;
            case 0xD8: /* RET C */
                if (ctx->f_c) { gbrt_timed_ret_cc(ctx); GBRT_INTERPRETER_RETURN(); }
                break;
            case 0xD9: /* RETI */
                gbrt_timed_reti(ctx);
                GBRT_INTERPRETER_RETURN();
                
            case 0xC7: gbrt_timed_rst(ctx, 0x00, ctx->pc); GBRT_INTERPRETER_RETURN();
            case 0xCF: gbrt_timed_rst(ctx, 0x08, ctx->pc); GBRT_INTERPRETER_RETURN();
            case 0xD7: gbrt_timed_rst(ctx, 0x10, ctx->pc); GBRT_INTERPRETER_RETURN();
            case 0xDF: gbrt_timed_rst(ctx, 0x18, ctx->pc); GBRT_INTERPRETER_RETURN();
            case 0xE7: gbrt_timed_rst(ctx, 0x20, ctx->pc); GBRT_INTERPRETER_RETURN();
            case 0xEF: gbrt_timed_rst(ctx, 0x28, ctx->pc); GBRT_INTERPRETER_RETURN();
            case 0xF7: gbrt_timed_rst(ctx, 0x30, ctx->pc); GBRT_INTERPRETER_RETURN();
            case 0xFF: gbrt_timed_rst(ctx, 0x38, ctx->pc); GBRT_INTERPRETER_RETURN();
                
            case 0xF3: ctx->ime = 0; ctx->ime_pending = 0; break; /* DI - also cancel pending EI */
            case 0xFB: ctx->ime_pending = 1; break; /* EI */
            
            /* Unused / Illegal opcodes (No-ops on some hardware, can reach here in tests) */
            case 0xD3: case 0xDB: case 0xDD: case 0xE3: case 0xE4:
            case 0xEB: case 0xEC: case 0xED: case 0xF4: case 0xFC:
            case 0xFD:
                DBG_GENERAL("Interpreter (0x%04X): Executed unused opcode 0x%02X", ctx->pc - 1, opcode);
                break;

            
            /* CB Prefix */
            case 0xCB: {
                uint8_t cb_op = READ8(ctx);
                cycles = CB_OPCODE_CYCLES[cb_op];  /* Override with CB-specific timing */
                uint8_t r = cb_op & 7;
                uint8_t b = (cb_op >> 3) & 7;
                const bool memory_operand = r == 6;
                const bool memory_rmw = memory_operand &&
                                        !(cb_op >= 0x40 && cb_op < 0x80);
                const uint16_t memory_target = ctx->hl;
                uint8_t val;
                if (memory_operand) {
                    const uint8_t leading_cycles = memory_rmw
                        ? (uint8_t)(cycles - 5u)
                        : (uint8_t)(cycles - 1u);
                    val = gbrt_timed_bus_read8(ctx,
                                               memory_target,
                                               leading_cycles);
                    if (!memory_rmw) {
                        cycles = 0;
                    }
                }
                else {
                    val = get_reg8(ctx, r);
                }
                
                if (cb_op < 0x40) {
                    /* Shifts and Rotates */
                    switch (b) {
                        case 0: val = gb_rlc(ctx, val); break;
                        case 1: val = gb_rrc(ctx, val); break;
                        case 2: val = gb_rl(ctx, val); break;
                        case 3: val = gb_rr(ctx, val); break;
                        case 4: val = gb_sla(ctx, val); break;
                        case 5: val = gb_sra(ctx, val); break;
                        case 6: val = gb_swap(ctx, val); break;
                        case 7: val = gb_srl(ctx, val); break;
                    }
                    if (memory_rmw) {
                        gbrt_timed_bus_rmw_write8(ctx, memory_target, val);
                        cycles = 0;
                    }
                    else {
                        set_reg8(ctx, r, val);
                    }
                }
                else if (cb_op < 0x80) {
                    /* BIT */
                    gb_bit(ctx, b, val);
                }
                else if (cb_op < 0xC0) {
                    /* RES */
                    val &= ~(1 << b);
                    if (memory_rmw) {
                        gbrt_timed_bus_rmw_write8(ctx, memory_target, val);
                        cycles = 0;
                    }
                    else {
                        set_reg8(ctx, r, val);
                    }
                }
                else {
                    /* SET */
                    val |= (1 << b);
                    if (memory_rmw) {
                        gbrt_timed_bus_rmw_write8(ctx, memory_target, val);
                        cycles = 0;
                    }
                    else {
                        set_reg8(ctx, r, val);
                    }
                }
                break;
            }
            
            default:
                gbrt_note_unimplemented_interpreter_opcode(ctx,
                                                           (uint16_t)(((ctx->pc - 1) < 0x4000) ? 0 : ctx->rom_bank),
                                                           (uint16_t)(ctx->pc - 1),
                                                           opcode);
                DBG_GENERAL("Interpreter (0x%04X): Unimplemented opcode 0x%02X", ctx->pc - 1, opcode);
                /* If we return, we might re-execute garbage or loop. Better to stop/break hard? */
                /* For now, return to dispatch (which might call us again if PC didn't advance?)
                   Actually we advanced PC at READ8. */
                gbrt_finish_interpreter_session(ctx, entry_bank, addr, instructions_executed, entry_cycles);
                return;
        }
        
        /* Per-instruction cycle counting - uses table lookup + extra for branches taken */
        gb_tick(ctx, cycles + extra_cycles);
        if (ctx->single_step_mode) {
            gbrt_finish_interpreter_session(ctx, entry_bank, addr, instructions_executed, entry_cycles);
            return;
        }
    }

    gbrt_finish_interpreter_session(ctx, entry_bank, addr, instructions_executed, entry_cycles);
#undef GBRT_INTERPRETER_RETURN
}
