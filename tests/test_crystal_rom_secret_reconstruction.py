#!/usr/bin/env python3
"""Test the private Crystal CI secret-shard reconstruction contract."""

from __future__ import annotations

import argparse
import base64
import hashlib
import importlib.util
import lzma
from pathlib import Path


def load_module(path: Path):
    spec = importlib.util.spec_from_file_location("crystal_rom_secret", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load reconstruction helper")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def split_exact(text: str, count: int) -> list[str]:
    width = (len(text) + count - 1) // count
    parts = [text[offset : offset + width] for offset in range(0, len(text), width)]
    return parts + [""] * (count - len(parts))


def expect_rejected(module, parts: list[str], size: int, digest: str) -> None:
    try:
        module.reconstruct(parts, expected_size=size, expected_sha256=digest)
    except RuntimeError:
        return
    raise RuntimeError("invalid secret shards were accepted")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--helper", type=Path, required=True)
    args = parser.parse_args()
    module = load_module(args.helper.resolve())
    payload = b"".join(
        hashlib.sha256(f"private-fixture-{index}".encode()).digest()
        for index in range(4096)
    )
    encoded = base64.b64encode(
        lzma.compress(payload, format=lzma.FORMAT_XZ)
    ).decode("ascii")
    parts = split_exact(encoded, module.PART_COUNT)
    digest = hashlib.sha256(payload).hexdigest()
    if module.reconstruct(
        parts,
        expected_size=len(payload),
        expected_sha256=digest,
    ) != payload:
        raise RuntimeError("valid secret shards did not reconstruct")

    missing = parts.copy()
    missing[3] = ""
    expect_rejected(module, missing, len(payload), digest)
    malformed = parts.copy()
    malformed[4] = malformed[4][:-1] + "!"
    expect_rejected(module, malformed, len(payload), digest)
    expect_rejected(module, parts, len(payload) + 1, digest)
    expect_rejected(module, parts, len(payload), "0" * 64)
    expect_rejected(module, parts[:-1], len(payload), digest)

    print("private Crystal ROM reconstruction is exact and fail-closed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
