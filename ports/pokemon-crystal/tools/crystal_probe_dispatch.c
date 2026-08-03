#include "gbrt.h"

/*
 * Generated games provide strong dispatch functions. The standalone
 * presentation probes do not include generated game objects, so provide the
 * runtime's ordinary interpreter fallback explicitly. GCC-compatible linkers
 * override the runtime's weak definitions; MSVC needs these definitions
 * because it has no equivalent weak fallback in gbrt.
 */
void gb_dispatch(GBContext* ctx, uint16_t addr) {
    gbrt_log_trace(ctx, gb_resolve_rom_bank(ctx, addr), addr);
    ctx->pc = addr;
    gb_interpret(ctx, addr);
}

void gb_dispatch_call(GBContext* ctx, uint16_t addr) {
    gbrt_log_trace(ctx, gb_resolve_rom_bank(ctx, addr), addr);
    ctx->pc = addr;
}
