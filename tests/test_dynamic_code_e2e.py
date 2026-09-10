#!/usr/bin/env python3
"""Run issue #7's overlapping decode and changing WRAM/HRAM instructions.

Use the normal generated dispatcher: single-step mode can bypass direct calls.
The ROM is synthetic and never embeds instructions from a commercial game.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import tempfile

from fixtures.make_mapper_rom import NINTENDO_LOGO
from test_bank_guard_e2e import run


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--gbrecomp", type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="gbrecomp-dynamic-code-") as raw:
        root = Path(raw)
        rom = bytearray([0xD3]) * 0x8000
        rom[0x100:0x150] = bytes(0x50)
        rom[0x104:0x134] = NINTENDO_LOGO
        rom[0x134:0x140] = b"DYNAMIC CODE"
        rom[0x100:0x103] = bytes([0xC3, 0x50, 0x01])
        # JR Z skips LD A's opcode and executes its immediate byte as XOR A.
        rom[0x150:0x155] = bytes([0x28, 0x01, 0x3E, 0xAF, 0x76])
        rom[0x200:0x204] = bytes([0xCD, 0x00, 0xC0, 0x76])
        rom[0x220:0x224] = bytes([0xCD, 0x90, 0xFF, 0x76])
        rom[0x14D] = (-sum(rom[0x134:0x14D]) - 25) & 255
        path = root / "dynamic_code.gb"
        path.write_bytes(rom)
        project = root / "generated"
        run([str(args.gbrecomp.resolve()), str(path), "--no-scan", "--reachable-only",
             "--add-entry-point", "0:0200", "--add-entry-point", "0:0220",
             "-o", str(project)])
        (project / "dynamic_code_main.c").write_text(r'''
#include "dynamic_code.h"
#include <stdio.h>

static int execute(GBContext* ctx, unsigned pc, unsigned a, unsigned z,
                   unsigned expected_pc, int compiled) {
    ctx->a = 0x55;
    ctx->halted = 0;
    ctx->sp = 0xFFFE;
    ctx->pc = pc;
    gb_dispatch(ctx, ctx->pc);
    if (ctx->a != a || ctx->f_z != z || ctx->sp != 0xFFFE ||
        ctx->pc != expected_pc || !ctx->halted || ctx->single_step_mode ||
        (compiled && ctx->total_dispatch_fallbacks)) {
        fprintf(stderr, "entry=%04X A=%02X Z=%u SP=%04X PC=%04X halted=%u\n",
            pc, ctx->a, ctx->f_z, ctx->sp, ctx->pc, ctx->halted);
        return 1;
    }
    return 0;
}

int main(void) {
    GBConfig config = {0};
    config.model = GB_MODEL_DMG;
    GBContext* ctx = gb_context_create(&config);
    if (!ctx) return 2;
    dynamic_code_init(ctx);
    gb_write8(ctx, 0xFFFF, 0);
    gb_write8(ctx, 0xFF0F, 0);
    ctx->ime = 0;
    gbrt_dispatch_fallback_tracking_enabled = true;
    for (int taken = 0; taken < 2; ++taken) {
        ctx->f_z = taken;
        if (execute(ctx, 0x150, taken ? 0 : 0xAF, taken, 0x155, 1)) return 1;
    }
    for (int hram = 0; hram < 2; ++hram) {
        const unsigned ram = hram ? 0xFF90 : 0xC000;
        const unsigned caller = hram ? 0x220 : 0x200;
        // Revisit the same address after changing both operands and opcodes.
        for (int revision = 0; revision < 3; ++revision) {
            gb_write8(ctx, ram, revision == 2 ? 0xAF : 0x3E);
            gb_write8(ctx, ram + 1, revision == 2 ? 0xC9 : 0x11 + revision);
            gb_write8(ctx, ram + 2, 0xC9);
            ctx->f_z = 0;
            if (execute(ctx, caller, revision == 2 ? 0 : 0x11 + revision,
                        revision == 2, caller + 4, 0)) return 1;
        }
        // The running guest overwrites an instruction operand before jumping
        // to it. This checks self-modification within one dispatch as well.
        const uint8_t code[] = {0x3E, 0x42, 0xEA, (ram + 10) & 255,
            (ram + 10) >> 8, 0xC3, (ram + 9) & 255, (ram + 9) >> 8,
            0x00, 0x3E, 0x11, 0xC9};
        for (unsigned i = 0; i < sizeof(code); ++i) gb_write8(ctx, ram + i, code[i]);
        ctx->f_z = 0;
        if (execute(ctx, caller, 0x42, 0, caller + 4, 0)) return 1;
    }
    gb_context_destroy(ctx);
    return 0;
}
''')
        run(["cmake", "-G", "Ninja", "-S", str(project), "-B", str(project / "build")])
        run(["ninja", "-C", str(project / "build"), "-j", "4"])
        binary = project / "build" / "dynamic_code"
        if not binary.exists():
            binary = binary.with_suffix(".exe")
        run([str(binary)])
        print("overlapping decode and mutable WRAM/HRAM execution: pass", flush=True)


if __name__ == "__main__":
    main()
