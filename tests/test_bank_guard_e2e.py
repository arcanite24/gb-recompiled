#!/usr/bin/env python3
"""Execute generated CALL/JP paths with mapper state different from analysis.

Single-step differential mode returns before direct calls, so this regression
must run the freshly generated C normally. MBC1 mode 1 also banks the lower
window: a statically resolved bank-zero target needs the same runtime guard.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

from fixtures.make_mapper_rom import NINTENDO_LOGO


def run(command: list[str]) -> None:
    result = subprocess.run(command, capture_output=True, text=True, timeout=180)
    if result.returncode:
        raise AssertionError(f"{command!r}\n{result.stdout}\n{result.stderr}")


def check_case(gbrecomp: Path, root: Path, opcode: int, lower: bool,
               native: bool = False) -> None:
    caller = 0x4000 if lower else 0x0150
    target = 0x0200 if lower else 0x4000
    static_bank, live_bank = (0, 32) if lower else (1, 2)
    source_bank = 33 if lower else 0
    banks = 64 if lower else 4
    rom = bytearray([0xD3]) * (banks * 0x4000)
    rom[0x100:0x150] = bytes(0x50)
    rom[0x104:0x134] = NINTENDO_LOGO
    rom[0x134:0x143] = b"BANK GUARD TEST"
    rom[0x147] = 1  # MBC1
    rom[0x148] = 5 if lower else 1
    rom[0x100:0x103] = bytes([0xC3, 0x50, 0x01])
    rom[0x150] = 0x76 if lower else opcode
    offset = source_bank * 0x4000 + (caller & 0x3FFF)
    rom[offset:offset + 4] = bytes([opcode, target & 255, target >> 8, 0x76])
    for bank, value in [(static_bank, 0x11), (live_bank, 0x22)]:
        offset = bank * 0x4000 + (target & 0x3FFF)
        rom[offset:offset + 4] = bytes([0x3E, value, 0xC9 if native else 0x76, 0xC9])
    rom[0x14D] = (-sum(rom[0x134:0x14D]) - 25) & 255
    rom_path = root / "bank_guard.gb"
    rom_path.write_bytes(rom)
    project = root / "generated"
    command = [str(gbrecomp), str(rom_path), "--no-scan", "--reachable-only",
               "-o", str(project), "--add-entry-point", f"{live_bank}:{target:04x}"]
    if lower:
        command += ["--add-entry-point", f"{source_bank}:{caller:04x}"]
    if native:
        (root / "patch.c").write_text('''#include "gbrt_native_patch.h"
GB_NATIVE_HOOK(guard_pre) {
    gb_native_context(call)->hram[0]++;
    return GB_NATIVE_STATUS_OK;
}
GB_NATIVE_HOOK(guard_post) {
    gb_native_context(call)->hram[1]++;
    return GB_NATIVE_STATUS_OK;
}
''')
        manifest = root / "patch.json"
        manifest.write_text(json.dumps({
            "schema": "gbrecomp.native-patch", "version": 1,
            "patch_id": "org.gbrecompiled.bank-guard-test",
            "rom": {"sha256": hashlib.sha256(rom).hexdigest(), "size": len(rom)},
            "sources": ["patch.c"],
            "bindings": [{"function": f"gbfn:v1:{static_bank:04x}:{target:04x}",
                          "pre": "guard_pre", "post": "guard_post"}],
        }))
        command += ["--native-patch", str(manifest)]
    run(command)

    is_call = opcode in (0xCD, 0xC4)
    conditional = opcode in (0xC4, 0xC2)
    # Only replace the generated CLI frontend; build and execute its actual
    # emitted functions, dispatcher, ROM data, and runtime through its CMake.
    (project / "bank_guard_main.c").write_text(f'''
#include "bank_guard.h"
#include "gbrt_native_patch.h"
#include <stdio.h>
int main(void) {{
    for (int mismatch = 0; mismatch < 2; ++mismatch) {{
        for (int taken = {0 if conditional else 1}; taken < 2; ++taken) {{
            GBConfig config = {{0}};
            config.model = GB_MODEL_DMG;
            GBContext* ctx = gb_context_create(&config);
            if (!ctx) return 2;
            bank_guard_init(ctx);
            /* Rejected native calls must not accumulate pending frames. */
            for (int attempt = 0; attempt < ({int(native)} && mismatch ? 40 : 1); ++attempt) {{
            ctx->halted = 0;
            gb_write8(ctx, 0x2000, {1 if lower else 'mismatch ? 2 : 1'});
            gb_write8(ctx, 0x4000, {1 if lower else 0});
            gb_write8(ctx, 0x6000, {"mismatch" if lower else 0});
            gb_write8(ctx, 0xFFFF, 0);
            gb_write8(ctx, 0xFF0F, 0);
            ctx->ime = 0;
            ctx->f_z = !taken;
            ctx->a = 0x55;
            ctx->sp = 0xFFFE;
            ctx->pc = 0x{caller:04X};
            gbrt_dispatch_fallback_tracking_enabled = true;
            gb_dispatch(ctx, ctx->pc);
            const unsigned expected = taken ? (mismatch ? 0x22 : 0x11) : 0x55;
            const unsigned sp = {"taken ? 0xFFFC : 0xFFFE" if is_call and not native else "0xFFFE"};
            const unsigned pc = taken ? 0x{caller + 4 if native else target + 3:04X} : 0x{caller + 4:04X};
            int failed = ctx->a != expected || !ctx->halted ||
                ctx->sp != sp || ctx->pc != pc || ctx->single_step_mode ||
                ctx->total_dispatch_fallbacks != 0;
            if ({int(is_call and not native)} && taken) {{
                failed |= gb_read16(ctx, ctx->sp) != 0x{caller + 3:04X};
            }}
            {"failed |= gb_native_patch_failed(ctx) || ctx->hram[0] != (!mismatch && taken) || ctx->hram[1] != (!mismatch && taken);" if native else ""}
            if (failed) {{
                fprintf(stderr, "bank guard: opcode={opcode:02X} lower={int(lower)} "
                    "mismatch=%d taken=%d A=%02X expected=%02X PC=%04X SP=%04X\\n",
                    mismatch, taken, ctx->a, expected, ctx->pc, ctx->sp);
            }}
            if (failed) {{ gb_context_destroy(ctx); return 1; }}
            }}
            gb_context_destroy(ctx);
        }}
    }}
    return 0;
}}
''')
    run(["cmake", "-G", "Ninja", "-S", str(project), "-B", str(project / "build")])
    run(["ninja", "-C", str(project / "build"), "-j", "4"])
    binary = project / "build" / "bank_guard"
    if not binary.exists():
        binary = binary.with_suffix(".exe")
    run([str(binary)])
    print(f"generated opcode={opcode:02X} lower={lower} native={native}: pass", flush=True)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--gbrecomp", type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="gbrecomp-bank-guard-") as raw:
        for lower in (False, True):
            for opcode in (0xCD, 0xC4, 0xC3, 0xC2):
                root = Path(raw) / f"{lower}-{opcode:02x}"
                root.mkdir()
                check_case(args.gbrecomp.resolve(), root, opcode, lower)
        for opcode in (0xCD, 0xC4):
            root = Path(raw) / f"native-{opcode:02x}"
            root.mkdir()
            check_case(args.gbrecomp.resolve(), root, opcode, False, native=True)


if __name__ == "__main__":
    main()
