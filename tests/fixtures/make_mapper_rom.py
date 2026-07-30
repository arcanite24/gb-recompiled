#!/usr/bin/env python3
"""Create small, repository-owned mapper ROM fixtures for generated-code tests."""

from __future__ import annotations

import argparse
from pathlib import Path


NINTENDO_LOGO = bytes(
    [
        0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B,
        0x03, 0x73, 0x00, 0x83, 0x00, 0x0C, 0x00, 0x0D,
        0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E,
        0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99,
        0xBB, 0xBB, 0x67, 0x63, 0x6E, 0x0E, 0xEC, 0xCC,
        0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E,
    ]
)


def make_mbc1_rom() -> bytearray:
    rom = bytearray(1024 * 1024)
    rom[0x100:0x104] = bytes([0x00, 0xC3, 0x00, 0x01])
    rom[0x104:0x134] = NINTENDO_LOGO
    rom[0x134:0x143] = b"GBRECOMP MBC1  "
    rom[0x143] = 0x00
    rom[0x147] = 0x01
    rom[0x148] = 0x05
    rom[0x149] = 0x00
    rom[0x14D] = (-sum(rom[0x134:0x14D]) - 25) & 0xFF

    # The generated-host test reads virtual address 0x1234 in MBC1 mode 1.
    # Bank 0 deliberately differs from the correctly remapped bank 32.
    rom[0x1234] = 0x11
    rom[(32 * 0x4000) + 0x1234] = 0xA5

    # Valid but deliberately unreferenced code used to verify that aggressive
    # discovery is scoped to each ROM in a multi-ROM process.
    rom[0x6000:0x6028] = bytes(
        [
            0xAF, 0x80, 0x91, 0xA2, 0xB3, 0x0C, 0x15, 0x1C,
            0x25, 0x2C, 0x3D, 0x87, 0x96, 0xA5, 0xB4, 0x07,
            0x17, 0x1F, 0x04, 0x0D, 0x14, 0x1D, 0x24, 0x2D,
            0x3C, 0x88, 0x99, 0xAA, 0xBB, 0x05, 0x0C, 0x14,
            0x1C, 0x24, 0x2C, 0x3C, 0x8F, 0x97, 0xA7, 0xC9,
        ]
    )
    return rom


def make_rom_only() -> bytearray:
    """Create a minimal legal 32 KiB ROM that advances hardware forever."""
    rom = bytearray(32 * 1024)
    rom[0x100:0x104] = bytes([0x00, 0xC3, 0x00, 0x01])
    rom[0x104:0x134] = NINTENDO_LOGO
    rom[0x134:0x143] = b"GBRECOMP SMOKE "
    rom[0x143] = 0x00
    rom[0x147] = 0x00
    rom[0x148] = 0x00
    rom[0x149] = 0x00
    rom[0x14D] = (-sum(rom[0x134:0x14D]) - 25) & 0xFF
    return rom


def make_native_patch_rom() -> bytearray:
    """Create a legal ROM with one direct, returning native-patch target."""
    rom = bytearray(32 * 1024)
    rom[0x100:0x104] = bytes([0x00, 0xC3, 0x50, 0x01])
    rom[0x104:0x134] = NINTENDO_LOGO
    rom[0x134:0x143] = b"GBRECOMP NL5   "
    rom[0x143] = 0x00
    rom[0x147] = 0x00
    rom[0x148] = 0x00
    rom[0x149] = 0x00

    # Caller: establish a known stack, call 0000:0160 exactly once, then spin.
    rom[0x150:0x158] = bytes(
        [
            0x31, 0xFE, 0xFF,  # LD SP,$FFFE
            0xCD, 0x60, 0x01,  # CALL $0160
            0x18, 0xFE,        # JR $0156
        ]
    )

    # Original: run for roughly 84K T-cycles so one invocation spans a frame
    # safepoint, expose completion at HRAM $FF83, then return. The patch fixture
    # uses $FF80-$FF82 for pre/replace/post.
    rom[0x160:0x16D] = bytes(
        [
            0x01, 0xB8, 0x0B,  # LD BC,$0BB8 (3000 iterations)
            0x0B,              # loop: DEC BC
            0x78,              # LD A,B
            0xB1,              # OR C
            0x20, 0xFB,        # JR NZ,loop
            0x3E, 0x01,        # LD A,1
            0xE0, 0x83,        # LDH ($83),A
            0xC9,              # RET
        ]
    )

    rom[0x14D] = (-sum(rom[0x134:0x14D]) - 25) & 0xFF
    return rom


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--mapper", choices=["mbc1", "rom-only", "native-patch"], required=True
    )
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    if args.mapper == "mbc1":
        rom = make_mbc1_rom()
    elif args.mapper == "native-patch":
        rom = make_native_patch_rom()
    else:
        rom = make_rom_only()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(rom)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
