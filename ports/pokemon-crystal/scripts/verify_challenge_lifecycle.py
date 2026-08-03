#!/usr/bin/env python3
"""Verify Challenge Mode save, removal, restart, and reinstall equivalence."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any

from validate_route import cycle_input, parse_fallback_log


class VerificationError(RuntimeError):
    pass


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def canonical_sha256(value: object) -> str:
    return hashlib.sha256(
        json.dumps(value, sort_keys=True, separators=(",", ":")).encode("utf-8")
    ).hexdigest()


def load(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise VerificationError(f"cannot read {path.name}: {error}") from error
    if not isinstance(value, dict):
        raise VerificationError(f"JSON root must be an object: {path.name}")
    return value


def receipt(path: Path, expected_patch_kind: str) -> dict[str, Any]:
    value = load(path)
    generated = value.get("generated")
    native_patch = value.get("native_patch")
    if (
        value.get("schema") != "crystal-recompiled.generation"
        or value.get("version") != 1
        or not isinstance(value.get("rom"), dict)
        or not isinstance(value["rom"].get("sha256"), str)
        or len(value["rom"]["sha256"]) != 64
        or not isinstance(generated, dict)
        or not isinstance(generated.get("source_inventory_sha256"), str)
        or not isinstance(native_patch, dict)
        or native_patch.get("kind") != expected_patch_kind
    ):
        raise VerificationError(f"generation receipt has wrong identity: {path.name}")
    return value


def expected_host_identity(configuration: Path | None) -> dict[str, object]:
    if configuration is None:
        return {
            "present": False,
            "applied": False,
            "enabled": False,
            "policy_id": "",
            "sha256": "",
        }
    return {
        "present": True,
        "applied": True,
        "enabled": True,
        "policy_id": "challenge-v1",
        "sha256": sha256(configuration),
    }


def run_mode(
    name: str,
    executable: Path,
    baseline_save: Path,
    input_script: str,
    output: Path,
    configuration: Path | None,
) -> dict[str, Any]:
    mode = output / name
    persistence = mode / "persistence"
    persistence.mkdir(parents=True)
    save = persistence / "pokemon_crystal.sav"
    shutil.copy2(baseline_save, save)
    state_path = mode / "state.json"
    log_path = mode / "runtime.log"
    frame_prefix = mode / "frame"
    command = [
        str(executable),
        "--headless",
        "--no-audio",
        "--limit-frames",
        "3500",
        "--input",
        input_script,
        "--dump-frames",
        "1000",
        "--screenshot-prefix",
        str(frame_prefix),
        "--dump-state",
        str(state_path),
        "--save-dir",
        str(persistence),
        "--log-file",
        str(log_path),
        "--rtc-unix-time",
        "1700000000",
        "--ignore-rtc-persistence",
        "--log-frame-fallbacks",
        "--report-interpreter-hotspots",
        "--interpreter-hotspot-limit",
        "16",
    ]
    if configuration is not None:
        command.extend(["--host-configuration", str(configuration)])
    completed = subprocess.run(command, text=True, capture_output=True, check=False)
    (mode / "launcher.stdout").write_text(completed.stdout, encoding="utf-8")
    (mode / "launcher.stderr").write_text(completed.stderr, encoding="utf-8")
    if completed.returncode != 0:
        raise VerificationError(f"{name} Continue route exited nonzero")
    if not state_path.is_file() or not (mode / "frame_01000.ppm").is_file():
        raise VerificationError(f"{name} omitted state or frame evidence")
    if sha256(save) != sha256(baseline_save):
        raise VerificationError(f"{name} changed the guest save")
    fallbacks = parse_fallback_log(log_path)
    if fallbacks["sites"] or fallbacks["summary"]["fallbacks"] != 0:
        raise VerificationError(f"{name} used interpreter fallback")
    state = load(state_path)
    if state.get("host_configuration") != expected_host_identity(configuration):
        raise VerificationError(f"{name} exposed the wrong host configuration")
    comparable = dict(state)
    comparable.pop("host_configuration", None)
    return {
        "executable_sha256": sha256(executable),
        "configuration_sha256": sha256(configuration) if configuration else None,
        "save_before_sha256": sha256(baseline_save),
        "save_after_sha256": sha256(save),
        "save_bytes": save.stat().st_size,
        "frame_1000_sha256": sha256(mode / "frame_01000.ppm"),
        "state_sha256": sha256(state_path),
        "comparable_state_sha256": canonical_sha256(comparable),
        "host_configuration": state["host_configuration"],
        "fallbacks": fallbacks,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--vanilla-executable", required=True, type=Path)
    parser.add_argument("--challenge-executable", required=True, type=Path)
    parser.add_argument("--vanilla-receipt", required=True, type=Path)
    parser.add_argument("--challenge-receipt", required=True, type=Path)
    parser.add_argument("--baseline-save", required=True, type=Path)
    parser.add_argument("--enabled-configuration", required=True, type=Path)
    parser.add_argument("--restart-input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    output = args.output.resolve()
    if output.exists():
        if not output.is_dir() or any(output.iterdir()):
            raise VerificationError("output must be absent or empty")
    else:
        output.mkdir(parents=True)
    vanilla_executable = args.vanilla_executable.resolve()
    challenge_executable = args.challenge_executable.resolve()
    baseline_save = args.baseline_save.resolve()
    configuration = args.enabled_configuration.resolve()
    restart_input = args.restart_input.resolve()
    if (
        not vanilla_executable.is_file()
        or not challenge_executable.is_file()
        or not baseline_save.is_file()
        or baseline_save.stat().st_size != 32768
        or not configuration.is_file()
        or not restart_input.is_file()
    ):
        raise VerificationError("lifecycle verifier input is missing or malformed")
    vanilla_receipt_path = args.vanilla_receipt.resolve()
    challenge_receipt_path = args.challenge_receipt.resolve()
    vanilla_receipt = receipt(vanilla_receipt_path, "none")
    challenge_receipt = receipt(challenge_receipt_path, "file")
    if vanilla_receipt["rom"] != challenge_receipt["rom"]:
        raise VerificationError("vanilla and Challenge receipts target different ROMs")
    configuration_value = load(configuration)
    if (
        configuration_value.get("schema") != "gbrecomp.host-configuration"
        or configuration_value.get("version") != 1
        or configuration_value.get("policy_id") != "challenge-v1"
        or configuration_value.get("applied") is not True
        or configuration_value.get("enabled") is not True
    ):
        raise VerificationError("enabled configuration has wrong identity")
    input_script = cycle_input(restart_input)

    modes: dict[str, dict[str, Any]] = {}
    for name, executable, active_configuration in (
        ("vanilla", vanilla_executable, None),
        ("disabled", challenge_executable, None),
        ("enabled_first", challenge_executable, configuration),
        ("enabled_restart", challenge_executable, configuration),
        ("removed", challenge_executable, None),
        ("reinstalled", challenge_executable, configuration),
    ):
        modes[name] = run_mode(
            name,
            executable,
            baseline_save,
            input_script,
            output,
            active_configuration,
        )

    comparable_states = {value["comparable_state_sha256"] for value in modes.values()}
    frames = {value["frame_1000_sha256"] for value in modes.values()}
    saves = {value["save_after_sha256"] for value in modes.values()}
    if len(comparable_states) != 1 or len(frames) != 1 or saves != {sha256(baseline_save)}:
        raise VerificationError("Challenge lifecycle diverged from vanilla Continue")

    malformed = output / "malformed"
    malformed_persistence = malformed / "persistence"
    malformed_persistence.mkdir(parents=True)
    malformed_save = malformed_persistence / "pokemon_crystal.sav"
    shutil.copy2(baseline_save, malformed_save)
    malformed_configuration = malformed / "configuration.json"
    malformed_configuration.write_text(
        configuration.read_text(encoding="utf-8").replace(
            "gbrecomp.host-configuration", "wrong.host-configuration", 1
        ),
        encoding="utf-8",
    )
    malformed_state = malformed / "state.json"
    malformed_prefix = malformed / "frame"
    malformed_log = malformed / "runtime.log"
    malformed_command = [
        str(challenge_executable),
        "--headless",
        "--no-audio",
        "--limit-frames",
        "1",
        "--input",
        input_script,
        "--dump-frames",
        "1",
        "--screenshot-prefix",
        str(malformed_prefix),
        "--dump-state",
        str(malformed_state),
        "--save-dir",
        str(malformed_persistence),
        "--log-file",
        str(malformed_log),
        "--host-configuration",
        str(malformed_configuration),
    ]
    rejected = subprocess.run(
        malformed_command, text=True, capture_output=True, check=False
    )
    (malformed / "launcher.stdout").write_text(rejected.stdout, encoding="utf-8")
    (malformed / "launcher.stderr").write_text(rejected.stderr, encoding="utf-8")
    malformed_frame = malformed / "frame_00001.ppm"
    rejected_before_guest = (
        rejected.returncode != 0
        and not malformed_state.exists()
        and not malformed_frame.exists()
        and sha256(malformed_save) == sha256(baseline_save)
    )
    if not rejected_before_guest:
        raise VerificationError("malformed configuration reached guest execution")

    result = {
        "schema": "crystal-recompiled.challenge-lifecycle-proof",
        "version": 1,
        "passed": True,
        "rom": vanilla_receipt["rom"],
        "builds": {
            "vanilla": {
                "executable_sha256": sha256(vanilla_executable),
                "generation_receipt_sha256": sha256(vanilla_receipt_path),
                "source_inventory_sha256": vanilla_receipt["generated"][
                    "source_inventory_sha256"
                ],
            },
            "challenge": {
                "executable_sha256": sha256(challenge_executable),
                "generation_receipt_sha256": sha256(challenge_receipt_path),
                "source_inventory_sha256": challenge_receipt["generated"][
                    "source_inventory_sha256"
                ],
                "native_patch": challenge_receipt["native_patch"],
            },
        },
        "baseline_save_sha256": sha256(baseline_save),
        "restart_input_sha256": sha256(restart_input),
        "enabled_configuration_sha256": sha256(configuration),
        "lifecycle_order": [
            "vanilla",
            "disabled",
            "enabled_first",
            "enabled_restart",
            "removed",
            "reinstalled",
        ],
        "modes": modes,
        "malformed_configuration": {
            "rejected_before_guest": True,
            "save_after_sha256": sha256(malformed_save),
        },
        "equivalence": {
            "comparable_state_sha256": next(iter(comparable_states)),
            "frame_1000_sha256": next(iter(frames)),
            "save_sha256": next(iter(saves)),
        },
    }
    result_path = output / "result.json"
    result_path.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    private_paths = {
        str(path.parent)
        for path in (
            baseline_save,
            configuration,
            restart_input,
            vanilla_receipt_path,
            challenge_receipt_path,
        )
    }
    for artifact in output.rglob("*"):
        if not artifact.is_file() or artifact.suffix == ".ppm":
            continue
        text = artifact.read_text(encoding="utf-8", errors="replace")
        if any(private in text for private in private_paths):
            raise VerificationError(f"private path leaked into {artifact.name}")
    print("ok  Challenge save lifecycle")
    print(f"    baseline_save_sha256={sha256(baseline_save)}")
    print(f"    comparable_state_sha256={next(iter(comparable_states))}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except VerificationError as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
