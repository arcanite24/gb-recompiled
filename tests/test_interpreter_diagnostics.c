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
    rom[0x0200] = 0x18; /* JR +0 */
    rom[0x0201] = 0x00;
    if (!gb_context_load_rom(ctx, rom, sizeof(rom))) {
        gb_context_destroy(ctx);
        return 2;
    }
    gb_context_reset(ctx, true);

    gbrt_dispatch_fallback_tracking_enabled = true;
    gbrt_execute_dispatch_fallback(
        ctx,
        0,
        0x0200,
        GB_DISPATCH_FALLBACK_BANK_NOT_COMPILED,
        4);
    gbrt_dispatch_fallback_tracking_enabled = false;

    const int failed =
        ctx->total_dispatch_fallbacks != 1 ||
        ctx->total_interpreter_entries != 1 ||
        ctx->total_interpreter_instructions != 1 ||
        ctx->total_interpreter_cycles != 12 ||
        ctx->dispatch_fallback_site_count != 1 ||
        ctx->dispatch_fallback_sites_dropped != 0 ||
        !ctx->dispatch_fallback_sites[0].valid ||
        ctx->dispatch_fallback_sites[0].reason !=
            GB_DISPATCH_FALLBACK_BANK_NOT_COMPILED ||
        ctx->dispatch_fallback_sites[0].compiled_bank_variants != 4 ||
        ctx->dispatch_fallback_sites[0].entries != 1 ||
        ctx->dispatch_fallback_sites[0].instructions != 1 ||
        ctx->dispatch_fallback_sites[0].cycles != 12 ||
        ctx->dispatch_fallback_sites[0].first_frame != 1 ||
        ctx->dispatch_fallback_sites[0].last_frame != 1 ||
        !ctx->interpreter_hotspots[0].valid ||
        ctx->interpreter_hotspots[0].bank != 0 ||
        ctx->interpreter_hotspots[0].addr != 0x0200 ||
        ctx->interpreter_hotspots[0].entries != 1 ||
        ctx->interpreter_hotspots[0].instructions != 1 ||
        ctx->interpreter_hotspots[0].cycles != 12;
    if (failed) {
        fprintf(stderr,
                "interpreter control-flow exit was not recorded "
                "(fallbacks=%llu entries=%llu instructions=%llu cycles=%llu "
                "sites=%u dropped=%llu)\n",
                (unsigned long long)ctx->total_dispatch_fallbacks,
                (unsigned long long)ctx->total_interpreter_entries,
                (unsigned long long)ctx->total_interpreter_instructions,
                (unsigned long long)ctx->total_interpreter_cycles,
                ctx->dispatch_fallback_site_count,
                (unsigned long long)ctx->dispatch_fallback_sites_dropped);
    }

    gb_context_destroy(ctx);
    return failed ? 1 : 0;
}
