#include "gbrt.h"
#include "ppu.h"

#include <stdio.h>
#include <string.h>

static GBContext* make_context_for_model(GBModel model) {
    GBConfig config;
    memset(&config, 0, sizeof(config));
    config.model = model;

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
    memset(ctx->oam, 0, OAM_SIZE);
    memset(ctx->vram, 0, VRAM_SIZE * 2u);
    return ctx;
}

static GBContext* make_context(void) {
    return make_context_for_model(GB_MODEL_DMG);
}

static void begin_visible_line(GBContext* ctx, uint8_t lcdc, uint8_t scx) {
    GBPPU* ppu = (GBPPU*)ctx->ppu;
    ppu->lcdc = lcdc;
    ppu->scx = scx;
    ppu->scy = 0;
    ppu->ly = 0;
    ppu->scanline = 0;
    ppu->wy = 0;
    ppu->wx = 87;
    ppu->mode = PPU_MODE_OAM;
    ppu->visible_mode = PPU_MODE_OAM;
    ppu->stat_irq_mode = PPU_MODE_OAM;
    ppu->mode_cycles = 0;
    ppu->window_line = 0;
    ppu->window_triggered = false;
    ctx->io[0x40] = lcdc;
    ctx->io[0x41] = (uint8_t)((ctx->io[0x41] & ~3u) | PPU_MODE_OAM);
    ctx->io[0x44] = 0;
}

static unsigned draw_length(GBContext* ctx) {
    GBPPU* ppu = (GBPPU*)ctx->ppu;
    ppu_tick(ppu, ctx, CYCLES_OAM_SCAN);
    if (ppu->mode != PPU_MODE_DRAW) {
        return 0;
    }
    unsigned dots = 0;
    while (ppu->mode == PPU_MODE_DRAW && dots < 400) {
        ppu_tick(ppu, ctx, 1);
        dots++;
    }
    return dots;
}

static int expect_length(GBContext* ctx, unsigned expected, const char* label) {
    unsigned actual = draw_length(ctx);
    if (actual != expected) {
        fprintf(stderr, "%s mode 3 length: expected %u, got %u\n",
                label, expected, actual);
        return 1;
    }
    GBPPU* ppu = (GBPPU*)ctx->ppu;
    const unsigned expected_hblank = CYCLES_SCANLINE - CYCLES_OAM_SCAN - expected;
    if (ppu->hblank_length != expected_hblank) {
        fprintf(stderr, "%s HBlank length: expected %u, got %u\n",
                label, expected_hblank, ppu->hblank_length);
        return 1;
    }
    if (ppu->visible_sprite_count == 0) {
        if (ppu->visible_mode != PPU_MODE_DRAW) {
            fprintf(stderr, "%s did not preserve mode 3 at its final bus dot\n",
                    label);
            return 1;
        }
        ppu_tick(ppu, ctx, 1);
        if (ppu->visible_mode != PPU_MODE_HBLANK) {
            fprintf(stderr, "%s did not expose HBlank after the final mode-3 dot\n",
                    label);
            return 1;
        }
        ppu_tick(ppu, ctx, expected_hblank - 1u);
    } else {
        if (ppu->visible_mode != PPU_MODE_HBLANK) {
            fprintf(stderr, "%s delayed sprite-line HBlank exposure\n", label);
            return 1;
        }
        ppu_tick(ppu, ctx, expected_hblank);
    }
    if (ppu->mode != PPU_MODE_OAM || ppu->ly != 1) {
        fprintf(stderr, "%s did not finish at dot 456: mode=%u LY=%u\n",
                label, ppu->mode, ppu->ly);
        return 1;
    }
    return 0;
}

