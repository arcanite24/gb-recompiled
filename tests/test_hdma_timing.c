#include "gbrt.h"
#include "ppu.h"

#include <stdio.h>
#include <string.h>

static int run_case(uint8_t double_speed) {
    GBConfig config;
    memset(&config, 0, sizeof(config));
    config.model = GB_MODEL_CGB;
    config.cartridge_supports_cgb = true;
    config.cartridge_requires_cgb = true;

    GBContext* ctx = gb_context_create(&config);
    if (!ctx) {
        return 2;
    }

    uint8_t rom[32u * 1024u];
    memset(rom, 0, sizeof(rom));
    rom[0x143] = 0xC0;
    rom[0x147] = 0x00;
    rom[0x148] = 0x00;
    for (int i = 0; i < 32; ++i) {
        rom[0x0200 + i] = (uint8_t)(0x80 + i);
    }
    if (!gb_context_load_rom(ctx, rom, sizeof(rom))) {
        gb_context_destroy(ctx);
        return 2;
    }
    gb_context_reset(ctx, true);
    ctx->cgb_double_speed = double_speed;

    gb_write8(ctx, 0xFF51, 0x02);
    gb_write8(ctx, 0xFF52, 0x00);
    gb_write8(ctx, 0xFF53, 0x00);
    gb_write8(ctx, 0xFF54, 0x00);

    const uint32_t start_cycles = ctx->cycles;
    const uint16_t start_div = ctx->div_counter;
    gb_write8(ctx, 0xFF55, 0x01); // General DMA, two 16-byte blocks.

    const uint32_t expected_system_cycles = 64;
    const uint16_t expected_cpu_cycles = double_speed ? 128 : 64;
    int failed = 0;
    if (ctx->cycles - start_cycles != expected_system_cycles ||
        (uint16_t)(ctx->div_counter - start_div) != expected_cpu_cycles ||
        gb_read8(ctx, 0xFF55) != 0xFF) {
        fprintf(stderr,
                "HDMA timing speed=%u system=%u cpu=%u status=%02X\n",
                double_speed,
                ctx->cycles - start_cycles,
                (uint16_t)(ctx->div_counter - start_div),
                gb_read8(ctx, 0xFF55));
        failed = 1;
    }
    for (int i = 0; i < 32 && !failed; ++i) {
        if (ctx->vram[i] != (uint8_t)(0x80 + i)) {
            fprintf(stderr, "HDMA copy mismatch at byte %d\n", i);
            failed = 1;
        }
    }

    gb_context_destroy(ctx);
    return failed;
}

static int run_hblank_case(uint8_t double_speed) {
    GBConfig config;
    memset(&config, 0, sizeof(config));
    config.model = GB_MODEL_CGB;
    config.cartridge_supports_cgb = true;
    config.cartridge_requires_cgb = true;
    GBContext* ctx = gb_context_create(&config);
    if (!ctx) {
        return 2;
    }

    uint8_t rom[32u * 1024u];
    memset(rom, 0, sizeof(rom));
    rom[0x143] = 0xC0;
    for (int i = 0; i < 16; ++i) {
        rom[0x0300 + i] = (uint8_t)(0x40 + i);
    }
    if (!gb_context_load_rom(ctx, rom, sizeof(rom))) {
        gb_context_destroy(ctx);
        return 2;
    }
    gb_context_reset(ctx, true);
    ctx->cgb_double_speed = double_speed;
    GBPPU* ppu = (GBPPU*)ctx->ppu;
    ppu->mode = PPU_MODE_OAM;
    ppu->mode_cycles = 0;
    ppu->ly = 0;
    ctx->io[0x41] = (uint8_t)((ctx->io[0x41] & ~0x03u) | PPU_MODE_OAM);
    ctx->io[0x44] = 0;
    gb_write8(ctx, 0xFF51, 0x03);
    gb_write8(ctx, 0xFF52, 0x00);
    gb_write8(ctx, 0xFF53, 0x00);
    gb_write8(ctx, 0xFF54, 0x00);
    gb_write8(ctx, 0xFF55, 0x80); // One block on the next HBlank.

    const uint32_t start_cycles = ctx->cycles;
    const uint16_t start_div = ctx->div_counter;
    gb_tick(ctx, double_speed ? 512 : 256);

    const uint32_t system_delta = ctx->cycles - start_cycles;
    const uint16_t cpu_delta = (uint16_t)(ctx->div_counter - start_div);
    const uint16_t expected_cpu = double_speed ? 576 : 288;
    int failed = 0;
    if (system_delta != 288 || cpu_delta != expected_cpu ||
        gb_read8(ctx, 0xFF55) != 0xFF) {
        fprintf(stderr,
                "HBlank HDMA timing speed=%u system=%u cpu=%u status=%02X\n",
                double_speed,
                system_delta,
                cpu_delta,
                gb_read8(ctx, 0xFF55));
        failed = 1;
    }
    for (int i = 0; i < 16 && !failed; ++i) {
        if (ctx->vram[i] != (uint8_t)(0x40 + i)) {
            failed = 1;
        }
    }
    gb_context_destroy(ctx);
    return failed;
}

int main(void) {
    int rc = run_case(0);
    if (rc) return rc;
    rc = run_case(1);
    if (rc) return rc;
    rc = run_hblank_case(0);
    return rc ? rc : run_hblank_case(1);
}
