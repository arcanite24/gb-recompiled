#!/usr/bin/env python3
"""Validate Crystal's exact-ROM native port-module manifest."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path


EXPECTED_ROM = "fdcc3c8c43813cf8731fc037d2a6d191bac75439c34b24ba1c27526e6acdc8a2"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require_keys(value: dict, expected: set[str], label: str) -> None:
    if set(value) != expected:
        raise ValueError(f"{label} fields are missing or unknown")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        payload = json.loads(args.manifest.read_text(encoding="utf-8"))
        require_keys(
            payload, {"schema", "version", "module", "rom", "sources"},
            "manifest",
        )
        if (
            payload["schema"] != "gbrecompiled.port-module"
            or payload["version"] != 1
        ):
            raise ValueError("unsupported port-module schema")
        module = payload["module"]
        require_keys(module, {"id", "version", "abi_version"}, "module")
        if (
            module != {
                "id": "crystal-workbench",
                "version": 8,
                "abi_version": 2,
            }
        ):
            raise ValueError("unsupported module identity or ABI")
        rom = payload["rom"]
        require_keys(rom, {"size", "sha256"}, "ROM")
        if rom != {"size": 2097152, "sha256": EXPECTED_ROM}:
            raise ValueError("unsupported exact ROM")
        sources = payload["sources"]
        expected_sources = {
            "crystal_port.c",
            "crystal_pc.c",
            "crystal_pc.h",
            "crystal_overworld.c",
            "crystal_overworld.h",
            "crystal_battle.c",
            "crystal_battle.h",
        }
        if not isinstance(sources, list) or len(sources) != len(expected_sources):
            raise ValueError("expected the complete native port source set")
        actual_sources: set[str] = set()
        entry_point_found = False
        for source in sources:
            require_keys(source, {"path", "sha256"}, "source")
            relative = source["path"]
            if (
                not isinstance(relative, str)
                or re.fullmatch(
                    r"[a-z][a-z0-9_-]*\.(?:c|h)", relative
                )
                is None
                or relative in actual_sources
            ):
                raise ValueError("invalid, duplicate, or escaping source path")
            actual_sources.add(relative)
            source_path = (args.manifest.parent / relative).resolve()
            source_path.relative_to(args.manifest.parent.resolve())
            if (
                not source_path.is_file()
                or sha256(source_path) != source["sha256"]
            ):
                raise ValueError(
                    "source is missing or its hash does not match"
                )
            if (
                relative == "crystal_port.c"
                and "gb_port_module_get"
                in source_path.read_text(encoding="utf-8")
            ):
                entry_point_found = True
        if actual_sources != expected_sources or not entry_point_found:
            raise ValueError("port source set or entry point is invalid")
        result = {
            "schema": "gbrecompiled.port-module-validation",
            "version": 1,
            "passed": True,
            "module_id": module["id"],
            "module_version": module["version"],
            "abi_version": module["abi_version"],
            "manifest_sha256": sha256(args.manifest),
            "source_sha256": {
                source["path"]: source["sha256"] for source in sources
            },
        }
        rendered = json.dumps(result, indent=2, sort_keys=True) + "\n"
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(rendered, encoding="utf-8")
        print(rendered, end="")
        return 0
    except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError) as error:
        print(f"FAIL: {error}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