static int expect_bulk_matches_scalar(uint8_t lcdc, const char* label) {
    GBContext* bulk_ctx = make_context();
    GBContext* scalar_ctx = make_context();
    if (!bulk_ctx || !scalar_ctx) {
        gb_context_destroy(bulk_ctx);
        gb_context_destroy(scalar_ctx);
        return 1;
    }

    for (size_t i = 0; i < VRAM_SIZE * 2u; ++i) {
        const uint8_t value = (uint8_t)(i * 37u + (i >> 3));
        bulk_ctx->vram[i] = value;
        scalar_ctx->vram[i] = value;
    }

    begin_visible_line(bulk_ctx, lcdc, 3);
    begin_visible_line(scalar_ctx, lcdc, 3);
    ((GBPPU*)bulk_ctx->ppu)->bgp = 0xE4;
    ((GBPPU*)scalar_ctx->ppu)->bgp = 0xE4;
    ppu_tick((GBPPU*)bulk_ctx->ppu, bulk_ctx, CYCLES_OAM_SCAN);
    ppu_tick((GBPPU*)scalar_ctx->ppu, scalar_ctx, CYCLES_OAM_SCAN);

    ppu_tick((GBPPU*)bulk_ctx->ppu, bulk_ctx, CYCLES_PIXEL_DRAW);
    for (unsigned dot = 0; dot < CYCLES_PIXEL_DRAW; ++dot) {
        ppu_tick((GBPPU*)scalar_ctx->ppu, scalar_ctx, 1);
    }

    int failed = 0;
    if (memcmp(bulk_ctx->ppu, scalar_ctx->ppu, sizeof(GBPPU)) != 0 ||
        memcmp(bulk_ctx->io, scalar_ctx->io, 0x81u) != 0) {
        fprintf(stderr, "%s bulk/scalar PPU state mismatch\n", label);
        failed = 1;
    }
#ifdef GBRT_ENABLE_PERFORMANCE_COUNTERS
    if (!failed && bulk_ctx->performance_counters.ppu_stable_span_dots == 0) {
        fprintf(stderr, "%s did not exercise a stable span\n", label);
        failed = 1;
    }
#endif

    gb_context_destroy(bulk_ctx);
    gb_context_destroy(scalar_ctx);
    return failed;
}

