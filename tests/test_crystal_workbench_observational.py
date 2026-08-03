#!/usr/bin/env python3

from __future__ import annotations

import copy
import json
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VERIFIER = (
    ROOT
    / "ports"
    / "pokemon-crystal"
    / "scripts"
    / "verify_workbench_observational.py"
)


def write_json(path: Path, payload: object) -> None:
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def route(*, capture_port_state: bool, port: dict | None) -> dict:
    segment = {
        "id": "boot",
        "input": "inputs/boot.json",
        "input_sha256": "1" * 64,
        "checkpoints": [
            {
                "id": "title",
                "frame": 1,
                "frame_sha256": "2" * 64,
                "passed": True,
            }
        ],
        "final_state": {"registers.pc": 4660},
        "state_sha256": "3" * 64,
        "pcm": {"bytes": 4, "seconds_limit": 1, "sha256": "4" * 64},
        "fallbacks": {"sites": [], "summary": {"fallbacks": 0}},
    }
    if port is not None:
        segment["port"] = port
    return {
        "schema": "gbrecompiled.pokemon-crystal.route-result",
        "version": 1,
        "passed": True,
        "capture_port_state": capture_port_state,
        "manifest_sha256": "5" * 64,
        "rtc_unix_time": 1700000000,
        "ignore_rtc_persistence": True,
        "fallback_policy": {"passed": True, "observed_sites": 0},
        "persistence": {
            "sav": {"bytes": 4, "sha256": "6" * 64},
            "rtc": {"bytes": 4, "sha256": "7" * 64},
        },
        "segments": [segment],
    }


def run_verifier(temp: Path, disabled: dict, closed: dict, interactive: dict):
    paths = {}
    for name, value in (
        ("disabled", disabled),
        ("closed", closed),
        ("interactive", interactive),
    ):
        paths[name] = temp / f"{name}.json"
        write_json(paths[name], value)
    return subprocess.run(
        [
            sys.executable,
            str(VERIFIER),
            "--disabled",
            str(paths["disabled"]),
            "--closed",
            str(paths["closed"]),
            "--interactive",
            str(paths["interactive"]),
            "--output",
            str(temp / "result.json"),
        ],
        text=True,
        capture_output=True,
        check=False,
    )


def main() -> int:
    disabled = route(capture_port_state=False, port=None)
    closed = route(
        capture_port_state=True,
        port={
            "module_id": "crystal-workbench",
            "input_events": 0,
            "semantic_events": [],
            "last_command_count": 0,
        },
    )
    events = [
        {"state": state}
        for state in ("shown", "hidden", "shown", "hidden", "shown")
    ]
    interactive = route(
        capture_port_state=True,
        port={
            "module_id": "crystal-workbench",
            "input_events": len(events),
            "semantic_events": events,
            "last_command_count": 11,
        },
    )

    with tempfile.TemporaryDirectory(
        prefix="crystal-workbench-observational-"
    ) as raw_temp:
        temp = Path(raw_temp)
        accepted = run_verifier(temp, disabled, closed, interactive)
        if accepted.returncode != 0:
            raise AssertionError(
                "verifier rejected equivalent guest evidence:\n"
                f"stdout:\n{accepted.stdout}\nstderr:\n{accepted.stderr}"
            )
        result = json.loads((temp / "result.json").read_text(encoding="utf-8"))
        assert result["passed"] is True
        assert result["semantic_events"] == 5
        assert result["guest_state_equal"] is True
        assert result["checkpoint_frames_equal"] is True
        assert result["pcm_equal"] is True
        assert result["save_equal"] is True
        assert result["rtc_equal"] is True
        assert result["fallback_equal"] is True

        divergent = copy.deepcopy(interactive)
        divergent["segments"][0]["state_sha256"] = "8" * 64
        rejected = run_verifier(temp, disabled, closed, divergent)
        if rejected.returncode == 0:
            raise AssertionError("verifier accepted a guest-state mismatch")
        assert "changed guest-visible results" in rejected.stdout

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
