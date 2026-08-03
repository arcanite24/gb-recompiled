#!/usr/bin/env python3
"""Prepare the pinned RGBDS Rev 1 symbol map for GB Recompiled."""

from __future__ import annotations

import hashlib
import re
import sys
from pathlib import Path


PORT_DIR = Path(__file__).resolve().parent.parent
SOURCE = (
    PORT_DIR
    / "references"
    / "vendor"
    / "pokecrystal-symbols"
    / "pokecrystal11.sym"
)
OUTPUT = PORT_DIR / "references" / "generated" / "pokecrystal11.gbrecomp.sym"
ADDRESS = re.compile(r"^[0-9a-fA-F]+:[0-9a-fA-F]+$")
SOURCE_SHA256 = "ca55588e83e4f4974e3872057eec12e8aac853bad1774e91486b5986cf6cb780"
OUTPUT_SHA256 = "16281bb303b0f61027a6e728d2517b463c12960e61aefb1f9d2d823dc49fe4cc"
OUTPUT_LINES = 58_456


def digest(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def main() -> int:
    if not SOURCE.is_file():
        print(
            "error: missing pinned symbol source; run scripts/references.py fetch",
            file=sys.stderr,
        )
        return 1

    source_bytes = SOURCE.read_bytes()
    if digest(source_bytes) != SOURCE_SHA256:
        print(f"error: unexpected symbol source hash: {SOURCE}", file=sys.stderr)
        return 1

    output_lines: list[str] = []
    for line in source_bytes.decode("utf-8").splitlines():
        fields = line.split(maxsplit=1)
        if fields and ADDRESS.fullmatch(fields[0]):
            output_lines.append(line)

    output_bytes = ("\n".join(output_lines) + "\n").encode("utf-8")
    if len(output_lines) != OUTPUT_LINES or digest(output_bytes) != OUTPUT_SHA256:
        print("error: prepared symbol output did not match its lock", file=sys.stderr)
        return 1

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_bytes(output_bytes)
    print(f"ok  prepared {len(output_lines)} address records: {OUTPUT}")
    print(f"    sha256={OUTPUT_SHA256}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
