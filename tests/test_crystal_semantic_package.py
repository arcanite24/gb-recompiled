#!/usr/bin/env python3
"""Fail-closed controls for the versioned Crystal semantic package."""

from __future__ import annotations

import copy
import json
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    validator = (
        root
        / "ports"
        / "pokemon-crystal"
        / "scripts"
        / "validate_semantic_package.py"
    )
    source_manifest = root / "ports/pokemon-crystal/semantic/package.json"
    schema = root / "ports/pokemon-crystal/semantic/package-schema.json"
    baseline = json.loads(source_manifest.read_text(encoding="utf-8"))

    def run(payload: dict) -> subprocess.CompletedProcess[str]:
        manifest.write_text(json.dumps(payload), encoding="utf-8")
        return subprocess.run(
            [
                sys.executable,
                str(validator),
                "--manifest",
                str(manifest),
                "--schema",
                str(schema),
            ],
            text=True,
            capture_output=True,
            check=False,
        )

    with tempfile.TemporaryDirectory() as raw:
        manifest = Path(raw) / "package.json"
        if run(baseline).returncode != 0:
            raise AssertionError("validator rejected the checked semantic package")

        controls = []
        wrong_version = copy.deepcopy(baseline)
        wrong_version["runtime_abi"]["version"] = 2
        controls.append(wrong_version)
        wrong_rom = copy.deepcopy(baseline)
        wrong_rom["rom"]["sha256"] = "0" * 64
        controls.append(wrong_rom)
        wrong_space = copy.deepcopy(baseline)
        wrong_space["views"][0]["memory"]["space"] = "host_pointer"
        controls.append(wrong_space)
        wrong_width = copy.deepcopy(baseline)
        wrong_width["views"][0]["memory"]["width"] = 0
        controls.append(wrong_width)
        wrong_save_bank = copy.deepcopy(baseline)
        wrong_save_bank["views"][0]["save_memory"]["bank"] = 4
        controls.append(wrong_save_bank)
        wrong_save_width = copy.deepcopy(baseline)
        wrong_save_width["views"][0]["save_memory"]["width"] = 10
        controls.append(wrong_save_width)
        wrong_save_space = copy.deepcopy(baseline)
        wrong_save_space["views"][0]["save_memory"]["space"] = "banked_wram"
        wrong_save_space["views"][0]["save_memory"]["address"] = "0xd00b"
        controls.append(wrong_save_space)
        wrong_access = copy.deepcopy(baseline)
        wrong_access["views"][4]["access"] = "read_only"
        controls.append(wrong_access)
        missing_backup = copy.deepcopy(baseline)
        del missing_backup["views"][4]["backup_memory"]
        controls.append(missing_backup)
        wrong_backup_bank = copy.deepcopy(baseline)
        wrong_backup_bank["views"][4]["backup_memory"]["bank"] = 4
        controls.append(wrong_backup_bank)
        missing_canonical = copy.deepcopy(baseline)
        del missing_canonical["views"][5]["canonical_memory"]
        controls.append(missing_canonical)
        wrong_canonical_stride = copy.deepcopy(baseline)
        wrong_canonical_stride["views"][5]["canonical_memory"]["stride"] = 1
        controls.append(wrong_canonical_stride)
        wrong_canonical_selector = copy.deepcopy(baseline)
        wrong_canonical_selector["views"][5]["canonical_memory"][
            "selector_memory"
        ]["address"] = "0xa701"
        controls.append(wrong_canonical_selector)
        overlap = copy.deepcopy(baseline)
        overlap["views"][1]["memory"] = copy.deepcopy(
            overlap["views"][0]["memory"]
        )
        controls.append(overlap)

        for index, payload in enumerate(controls):
            if run(payload).returncode == 0:
                raise AssertionError(
                    f"validator accepted invalid semantic control {index}"
                )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
