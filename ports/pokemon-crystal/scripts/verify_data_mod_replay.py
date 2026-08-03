#!/usr/bin/env python3
"""Preflight and reproduce a portable Crystal data-mod replay."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import re
import subprocess
from pathlib import Path
from typing import Any

from validate_route import cycle_input, parse_fallback_log


SHA256_RE = re.compile(r"[0-9a-f]{64}")


class ReplayError(RuntimeError):
    pass


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def canonical(value: object) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":")).encode("utf-8")


def load(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ReplayError(f"cannot read JSON {path}: {error}") from error
    if not isinstance(value, dict):
        raise ReplayError(f"JSON root must be an object: {path}")
    return value


def exact(value: object, keys: set[str], label: str) -> dict[str, Any]:
    if not isinstance(value, dict) or set(value) != keys:
        raise ReplayError(f"{label} fields are missing or unknown")
    return value


def decode_base64(value: object, label: str) -> bytes:
    if not isinstance(value, str):
        raise ReplayError(f"{label} is not base64 text")
    try:
        return base64.b64decode(value, validate=True)
    except (ValueError, base64.binascii.Error) as error:
        raise ReplayError(f"{label} is malformed base64") from error


def package_set_digest(packages: object) -> str:
    if not isinstance(packages, list) or not packages:
        raise ReplayError("replay has no ordered packages")
    for package in packages:
        exact(
            package,
            {"id", "version", "order", "manifest_sha256", "content"},
            "replay package",
        )
        content = package["content"]
        if not isinstance(content, list) or not content:
            raise ReplayError("replay package has no content hashes")
        for item in content:
            exact(item, {"id", "target", "sha256"}, "replay content")
            if SHA256_RE.fullmatch(item["sha256"] or "") is None:
                raise ReplayError("replay content hash is invalid")
        if SHA256_RE.fullmatch(package["manifest_sha256"] or "") is None:
            raise ReplayError("replay package manifest hash is invalid")
    if (
        packages != sorted(packages, key=lambda item: (item["order"], item["id"]))
        or len({item["id"] for item in packages}) != len(packages)
    ):
        raise ReplayError("replay package order/identity mismatch")
    return sha256_bytes(canonical(packages))


def preflight(
    manifest_path: Path,
    rom_path: Path,
    executable_path: Path,
    receipt_path: Path,
) -> tuple[dict[str, Any], bytes]:
    replay = load(manifest_path)
    exact(
        replay,
        {
            "schema",
            "version",
            "game",
            "build",
            "mods",
            "configuration",
            "configuration_sha256",
            "seed",
            "portable_seed_sha256",
        },
        "replay",
    )
    if replay["schema"] != "gbrecompiled.data-mod-replay" or replay["version"] != 1:
        raise ReplayError("unsupported replay manifest")
    game = exact(replay["game"], {"id", "rom_sha256", "rom_size"}, "replay game")
    if (
        game["id"] != "crystal-recompiled"
        or SHA256_RE.fullmatch(game["rom_sha256"] or "") is None
        or not isinstance(game["rom_size"], int)
    ):
        raise ReplayError("unsupported replay game identity")
    rom = rom_path.read_bytes()
    if len(rom) != game["rom_size"] or sha256_bytes(rom) != game["rom_sha256"]:
        raise ReplayError("user ROM does not match replay")

    build = exact(
        replay["build"],
        {
            "executable_sha256",
            "generation_receipt_sha256",
            "source_inventory_sha256",
        },
        "replay build",
    )
    if any(SHA256_RE.fullmatch(value or "") is None for value in build.values()):
        raise ReplayError("replay build hashes are invalid")
    if sha256_bytes(executable_path.read_bytes()) != build["executable_sha256"]:
        raise ReplayError("replay executable hash mismatch")
    receipt_bytes = receipt_path.read_bytes()
    receipt = load(receipt_path)
    if (
        sha256_bytes(receipt_bytes) != build["generation_receipt_sha256"]
        or receipt.get("rom", {}).get("sha256") != game["rom_sha256"]
        or receipt.get("generated", {}).get("source_inventory_sha256") !=
            build["source_inventory_sha256"]
    ):
        raise ReplayError("replay generation receipt mismatch")

    configuration = exact(
        replay["configuration"],
        {
            "hardware_model",
            "headless",
            "no_audio",
            "rtc_unix_time",
            "ignore_rtc_persistence",
            "fallback_diagnostics",
        },
        "replay configuration",
    )
    if configuration != {
        "hardware_model": "auto",
        "headless": True,
        "no_audio": True,
        "rtc_unix_time": 1700000000,
        "ignore_rtc_persistence": True,
        "fallback_diagnostics": True,
    }:
        raise ReplayError("unsupported replay configuration")
    if sha256_bytes(canonical(configuration)) != replay["configuration_sha256"]:
        raise ReplayError("replay configuration hash mismatch")

    mods = exact(
        replay["mods"],
        {
            "artifact_sha256",
            "artifact_base64",
            "package_set_sha256",
            "contract",
            "load_order",
            "packages",
        },
        "replay mods",
    )
    contract = exact(
        mods["contract"],
        {
            "policy_sha256",
            "package_schema_sha256",
            "semantic_manifest_sha256",
            "semantic_schema_sha256",
        },
        "replay mod contract",
    )
    if any(SHA256_RE.fullmatch(value or "") is None for value in contract.values()):
        raise ReplayError("replay schema hashes are invalid")
    package_digest = package_set_digest(mods["packages"])
    if (
        package_digest != mods["package_set_sha256"]
        or mods["load_order"] != [item["id"] for item in mods["packages"]]
    ):
        raise ReplayError("replay package set/order mismatch")
    artifact = decode_base64(mods["artifact_base64"], "overlay artifact")
    if (
        sha256_bytes(artifact) != mods["artifact_sha256"]
        or len(artifact) < 92
        or artifact[:8] != b"GBDMOD1\0"
        or artifact[24:56].hex() != game["rom_sha256"]
        or artifact[56:88].hex() != package_digest
    ):
        raise ReplayError("replay overlay artifact mismatch")

    seed = exact(replay["seed"], {"source_sha256", "segments"}, "replay seed")
    if SHA256_RE.fullmatch(seed["source_sha256"] or "") is None:
        raise ReplayError("replay source seed hash is invalid")
    segments = seed["segments"]
    if not isinstance(segments, list) or len(segments) != 2:
        raise ReplayError("Crystal replay requires exactly two segments")
    ids = []
    for segment in segments:
        exact(
            segment,
            {
                "id",
                "frame_limit",
                "capture_frame",
                "expected_frame_sha256",
                "expected_state",
                "input_sha256",
                "input_base64",
            },
            "replay segment",
        )
        input_bytes = decode_base64(segment["input_base64"], "replay input")
        if (
            sha256_bytes(input_bytes) != segment["input_sha256"]
            or not isinstance(json.loads(input_bytes), list)
            or not isinstance(segment["frame_limit"], int)
            or not isinstance(segment["capture_frame"], int)
            or segment["frame_limit"] <= segment["capture_frame"]
            or SHA256_RE.fullmatch(segment["expected_frame_sha256"] or "") is None
            or not isinstance(segment["expected_state"], dict)
            or not segment["expected_state"]
        ):
            raise ReplayError("replay segment/input is invalid")
        ids.append(segment["id"])
    if ids != ["new-game", "route29-wild-battle"]:
        raise ReplayError("replay segment order mismatch")
    portable = {
        "game": replay["game"],
        "mods": replay["mods"],
        "configuration": replay["configuration"],
        "segments": segments,
    }
    if sha256_bytes(canonical(portable)) != replay["portable_seed_sha256"]:
        raise ReplayError("portable seed hash mismatch")
    return replay, artifact


def resolve_state(state: dict[str, Any], path: str) -> object:
    value: object = state
    for part in path.split("."):
        if isinstance(value, dict):
            if part not in value:
                raise ReplayError(f"state path is missing: {path}")
            value = value[part]
        elif isinstance(value, list) and part.isdigit():
            index = int(part)
            if index >= len(value):
                raise ReplayError(f"state path is out of range: {path}")
            value = value[index]
        else:
            raise ReplayError(f"state path is invalid: {path}")
    return value


def run_segment(
    executable: Path,
    artifact: Path,
    segment: dict[str, Any],
    root: Path,
    persistence: Path,
) -> dict[str, Any]:
    root.mkdir()
    input_path = root / "input.json"
    input_path.write_bytes(decode_base64(segment["input_base64"], "replay input"))
    state_path = root / "state.json"
    log_path = root / "runtime.log"
    prefix = root / "frame"
    command = [
        str(executable),
        "--headless",
        "--limit-frames",
        str(segment["frame_limit"]),
        "--input",
        cycle_input(input_path),
        "--dump-frames",
        str(segment["capture_frame"]),
        "--screenshot-prefix",
        str(prefix),
        "--dump-state",
        str(state_path),
        "--save-dir",
        str(persistence),
        "--log-file",
        str(log_path),
        "--no-audio",
        "--model",
        "auto",
        "--rtc-unix-time",
        "1700000000",
        "--ignore-rtc-persistence",
        "--log-frame-fallbacks",
        "--report-interpreter-hotspots",
        "--interpreter-hotspot-limit",
        "16",
        "--data-mod",
        str(artifact),
    ]
    completed = subprocess.run(
        command, cwd=root, text=True, capture_output=True, check=False
    )
    (root / "launcher.stdout").write_text(completed.stdout, encoding="utf-8")
    (root / "launcher.stderr").write_text(completed.stderr, encoding="utf-8")
    if completed.returncode != 0:
        raise ReplayError(f"replay segment {segment['id']} exited nonzero")
    frame = root / f"frame_{segment['capture_frame']:05d}.ppm"
    if sha256_bytes(frame.read_bytes()) != segment["expected_frame_sha256"]:
        raise ReplayError(f"replay segment {segment['id']} frame mismatch")
    state = load(state_path)
    selected_state = {
        path: resolve_state(state, path)
        for path in sorted(segment["expected_state"])
    }
    if selected_state != segment["expected_state"]:
        raise ReplayError(f"replay segment {segment['id']} state mismatch")
    fallback = parse_fallback_log(log_path)
    if fallback["sites"] or fallback["summary"]["fallbacks"] != 0:
        raise ReplayError(f"replay segment {segment['id']} used fallback")
    return {
        "id": segment["id"],
        "frame_sha256": segment["expected_frame_sha256"],
        "state": selected_state,
        "fallback_sites": 0,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--generation-receipt", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--preflight-only", action="store_true")
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists():
        raise ReplayError("output must not exist")
    manifest = args.manifest.resolve()
    replay, artifact_bytes = preflight(
        manifest,
        args.rom.resolve(),
        args.executable.resolve(),
        args.generation_receipt.resolve(),
    )
    if args.preflight_only:
        print("PASS preflight")
        return 0

    output.mkdir(parents=True)
    artifact = output / "replay-overlay.gbdm"
    artifact.write_bytes(artifact_bytes)
    persistence = output / "persistence"
    persistence.mkdir()
    segments = [
        run_segment(
            args.executable.resolve(),
            artifact,
            segment,
            output / f"{index + 1:02d}-{segment['id']}",
            persistence,
        )
        for index, segment in enumerate(replay["seed"]["segments"])
    ]
    reproduction_hash = sha256_bytes(canonical(segments))
    result = {
        "schema": "gbrecompiled.data-mod-replay-result",
        "version": 1,
        "passed": True,
        "replay_manifest_sha256": sha256_bytes(manifest.read_bytes()),
        "portable_seed_sha256": replay["portable_seed_sha256"],
        "provenance": {
            "rom_sha256": replay["game"]["rom_sha256"],
            "executable_sha256": replay["build"]["executable_sha256"],
            "generation_receipt_sha256":
                replay["build"]["generation_receipt_sha256"],
            "source_inventory_sha256": replay["build"]["source_inventory_sha256"],
            "configuration_sha256": replay["configuration_sha256"],
            "artifact_sha256": replay["mods"]["artifact_sha256"],
            "package_set_sha256": replay["mods"]["package_set_sha256"],
            "contract": replay["mods"]["contract"],
            "load_order": replay["mods"]["load_order"],
            "packages": replay["mods"]["packages"],
        },
        "guest_processes_started": len(segments),
        "segments": segments,
        "reproduction_sha256": reproduction_hash,
    }
    (output / "result.json").write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, TypeError, ValueError, ReplayError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
