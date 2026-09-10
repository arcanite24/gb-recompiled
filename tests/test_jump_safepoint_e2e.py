#!/usr/bin/env python3
"""Direct JP/JR loops must yield without relying on single-step dispatch."""
from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import tempfile

from fixtures.make_mapper_rom import NINTENDO_LOGO
from test_bank_guard_e2e import run


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('--gbrecomp', type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='gbrecomp-jump-safepoint-') as raw:
        root = Path(raw)
        rom = bytearray([0xD3]) * 0x8000
        rom[0x100:0x150] = bytes(0x50)
        rom[0x104:0x134] = NINTENDO_LOGO
        rom[0x134:0x13E] = b'JUMP YIELD'
        rom[0x147] = 1  # MBC1
        rom[0x100:0x103] = bytes([0xC3, 0x50, 0x01])
        # A mapper write prevents local-goto lowering for the whole body.
        # Each following loop therefore uses a direct generated function call.
        rom[0x150:0x155] = bytes([0xEA, 0x00, 0x20, 0x18, 0xFE])
        rom[0x200:0x206] = bytes([0xEA, 0x00, 0x20, 0xC3, 0x03, 0x02])
        rom[0x14D] = (-sum(rom[0x134:0x14D]) - 25) & 255
        path = root / 'jump_yield.gb'
        path.write_bytes(rom)
        project = root / 'generated'
        run([str(args.gbrecomp.resolve()), str(path), '--no-scan', '--reachable-only',
             '--add-entry-point', '0:0200', '-o', str(project)])
        (project / 'jump_yield_main.c').write_text(r'''
#include "jump_yield.h"
#include <stdio.h>
int main(void) {
    for (int jp = 0; jp < 2; ++jp) {
        GBConfig config = {0};
        config.model = GB_MODEL_DMG;
        GBContext* ctx = gb_context_create(&config);
        if (!ctx) return 2;
        jump_yield_init(ctx);
        gb_write8(ctx, 0xFFFF, 0);
        gb_write8(ctx, 0xFF0F, 0);
        ctx->ime = 0;
        ctx->pc = jp ? 0x203 : 0x153;
        const unsigned cycles = jp ? 16 : 12;
        gbrt_dispatch_fallback_tracking_enabled = true;
        // Budget expiry must return from a direct loop, preserving its guest PC.
        unsigned elapsed = gb_run_cycles(ctx, cycles * 3);
        if (elapsed != cycles * 3 || ctx->pc != (jp ? 0x203 : 0x153) ||
            !ctx->stopped || ctx->single_step_mode || ctx->total_dispatch_fallbacks) {
            fprintf(stderr, "jump=%d elapsed=%u pc=%04X stopped=%u\n",
                    jp, elapsed, ctx->pc, ctx->stopped);
            return 1;
        }
        // Frame boundaries must also return in normal generated execution.
        gb_reset_frame(ctx);
        gb_run_cycles(ctx, 0);
        if (!ctx->frame_done || ctx->pc != (jp ? 0x203 : 0x153) ||
            ctx->total_dispatch_fallbacks) return 1;
        // A pending interrupt must regain the dispatcher at the jump boundary.
        gb_reset_frame(ctx);
        ctx->stopped = 0;
        ctx->ime = 1;
        gb_write8(ctx, 0xFFFF, 1);
        gb_write8(ctx, 0xFF0F, 1);
        unsigned before = ctx->cycles;
        gb_dispatch(ctx, ctx->pc);
        if (!ctx->stopped || ctx->cycles - before != cycles ||
            ctx->pc != (jp ? 0x203 : 0x153) || ctx->total_dispatch_fallbacks) return 1;
        gb_context_destroy(ctx);
    }
    return 0;
}
''')
        run(['cmake', '-G', 'Ninja', '-S', str(project), '-B', str(project / 'build')])
        run(['ninja', '-C', str(project / 'build'), '-j', '4'])
        binary = project / 'build' / 'jump_yield'
        if not binary.exists():
            binary = binary.with_suffix('.exe')
        subprocess.run([str(binary)], check=True, timeout=5)
        print('direct JP/JR cycle-budget, frame, and interrupt yields: pass', flush=True)


if __name__ == '__main__':
    main()