int main(void) {
    GBContext* ctx = make_context();
    if (!ctx) {
        return 2;
    }
    GBPPU* ppu = (GBPPU*)ctx->ppu;

    begin_visible_line(ctx, LCDC_LCD_ENABLE | LCDC_BG_ENABLE, 0);
    if (expect_length(ctx, 172, "baseline")) goto fail;
    if (expect_bulk_matches_scalar(
            LCDC_LCD_ENABLE | LCDC_BG_ENABLE,
            "background stable span")) goto fail;
    if (expect_bulk_matches_scalar(
            LCDC_LCD_ENABLE | LCDC_BG_ENABLE | LCDC_WINDOW_ENABLE,
            "window-boundary stable span")) goto fail;

    begin_visible_line(ctx, LCDC_LCD_ENABLE | LCDC_BG_ENABLE, 0);
    ppu_tick(ppu, ctx, CYCLES_OAM_SCAN);
    if ((ppu_read_register(ppu, 0xFF41) & STAT_MODE_MASK) != PPU_MODE_OAM) {
        fputs("STAT exposed mode 3 at the internal transfer boundary\n", stderr);
        goto fail;
    }
    ctx->oam[0] = 0;
    ctx->vram[0] = 0;
    if (gb_read8(ctx, 0xFE00) != 0xFF) {
        fputs("OAM read was not blocked during the draw handoff\n", stderr);
        goto fail;
    }
    if (gb_read8(ctx, 0x8000) != 0xFF) {
        fputs("VRAM read was not blocked during the draw handoff\n", stderr);
        goto fail;
    }
    gb_write8(ctx, 0xFE00, 0x5A);
    gb_write8(ctx, 0x8000, 0xA5);
    if (ctx->oam[0] != 0x5A || ctx->vram[0] != 0xA5) {
        fputs("PPU bus did not expose the OAM-to-draw access window\n", stderr);
        goto fail;
    }
    ppu_tick(ppu, ctx, 2);
    if ((ppu_read_register(ppu, 0xFF41) & STAT_MODE_MASK) != PPU_MODE_OAM) {
        fputs("STAT exposed mode 3 before its three-dot visible phase\n", stderr);
        goto fail;
    }
    ppu_tick(ppu, ctx, 1);
    if ((ppu_read_register(ppu, 0xFF41) & STAT_MODE_MASK) != PPU_MODE_DRAW) {
        fputs("STAT did not expose mode 3 after three transfer dots\n", stderr);
        goto fail;
    }

    /* LCD-off freezes the LYC comparison result. Re-enabling clocks one new
     * comparison and only a false-to-true result may raise STAT. */
    ppu->ly = 5;
    ppu->scanline = 5;
    ppu_write_register(ppu, ctx, 0xFF45, 5);
    ppu_write_register(ppu, ctx, 0xFF41, STAT_LYC_INT);
    ppu_write_register(ppu, ctx, 0xFF40, 0);
    ppu_write_register(ppu, ctx, 0xFF45, 1);
    if (!(ppu_read_register(ppu, 0xFF41) & STAT_LYC_MATCH)) {
        fputs("LCD-off LYC write changed the frozen comparison bit\n", stderr);
        goto fail;
    }
    ctx->io[0x0F] = 0;
    ppu_write_register(ppu, ctx, 0xFF40, LCDC_LCD_ENABLE);
    if ((ppu_read_register(ppu, 0xFF41) & STAT_LYC_MATCH) ||
        (ctx->io[0x0F] & 0x02)) {
        fputs("LCD enable mishandled a true-to-false LYC comparison\n", stderr);
        goto fail;
    }
    ppu_write_register(ppu, ctx, 0xFF40, 0);
    ppu_write_register(ppu, ctx, 0xFF45, 0);
    ctx->io[0x0F] = 0;
    ppu_write_register(ppu, ctx, 0xFF40, LCDC_LCD_ENABLE);
    if (!(ppu_read_register(ppu, 0xFF41) & STAT_LYC_MATCH) ||
        !(ctx->io[0x0F] & 0x02)) {
        fputs("LCD enable missed a false-to-true LYC STAT edge\n", stderr);
        goto fail;
    }

    if (ppu->mode != PPU_MODE_HBLANK || ppu->ly != 0) {
        fputs("LCD enable did not begin in line-0 mode 0\n", stderr);
        goto fail;
    }
    ppu_tick(ppu, ctx, 78);
    if (ppu->mode != PPU_MODE_HBLANK || ppu->ly != 0) {
        fputs("LCD startup left mode 0 before dot 79\n", stderr);
        goto fail;
    }
    ppu_tick(ppu, ctx, 1);
    if (ppu->mode != PPU_MODE_DRAW || ppu->ly != 0) {
        fputs("LCD startup did not enter mode 3 at dot 79\n", stderr);
        goto fail;
    }
    ppu_tick(ppu, ctx, 172);
    if (ppu->mode != PPU_MODE_HBLANK || ppu->ly != 0) {
        fputs("LCD startup mode 3 did not end after 172 dots\n", stderr);
        goto fail;
    }
    ppu_tick(ppu, ctx, 201);
    if (ppu->mode != PPU_MODE_OAM ||
        ppu->visible_mode != PPU_MODE_HBLANK ||
        ppu->ly != 1) {
        fputs("LCD startup did not expose line 1 at dot 452\n", stderr);
        goto fail;
    }
    ppu_tick(ppu, ctx, 3);
    if (ppu->mode != PPU_MODE_OAM ||
        ppu->visible_mode != PPU_MODE_HBLANK ||
        ppu->ly != 1) {
        fputs("LCD startup did not preserve the four-dot line-1 mode 0 phase\n",
              stderr);
        goto fail;
    }
    ppu_tick(ppu, ctx, 1);
    if (ppu->mode != PPU_MODE_OAM || ppu->ly != 1) {
        fputs("LCD startup did not enter line-1 OAM after four mode-0 dots\n",
              stderr);
        goto fail;
    }
    ppu_tick(ppu, ctx, CYCLES_OAM_SCAN + 172u + 200u);
    if (ppu->mode != PPU_MODE_OAM ||
        ppu->visible_mode != PPU_MODE_HBLANK ||
        ppu->ly != 2) {
        fputs("LCD startup line 1 did not begin its next internal OAM scan\n",
              stderr);
        goto fail;
    }
    ppu_tick(ppu, ctx, 4);
    if (ppu->mode != PPU_MODE_OAM || ppu->ly != 2) {
        fputs("LCD startup line 1 did not retain its 456-dot mode interval\n",
              stderr);
        goto fail;
    }

    begin_visible_line(ctx, LCDC_LCD_ENABLE | LCDC_BG_ENABLE, 0);
    (void)draw_length(ctx);
    ppu = (GBPPU*)ctx->ppu;
    ppu_tick(ppu, ctx, 1);
    ppu->mode_cycles = ppu->hblank_length - 5u;
    ppu_write_register(ppu, ctx, 0xFF45, 1);
    ppu_write_register(ppu, ctx, 0xFF41, STAT_OAM_INT);
    ctx->io[0x0F] = 0;
    ppu_tick(ppu, ctx, 4);
    if (ppu->mode != PPU_MODE_HBLANK || ppu->ly != 1 ||
        !(ctx->io[0x0F] & 0x02)) {
        fputs("LY/mode-2 STAT source did not advance one dot before the boundary\n",
              stderr);
        goto fail;
    }
    ppu_tick(ppu, ctx, 1);
    if (ppu->mode != PPU_MODE_OAM ||
        ppu->visible_mode != PPU_MODE_HBLANK ||
        ppu->ly != 1 || (ppu->stat & STAT_LYC_MATCH)) {
        fputs("internal OAM scan did not begin before visible mode 2\n", stderr);
        goto fail;
    }
    ctx->oam[0] = 0;
    if (gb_read8(ctx, 0xFE00) != 0xFF) {
        fputs("hidden mode-2 OAM read was not blocked\n", stderr);
        goto fail;
    }
    gb_write8(ctx, 0xFE00, 0x6C);
    if (ctx->oam[0] != 0x6C) {
        fputs("hidden mode-2 OAM write was blocked too early\n", stderr);
        goto fail;
    }
    ppu_tick(ppu, ctx, 3);
    if (ppu->mode != PPU_MODE_OAM ||
        ppu->visible_mode != PPU_MODE_HBLANK || ppu->ly != 1) {
        fprintf(stderr,
                "visible mode 2 advanced before its hidden dots: mode=%u LY=%u IF=%02X\n",
                ppu->mode,
                ppu->ly,
                ctx->io[0x0F]);
        goto fail;
    }
    ppu_tick(ppu, ctx, 1);
    if (ppu->mode != PPU_MODE_OAM ||
        ppu->visible_mode != PPU_MODE_OAM || ppu->ly != 1 ||
        !(ppu->stat & STAT_LYC_MATCH)) {
        fputs("visible mode-2 transition did not follow its early IRQ edge\n",
              stderr);
        goto fail;
    }

    /* CPU timing must expose PPU events at instruction boundaries.  The old
     * scheduler only synchronized every 256 T-cycles, which made a HALT wait
     * for HBlank resume tens or hundreds of dots late. */
    begin_visible_line(ctx, LCDC_LCD_ENABLE | LCDC_BG_ENABLE, 0);
    ppu = (GBPPU*)ctx->ppu;
    ppu_tick(ppu, ctx, CYCLES_OAM_SCAN + 171);
    ppu_write_register(ppu, ctx, 0xFF41, STAT_HBLANK_INT);
    ctx->io[0x0F] = 0;
    gb_tick(ctx, 4);
    if (ppu->mode != PPU_MODE_HBLANK || !(ctx->io[0x0F] & 0x02)) {
        fprintf(stderr,
                "CPU tick delayed HBlank synchronization: mode=%u IF=%02X\n",
                ppu->mode,
                ctx->io[0x0F]);
        goto fail;
    }

    begin_visible_line(ctx, LCDC_LCD_ENABLE | LCDC_BG_ENABLE, 7);
    if (expect_length(ctx, 179, "SCX discard")) goto fail;

    begin_visible_line(ctx,
                       LCDC_LCD_ENABLE | LCDC_BG_ENABLE | LCDC_WINDOW_ENABLE,
                       0);
    if (expect_length(ctx, 178, "window restart")) goto fail;

    memset(ctx->oam, 0, OAM_SIZE);
    ctx->oam[0] = 16;  /* Visible on LY 0. */
    ctx->oam[1] = 48;  /* Left edge at screen X 40, tile pixel 0. */
    begin_visible_line(ctx,
                       LCDC_LCD_ENABLE | LCDC_BG_ENABLE | LCDC_OBJ_ENABLE,
                       0);
    if (expect_length(ctx, 183, "object fetch")) goto fail;

    memset(ctx->oam, 0, OAM_SIZE);
    for (int sprite = 0; sprite < 10; ++sprite) {
        ctx->oam[sprite * 4] = 16;
        ctx->oam[sprite * 4 + 1] = 0;
    }
    begin_visible_line(ctx,
                       LCDC_LCD_ENABLE | LCDC_BG_ENABLE | LCDC_OBJ_ENABLE,
                       0);
    if (expect_length(ctx, 237, "overlapping X=0 objects")) goto fail;

    /* One raw color-1 tile across the background. */
    memset(ctx->oam, 0, OAM_SIZE);
    memset(ctx->vram, 0, VRAM_SIZE * 2u);
    for (int row = 0; row < 8; ++row) {
        ctx->vram[row * 2] = 0xFF;
    }
    memset(ctx->vram + 0x1800, 0, 32u * 32u);
    begin_visible_line(ctx, LCDC_LCD_ENABLE | LCDC_BG_ENABLE | LCDC_TILE_DATA, 0);
    ppu = (GBPPU*)ctx->ppu;
    ppu->bgp = 0xE4; /* Identity mapping. */
    ppu_tick(ppu, ctx, CYCLES_OAM_SCAN);
    ppu_tick(ppu, ctx, 22); /* 12 startup dots + 10 visible pixels. */
    ppu_write_register(ppu, ctx, 0xFF47, 0xEC); /* Raw 1 now maps to shade 3. */
    while (ppu->mode == PPU_MODE_DRAW) {
        ppu_tick(ppu, ctx, 1);
    }
    if (ppu->framebuffer[9] != 1 || ppu->framebuffer[10] != 3) {
        fprintf(stderr,
                "mid-line BGP write was not pixel-accurate: x9=%u x10=%u\n",
                ppu->framebuffer[9],
                ppu->framebuffer[10]);
        goto fail;
    }

    ppu->mode = PPU_MODE_VBLANK;
    ppu->mode_cycles = 0;
    ppu->scanline = 153;
    ppu->ly = 153;
    ppu->lyc = 0;
    ppu_tick(ppu, ctx, 3);
    if (ppu->ly != 153 || ppu->mode != PPU_MODE_VBLANK) {
        fputs("LY wrapped before dot 4 of line 153\n", stderr);
        goto fail;
    }
    ppu_tick(ppu, ctx, 1);
    if (ppu->ly != 0 || ppu->scanline != 153 ||
        ppu->mode != PPU_MODE_VBLANK || !(ppu->stat & STAT_LYC_MATCH)) {
        fputs("LY/LYC did not expose the line-153 dot-4 quirk\n", stderr);
        goto fail;
    }
    ppu_tick(ppu, ctx, CYCLES_SCANLINE - 4);
    if (ppu->mode != PPU_MODE_OAM || ppu->ly != 0 || ppu->scanline != 0) {
        fputs("line 153 did not enter line-0 OAM at dot 456\n", stderr);
        goto fail;
    }

    /* On DMG the line-144 mode-2 source coincides with VBlank. */
    ppu->lcdc = LCDC_LCD_ENABLE;
    ppu->mode = PPU_MODE_HBLANK;
    ppu->visible_mode = PPU_MODE_HBLANK;
    ppu->stat_irq_mode = PPU_MODE_HBLANK;
    ppu->scanline = 143;
    ppu->ly = 143;
    ppu->hblank_length = 20;
    ppu->mode_cycles = 19;
    ppu_write_register(ppu, ctx, 0xFF41, STAT_OAM_INT);
    ctx->io[0x0F] = 0;
    ppu_tick(ppu, ctx, 1);
    if (ppu->mode != PPU_MODE_VBLANK || ppu->ly != 144 ||
        !(ctx->io[0x0F] & 0x02)) {
        fputs("DMG line-144 mode-2 source did not coincide with VBlank\n", stderr);
        goto fail;
    }

    GBContext* cgb_ctx = make_context_for_model(GB_MODEL_CGB);
    if (!cgb_ctx) {
        goto fail;
    }
    GBPPU* cgb_ppu = (GBPPU*)cgb_ctx->ppu;
    cgb_ppu->lcdc = LCDC_LCD_ENABLE;
    cgb_ppu->mode = PPU_MODE_HBLANK;
    cgb_ppu->visible_mode = PPU_MODE_HBLANK;
    cgb_ppu->stat_irq_mode = PPU_MODE_HBLANK;
    cgb_ppu->scanline = 143;
    cgb_ppu->ly = 143;
    cgb_ppu->hblank_length = 20;
    cgb_ppu->mode_cycles = 15;
    ppu_write_register(cgb_ppu, cgb_ctx, 0xFF41, STAT_OAM_INT);
    cgb_ctx->io[0x0F] = 0;
    ppu_tick(cgb_ppu, cgb_ctx, 1);
    if (cgb_ppu->mode != PPU_MODE_HBLANK || cgb_ppu->ly != 143 ||
        !(cgb_ctx->io[0x0F] & 0x02)) {
        fputs("CGB line-144 mode-2 source was not one M-cycle early\n", stderr);
        gb_context_destroy(cgb_ctx);
        goto fail;
    }
    gb_context_destroy(cgb_ctx);

    gb_context_destroy(ctx);
    return 0;

fail:
    gb_context_destroy(ctx);
    return 1;
}
