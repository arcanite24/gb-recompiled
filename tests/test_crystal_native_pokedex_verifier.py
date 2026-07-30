#!/usr/bin/env python3
"""Fail-closed controls for the Crystal native-Pokédex route comparator."""

from __future__ import annotations

import copy
import json
import subprocess
import sys
import tempfile
from pathlib import Path


EVENTS = [
    {"module": "crystal-workbench", "state": "shown", "surface": "native-pokedex"},
    {"module": "crystal-workbench", "species": 156, "surface": "native-pokedex"},
    {"module": "crystal-workbench", "species": 157, "surface": "native-pokedex"},
    {"module": "crystal-workbench", "species": 156, "surface": "native-pokedex"},
    {"module": "crystal-workbench", "state": "hidden", "surface": "native-pokedex"},
]


def segment(segment_id: str, checkpoint_id: str, frame_hash: str) -> dict:
    return {
        "id": segment_id,
        "checkpoints": [
            {"id": checkpoint_id, "frame": 1, "frame_sha256": frame_hash, "passed": True}
        ],
        "final_state": [
            {"path": "completed_frames", "expected": 1, "actual": 1, "passed": True},
            {"path": "total_cycles", "expected": 2, "actual": 2, "passed": True},
            {"path": "wram.0", "expected": 3, "actual": 3, "passed": True},
        ],
        "port": {
            "input_events": 0,
            "semantic_events": [],
            "last_command_count": 0,
        },
    }


def route(presentation: str) -> dict:
    value = {
        "schema": "gbrecompiled.pokemon-crystal.route-result",
        "version": 1,
        "passed": True,
        "runtime_args": ["--native-presentation", presentation],
        "executable_sha256": "1" * 64,
        "generation_receipt_sha256": "2" * 64,
        "fallback_policy": {"passed": True, "observed_sites": 0},
        "persistence": {
            "sav": {"bytes": 1, "sha256": "3" * 64},
            "rtc": {"bytes": 1, "sha256": "4" * 64},
        },
        "segments": [
            segment("adventure", "pokedex", "5" * 64),
            segment("pc-save", "save", "6" * 64),
        ],
    }
    if presentation == "native":
        value["segments"][0]["port"] = {
            "input_events": 5,
            "semantic_events": EVENTS,
            "last_command_count": 0,
        }
    return value


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    verifier = root / "ports/pokemon-crystal/scripts/verify_native_pokedex.py"
    with tempfile.TemporaryDirectory() as raw:
        directory = Path(raw)
        original_path = directory / "original.json"
        native_path = directory / "native.json"
        output_path = directory / "verification.json"
        original = route("original")
        native = route("native")

        def run(native_value: dict) -> int:
            original_path.write_text(json.dumps(original), encoding="utf-8")
            native_path.write_text(json.dumps(native_value), encoding="utf-8")
            return subprocess.run(
                [
                    sys.executable,
                    str(verifier),
                    "--original",
                    str(original_path),
                    "--native",
                    str(native_path),
                    "--output",
                    str(output_path),
                ],
                capture_output=True,
                check=False,
            ).returncode

        if run(native) != 0:
            raise AssertionError("verifier rejected matching native route evidence")
        bad_save = copy.deepcopy(native)
        bad_save["persistence"]["sav"]["sha256"] = "9" * 64
        if run(bad_save) == 0:
            raise AssertionError("verifier accepted a changed save")
        bad_event = copy.deepcopy(native)
        bad_event["segments"][0]["port"]["semantic_events"][2]["species"] = 200
        if run(bad_event) == 0:
            raise AssertionError("verifier accepted changed browsing evidence")
        bad_state = copy.deepcopy(native)
        bad_state["segments"][1]["final_state"][2]["actual"] = 4
        if run(bad_state) == 0:
            raise AssertionError("verifier accepted changed semantic state")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
