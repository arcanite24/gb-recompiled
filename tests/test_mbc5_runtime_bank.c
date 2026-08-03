#include "gbrt.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    GBContext ctx;
    memset(&ctx, 0, sizeof(ctx));

    gbrt_dispatch_fallback_tracking_enabled = true;
    gbrt_note_dispatch_fallback(&ctx, 511, 0x4000);
    gbrt_note_interpreter_session(&ctx, 511, 0x4000, 3, 12);
    gbrt_note_unimplemented_interpreter_opcode(&ctx, 256, 0x4123, 0xD3);

    if (ctx.dispatch_fallback_bank != 511 ||
        ctx.frame_first_fallback_bank != 511 ||
        ctx.frame_last_fallback_bank != 511 ||
        !ctx.interpreter_hotspots[0].valid ||
        ctx.interpreter_hotspots[0].bank != 511 ||
        ctx.last_unimplemented_bank != 256) {
        fputs("runtime diagnostics truncated an MBC5 bank ID\n", stderr);
        return 1;
    }
    return 0;
}
