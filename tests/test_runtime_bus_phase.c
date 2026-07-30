#include "gbrt.h"
#include "ppu.h"

#include <stdio.h>
#include <string.h>

static GBContext* make_context(void) {
    GBConfig config;
    memset(&config, 0, sizeof(config));
    config.model = GB_MODEL_DMG;

    GBContext* ctx = gb_context_create(&config);
    if (!ctx) {
        return NULL;
    }

    uint8_t rom[32u * 1024u];
    memset(rom, 0, sizeof(rom));
    if (!gb_context_load_rom(ctx, rom, sizeof(rom))) {
        gb_context_destroy(ctx);
        return NULL;
    }
    gb_context_reset(ctx, true);
    return ctx;
}

static void prepare_vram_boundary(GBContext* ctx,
                                  uint32_t start_dot,
                                  uint8_t low,
                                  uint8_t high) {
    gb_write8(ctx, 0xFF40, 0);
    gb_write8(ctx, 0x8000, low);
    gb_write8(ctx, 0x8001, high);
    gb_write8(ctx, 0xFF40, LCDC_LCD_ENABLE | LCDC_BG_ENABLE);
    gb_tick(ctx, start_dot);
}

static void prepare_dma_immediate_boundary(GBContext* ctx,
                                           uint16_t cycles_remaining) {
    ctx->oam[0] = 0x42;
    ctx->dma.active = 1;
    ctx->dma.pending = 0;
    ctx->dma.active_source_high = 0x80;
    ctx->dma.progress = 158;
    ctx->dma.cycles_remaining = cycles_remaining;
}

