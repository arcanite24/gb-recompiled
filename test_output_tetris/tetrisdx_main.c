/* Main entry point */
#include "tetrisdx.h"
#include "gbrt.h"
#ifdef GB_HAS_SDL2
#include "platform_sdl.h"
#endif
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    GBContext* ctx = gb_context_create(NULL);
    if (!ctx) {
        fprintf(stderr, "Failed to create context\n");
        return 1;
    }
    tetrisdx_init(ctx);

#ifdef GB_HAS_SDL2
    // Initialize SDL2 platform with 3x scaling
    if (!gb_platform_init(3)) {
        fprintf(stderr, "Failed to initialize platform\n");
        gb_context_destroy(ctx);
        return 1;
    }

    // Run the game - rendering happens inside gb_halt()
    tetrisdx_run(ctx);

    gb_platform_shutdown();
#else
    // No SDL2 - just run for testing
    tetrisdx_run(ctx);
    printf("Recompiled code executed successfully!\n");
    printf("Registers: A=%02X B=%02X C=%02X\n", ctx->a, ctx->b, ctx->c);
#endif

    gb_context_destroy(ctx);
    return 0;
}
