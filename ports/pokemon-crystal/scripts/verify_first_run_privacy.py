#!/usr/bin/env python3
"""Audit one first-run cache and optional public source tree."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import stat
import sys
from pathlib import Path


FORBIDDEN_PUBLIC_SUFFIXES = {
    ".a",
    ".dll",
    ".dylib",
    ".exe",
    ".gb",
    ".gbc",
    ".o",
    ".obj",
    ".rtc",
    ".sav",
    ".so",
}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_events(path: Path) -> list[dict[str, object]]:
    return [
        json.loads(line)
        for line in path.read_text(encoding="utf-8").splitlines()
        if line
    ]


def contains_bytes(path: Path, needle: bytes) -> bool:
    overlap = max(0, len(needle) - 1)
    previous = b""
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            payload = previous + chunk
            if needle in payload:
                return True
            previous = payload[-overlap:] if overlap else b""
    return False


def require_sequence(
    events: list[dict[str, object]],
    *,
    schema: str,
    stages: list[tuple[str, str, int]],
    total: int,
) -> None:
    actual = [
        (event.get("event"), event.get("stage"), event.get("completed"))
        for event in events
    ]
    if actual != stages:
        raise RuntimeError(f"unexpected {schema} sequence: {actual}")
    if any(
        event.get("schema") != schema
        or event.get("schema_version") != 1
        or event.get("total") != total
        or "code" in event
        for event in events
    ):
        raise RuntimeError(f"invalid successful {schema} event")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cache", type=Path, required=True)
    parser.add_argument("--selected-rom", type=Path, required=True)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--public-tree", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    cache = args.cache.resolve()
    selected_rom = args.selected_rom.resolve()
    source_root = args.source_root.resolve()
    try:
        cache.relative_to(source_root)
    except ValueError:
        pass
    else:
        raise RuntimeError("private cache is inside the source tree")

    first_run_events = read_events(cache / "status" / "first-run-progress.jsonl")
    require_sequence(
        first_run_events,
        schema="crystal-recompiled.first-run-progress",
        stages=[
            ("stage", "rom-selection", 0),
            ("stage", "rom-validated", 1),
            ("stage", "local-generation", 2),
            ("stage", "configure", 3),
            ("stage", "build", 4),
            ("complete", "complete", 5),
        ],
        total=5,
    )
    recompiler_events = read_events(cache / "status" / "gbrecomp-progress.jsonl")
    require_sequence(
        recompiler_events,
        schema="gbrecomp.progress",
        stages=[
            ("stage", "rom-validated", 1),
            ("stage", "analysis-complete", 2),
            ("stage", "ir-complete", 3),
            ("stage", "code-generation-complete", 4),
            ("stage", "output-complete", 5),
            ("complete", "complete", 6),
        ],
        total=6,
    )

    receipt_path = cache / "first-run.json"
    receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
    if (
        receipt.get("schema") != "crystal-recompiled.first-run"
        or receipt.get("version") != 1
        or receipt.get("privacy")
        != {
            "save_included": False,
            "source_path_retained": False,
            "telemetry": False,
        }
    ):
        raise RuntimeError("invalid first-run privacy receipt")
    generated = cache / "generated" / "crystal-rev1-v1"
    metadata = json.loads(
        (generated / "pokemon_crystal_metadata.json").read_text(encoding="utf-8")
    )
    if metadata.get("rom_name") != "pokemon_crystal":
        raise RuntimeError("generated metadata retained a user-controlled ROM name")

    selected_path_bytes = str(selected_rom).encode("utf-8")
    leaked = [
        path
        for path in cache.rglob("*")
        if path.is_file() and contains_bytes(path, selected_path_bytes)
    ]
    if leaked:
        raise RuntimeError("selected ROM path entered the private generated cache")

    private_directories = 0
    if os.name != "nt":
        for directory in [cache, *[path for path in cache.rglob("*") if path.is_dir()]]:
            private_directories += 1
            mode = stat.S_IMODE(directory.stat().st_mode)
            if mode & 0o077:
                raise RuntimeError("private cache directory is group/world accessible")

    public_file_count = 0
    if args.public_tree is not None:
        public_tree = args.public_tree.resolve()
        forbidden = []
        for path in public_tree.rglob("*"):
            if not path.is_file():
                continue
            public_file_count += 1
            if path.suffix.lower() in FORBIDDEN_PUBLIC_SUFFIXES:
                forbidden.append(path)
        if forbidden:
            raise RuntimeError("public source tree contains a private/derived artifact")
        if contains_bytes(
            public_tree / "SOURCE-MANIFEST.json", selected_path_bytes
        ):
            raise RuntimeError("public source manifest disclosed selected ROM path")

    report = {
        "schema": "crystal-recompiled.first-run-privacy-audit",
        "version": 1,
        "passed": True,
        "launcher_progress_events": len(first_run_events),
        "recompiler_progress_events": len(recompiler_events),
        "private_directory_count": private_directories,
        "public_file_count": public_file_count,
        "selected_path_matches": 0,
        "metadata_rom_name": metadata["rom_name"],
        "receipt_sha256": sha256_file(receipt_path),
        "generation_receipt_sha256": sha256_file(
            generated / "crystal-generation.json"
        ),
        "executable_sha256": receipt["generated"]["executable_sha256"],
        "telemetry": False,
    }
    args.output.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print("first-run privacy audit passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
