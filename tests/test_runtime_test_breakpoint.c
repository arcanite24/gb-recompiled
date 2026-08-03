#include "gbrt.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    GBConfig config;
    memset(&config, 0, sizeof(config));
    config.model = GB_MODEL_DMG;

    GBContext* ctx = gb_context_create(&config);
    if (!ctx) {
        return 2;
    }

    uint8_t rom[32u * 1024u];
    memset(rom, 0, sizeof(rom));
    rom[0x0200] = 0x40; /* LD B,B - Mooneye magic breakpoint */
    if (!gb_context_load_rom(ctx, rom, sizeof(rom))) {
        gb_context_destroy(ctx);
        return 2;
    }
    gb_context_reset(ctx, true);
    ctx->b = 3;
    ctx->c = 5;
    ctx->d = 8;
    ctx->e = 13;
    ctx->h = 21;
    ctx->l = 34;

    gbrt_test_breakpoint_enabled = true;
    gb_interpret(ctx, 0x0200);
    gbrt_test_breakpoint_enabled = false;

    const int failed = !ctx->stopped || ctx->pc != 0x0201 ||
        ctx->b != 3 || ctx->c != 5 || ctx->d != 8 ||
        ctx->e != 13 || ctx->h != 21 || ctx->l != 34;
    if (failed) {
        fprintf(stderr,
                "test breakpoint did not stop at result registers (PC=%04X stopped=%u)\n",
                ctx->pc,
                ctx->stopped);
    }
    gb_context_destroy(ctx);
    return failed ? 1 : 0;
}
