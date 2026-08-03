#include "gbrt.h"

#include <stdio.h>
#include <string.h>

static GBContext* make_context(GBModel model) {
    GBConfig config;
    memset(&config, 0, sizeof(config));
    config.model = model;
    config.cartridge_supports_cgb = model == GB_MODEL_CGB;
    config.cartridge_requires_cgb = model == GB_MODEL_CGB;
    GBContext* ctx = gb_context_create(&config);
    if (!ctx) {
        return NULL;
    }

    uint8_t rom[32u * 1024u];
    memset(rom, 0, sizeof(rom));
    rom[0x143] = model == GB_MODEL_CGB ? 0xC0 : 0x00;
    rom[0x0200] = 0x5A;
    if (!gb_context_load_rom(ctx, rom, sizeof(rom))) {
        gb_context_destroy(ctx);
        return NULL;
    }
    gb_context_reset(ctx, true);
    ctx->wram[0] = 0x44;
    ctx->hram[0] = 0x55;
    return ctx;
}

static int activate_dma(GBContext* ctx, uint8_t source_high) {
    gb_write8(ctx, 0xFF46, source_high);
    gb_tick(ctx, 8);
    return ctx->dma.active ? 0 : 1;
}

static int copy_dma_source(GBContext* ctx, uint8_t source_high) {
    if (activate_dma(ctx, source_high)) {
        return 1;
    }
    gb_tick(ctx, 640);
    return ctx->dma.active || ctx->dma.pending || ctx->dma.progress != 160;
}

static int verify_dma_startup_and_restart(GBContext* ctx) {
    gb_write8(ctx, 0xFF46, 0xC0);
    if (ctx->dma.active || !ctx->dma.pending) {
        return 1;
    }
    gb_tick(ctx, 3);
    if (ctx->dma.active || !ctx->dma.pending || gb_read8(ctx, 0xC000) != 0x44) {
        return 1;
    }
    gb_tick(ctx, 1);
    if (ctx->dma.active || !ctx->dma.pending || gb_read8(ctx, 0xC000) != 0xFF) {
        return 1;
    }
    gb_tick(ctx, 4);
    if (!ctx->dma.active || ctx->dma.pending || ctx->dma.progress != 0) {
        return 1;
    }

    gb_tick(ctx, 4);
    const uint8_t old_progress = ctx->dma.progress;
    gb_write8(ctx, 0xFF46, 0xC1);
    if (!ctx->dma.active || !ctx->dma.pending) {
        return 1;
    }
    gb_tick(ctx, 7);
    if (!ctx->dma.active || !ctx->dma.pending ||
        ctx->dma.progress < old_progress) {
        return 1;
    }
    gb_tick(ctx, 1);
    return !ctx->dma.active || ctx->dma.pending || ctx->dma.progress != 0;
}

static int verify_dmg_high_source_aliases(GBContext* ctx) {
    for (uint16_t i = 0; i < 160; ++i) {
        gb_write8(ctx, (uint16_t)(0xDE00u + i), (uint8_t)(i ^ 0x3Cu));
        gb_write8(ctx, (uint16_t)(0xDF00u + i), (uint8_t)(i ^ 0xC3u));
    }

    if (copy_dma_source(ctx, 0xFE)) {
        return 1;
    }
    for (uint16_t i = 0; i < 160; ++i) {
        if (ctx->oam[i] != (uint8_t)(i ^ 0x3Cu)) {
            return 1;
        }
    }

    if (copy_dma_source(ctx, 0xFF)) {
        return 1;
    }
    for (uint16_t i = 0; i < 160; ++i) {
        if (ctx->oam[i] != (uint8_t)(i ^ 0xC3u)) {
            return 1;
        }
    }
    return 0;
}

static int execute_b_counter_hram_dma_helper(GBContext* ctx) {
    enum { DMA_TRANSFER_SIZE = 160 };
    static const uint8_t helper[] = {
        0xE0, 0x46,       /* LDH ($46),A */
        0x06, 0x28,       /* LD B,$28 */
        0x05,             /* DEC B */
        0x20, 0xFD,       /* JR NZ,-3 */
        0xC9,             /* RET */
    };
    const uint16_t helper_addr = 0xFF80;
    const uint16_t return_addr = 0xC100;

    memcpy(ctx->hram, helper, sizeof(helper));
    for (uint16_t i = 0; i < DMA_TRANSFER_SIZE; ++i) {
        ctx->wram[i] = (uint8_t)(i ^ 0xA5u);
    }
    ctx->a = 0xC0;
    /* The real helper returns through the normal WRAM stack. DMA must finish
     * during RET's pre-read machine cycles before the stack bus is sampled. */
    ctx->sp = 0xDFF0;
    gb_write16(ctx, ctx->sp, return_addr);
    ctx->pc = helper_addr;

    for (unsigned instructions = 0;
        instructions < 128 && ctx->pc != return_addr;
         ++instructions) {
        if (!gbrt_try_execute_ram_stub(ctx, ctx->pc)) {
            fprintf(stderr,
                    "HRAM DMA helper stopped at PC=%04X B=%02X active=%u pending=%u progress=%u\n",
                    ctx->pc,
                    ctx->b,
                    ctx->dma.active,
                    ctx->dma.pending,
                    ctx->dma.progress);
            return 1;
        }
    }

    if (ctx->pc != return_addr || ctx->b != 0 || ctx->dma.active || ctx->dma.pending) {
        fprintf(stderr,
                "HRAM DMA helper ended at PC=%04X B=%02X active=%u pending=%u progress=%u\n",
                ctx->pc,
                ctx->b,
                ctx->dma.active,
                ctx->dma.pending,
                ctx->dma.progress);
        return 1;
    }
    for (uint16_t i = 0; i < DMA_TRANSFER_SIZE; ++i) {
        if (ctx->oam[i] != (uint8_t)(i ^ 0xA5u)) {
            fprintf(stderr,
                    "HRAM DMA helper copied OAM[%u]=%02X, expected %02X\n",
                    i,
                    ctx->oam[i],
                    (uint8_t)(i ^ 0xA5u));
            return 1;
        }
    }
    return 0;
}

