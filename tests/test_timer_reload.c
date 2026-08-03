#include "gbrt.h"

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
    ctx->io[0x0F] &= (uint8_t)~0x04u;
    ctx->io[0x06] = 0x42;
    ctx->io[0x07] = 0x05;
    ctx->div_counter = 0x000F;
    ctx->io[0x05] = 0xFF;
    ctx->tima_reload_pending = 0;
    return ctx;
}

static int overflow_once(GBContext* ctx) {
    gb_tick(ctx, 1);
    if (ctx->io[0x05] != 0 || ctx->tima_reload_pending != 4 ||
        (ctx->io[0x0F] & 0x04)) {
        fputs("TIMA overflow did not begin a four-T-cycle zero window\n", stderr);
        return 1;
    }
    return 0;
}

int main(void) {
    GBContext* cancel = make_context();
    if (!cancel || overflow_once(cancel)) {
        gb_context_destroy(cancel);
        return 1;
    }
    gb_tick(cancel, 3);
    gb_write8(cancel, 0xFF05, 0x99);
    gb_tick(cancel, 1);
    if (cancel->io[0x05] != 0x99 || cancel->tima_reload_pending != 0 ||
        (cancel->io[0x0F] & 0x04)) {
        fputs("TIMA write in overflow cycle did not cancel the reload\n", stderr);
        gb_context_destroy(cancel);
        return 1;
    }
    gb_context_destroy(cancel);

    GBContext* reload = make_context();
    if (!reload || overflow_once(reload)) {
        gb_context_destroy(reload);
        return 1;
    }
    gb_tick(reload, 4);
    if (reload->io[0x05] != 0x42 || !(reload->io[0x0F] & 0x04) ||
        !(reload->tima_reload_pending & 0x80)) {
        fputs("TIMA did not reload TMA and request IF after four T-cycles\n", stderr);
        gb_context_destroy(reload);
        return 1;
    }
    gb_write8(reload, 0xFF05, 0x99);
    if (reload->io[0x05] != 0x42) {
        fputs("TIMA write during the reload M-cycle was not ignored\n", stderr);
        gb_context_destroy(reload);
        return 1;
    }
    gb_write8(reload, 0xFF06, 0x77);
    if (reload->io[0x06] != 0x77 || reload->io[0x05] != 0x77) {
        fputs("TMA write during reload did not update TIMA with it\n", stderr);
        gb_context_destroy(reload);
        return 1;
    }
    gb_tick(reload, 4);
    gb_write8(reload, 0xFF05, 0x55);
    if (reload->io[0x05] != 0x55) {
        fputs("TIMA remained write-blocked after the reload M-cycle\n", stderr);
        gb_context_destroy(reload);
        return 1;
    }
    gb_context_destroy(reload);

    GBContext* glitches = make_context();
    if (!glitches) {
        return 2;
    }
    glitches->io[0x05] = 0x10;
    glitches->div_counter = 0x0008;
    gb_write8(glitches, 0xFF07, 0x04);
    if (glitches->io[0x05] != 0x11) {
        fputs("TAC falling-input write did not increment TIMA\n", stderr);
        gb_context_destroy(glitches);
        return 1;
    }
    glitches->io[0x05] = 0xFF;
    glitches->io[0x06] = 0x33;
    glitches->io[0x07] = 0x05;
    glitches->div_counter = 0x0008;
    glitches->tima_reload_pending = 0;
    glitches->io[0x0F] &= (uint8_t)~0x04u;
    gb_write8(glitches, 0xFF04, 0);
    if (glitches->io[0x05] != 0 || glitches->tima_reload_pending != 4 ||
        (glitches->io[0x0F] & 0x04)) {
        fputs("DIV glitch overflow bypassed the normal reload delay\n", stderr);
        gb_context_destroy(glitches);
        return 1;
    }
    gb_context_destroy(glitches);

    GBContext* rapid = make_context();
    if (!rapid) {
        return 2;
    }
    rapid->div_counter = 0;
    rapid->io[0x07] = 0;
    rapid->io[0x05] = 0;
    rapid->tima_reload_pending = 0;
    rapid->io[0x0F] &= (uint8_t)~0x04u;
    gb_tick(rapid, 1);
    gb_tick(rapid, 8);
    gb_tick(rapid, 11);
    gb_write8(rapid, 0xFF05, 0xF0);
    gb_tick(rapid, 1);
    gb_tick(rapid, 8);
    gb_tick(rapid, 11);
    gb_write8(rapid, 0xFF07, 0x04);
    gb_tick(rapid, 1);
    gb_tick(rapid, 12);
    gb_tick(rapid, 4);
    rapid->bc = 0xFFFF;
    for (unsigned iteration = 0;
         iteration < 0x10000u && !(rapid->io[0x0F] & 0x04);
         ++iteration) {
        gb_tick(rapid, 8);
        gb_tick(rapid, 11);
        gb_write8(rapid, 0xFF07, 0x04);
        gb_tick(rapid, 1);
        gb_tick(rapid, 8);
        gb_tick(rapid, 11);
        gb_write8(rapid, 0xFF07, 0x00);
        gb_tick(rapid, 1);
        rapid->bc--;
        gb_tick(rapid, 8);
        gb_tick(rapid, 4);
        gb_tick(rapid, 4);
        gb_tick(rapid, 12);
    }
    if (rapid->bc != 0xFFD9) {
        fprintf(stderr,
                "rapid TAC toggle requested IF at BC=%04X instead of FFD9 (DIV=%04X TIMA=%02X)\n",
                rapid->bc, rapid->div_counter, rapid->io[0x05]);
        gb_context_destroy(rapid);
        return 1;
    }
    gb_context_destroy(rapid);
    return 0;
}
