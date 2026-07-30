#!/usr/bin/env python3
"""Fail unless two fresh Crystal generations have identical inventories."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path


METADATA = "pokemon_crystal_metadata.json"
RECEIPT = "crystal-generation.json"
EXCLUDED_ROOTS = frozenset({"build"})
EXCLUDED_FILES = frozenset({".DS_Store", "imgui.ini"})


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def inventory(root: Path) -> dict[str, dict[str, int | str]]:
    if not root.is_dir():
        raise RuntimeError(f"missing generated directory: {root}")
    result: dict[str, dict[str, int | str]] = {}
    for path in sorted(candidate for candidate in root.rglob("*") if candidate.is_file()):
        relative = path.relative_to(root)
        if relative.parts[0] in EXCLUDED_ROOTS:
            continue
        if path.name in EXCLUDED_FILES:
            continue
        result[relative.as_posix()] = {
            "size": path.stat().st_size,
            "sha256": sha256(path),
        }
    return result


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("first", type=Path)
    parser.add_argument("second", type=Path)
    parser.add_argument("--json-out", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    first = args.first.resolve()
    second = args.second.resolve()
    first_inventory = inventory(first)
    second_inventory = inventory(second)

    missing_from_second = sorted(first_inventory.keys() - second_inventory.keys())
    missing_from_first = sorted(second_inventory.keys() - first_inventory.keys())
    changed = sorted(
        path
        for path in first_inventory.keys() & second_inventory.keys()
        if first_inventory[path] != second_inventory[path]
    )
    metadata_equal = (
        first_inventory.get(METADATA) == second_inventory.get(METADATA)
        and METADATA in first_inventory
    )
    receipt_equal = (
        first_inventory.get(RECEIPT) == second_inventory.get(RECEIPT)
        and RECEIPT in first_inventory
    )
    passed = not missing_from_second and not missing_from_first and not changed

    payload = {
        "schema": "crystal-recompiled.generation-comparison",
        "version": 1,
        "passed": passed,
        "file_count": len(first_inventory),
        "metadata_equal": metadata_equal,
        "receipt_equal": receipt_equal,
        "missing_from_first": missing_from_first,
        "missing_from_second": missing_from_second,
        "changed": changed,
        "first_inventory": first_inventory,
        "second_inventory": second_inventory,
        "exclusions": {
            "roots": sorted(EXCLUDED_ROOTS),
            "files": sorted(EXCLUDED_FILES),
            "reason": "post-generation build and host UI state are not generation outputs",
        },
    }
    rendered = json.dumps(payload, indent=2, sort_keys=True) + "\n"
    if args.json_out is not None:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(rendered, encoding="utf-8")
    print(
        f"{'PASS' if passed else 'FAIL'} files={len(first_inventory)} "
        f"metadata_equal={metadata_equal} receipt_equal={receipt_equal} "
        f"changed={len(changed)}"
    )
    if not passed:
        print(rendered, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