int main(void) {
    GBContext* dmg_startup = make_context(GB_MODEL_DMG);
    if (!dmg_startup || verify_dma_startup_and_restart(dmg_startup)) {
        fputs("OAM DMA startup or restart did not preserve the two-M-cycle boundary\n", stderr);
        gb_context_destroy(dmg_startup);
        return 1;
    }
    gb_context_destroy(dmg_startup);

    GBContext* dmg_sources = make_context(GB_MODEL_DMG);
    if (!dmg_sources || verify_dmg_high_source_aliases(dmg_sources)) {
        fputs("DMG OAM DMA did not alias FE/FF sources to DE/DF WRAM\n", stderr);
        gb_context_destroy(dmg_sources);
        return 1;
    }
    gb_context_destroy(dmg_sources);

    GBContext* dmg_hram = make_context(GB_MODEL_DMG);
    if (!dmg_hram || execute_b_counter_hram_dma_helper(dmg_hram)) {
        fputs("DMG B-counter HRAM DMA helper did not execute through active DMA\n", stderr);
        gb_context_destroy(dmg_hram);
        return 1;
    }
    gb_context_destroy(dmg_hram);

    GBContext* dmg = make_context(GB_MODEL_DMG);
    if (!dmg || activate_dma(dmg, 0xC0)) {
        return 2;
    }
    if (gb_read8(dmg, 0xFF80) != 0x55 ||
        gb_read8(dmg, 0x0200) != 0xFF ||
        gb_read8(dmg, 0xC000) != 0xFF) {
        fputs("DMG OAM DMA did not preserve its first transfer warm-up cycle\n", stderr);
        gb_context_destroy(dmg);
        return 1;
    }
    gb_tick(dmg, 4);
    if (gb_read8(dmg, 0xFF80) != 0x55 ||
        gb_read8(dmg, 0x0200) != 0x44 ||
        gb_read8(dmg, 0xFE00) != 0xFF) {
        fputs("DMG OAM DMA did not expose its current source byte on the shared bus\n",
              stderr);
        gb_context_destroy(dmg);
        return 1;
    }
    gb_context_destroy(dmg);

    GBContext* dmg_vram = make_context(GB_MODEL_DMG);
    if (!dmg_vram) {
        return 2;
    }
    dmg_vram->vram[0] = 0x66;
    if (activate_dma(dmg_vram, 0x80) ||
        gb_read8(dmg_vram, 0x0200) != 0x5A ||
        gb_read8(dmg_vram, 0xC000) != 0x44 ||
        gb_read8(dmg_vram, 0x8000) != 0xFF) {
        fputs("DMG VRAM-source DMA did not leave the main bus accessible\n",
              stderr);
        gb_context_destroy(dmg_vram);
        return 1;
    }
    gb_tick(dmg_vram, 4);
    if (gb_read8(dmg_vram, 0x8001) != 0x66 ||
        gb_read8(dmg_vram, 0xC000) != 0x44) {
        fputs("DMG VRAM-source DMA did not expose only its owned bus\n", stderr);
        gb_context_destroy(dmg_vram);
        return 1;
    }
    gb_context_destroy(dmg_vram);

    GBContext* cgb_wram = make_context(GB_MODEL_CGB);
    if (!cgb_wram || activate_dma(cgb_wram, 0xC0)) {
        return 2;
    }
    if (gb_read8(cgb_wram, 0x0200) != 0x5A ||
        gb_read8(cgb_wram, 0xC000) != 0xFF ||
        gb_read8(cgb_wram, 0xFF80) != 0x55) {
        fputs("CGB WRAM-source DMA blocked the wrong CPU bus\n", stderr);
        gb_context_destroy(cgb_wram);
        return 1;
    }
    gb_context_destroy(cgb_wram);

    GBContext* cgb_rom = make_context(GB_MODEL_CGB);
    if (!cgb_rom || activate_dma(cgb_rom, 0x00)) {
        return 2;
    }
    if (gb_read8(cgb_rom, 0x0200) != 0xFF ||
        gb_read8(cgb_rom, 0xC000) != 0x44) {
        fputs("CGB ROM-source DMA blocked the wrong CPU bus\n", stderr);
        gb_context_destroy(cgb_rom);
        return 1;
    }
    gb_context_destroy(cgb_rom);
    return 0;
}
