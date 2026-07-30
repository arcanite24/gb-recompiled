#!/usr/bin/env python3
"""Verify unsupported first-run input fails before cache creation."""

from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--first-run", type=Path, required=True)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="crystal-first-run-test-") as raw:
        root = Path(raw)
        secret = "private-user-selected-name"
        rom = root / f"{secret}.gbc"
        rom.write_bytes(b"\x00" * 2_097_152)
        cache = root / "private-cache"
        result = subprocess.run(
            [
                sys.executable,
                str(args.first_run),
                "--rom",
                str(rom),
                "--cache-dir",
                str(cache),
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=30,
        )
        if result.returncode != 2:
            raise RuntimeError(
                f"unsupported ROM returned {result.returncode}: {result.stderr}"
            )
        if cache.exists():
            raise RuntimeError("unsupported ROM created a private cache")
        retained = result.stdout + result.stderr
        if secret in retained or str(root) in retained:
            raise RuntimeError("unsupported-ROM diagnostics disclosed a private path")
        expected_code = '"code":"unsupported-rom"'
        if expected_code not in retained:
            raise RuntimeError(f"missing stable rejection code: {retained}")

    print("first run rejects unsupported ROMs before cache creation without path disclosure")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
