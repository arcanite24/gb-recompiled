#!/usr/bin/env python3
"""Fail-closed controls for Crystal's native port-module manifest."""

from __future__ import annotations

import copy
import json
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    module_dir = root / "ports/pokemon-crystal/module"
    source_manifest = module_dir / "port-module.json"
    validator = (
        root
        / "ports/pokemon-crystal/scripts/validate_port_module.py"
    )
    baseline = json.loads(source_manifest.read_text(encoding="utf-8"))

    def run(payload: dict, directory: Path) -> int:
        manifest = directory / "port-module.json"
        manifest.write_text(json.dumps(payload), encoding="utf-8")
        for source in baseline["sources"]:
            (directory / source["path"]).write_bytes(
                (module_dir / source["path"]).read_bytes()
            )
        return subprocess.run(
            [
                sys.executable,
                str(validator),
                "--manifest",
                str(manifest),
            ],
            capture_output=True,
            check=False,
        ).returncode

    with tempfile.TemporaryDirectory() as raw:
        base = Path(raw)
        valid = base / "valid"
        valid.mkdir()
        if run(baseline, valid) != 0:
            raise AssertionError("validator rejected checked module")
        controls = []
        wrong_abi = copy.deepcopy(baseline)
        wrong_abi["module"]["abi_version"] = 1
        controls.append(wrong_abi)
        wrong_rom = copy.deepcopy(baseline)
        wrong_rom["rom"]["sha256"] = "0" * 64
        controls.append(wrong_rom)
        wrong_hash = copy.deepcopy(baseline)
        wrong_hash["sources"][0]["sha256"] = "0" * 64
        controls.append(wrong_hash)
        escaping = copy.deepcopy(baseline)
        escaping["sources"][0]["path"] = "../crystal_port.c"
        controls.append(escaping)
        unknown = copy.deepcopy(baseline)
        unknown["unexpected"] = True
        controls.append(unknown)
        for index, payload in enumerate(controls):
            directory = base / f"invalid-{index}"
            directory.mkdir()
            if run(payload, directory) == 0:
                raise AssertionError(
                    f"validator accepted invalid module control {index}"
                )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
