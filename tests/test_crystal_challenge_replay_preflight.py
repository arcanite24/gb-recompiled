#!/usr/bin/env python3
"""Synthetic v2 Challenge replay identity and no-guest preflight proof."""

from __future__ import annotations

import copy
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VALIDATOR = ROOT / "ports" / "pokemon-crystal" / "scripts" / "validate_route.py"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def canonical_sha256(value: object) -> str:
    return hashlib.sha256(
        json.dumps(value, sort_keys=True, separators=(",", ":")).encode("utf-8")
    ).hexdigest()


def write_json(path: Path, value: object, *, compact: bool = False) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if compact:
        text = json.dumps(value, separators=(",", ":")) + "\n"
    else:
        text = json.dumps(value, indent=2, sort_keys=True) + "\n"
    path.write_text(text, encoding="utf-8")


def run_validator(
    manifest: Path,
    executable: Path,
    receipt: Path,
    patch: Path,
    configuration: Path,
    evidence: Path,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            sys.executable,
            str(VALIDATOR),
            "--manifest",
            str(manifest),
            "--executable",
            str(executable),
            "--generation-receipt",
            str(receipt),
            "--native-patch-manifest",
            str(patch),
            "--host-configuration",
            str(configuration),
            "--evidence-dir",
            str(evidence),
        ],
        text=True,
        capture_output=True,
        check=False,
    )


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="challenge-replay-") as raw:
        root = Path(raw)
        route = root / "route"
        inputs = route / "inputs"
        executable = root / "crystal"
        receipt = root / "generation.json"
        patch = root / "challenge-manifest.json"
        configuration = root / "challenge.json"
        launched = root / "guest-launched"
        frame_bytes = b"P6\n1 1\n255\n\x12\x34\x56"
        frame_hash = hashlib.sha256(frame_bytes).hexdigest()
        rom_hash = "a" * 64

        input_payload = [{"cycle": 0, "buttons": "A", "duration": 4}]
        write_json(inputs / "new-game.json", input_payload)
        write_json(inputs / "adventure.json", input_payload)
        adventure_overlay = inputs / "adventure-tail.input"
        adventure_overlay.write_text("p30-40/10:B:4\n", encoding="utf-8")

        patch_value = {
            "schema": "gbrecomp.native-patch",
            "version": 1,
            "patch_id": "org.example.challenge",
            "rom": {"sha256": rom_hash, "size": 13},
            "host_configuration": {
                "schema": "gbrecomp.host-configuration",
                "version": 1,
                "policy_id": "challenge-v1",
                "offset_limit": 5,
                "value_minimum": 1,
                "value_maximum": 100,
            },
            "sources": ["challenge.c"],
            "bindings": [],
        }
        write_json(patch, patch_value)
        configuration_value = {
            "schema": "gbrecomp.host-configuration",
            "version": 1,
            "policy_id": "challenge-v1",
            "applied": True,
            "enabled": True,
            "offset": 3,
            "minimum": 1,
            "maximum": 100,
        }
        write_json(configuration, configuration_value, compact=True)

        executable.write_text(
            f"""#!/usr/bin/env python3
import json
import sys
from pathlib import Path

args = sys.argv[1:]
def value(flag):
    return args[args.index(flag) + 1]

marker = Path({str(launched)!r})
expected_input = "c0:A:4" if not marker.exists() else "c15:A:4,p30-40/10:B:4"
if value("--input") != expected_input:
    raise SystemExit(8)
marker.write_text("yes", encoding="utf-8")
if Path(value("--host-configuration")).read_bytes() != Path({str(configuration)!r}).read_bytes():
    raise SystemExit(7)
prefix = Path(value("--screenshot-prefix"))
for frame in value("--dump-frames").split(","):
    (prefix.parent / f"{{prefix.name}}_{{int(frame):05d}}.ppm").write_bytes(
        b"P6\\n1 1\\n255\\n\\x12\\x34\\x56"
    )
Path(value("--dump-state")).write_text(json.dumps({{
    "registers": {{"pc": 4660}},
    "host_configuration": {{
        "present": True,
        "applied": True,
        "enabled": True,
        "policy_id": "challenge-v1",
        "sha256": {sha256(configuration)!r},
    }},
}}) + "\\n", encoding="utf-8")
save_dir = Path(value("--save-dir"))
save_dir.mkdir(parents=True, exist_ok=True)
(save_dir / "pokemon_crystal.sav").write_bytes(b"save")
(save_dir / "pokemon_crystal.rtc").write_bytes(b"rtc")
Path(value("--log-file")).write_text("", encoding="utf-8")
""",
            encoding="utf-8",
        )
        executable.chmod(0o755)
        receipt_value = {
            "schema": "crystal-recompiled.generation",
            "version": 1,
            "rom": {"sha256": rom_hash},
            "generated": {"source_inventory_sha256": "b" * 64},
            "native_patch": {
                "kind": "file",
                "name": patch.name,
                "sha256": sha256(patch),
            },
        }
        write_json(receipt, receipt_value)

        checkpoints = (
            ("title", "new-game"),
            ("new_game", "new-game"),
            ("overworld", "new-game"),
            ("map_transition", "new-game"),
            ("wild_battle", "adventure"),
            ("trainer_battle", "adventure"),
        )
        segments = []
        for segment_id in ("new-game", "adventure"):
            input_path = inputs / f"{segment_id}.json"
            segment = {
                    "id": segment_id,
                    "input": f"inputs/{segment_id}.json",
                    "input_sha256": sha256(input_path),
                    "frame_limit": 2,
                    "checkpoints": [
                        {"id": name, "frame": 1, "frame_sha256": frame_hash}
                        for name, owner in checkpoints
                        if owner == segment_id
                    ],
                    "final_state": {"registers.pc": 4660},
                }
            if segment_id == "adventure":
                segment["input_prefix_actions"] = 1
                segment["input_cycle_shift"] = {
                    "start_index": 0,
                    "delta": 15,
                }
                segment["input_overlay"] = "inputs/adventure-tail.input"
                segment["input_overlay_sha256"] = sha256(adventure_overlay)
            segments.append(segment)
        manifest_value = {
            "schema": "gbrecompiled.pokemon-crystal.route",
            "version": 2,
            "profile": "challenge-wild-trainer-v1",
            "rom_sha256": rom_hash,
            "rtc_unix_time": 1700000000,
            "ignore_rtc_persistence": True,
            "build": {
                "executable_sha256": sha256(executable),
                "generation_receipt_sha256": sha256(receipt),
                "source_inventory_sha256": "b" * 64,
            },
            "native_patch": {
                "kind": "file",
                "patch_id": "org.example.challenge",
                "manifest_sha256": sha256(patch),
            },
            "host_configuration": {
                "schema": "gbrecomp.host-configuration",
                "version": 1,
                "policy_id": "challenge-v1",
                "sha256": sha256(configuration),
            },
            "mods": {"kind": "none"},
            "segments": segments,
        }
        manifest_value["portable_seed_sha256"] = canonical_sha256(manifest_value)
        manifest = route / "challenge.json"
        write_json(manifest, manifest_value)

        evidence = root / "evidence"
        completed = run_validator(
            manifest, executable, receipt, patch, configuration, evidence
        )
        if completed.returncode != 0:
            raise AssertionError(
                f"valid v2 replay failed:\n{completed.stdout}\n{completed.stderr}"
            )
        report = json.loads((evidence / "result.json").read_text(encoding="utf-8"))
        assert report["replay_preflight"] == {
            "portable_seed_sha256": manifest_value["portable_seed_sha256"],
            "build": manifest_value["build"],
            "native_patch": manifest_value["native_patch"],
            "host_configuration": manifest_value["host_configuration"],
            "mods": {"kind": "none"},
        }
        assert report["segments"][0]["command"].count("--host-configuration") == 1

        mutations = {
            "portable_seed": lambda value: value.__setitem__(
                "portable_seed_sha256", "0" * 64
            ),
            "executable": lambda value: value["build"].__setitem__(
                "executable_sha256", "0" * 64
            ),
            "ruleset": lambda value: value["native_patch"].__setitem__(
                "manifest_sha256", "0" * 64
            ),
            "configuration": lambda value: value["host_configuration"].__setitem__(
                "sha256", "0" * 64
            ),
            "mods": lambda value: value["mods"].__setitem__("kind", "changed"),
            "input": lambda value: value["segments"][0].__setitem__(
                "input_sha256", "0" * 64
            ),
            "input_overlay": lambda value: value["segments"][1].__setitem__(
                "input_overlay_sha256", "0" * 64
            ),
        }
        for name, mutate in mutations.items():
            changed = copy.deepcopy(manifest_value)
            mutate(changed)
            changed_path = route / f"mismatch-{name}.json"
            write_json(changed_path, changed)
            forbidden = root / f"forbidden-{name}"
            launched.unlink(missing_ok=True)
            rejected = run_validator(
                changed_path,
                executable,
                receipt,
                patch,
                configuration,
                forbidden,
            )
            if rejected.returncode == 0 or forbidden.exists() or launched.exists():
                raise AssertionError(f"{name} mismatch reached guest execution")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
