#!/usr/bin/env python3
"""Fail-closed controls for the Crystal BillsPC route comparator."""

from __future__ import annotations

import copy
import json
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(
    0,
    str(Path(__file__).resolve().parents[1] / "ports/pokemon-crystal/scripts"),
)
from verify_writable_saves import BACKUP, PRIMARY  # noqa: E402


def segment(segment_id: str, frame_hash: str) -> dict:
    return {
        "id": segment_id,
        "checkpoints": [
            {"id": segment_id, "frame_sha256": frame_hash, "passed": True}
        ],
        "final_state": [
            {"path": "completed_frames", "actual": 1, "passed": True},
            {"path": "total_cycles", "actual": 2, "passed": True},
            {"path": "wram.0", "actual": 3, "passed": True},
        ],
        "port": {
            "module_version": 8,
            "input_events": 0,
            "semantic_events": [],
            "last_command_count": 0,
        },
    }


def route(presentation: str) -> dict:
    result = {
        "schema": "gbrecompiled.pokemon-crystal.route-result",
        "passed": True,
        "runtime_args": ["--native-presentation", presentation],
        "executable_sha256": "1" * 64,
        "generation_receipt_sha256": "2" * 64,
        "fallback_policy": {"passed": True, "observed_sites": 0},
        "persistence": {
            "sav": {"bytes": 32768, "sha256": "3" * 64},
            "rtc": {"bytes": 40, "sha256": "4" * 64},
        },
        "segments": [
            segment("adventure", "4" * 64),
            segment("pc-save", "5" * 64),
            segment("restart-continue", "6" * 64),
        ],
    }
    if presentation == "native":
        event = {
            "module": "crystal-workbench",
            "surface": "native-pc",
            "state": "shown",
        }
        for route_segment in result["segments"]:
            if route_segment["id"] in {"adventure", "pc-save"}:
                route_segment["port"] = {
                    "module_version": 8,
                    "input_events": 2,
                    "semantic_events": [event, event],
                    "last_command_count": 12,
                }
    return result


def valid_save() -> bytes:
    data = bytearray(32768)
    for layout in (PRIMARY, BACKUP):
        data[layout["check1"]] = 99
        data[layout["check2"]] = 127
        total = sum(data[layout["start"] : layout["end"]]) & 0xFFFF
        data[layout["checksum"] : layout["checksum"] + 2] = total.to_bytes(2, "little")
    return bytes(data)


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    verifier = root / "ports/pokemon-crystal/scripts/verify_native_bills_pc.py"
    with tempfile.TemporaryDirectory() as raw:
        directory = Path(raw)
        original_path = directory / "original.json"
        native_path = directory / "native.json"
        save_path = directory / "route.sav"
        metadata_path = directory / "metadata.json"
        patch_path = directory / "manifest.json"
        output_path = directory / "result.json"
        original = route("original")
        native = route("native")
        save_path.write_bytes(valid_save())
        metadata_path.write_text(
            json.dumps(
                {
                    "functions": [
                        {
                            "id": "gbfn:v1:0005:5668",
                            "bank": 5,
                            "address": "0x5668",
                            "patchable": True,
                            "emitted_name": "sym_BillsPC",
                            "source_symbol": "BillsPC",
                        }
                    ]
                }
            ),
            encoding="utf-8",
        )
        patch_path.write_text(
            json.dumps(
                {
                    "patch_id": "org.gbrecompiled.crystal.bills-pc",
                    "bindings": [
                        {
                            "function": "gbfn:v1:0005:5668",
                            "replace": "crystal_native_bills_pc",
                            "entry_contract": "return-stack",
                        }
                    ],
                }
            ),
            encoding="utf-8",
        )

        def run(native_value: dict, native_save: Path = save_path) -> int:
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
                    "--original-save",
                    str(save_path),
                    "--native-save",
                    str(native_save),
                    "--metadata",
                    str(metadata_path),
                    "--patch-manifest",
                    str(patch_path),
                    "--output",
                    str(output_path),
                ],
                capture_output=True,
                check=False,
            ).returncode

        if run(native) != 0:
            raise AssertionError("verifier rejected matching BillsPC evidence")
        bad_event = copy.deepcopy(native)
        bad_event["segments"][0]["port"]["semantic_events"] = []
        if run(bad_event) == 0:
            raise AssertionError("verifier accepted a missing BillsPC event")
        bad_restart = copy.deepcopy(native)
        bad_restart["segments"][2]["final_state"][2]["actual"] = 4
        if run(bad_restart) == 0:
            raise AssertionError("verifier accepted restart state drift")
        bad_save = directory / "bad.sav"
        bad_save.write_bytes(save_path.read_bytes()[:-1] + b"\x01")
        if run(native, bad_save) == 0:
            raise AssertionError("verifier accepted changed persistence")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
