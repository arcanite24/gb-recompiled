#!/usr/bin/env python3
"""Synthetic fail-closed tests for portable data-mod replay preflight."""

from __future__ import annotations

import base64
import copy
import hashlib
import importlib.util
import json
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT_DIR = ROOT / "ports" / "pokemon-crystal" / "scripts"
sys.path.insert(0, str(SCRIPT_DIR))
SPEC = importlib.util.spec_from_file_location(
    "verify_data_mod_replay", SCRIPT_DIR / "verify_data_mod_replay.py"
)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def write(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, sort_keys=True), encoding="utf-8")


def expect_failure(
    replay_path: Path, rom: Path, executable: Path, receipt: Path
) -> None:
    try:
        MODULE.preflight(replay_path, rom, executable, receipt)
    except MODULE.ReplayError:
        return
    raise AssertionError("mismatched replay provenance passed preflight")


def main() -> int:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        rom_bytes = b"synthetic-rom"
        executable_bytes = b"synthetic-executable"
        rom = root / "game.gb"
        executable = root / "game"
        receipt_path = root / "receipt.json"
        replay_path = root / "replay.json"
        rom.write_bytes(rom_bytes)
        executable.write_bytes(executable_bytes)
        source_inventory = "1" * 64
        receipt = {
            "rom": {"sha256": digest(rom_bytes)},
            "generated": {"source_inventory_sha256": source_inventory},
        }
        write(receipt_path, receipt)
        receipt_hash = digest(receipt_path.read_bytes())
        packages = [
            {
                "id": "example.mod",
                "version": "1.0.0",
                "order": 10,
                "manifest_sha256": "2" * 64,
                "content": [
                    {
                        "id": "encounters",
                        "target": "crystal.encounters.v1",
                        "sha256": "3" * 64,
                    }
                ],
            }
        ]
        package_hash = digest(MODULE.canonical(packages))
        artifact = bytearray(92)
        artifact[:8] = b"GBDMOD1\0"
        artifact[24:56] = bytes.fromhex(digest(rom_bytes))
        artifact[56:88] = bytes.fromhex(package_hash)
        input_bytes = b"[]"
        segments = [
            {
                "id": segment_id,
                "frame_limit": 2,
                "capture_frame": 1,
                "expected_frame_sha256": "4" * 64,
                "expected_state": {"a": index},
                "input_sha256": digest(input_bytes),
                "input_base64": base64.b64encode(input_bytes).decode("ascii"),
            }
            for index, segment_id in enumerate(
                ("new-game", "route29-wild-battle")
            )
        ]
        configuration = {
            "hardware_model": "auto",
            "headless": True,
            "no_audio": True,
            "rtc_unix_time": 1700000000,
            "ignore_rtc_persistence": True,
            "fallback_diagnostics": True,
        }
        replay = {
            "schema": "gbrecompiled.data-mod-replay",
            "version": 1,
            "game": {
                "id": "crystal-recompiled",
                "rom_sha256": digest(rom_bytes),
                "rom_size": len(rom_bytes),
            },
            "build": {
                "executable_sha256": digest(executable_bytes),
                "generation_receipt_sha256": receipt_hash,
                "source_inventory_sha256": source_inventory,
            },
            "mods": {
                "artifact_sha256": digest(artifact),
                "artifact_base64": base64.b64encode(artifact).decode("ascii"),
                "package_set_sha256": package_hash,
                "contract": {
                    "policy_sha256": "5" * 64,
                    "package_schema_sha256": "6" * 64,
                    "semantic_manifest_sha256": "7" * 64,
                    "semantic_schema_sha256": "8" * 64,
                },
                "load_order": ["example.mod"],
                "packages": packages,
            },
            "configuration": configuration,
            "configuration_sha256": digest(MODULE.canonical(configuration)),
            "seed": {"source_sha256": "9" * 64, "segments": segments},
        }
        replay["portable_seed_sha256"] = digest(
            MODULE.canonical(
                {
                    "game": replay["game"],
                    "mods": replay["mods"],
                    "configuration": configuration,
                    "segments": segments,
                }
            )
        )
        write(replay_path, replay)
        MODULE.preflight(replay_path, rom, executable, receipt_path)

        for mutate in ("load_order", "content", "configuration", "executable"):
            changed = copy.deepcopy(replay)
            if mutate == "load_order":
                changed["mods"]["load_order"] = []
            elif mutate == "content":
                changed["mods"]["packages"][0]["content"][0]["sha256"] = "0" * 64
            elif mutate == "configuration":
                changed["configuration"]["rtc_unix_time"] += 1
            else:
                changed["build"]["executable_sha256"] = "0" * 64
            write(replay_path, changed)
            expect_failure(replay_path, rom, executable, receipt_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
