#include "gbrt.h"

#include <stdio.h>
#include <string.h>

static GBContext* make_context(uint8_t mapper, uint8_t ram_size) {
    GBConfig config = {0};
    config.model = GB_MODEL_DMG;
    GBContext* ctx = gb_context_create(&config);
    uint8_t rom[0x8000] = {0};
    rom[0x147] = mapper;
    rom[0x149] = ram_size;
    if (!ctx || !gb_context_load_rom(ctx, rom, sizeof(rom))) {
        gb_context_destroy(ctx);
        return NULL;
    }
    gb_context_reset(ctx, true);
    gb_write8(ctx, 0x0000, 0x0A);
    return ctx;
}

static void select_ram_bank(GBContext* ctx, uint8_t bank) {
    if (ctx->mbc_type == 0x02) {
        gb_write8(ctx, 0x6000, 1);
    }
    gb_write8(ctx, 0x4000, bank);
}

static int verify_small_chip(uint8_t mapper, uint8_t ram_size) {
    GBContext* ctx = make_context(mapper, ram_size);
    if (!ctx) return 1;
    const uint16_t alias = ram_size == 1 ? 0xA800 : 0xA000;
    gb_write8(ctx, 0xA000, 0x5A);
    select_ram_bank(ctx, 3);
    int failed = gb_read8(ctx, alias) != 0x5A;
    gb_write8(ctx, (uint16_t)(alias + 1), 0xA5);
    select_ram_bank(ctx, 0);
    failed |= gb_read8(ctx, 0xA001) != 0xA5;

    /* DMA must use the same mirrored chip address as CPU reads/writes. */
    for (unsigned i = 0; i < 160; ++i) {
        gb_write8(ctx, (uint16_t)(0xA000 + i), (uint8_t)(i ^ 0x3C));
    }
    select_ram_bank(ctx, 3);
    gb_write8(ctx, 0xFF46, (uint8_t)(alias >> 8));
    gb_tick(ctx, 648);
    failed |= ctx->dma.active || ctx->dma.pending || ctx->dma.progress != 160;
    for (unsigned i = 0; i < 160; ++i) {
        failed |= ctx->oam[i] != (uint8_t)(i ^ 0x3C);
    }

    gb_write8(ctx, 0x0000, 0);
    gb_write8(ctx, alias, 0xFF);
    failed |= gb_read8(ctx, alias) != 0xFF;
    gb_write8(ctx, 0xFF46, (uint8_t)(alias >> 8));
    gb_tick(ctx, 648);
    for (unsigned i = 0; i < 160; ++i) failed |= ctx->oam[i] != 0xFF;
    gb_write8(ctx, 0x0000, 0x0A);
    failed |= gb_read8(ctx, alias) != 0x3C;
    if (failed) fprintf(stderr, "RAM mirror/enable/DMA failed: mapper=%02X size=%u\n", mapper, ram_size);
    gb_context_destroy(ctx);
    return failed;
}

static int verify_banked_and_absent_ram(void) {
    GBContext* ctx = make_context(0x02, 3); /* Four distinct 8 KiB banks. */
    if (!ctx) return 1;
    for (uint8_t bank = 0; bank < 4; ++bank) {
        select_ram_bank(ctx, bank);
        gb_write8(ctx, 0xA123, (uint8_t)(0x40 + bank));
    }
    int failed = 0;
    for (uint8_t bank = 0; bank < 4; ++bank) {
        select_ram_bank(ctx, bank);
        failed |= gb_read8(ctx, 0xA123) != (uint8_t)(0x40 + bank);
    }
    gb_context_destroy(ctx);

    ctx = make_context(0x01, 0);
    if (!ctx) return 1;
    select_ram_bank(ctx, 3);
    gb_write8(ctx, 0xA000, 0x5A);
    failed |= gb_read8(ctx, 0xA000) != 0xFF;
    gb_write8(ctx, 0xFF46, 0xA0);
    gb_tick(ctx, 648);
    for (unsigned i = 0; i < 160; ++i) failed |= ctx->oam[i] != 0xFF;
    gb_context_destroy(ctx);
    if (failed) fputs("Distinct RAM banks or absent RAM regressed\n", stderr);
    return failed;
}

int main(void) {
    int failed = 0;
    const uint8_t mappers[] = {0x02, 0x12, 0x1A}; /* MBC1, MBC3, MBC5. */
    for (unsigned i = 0; i < sizeof(mappers); ++i) {
        failed |= verify_small_chip(mappers[i], 1);
        failed |= verify_small_chip(mappers[i], 2);
    }
    return failed | verify_banked_and_absent_ram();
}
