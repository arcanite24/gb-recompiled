#!/usr/bin/env python3

from __future__ import annotations

import hashlib
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VALIDATOR = ROOT / "ports" / "pokemon-crystal" / "scripts" / "validate_route.py"
REQUIRED_CHECKPOINTS = (
    "title",
    "new_game",
    "overworld",
    "map_transition",
    "wild_battle",
    "trainer_battle",
    "start_menu",
    "pokedex",
    "pc",
    "save",
    "restart",
    "continue",
)


def write_json(path: Path, payload: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="crystal-route-validator-") as raw_temp:
        temp = Path(raw_temp)
        route = temp / "route"
        evidence = temp / "evidence"
        frame_bytes = b"P6\n1 1\n255\n\x12\x34\x56"
        frame_hash = hashlib.sha256(frame_bytes).hexdigest()
        rom_hash = "a" * 64

        write_json(
            route / "manifest.json",
            {
                "schema": "gbrecompiled.pokemon-crystal.route",
                "version": 1,
                "rom_sha256": rom_hash,
                "segments": [
                    {
                        "id": "boot",
                        "input": "inputs/boot.json",
                        "frame_limit": 2,
                        "checkpoints": [
                            {
                                "id": checkpoint,
                                "frame": 1,
                                "frame_sha256": frame_hash,
                            }
                            for checkpoint in REQUIRED_CHECKPOINTS
                        ],
                        "final_state": {
                            "registers.pc": 4660,
                            "wram.1": 2,
                        },
                    }
                ],
            },
        )
        write_json(
            route / "inputs" / "boot.json",
            [
                {"cycle": 0, "buttons": "A", "duration": 4},
                {
                    "start_cycle": 16,
                    "end_cycle": 48,
                    "step_cycles": 16,
                    "buttons": "B",
                    "duration": 4,
                },
                {
                    "start_cycle": 64,
                    "step_cycles": 8,
                    "count": 3,
                    "buttons_sequence": ["L", "R"],
                    "duration": 2,
                },
            ],
        )
        write_json(
            temp / "generation.json",
            {
                "schema": "crystal-recompiled.generation",
                "version": 1,
                "rom": {"sha256": rom_hash},
            },
        )
        write_json(
            route / "fallback-policy.json",
            {
                "schema": "gbrecompiled.pokemon-crystal.fallback-policy",
                "version": 1,
                "allowed_sites": [
                    {
                        "bank": 2,
                        "address": "0x41ca",
                        "reason": "bank_not_compiled",
                        "correctness": "universal_interpreter",
                        "rationale": "Synthetic retained-site fixture.",
                    }
                ],
            },
        )

        fake_executable = temp / "fake-crystal"
        fake_executable.write_text(
            """#!/usr/bin/env python3
import json
import sys
from pathlib import Path

args = sys.argv[1:]
def value(flag):
    return args[args.index(flag) + 1]

if args.count("--log-file") != 1:
    raise SystemExit(4)
if value("--input") != "c0:A:4,p16-48/16:B:4,c64:L:2,c72:R:2,c80:L:2":
    raise SystemExit(3)
prefix = Path(value("--screenshot-prefix"))
prefix.parent.mkdir(parents=True, exist_ok=True)
(prefix.parent / f"{prefix.name}_00001.ppm").write_bytes(b"P6\\n1 1\\n255\\n\\x12\\x34\\x56")
Path(value("--dump-state")).write_text(
    json.dumps({"registers": {"pc": 4660}, "wram": [1, 2, 3]}) + "\\n",
    encoding="utf-8",
)
save_dir = Path(value("--save-dir"))
save_dir.mkdir(parents=True, exist_ok=True)
(save_dir / "pokemon_crystal.sav").write_bytes(b"save")
(save_dir / "pokemon_crystal.rtc").write_bytes(b"rtc")
Path(value("--log-file")).write_text(
    "[INTERP] Fallback inventory: sites=1 dropped=0 complete=yes\\n"
    "[INTERP] Fallback site #1 002:41CA reason=bank_not_compiled "
    "entries=3 instructions=6 cycles=48 first_frame=1 last_frame=2 "
    "compiled_bank_variants=4\\n"
    "[INTERP] Summary: fallbacks=3 interpreter_entries=3 "
    "interpreter_instructions=6 interpreter_cycles=48\\n",
    encoding="utf-8",
)
if "--debug-audio" in args:
    if value("--debug-audio-seconds") != "1":
        raise SystemExit(5)
    Path("debug_audio.raw").write_bytes(b"deterministic-pcm")
""",
            encoding="utf-8",
        )
        fake_executable.chmod(fake_executable.stat().st_mode | 0o111)

        result = subprocess.run(
            [
                sys.executable,
                str(VALIDATOR),
                "--manifest",
                str(route / "manifest.json"),
                "--executable",
                str(fake_executable),
                "--generation-receipt",
                str(temp / "generation.json"),
                "--evidence-dir",
                str(evidence),
                "--pcm-seconds",
                "1",
            ],
            text=True,
            capture_output=True,
            check=False,
        )
        if result.returncode != 0:
            raise AssertionError(
                f"validator rejected a valid route:\nstdout:\n{result.stdout}\n"
                f"stderr:\n{result.stderr}"
            )
        report = json.loads((evidence / "result.json").read_text(encoding="utf-8"))
        assert report["passed"] is True
        assert report["segments"][0]["checkpoints"][0]["id"] == "title"
        assert report["segments"][0]["checkpoints"][0]["passed"] is True
        assert report["segments"][0]["pcm"] == {
            "bytes": 17,
            "seconds_limit": 1,
            "sha256": hashlib.sha256(b"deterministic-pcm").hexdigest(),
        }
        assert report["persistence"]["sav"]["sha256"] == hashlib.sha256(
            b"save"
        ).hexdigest()
        assert report["persistence"]["rtc"]["sha256"] == hashlib.sha256(
            b"rtc"
        ).hexdigest()

        fallback_evidence = temp / "fallback-evidence"
        fallback_result = subprocess.run(
            [
                sys.executable,
                str(VALIDATOR),
                "--manifest",
                str(route / "manifest.json"),
                "--executable",
                str(fake_executable),
                "--generation-receipt",
                str(temp / "generation.json"),
                "--evidence-dir",
                str(fallback_evidence),
                "--fallback-policy",
                str(route / "fallback-policy.json"),
            ],
            text=True,
            capture_output=True,
            check=False,
        )
        if fallback_result.returncode != 0:
            raise AssertionError(
                "validator rejected a matching fallback policy:\n"
                f"stdout:\n{fallback_result.stdout}\n"
                f"stderr:\n{fallback_result.stderr}"
            )
        fallback_report = json.loads(
            (fallback_evidence / "result.json").read_text(encoding="utf-8")
        )
        assert fallback_report["fallback_policy"]["passed"] is True
        assert fallback_report["fallback_policy"]["observed_sites"] == 1
        assert fallback_report["fallback_policy"]["total_entries"] == 3
        assert fallback_report["segments"][0]["fallbacks"]["sites"][0] == {
            "address": "0x41ca",
            "bank": 2,
            "compiled_bank_variants": 4,
            "cycles": 48,
            "entries": 3,
            "first_frame": 1,
            "instructions": 6,
            "last_frame": 2,
            "reason": "bank_not_compiled",
        }

        write_json(
            route / "empty-fallback-policy.json",
            {
                "schema": "gbrecompiled.pokemon-crystal.fallback-policy",
                "version": 1,
                "allowed_sites": [],
            },
        )
        unexplained_result = subprocess.run(
            [
                sys.executable,
                str(VALIDATOR),
                "--manifest",
                str(route / "manifest.json"),
                "--executable",
                str(fake_executable),
                "--generation-receipt",
                str(temp / "generation.json"),
                "--evidence-dir",
                str(temp / "unexplained-evidence"),
                "--fallback-policy",
                str(route / "empty-fallback-policy.json"),
            ],
            text=True,
            capture_output=True,
            check=False,
        )
        if (
            unexplained_result.returncode == 0
            or "unexplained fallback site" not in unexplained_result.stderr
        ):
            raise AssertionError("validator accepted an unexplained fallback site")

        incomplete = json.loads(
            (route / "manifest.json").read_text(encoding="utf-8")
        )
        incomplete["segments"][0]["checkpoints"] = [
            incomplete["segments"][0]["checkpoints"][0]
        ]
        write_json(route / "incomplete.json", incomplete)
        rejected = subprocess.run(
            [
                sys.executable,
                str(VALIDATOR),
                "--manifest",
                str(route / "incomplete.json"),
                "--executable",
                str(fake_executable),
                "--generation-receipt",
                str(temp / "generation.json"),
                "--evidence-dir",
                str(temp / "incomplete-evidence"),
            ],
            text=True,
            capture_output=True,
            check=False,
        )
        if rejected.returncode == 0:
            raise AssertionError("validator accepted a route missing required checkpoints")

        out_of_range = json.loads(
            (route / "manifest.json").read_text(encoding="utf-8")
        )
        out_of_range["segments"][0]["checkpoints"][-1]["frame"] = 3
        write_json(route / "out-of-range.json", out_of_range)
        out_of_range_result = subprocess.run(
            [
                sys.executable,
                str(VALIDATOR),
                "--manifest",
                str(route / "out-of-range.json"),
                "--executable",
                str(fake_executable),
                "--generation-receipt",
                str(temp / "generation.json"),
                "--evidence-dir",
                str(temp / "out-of-range-evidence"),
            ],
            text=True,
            capture_output=True,
            check=False,
        )
        if (
            out_of_range_result.returncode == 0
            or "exceeds frame_limit" not in out_of_range_result.stderr
        ):
            raise AssertionError(
                "validator did not reject an out-of-range checkpoint during preflight"
            )

        unsafe_segment = json.loads(
            (route / "manifest.json").read_text(encoding="utf-8")
        )
        unsafe_segment["segments"][0]["id"] = "../escaped"
        write_json(route / "unsafe-segment.json", unsafe_segment)
        unsafe_segment_result = subprocess.run(
            [
                sys.executable,
                str(VALIDATOR),
                "--manifest",
                str(route / "unsafe-segment.json"),
                "--executable",
                str(fake_executable),
                "--generation-receipt",
                str(temp / "generation.json"),
                "--evidence-dir",
                str(temp / "unsafe-segment-evidence"),
            ],
            text=True,
            capture_output=True,
            check=False,
        )
        if (
            unsafe_segment_result.returncode == 0
            or "unsafe id" not in unsafe_segment_result.stderr
        ):
            raise AssertionError("validator did not reject an unsafe segment id")

        stale_evidence = temp / "stale-evidence"
        stale_evidence.mkdir()
        (stale_evidence / "stale.txt").write_text("old run\n", encoding="utf-8")
        stale_result = subprocess.run(
            [
                sys.executable,
                str(VALIDATOR),
                "--manifest",
                str(route / "manifest.json"),
                "--executable",
                str(fake_executable),
                "--generation-receipt",
                str(temp / "generation.json"),
                "--evidence-dir",
                str(stale_evidence),
            ],
            text=True,
            capture_output=True,
            check=False,
        )
        if stale_result.returncode == 0:
            raise AssertionError("validator accepted a non-empty evidence directory")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
