#!/usr/bin/env python3
"""Validate the two tracked, ROM-free Crystal sample packages."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    mods = root / "ports/pokemon-crystal/mods"
    packages = [
        mods / "samples/route29-encounter-guide/package.json",
        mods / "samples/route29-level-five/package.json",
    ]
    with tempfile.TemporaryDirectory() as temporary:
        output = Path(temporary) / "resolution.json"
        command = [
            sys.executable,
            str(root / "tools/validate_data_mods.py"),
            "--policy",
            str(mods / "target-policy.json"),
            "--package-schema",
            str(mods / "package-schema.json"),
            "--semantic-package",
            str(root / "ports/pokemon-crystal/semantic/package.json"),
            "--semantic-schema",
            str(root / "ports/pokemon-crystal/semantic/package-schema.json"),
            "--output",
            str(output),
        ]
        for package in reversed(packages):
            command.extend(["--manifest", str(package)])
        first = subprocess.run(command, capture_output=True, text=True, check=False)
        if first.returncode != 0:
            raise AssertionError(f"sample packages were rejected: {first.stdout}")
        first_bytes = output.read_bytes()
        resolution = json.loads(first_bytes)
        expected_order = [
            "org.gbrecompiled.crystal.route29-level-five",
            "org.gbrecompiled.crystal.route29-encounter-guide",
        ]
        if resolution["load_order"] != expected_order:
            raise AssertionError("sample package load order is not deterministic")
        second = subprocess.run(command, capture_output=True, text=True, check=False)
        if second.returncode != 0 or output.read_bytes() != first_bytes:
            raise AssertionError("sample resolution did not reproduce byte-for-byte")
        targets = {
            item["target"]
            for package in resolution["packages"]
            for item in package["content"]
        }
        if targets != {"crystal.encounters.v1", "crystal.accessibility.v1"}:
            raise AssertionError("sample packages do not cover both promised targets")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
