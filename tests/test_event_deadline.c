#include "gbrt.h"
#include "ppu.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static GBContext* make_context(void) {
    const GBConfig config = {
        .model = GB_MODEL_DMG,
        .enable_audio = false,
        .enable_serial = false,
        .speed_percent = 100,
    };
    GBContext* ctx = gb_context_create(&config);
    if (!ctx) {
        return NULL;
    }

    uint8_t rom[32u * 1024u] = {0};
    if (!gb_context_load_rom(ctx, rom, sizeof(rom))) {
        gb_context_destroy(ctx);
        return NULL;
    }
    gb_context_reset(ctx, true);
    return ctx;
}

static void clear_deadline_sources(GBContext* ctx) {
    GBPPU* ppu = (GBPPU*)ctx->ppu;
    ctx->stopped = 0;
    ctx->frame_done = 0;
    ctx->single_step_mode = 0;
    ctx->halted = 0;
    ctx->halt_bug = 0;
    ctx->stop_mode_active = 0;
    ctx->ime = 0;
    ctx->ime_pending = 0;
    ctx->cgb_double_speed = 0;
    ctx->cycles = 0;
    ctx->last_sync_cycles = 0;
    ctx->run_cycle_budget = 0;
    ctx->tima_reload_pending = 0;
    ctx->io[0x07] = 0;
    ctx->io[0x0F] = 0;
    ctx->io[0x80] = 0;
    memset(&ctx->dma, 0, sizeof(ctx->dma));
    memset(&ctx->serial_transfer, 0, sizeof(ctx->serial_transfer));
    ctx->hdma.cpu_stall_cycles = 0;
    ctx->trace_file = NULL;
    ctx->ppu_trace_file = NULL;
    ppu->lcdc = 0;
}

static int expect_deadline(GBContext* ctx,
                           uint32_t expected,
                           const char* label) {
    const uint32_t actual = gbrt_cycles_until_next_event(ctx);
    if (actual != expected) {
        fprintf(stderr,
                "%s deadline: expected %u, got %u\n",
                label,
                expected,
                actual);
        return 1;
    }
    return 0;
}

int main(void) {
    GBContext* ctx = make_context();
    if (!ctx) {
        fputs("failed to create deadline test context\n", stderr);
        return 2;
    }
    GBPPU* ppu = (GBPPU*)ctx->ppu;

    clear_deadline_sources(ctx);
    if (expect_deadline(ctx, UINT32_MAX, "idle")) goto fail;
    ctx->cycles = 4;
    if (expect_deadline(ctx, 0, "unsynchronized PPU state")) goto fail;
    clear_deadline_sources(ctx);
    gbrt_benchmark_fast_tick_enabled = true;
    if (expect_deadline(ctx, 0, "coarse benchmark scheduler")) goto fail;
    gbrt_benchmark_fast_tick_enabled = false;

    ppu->lcdc = LCDC_LCD_ENABLE;
    ppu->mode = PPU_MODE_OAM;
    ppu->visible_mode = PPU_MODE_OAM;
    ppu->stat_irq_mode = PPU_MODE_OAM;
    ppu->mode_cycles = 20;
    if (expect_deadline(ctx, 60, "OAM boundary")) goto fail;

    ppu->mode = PPU_MODE_DRAW;
    ppu->mode_cycles = 40;
    if (expect_deadline(ctx, 1, "dynamic pixel-transfer boundary")) goto fail;

    ppu->mode = PPU_MODE_HBLANK;
    ppu->visible_mode = PPU_MODE_HBLANK;
    ppu->stat_irq_mode = PPU_MODE_HBLANK;
    ppu->scanline = 10;
    ppu->hblank_length = 20;
    ppu->mode_cycles = 10;
    ppu->lcd_startup_phase = 0;
    if (expect_deadline(ctx, 9, "early OAM HBlank edge")) goto fail;

    ppu->mode = PPU_MODE_VBLANK;
    ppu->scanline = 153;
    ppu->mode_cycles = 2;
    if (expect_deadline(ctx, 2, "line-153 LY edge")) goto fail;

    clear_deadline_sources(ctx);
    ctx->io[0x07] = 0x05; /* Enable TIMA from DIV bit 3: falling every 16 T-cycles. */
    ctx->div_counter = 5;
    if (expect_deadline(ctx, 11, "timer falling edge")) goto fail;
    ctx->tima_reload_pending = 3;
    if (expect_deadline(ctx, 1, "timer reload state")) goto fail;

    clear_deadline_sources(ctx);
    ctx->run_cycle_budget = 20;
    ctx->run_cycle_budget_start = 100;
    ctx->cycles = 112;
    ctx->last_sync_cycles = 112;
    if (expect_deadline(ctx, 8, "run budget")) goto fail;

    clear_deadline_sources(ctx);
    ctx->ime = 1;
    ctx->io[0x0F] = 0x01;
    ctx->io[0x80] = 0x01;
    if (expect_deadline(ctx, 0, "pending interrupt")) goto fail;
    ctx->ime = 0;
    ctx->ime_pending = 1;
    if (expect_deadline(ctx, 0, "delayed IME")) goto fail;

    clear_deadline_sources(ctx);
    ctx->dma.active = 1;
    ctx->dma.cycles_remaining = 638;
    if (expect_deadline(ctx, 2, "OAM DMA byte")) goto fail;

    clear_deadline_sources(ctx);
    ctx->serial_transfer.active = 1;
    ctx->serial_transfer.cycles_remaining = 7;
    if (expect_deadline(ctx, 7, "serial completion")) goto fail;

    gb_context_destroy(ctx);
    return 0;

fail:
    gb_context_destroy(ctx);
    return 1;
}