int main(void) {
    GBContext* ctx = make_context();
    if (!ctx) {
        return 2;
    }

    /* ADD SP,e samples its immediate late in M1. If OAM DMA has one T-cycle
     * left at that boundary, FE00 is still blocked and reads as FF (-1). */
    prepare_dma_immediate_boundary(ctx, 8);
    ctx->sp = 0;
    const uint32_t add_sp_blocked_start = ctx->cycles;
    gbrt_timed_add_sp(ctx, 0xFE00);
    if (ctx->cycles - add_sp_blocked_start != 16 ||
        ctx->sp != 0xFFFF ||
        ctx->dma.active) {
        fputs("timed ADD SP,e did not sample its immediate in M1\n", stderr);
        gb_context_destroy(ctx);
        return 1;
    }

    /* Moving DMA completion one T-cycle earlier exposes the OAM byte at the
     * same immediate-read phase, proving the helper does not sample at entry. */
    prepare_dma_immediate_boundary(ctx, 7);
    ctx->sp = 0;
    const uint32_t add_sp_visible_start = ctx->cycles;
    gbrt_timed_add_sp(ctx, 0xFE00);
    if (ctx->cycles - add_sp_visible_start != 16 ||
        ctx->sp != 0x0042 ||
        ctx->dma.active) {
        fputs("timed ADD SP,e sampled its immediate before M1\n", stderr);
        gb_context_destroy(ctx);
        return 1;
    }

    /* The interpreter sees the same boundary when executing copied code with
     * its opcode at FDFF and immediate at FE00. */
    gb_write8(ctx, 0xFDFF, 0xE8);
    prepare_dma_immediate_boundary(ctx, 7);
    ctx->pc = 0xFDFF;
    ctx->sp = 0;
    const uint32_t interpreted_add_sp_start = ctx->cycles;
    if (gb_debug_step(ctx, GB_EXECUTION_INTERPRETER) != 16 ||
        ctx->cycles - interpreted_add_sp_start != 16 ||
        ctx->pc != 0xFE01 ||
        ctx->sp != 0x0042 ||
        ctx->dma.active) {
        fputs("interpreter ADD SP,e bypassed the timed immediate read\n", stderr);
        gb_context_destroy(ctx);
        return 1;
    }

    /* The copied-RAM fast path uses the same primitive for HRAM helpers. */
    gb_write8(ctx, 0xFF80, 0xE8);
    gb_write8(ctx, 0xFF81, 0x42);
    prepare_dma_immediate_boundary(ctx, 8);
    ctx->pc = 0xFF80;
    ctx->sp = 0;
    const uint32_t stub_add_sp_start = ctx->cycles;
    if (!gbrt_try_execute_ram_stub(ctx, 0xFF80) ||
        ctx->cycles - stub_add_sp_start != 16 ||
        ctx->pc != 0xFF82 ||
        ctx->sp != 0x0042) {
        fputs("copied-RAM ADD SP,e bypassed the timed immediate read\n", stderr);
        gb_context_destroy(ctx);
        return 1;
    }

    /* LD HL,SP+e shares ADD SP,e's M1 immediate phase but retires after one
     * idle M-cycle. Check both sides of the DMA completion boundary. */
    prepare_dma_immediate_boundary(ctx, 8);
    ctx->sp = 0;
    const uint32_t ld_hl_blocked_start = ctx->cycles;
    gbrt_timed_ld_hl_sp_n(ctx, 0xFE00);
    if (ctx->cycles - ld_hl_blocked_start != 12 ||
        ctx->hl != 0xFFFF ||
        ctx->dma.active) {
        fputs("timed LD HL,SP+e did not sample its immediate in M1\n", stderr);
        gb_context_destroy(ctx);
        return 1;
    }

    prepare_dma_immediate_boundary(ctx, 7);
    ctx->sp = 0;
    const uint32_t ld_hl_visible_start = ctx->cycles;
    gbrt_timed_ld_hl_sp_n(ctx, 0xFE00);
    if (ctx->cycles - ld_hl_visible_start != 12 ||
        ctx->hl != 0x0042 ||
        ctx->dma.active) {
        fputs("timed LD HL,SP+e sampled its immediate before M1\n", stderr);
        gb_context_destroy(ctx);
        return 1;
    }

    /* Interpreter execution at the OAM boundary must use the same M1 read. */
    gb_write8(ctx, 0xFDFF, 0xF8);
    prepare_dma_immediate_boundary(ctx, 7);
    ctx->pc = 0xFDFF;
    ctx->sp = 0;
    const uint32_t interpreted_ld_hl_start = ctx->cycles;
    if (gb_debug_step(ctx, GB_EXECUTION_INTERPRETER) != 12 ||
        ctx->cycles - interpreted_ld_hl_start != 12 ||
        ctx->pc != 0xFE01 ||
        ctx->hl != 0x0042 ||
        ctx->dma.active) {
        fputs("interpreter LD HL,SP+e bypassed the timed immediate read\n", stderr);
        gb_context_destroy(ctx);
        return 1;
    }

    /* Copied HRAM execution must route through the shared timed primitive. */
    gb_write8(ctx, 0xFF80, 0xF8);
    gb_write8(ctx, 0xFF81, 0x42);
    prepare_dma_immediate_boundary(ctx, 8);
    ctx->pc = 0xFF80;
    ctx->sp = 0;
    const uint32_t stub_ld_hl_start = ctx->cycles;
    if (!gbrt_try_execute_ram_stub(ctx, 0xFF80) ||
        ctx->cycles - stub_ld_hl_start != 12 ||
        ctx->pc != 0xFF82 ||
        ctx->hl != 0x0042) {
        fputs("copied-RAM LD HL,SP+e bypassed the timed immediate read\n", stderr);
        gb_context_destroy(ctx);
        return 1;
    }

    /* SameBoy commits a generic read-modify-write store at the start of its
     * final M-cycle. SET 7,(HL) therefore enables LCDC with four retirement
     * dots still remaining, rather than delaying the write to the last dot. */
    gb_write8(ctx, 0xFF40, 0);
    gb_write8(ctx, 0xC000, 0xCB);
    gb_write8(ctx, 0xC001, 0xFE); /* SET 7,(HL) */
    ctx->pc = 0xC000;
    ctx->hl = 0xFF40;
    const uint32_t interpreted_set_lcdc_start = ctx->cycles;
    if (gb_debug_step(ctx, GB_EXECUTION_INTERPRETER) != 16 ||
        ctx->cycles - interpreted_set_lcdc_start != 16 ||
        ctx->pc != 0xC002 ||
        !(ctx->io[0x40] & LCDC_LCD_ENABLE) ||
        ((GBPPU*)ctx->ppu)->mode_cycles != 4) {
        fputs("interpreter RMW write did not begin its final M-cycle\n", stderr);
        gb_context_destroy(ctx);
        return 1;
    }

    /* The first LCD line enters mode 3 at dot 79. A copied `LD (HL),A`
     * beginning at dot 73 must put its data-bus write in the final M-cycle,
     * after mode 3 has started, so the OAM write is blocked. */
    gb_write8(ctx, 0xFF40, 0);
    gb_write8(ctx, 0xFF40, LCDC_LCD_ENABLE | LCDC_BG_ENABLE);
    gb_tick(ctx, 73);
    gb_write8(ctx, 0xC000, 0x77); /* LD (HL),A */
    ctx->hl = 0xFE00;
    ctx->a = 0x55;
    ctx->oam[0] = 0;

    const uint32_t start_cycles = ctx->cycles;
    if (!gbrt_try_execute_ram_stub(ctx, 0xC000)) {
        fputs("RAM stub executor rejected a supported LD (HL),A\n", stderr);
        gb_context_destroy(ctx);
        return 1;
    }
    if (ctx->cycles - start_cycles != 8) {
        fprintf(stderr, "RAM stub used %u cycles instead of 8\n",
                ctx->cycles - start_cycles);
        gb_context_destroy(ctx);
        return 1;
    }
    if (ctx->oam[0] != 0) {
        fputs("RAM stub performed its OAM write before the final M-cycle\n",
              stderr);
        gb_context_destroy(ctx);
        return 1;
    }

    /* Opcodes not recognized by the fast RAM-stub path fall through to the
     * general interpreter and must use the same bus phase. */
    gb_write8(ctx, 0xFF40, 0);
    gb_write8(ctx, 0xFF40, LCDC_LCD_ENABLE | LCDC_BG_ENABLE);
    gb_tick(ctx, 73);
    gb_write8(ctx, 0xC000, 0x12); /* LD (DE),A */
    ctx->pc = 0xC000;
    ctx->de = 0xFE00;
    ctx->a = 0x66;
    ctx->oam[0] = 0;

    const uint32_t interpreted_start_cycles = ctx->cycles;
    if (gb_debug_step(ctx, GB_EXECUTION_INTERPRETER) != 8) {
        fputs("interpreter did not retire LD (DE),A in 8 cycles\n", stderr);
        gb_context_destroy(ctx);
        return 1;
    }
    if (ctx->cycles - interpreted_start_cycles != 8 || ctx->oam[0] != 0) {
        fputs("interpreter performed its OAM write before the final M-cycle\n",
              stderr);
        gb_context_destroy(ctx);
        return 1;
    }

    /* CB BIT b,(HL) fetches the CB opcode in M1 and reads (HL) in M2. Start
     * at dot 69 so a correct M2 VRAM read lands after mode 3 begins and sees
     * FF, while an eager read at instruction entry would still see 00. */
    prepare_vram_boundary(ctx, 69, 0x00, 0x00);
    gb_write8(ctx, 0xC000, 0xCB);
    gb_write8(ctx, 0xC001, 0x46); /* BIT 0,(HL) */
    ctx->pc = 0xC000;
    ctx->hl = 0x8000;
    ctx->f_z = 1;

    const uint32_t interpreted_bit_start = ctx->cycles;
    if (gb_debug_step(ctx, GB_EXECUTION_INTERPRETER) != 12 ||
        ctx->cycles - interpreted_bit_start != 12 ||
        ctx->pc != 0xC002 ||
        ctx->f_z != 0) {
        fputs("interpreter BIT (HL) did not read in its final M-cycle\n",
              stderr);
        gb_context_destroy(ctx);
        return 1;
    }

    /* INC (HL) reads in M1 and writes in M2. At this boundary the read must
     * still see VRAM in mode 2, but the following write must be rejected in
     * mode 3. Eager or collapsed read/write execution cannot satisfy both. */
    prepare_vram_boundary(ctx, 71, 0x0F, 0x00);
    gb_write8(ctx, 0xC000, 0x34); /* INC (HL) */
    ctx->pc = 0xC000;
    ctx->hl = 0x8000;
    ctx->f_z = 1;
    ctx->f_n = 1;
    ctx->f_h = 0;

    const uint32_t interpreted_inc_hl_start = ctx->cycles;
    if (gb_debug_step(ctx, GB_EXECUTION_INTERPRETER) != 12 ||
        ctx->cycles - interpreted_inc_hl_start != 12 ||
        ctx->pc != 0xC001 ||
        ctx->vram[0] != 0x0F ||
        ctx->f_z != 0 ||
        ctx->f_n != 0 ||
        ctx->f_h != 1) {
        fputs("interpreter INC (HL) did not split read and write M-cycles\n",
              stderr);
        gb_context_destroy(ctx);
        return 1;
    }

    /* CB read-modify-write instructions add a prefix-fetch M-cycle, so their
     * (HL) read/write pair is M2/M3. The same boundary must expose the read
     * and block the write without collapsing the two accesses. */
    prepare_vram_boundary(ctx, 67, 0x81, 0x00);
    gb_write8(ctx, 0xC000, 0xCB);
    gb_write8(ctx, 0xC001, 0x06); /* RLC (HL) */
    ctx->pc = 0xC000;
    ctx->hl = 0x8000;
    ctx->f_z = 1;
    ctx->f_c = 0;

    const uint32_t interpreted_rlc_hl_start = ctx->cycles;
    if (gb_debug_step(ctx, GB_EXECUTION_INTERPRETER) != 16 ||
        ctx->cycles - interpreted_rlc_hl_start != 16 ||
        ctx->pc != 0xC002 ||
        ctx->vram[0] != 0x81 ||
        ctx->f_z != 0 ||
        ctx->f_c != 1) {
        fputs("interpreter RLC (HL) did not split read and write M-cycles\n",
              stderr);
        gb_context_destroy(ctx);
        return 1;
    }

    /* PUSH writes the high byte in its penultimate M-cycle and the low byte in
     * its final M-cycle. Start at dot 65 so the high write lands while VRAM is
     * still accessible in mode 2 and the low write lands after mode 3 begins. */
    prepare_vram_boundary(ctx, 65, 0xA5, 0x5A);
    ctx->sp = 0x8002;
    const uint32_t push_start_cycles = ctx->cycles;
    gbrt_timed_push16(ctx, 0x1234);
    if (ctx->cycles - push_start_cycles != 16 ||
        ctx->sp != 0x8000 ||
        ctx->vram[1] != 0x12 ||
        ctx->vram[0] != 0xA5) {
        fputs("timed PUSH did not split high/low writes across M-cycles\n",
              stderr);
        gb_context_destroy(ctx);
        return 1;
    }

    /* POP reads low then high. Starting at dot 69 makes the low byte visible
     * in mode 2 and the high byte blocked in mode 3. */
    prepare_vram_boundary(ctx, 69, 0x34, 0x12);
    ctx->sp = 0x8000;
    const uint32_t pop_start_cycles = ctx->cycles;
    const uint16_t popped = gbrt_timed_pop16(ctx);
    if (ctx->cycles - pop_start_cycles != 12 ||
        ctx->sp != 0x8002 ||
        popped != 0xFF34) {
        fprintf(stderr,
                "timed POP returned %04X instead of FF34 at the mode boundary\n",
                popped);
        gb_context_destroy(ctx);
        return 1;
    }

    /* CALL shares PUSH's two stack phases after its immediate fetches and
     * internal cycle. */
    prepare_vram_boundary(ctx, 57, 0xA5, 0x5A);
    ctx->sp = 0x8002;
    const uint32_t call_start_cycles = ctx->cycles;
    gbrt_timed_call(ctx, 0x3456, 0xABCD);
    if (ctx->cycles - call_start_cycles != 24 ||
        ctx->sp != 0x8000 ||
        ctx->vram[1] != 0xAB ||
        ctx->vram[0] != 0xA5 ||
        ctx->pc != 0x3456) {
        fputs("timed CALL did not preserve stack bus phases and target PC\n",
              stderr);
        gb_context_destroy(ctx);
        return 1;
    }

    /* RET reads the two bytes in M2/M3, commits PC, then consumes its final
     * internal cycle. */
    prepare_vram_boundary(ctx, 69, 0x78, 0x56);
    ctx->sp = 0x8000;
    const uint32_t ret_start_cycles = ctx->cycles;
    gbrt_timed_ret(ctx);
    if (ctx->cycles - ret_start_cycles != 16 ||
        ctx->sp != 0x8002 ||
        ctx->pc != 0xFF78) {
        fputs("timed RET did not read stack bytes in consecutive M-cycles\n",
              stderr);
        gb_context_destroy(ctx);
        return 1;
    }

    /* The interpreter must route PUSH through the same primitive. */
    prepare_vram_boundary(ctx, 65, 0xA5, 0x5A);
    gb_write8(ctx, 0xC000, 0xC5); /* PUSH BC */
    ctx->pc = 0xC000;
    ctx->sp = 0x8002;
    ctx->bc = 0xBEEF;
    const uint32_t interpreted_push_start = ctx->cycles;
    if (gb_debug_step(ctx, GB_EXECUTION_INTERPRETER) != 16 ||
        ctx->cycles - interpreted_push_start != 16 ||
        ctx->sp != 0x8000 ||
        ctx->vram[1] != 0xBE ||
        ctx->vram[0] != 0xA5) {
        fputs("interpreter PUSH bypassed the shared stack bus primitive\n",
              stderr);
        gb_context_destroy(ctx);
        return 1;
    }

    gb_context_destroy(ctx);
    return 0;
}
