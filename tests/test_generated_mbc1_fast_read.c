#include "mbc1_mode1_lower_window_internal.h"

#undef gb_read8
#undef gb_write8

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    const size_t rom_size = 1024u * 1024u;
    uint8_t* rom = (uint8_t*)calloc(rom_size, 1);
    uint8_t wram[0x8000];
    uint8_t hram[0x7F];
    uint8_t io[0x81];
    if (!rom) {
        fputs("failed to allocate synthetic ROM\n", stderr);
        return 2;
    }

    memset(wram, 0, sizeof(wram));
    memset(hram, 0, sizeof(hram));
    memset(io, 0, sizeof(io));
    rom[0x1234] = 0x11;
    rom[(32u * 0x4000u) + 0x1234u] = 0xA5;

    GBContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.rom = rom;
    ctx.rom_size = rom_size;
    ctx.wram = wram;
    ctx.hram = hram;
    ctx.io = io;
    ctx.mbc_type = 0x01;
    ctx.mbc_mode = 1;
    ctx.rom_bank = 1;
    ctx.rom_bank_upper = 1;
    ctx.wram_bank = 3;

    const uint8_t actual = mbc1_mode1_lower_window_fast_read8(&ctx, 0x1234);
    if (actual != 0xA5) {
        fprintf(stderr,
                "MBC1 mode-1 lower-window read returned 0x%02X, expected 0xA5\n",
                actual);
        free(rom);
        return 1;
    }

    wram[0x0234] = 0x31;
    wram[(3u * 0x1000u) + 0x0567u] = 0x62;
    hram[0x2Au] = 0x93;
    if (mbc1_mode1_lower_window_fast_read8(&ctx, 0xC234) != 0x31 ||
        mbc1_mode1_lower_window_fast_read8(&ctx, 0xD567) != 0x62 ||
        mbc1_mode1_lower_window_fast_read8(&ctx, 0xFFAA) != 0x93) {
        fputs("generated fast reads did not select fixed WRAM, banked WRAM, and HRAM correctly\n",
              stderr);
        free(rom);
        return 1;
    }

    mbc1_mode1_lower_window_fast_write8(&ctx, 0xC345, 0x14);
    mbc1_mode1_lower_window_fast_write8(&ctx, 0xD678, 0x25);
    mbc1_mode1_lower_window_fast_write8(&ctx, 0xFFBB, 0x36);
    if (wram[0x0345] != 0x14 ||
        wram[(3u * 0x1000u) + 0x0678u] != 0x25 ||
        hram[0x3Bu] != 0x36) {
        fputs("generated fast writes did not select fixed WRAM, banked WRAM, and HRAM correctly\n",
              stderr);
        free(rom);
        return 1;
    }

    /* Active OAM DMA must reject the direct ranges and preserve the generic,
     * model/source-specific bus behavior. */
    ctx.config.model = GB_MODEL_CGB;
    ctx.dma.active = 1;
    wram[0x0234] = 0x5A;
    const uint8_t expected_dma_read = gb_read8(&ctx, 0xC234);
    if (mbc1_mode1_lower_window_fast_read8(&ctx, 0xC234) != expected_dma_read) {
        fputs("generated fast read bypassed active-DMA bus ownership\n", stderr);
        free(rom);
        return 1;
    }

    gb_write8(&ctx, 0xC234, 0xA5);
    const uint8_t expected_dma_write = wram[0x0234];
    wram[0x0234] = 0x5A;
    mbc1_mode1_lower_window_fast_write8(&ctx, 0xC234, 0xA5);
    if (wram[0x0234] != expected_dma_write) {
        fputs("generated fast write bypassed active-DMA bus ownership\n", stderr);
        free(rom);
        return 1;
    }

    free(rom);
    return 0;
}
