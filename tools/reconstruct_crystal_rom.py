#!/usr/bin/env python3
"""Reconstruct the private Crystal CI ROM from encrypted secret shards."""

from __future__ import annotations

import argparse
import base64
import hashlib
import lzma
import os
import sys
from pathlib import Path


PART_COUNT = 30
EXPECTED_SIZE = 2_097_152
EXPECTED_SHA256 = "fdcc3c8c43813cf8731fc037d2a6d191bac75439c34b24ba1c27526e6acdc8a2"


def decode_payload(
    encoded: str,
    *,
    expected_size: int,
    expected_sha256: str,
) -> bytes:
    if not encoded or not encoded.isascii():
        raise RuntimeError("private ROM secret payload is incomplete")
    normalized = "".join(encoded.split())
    if not normalized:
        raise RuntimeError("private ROM secret payload is incomplete")
    try:
        compressed = base64.b64decode(normalized, validate=True)
        payload = lzma.decompress(compressed, format=lzma.FORMAT_XZ)
    except (ValueError, lzma.LZMAError) as error:
        raise RuntimeError("private ROM secret payload is malformed") from error
    if len(payload) != expected_size:
        raise RuntimeError("private ROM has an unsupported size")
    if hashlib.sha256(payload).hexdigest() != expected_sha256:
        raise RuntimeError("private ROM has an unsupported identity")
    return payload


def reconstruct(
    parts: list[str],
    *,
    expected_size: int,
    expected_sha256: str,
) -> bytes:
    if len(parts) != PART_COUNT or any(not part for part in parts):
        raise RuntimeError("private ROM secret shards are incomplete")
    return decode_payload(
        "".join(parts),
        expected_size=expected_size,
        expected_sha256=expected_sha256,
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input-base64", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    source = args.input_base64.expanduser().resolve()
    output = args.output.expanduser().resolve()
    if output.exists():
        raise RuntimeError("private ROM output already exists")
    if not source.is_file() or source.is_symlink():
        raise RuntimeError("private ROM Base64 input is missing or unsafe")
    if source.stat().st_size > 1_500_000:
        raise RuntimeError("private ROM Base64 input is unexpectedly large")
    try:
        encoded = source.read_text(encoding="ascii")
    except UnicodeDecodeError as error:
        raise RuntimeError("private ROM Base64 input is not ASCII") from error
    payload = decode_payload(
        encoded,
        expected_size=EXPECTED_SIZE,
        expected_sha256=EXPECTED_SHA256,
    )
    output.parent.mkdir(parents=True, exist_ok=True)
    descriptor = os.open(output, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    try:
        with os.fdopen(descriptor, "wb") as handle:
            handle.write(payload)
    except BaseException:
        output.unlink(missing_ok=True)
        raise
    print("verified private Crystal ROM input")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
