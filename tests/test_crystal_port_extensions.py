#!/usr/bin/env python3
"""Fail-closed tests for source-built Crystal port extensions."""

from __future__ import annotations

import copy
import json
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    package = (
        root
        / "ports/pokemon-crystal/native-extensions/encounter-lens"
    )
    validator = (
        root / "ports/pokemon-crystal/scripts/validate_port_extensions.py"
    )
    baseline = json.loads(
        (package / "manifest.json").read_text(encoding="utf-8")
    )
    source = (package / "encounter_lens.c").read_bytes()
    with tempfile.TemporaryDirectory() as raw:
        temporary = Path(raw)
        output = temporary / "resolution.json"

        def materialize(name: str, payload: dict) -> Path:
            directory = temporary / name
            directory.mkdir()
            (directory / "encounter_lens.c").write_bytes(source)
            manifest = directory / "manifest.json"
            manifest.write_text(json.dumps(payload), encoding="utf-8")
            return manifest

        def run(manifests: list[Path]) -> subprocess.CompletedProcess[str]:
            command = [
                sys.executable,
                str(validator),
                "--output",
                str(output),
            ]
            for manifest in manifests:
                command.extend(["--manifest", str(manifest)])
            return subprocess.run(
                command, capture_output=True, text=True, check=False
            )

        valid = materialize("valid", baseline)
        first = run([valid])
        if first.returncode != 0:
            raise AssertionError(f"valid extension was rejected: {first.stdout}")
        first_bytes = output.read_bytes()
        if run([valid]).returncode != 0 or output.read_bytes() != first_bytes:
            raise AssertionError("extension resolution did not reproduce")

        controls: list[tuple[str, dict]] = []
        wrong_abi = copy.deepcopy(baseline)
        wrong_abi["extension"]["abi_version"] = 2
        controls.append(("wrong ABI", wrong_abi))
        wrong_rom = copy.deepcopy(baseline)
        wrong_rom["rom"]["sha256"] = "0" * 64
        controls.append(("wrong ROM", wrong_rom))
        wrong_host = copy.deepcopy(baseline)
        wrong_host["host"]["version"] = 7
        controls.append(("wrong host", wrong_host))
        bad_hash = copy.deepcopy(baseline)
        bad_hash["sources"][0]["sha256"] = "0" * 64
        controls.append(("source hash", bad_hash))
        capability_escape = copy.deepcopy(baseline)
        capability_escape["capabilities"].append("filesystem-write")
        controls.append(("capability escalation", capability_escape))
        escaping = copy.deepcopy(baseline)
        escaping["sources"][0]["path"] = "../encounter_lens.c"
        controls.append(("source escape", escaping))
        unknown = copy.deepcopy(baseline)
        unknown["dynamic_library"] = "extension.dylib"
        controls.append(("unknown dynamic field", unknown))
        for index, (label, payload) in enumerate(controls):
            manifest = materialize(f"invalid-{index}", payload)
            if run([manifest]).returncode == 0:
                raise AssertionError(f"validator accepted {label}")
            if output.exists():
                raise AssertionError(f"validator retained output for {label}")

        addon = copy.deepcopy(baseline)
        addon["extension"]["id"] = "org.gbrecompiled.crystal.second-lens"
        addon["extension"]["priority"] = 300
        addon["entry_symbol"] = "crystal_second_lens_extension_get"
        addon_source = source.replace(
            b"crystal_encounter_lens_extension_get",
            b"crystal_second_lens_extension_get",
        )
        addon["sources"][0]["sha256"] = __import__("hashlib").sha256(
            addon_source
        ).hexdigest()
        addon_dir = temporary / "addon"
        addon_dir.mkdir()
        (addon_dir / "encounter_lens.c").write_bytes(addon_source)
        addon_manifest = addon_dir / "manifest.json"
        addon_manifest.write_text(json.dumps(addon), encoding="utf-8")
        if run([addon_manifest, valid]).returncode != 0:
            raise AssertionError("compatible extension set was rejected")
        resolution = json.loads(output.read_text(encoding="utf-8"))
        if resolution["load_order"] != [
            "org.gbrecompiled.crystal.encounter-lens",
            "org.gbrecompiled.crystal.second-lens",
        ]:
            raise AssertionError("extension priority/order was not deterministic")

        conflict = copy.deepcopy(addon)
        conflict["load"]["conflicts"] = [
            "org.gbrecompiled.crystal.encounter-lens"
        ]
        addon_manifest.write_text(json.dumps(conflict), encoding="utf-8")
        if run([valid, addon_manifest]).returncode == 0 or output.exists():
            raise AssertionError("installed extension conflict was accepted")

        missing = copy.deepcopy(addon)
        missing["load"]["dependencies"] = [
            {
                "id": "org.gbrecompiled.crystal.missing",
                "version": "1.0.0",
            }
        ]
        addon_manifest.write_text(json.dumps(missing), encoding="utf-8")
        if run([addon_manifest]).returncode == 0 or output.exists():
            raise AssertionError("missing extension dependency was accepted")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
