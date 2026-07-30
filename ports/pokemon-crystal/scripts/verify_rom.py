#!/usr/bin/env python3
"""Fail closed unless the selected ROM is Pokémon Crystal UE Rev 1."""

from __future__ import annotations

import argparse
import hashlib
import sys
from pathlib import Path


PORT_DIR = Path(__file__).resolve().parent.parent
REPO_ROOT = PORT_DIR.parent.parent
DEFAULT_ROM = REPO_ROOT / "roms" / "selected_gbc_top10" / "pokemon_crystal.gbc"
EXPECTED = {
    "size": 2_097_152,
    "sha1": "f2f52230b536214ef7c9924f483392993e226cfb",
    "sha256": "fdcc3c8c43813cf8731fc037d2a6d191bac75439c34b24ba1c27526e6acdc8a2",
    "md5": "301899b8087289a6436b0a241fbbb474",
}


def identify(path: Path) -> dict[str, int | str]:
    sha1 = hashlib.sha1()
    sha256 = hashlib.sha256()
    md5 = hashlib.md5()
    size = 0
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            size += len(chunk)
            sha1.update(chunk)
            sha256.update(chunk)
            md5.update(chunk)
    return {
        "size": size,
        "sha1": sha1.hexdigest(),
        "sha256": sha256.hexdigest(),
        "md5": md5.hexdigest(),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("rom", nargs="?", type=Path, default=DEFAULT_ROM)
    args = parser.parse_args()

    if not args.rom.is_file():
        print(f"error: ROM not found: {args.rom}", file=sys.stderr)
        return 1

    actual = identify(args.rom)
    mismatches = [
        key for key, expected in EXPECTED.items() if actual.get(key) != expected
    ]
    if mismatches:
        print(
            "error: unsupported ROM; mismatched " + ", ".join(mismatches),
            file=sys.stderr,
        )
        print(f"actual SHA-256: {actual['sha256']}", file=sys.stderr)
        return 1

    print(f"ok  Pokémon Crystal UE Rev 1: {args.rom}")
    print(f"    size={actual['size']} sha256={actual['sha256']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
