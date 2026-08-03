#!/usr/bin/env python3
"""Create a portable, provenance-complete data-mod replay manifest."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import re
from pathlib import Path
from typing import Any


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


def stable_packages(resolution: dict[str, Any]) -> list[dict[str, Any]]:
    packages = resolution.get("packages")
    if not isinstance(packages, list) or not packages:
        raise ReplayError("resolution has no packages")
    stable = []
    for package in packages:
        if not isinstance(package, dict):
            raise ReplayError("resolution contains a non-object package")
        content = package.get("content")
        if not isinstance(content, list) or not content:
            raise ReplayError("resolution package has no content")
        stable.append(
            {
                "id": package.get("id"),
                "version": package.get("version"),
                "order": package.get("order"),
                "manifest_sha256": package.get("manifest_sha256"),
                "content": [
                    {
                        "id": item.get("id"),
                        "target": item.get("target"),
                        "sha256": item.get("sha256"),
                    }
                    for item in content
                    if isinstance(item, dict)
                ],
            }
        )
    if (
        any(
            not isinstance(package["id"], str)
            or not isinstance(package["version"], str)
            or not isinstance(package["order"], int)
            or SHA256_RE.fullmatch(package["manifest_sha256"] or "") is None
            or not package["content"]
            or any(
                not isinstance(item["id"], str)
                or not isinstance(item["target"], str)
                or SHA256_RE.fullmatch(item["sha256"] or "") is None
                for item in package["content"]
            )
            for package in stable
        )
        or stable != sorted(stable, key=lambda item: (item["order"], item["id"]))
        or len({item["id"] for item in stable}) != len(stable)
    ):
        raise ReplayError("resolution package provenance is invalid")
    return stable


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--seed", type=Path, required=True)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--generation-receipt", type=Path, required=True)
    parser.add_argument("--resolution", type=Path, required=True)
    parser.add_argument("--compile-report", type=Path, required=True)
    parser.add_argument("--artifact", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise ReplayError("output must not exist")

    seed_path = args.seed.resolve()
    seed = load(seed_path)
    exact(seed, {"schema", "version", "game", "configuration", "segments"}, "seed")
    if seed["schema"] != "gbrecompiled.data-mod-replay-seed" or seed["version"] != 1:
        raise ReplayError("unsupported replay seed")
    game = exact(seed["game"], {"id", "rom_sha256", "rom_size"}, "seed game")
    rom = args.rom.read_bytes()
    if (
        len(rom) != game["rom_size"]
        or sha256_bytes(rom) != game["rom_sha256"]
    ):
        raise ReplayError("user ROM does not match replay seed")
    configuration = seed["configuration"]
    if not isinstance(configuration, dict) or not configuration:
        raise ReplayError("seed configuration must be a nonempty object")

    receipt_bytes = args.generation_receipt.read_bytes()
    receipt = load(args.generation_receipt)
    source_inventory_sha256 = receipt.get("generated", {}).get(
        "source_inventory_sha256"
    )
    if (
        receipt.get("rom", {}).get("sha256") != game["rom_sha256"]
        or SHA256_RE.fullmatch(source_inventory_sha256 or "") is None
    ):
        raise ReplayError("generation receipt uses a different ROM")
    resolution_bytes = args.resolution.read_bytes()
    resolution = load(args.resolution)
    report = load(args.compile_report)
    artifact = args.artifact.read_bytes()
    packages = stable_packages(resolution)
    package_set_sha256 = sha256_bytes(canonical(packages))
    if (
        resolution.get("load_order") != [package["id"] for package in packages]
        or report.get("passed") is not True
        or report.get("resolution_sha256") != sha256_bytes(resolution_bytes)
        or report.get("package_set_sha256") != package_set_sha256
        or report.get("artifact", {}).get("sha256") != sha256_bytes(artifact)
        or len(artifact) < 92
        or artifact[:8] != b"GBDMOD1\0"
        or artifact[24:56].hex() != game["rom_sha256"]
        or artifact[56:88].hex() != package_set_sha256
    ):
        raise ReplayError("resolution, compile report, and artifact disagree")

    segments = seed["segments"]
    if not isinstance(segments, list) or not segments:
        raise ReplayError("seed must contain replay segments")
    portable_segments = []
    segment_ids: set[str] = set()
    for index, segment_value in enumerate(segments):
        segment = exact(
            segment_value,
            {
                "id",
                "input",
                "frame_limit",
                "capture_frame",
                "expected_frame_sha256",
                "expected_state",
            },
            f"seed segment {index}",
        )
        relative = segment["input"]
        if (
            not isinstance(relative, str)
            or Path(relative).is_absolute()
            or "\\" in relative
        ):
            raise ReplayError("seed input path must be portable and relative")
        input_path = seed_path.parent.joinpath(relative).resolve()
        try:
            input_path.relative_to(seed_path.parent.parent.resolve())
        except ValueError as error:
            raise ReplayError("seed input path escapes the replay project") from error
        input_bytes = input_path.read_bytes()
        events = json.loads(input_bytes)
        if (
            not isinstance(events, list)
            or not isinstance(segment["id"], str)
            or not segment["id"]
            or segment["id"] in segment_ids
            or not isinstance(segment["frame_limit"], int)
            or not isinstance(segment["capture_frame"], int)
            or segment["capture_frame"] <= 0
            or segment["frame_limit"] <= segment["capture_frame"]
            or SHA256_RE.fullmatch(segment["expected_frame_sha256"] or "") is None
            or not isinstance(segment["expected_state"], dict)
            or not segment["expected_state"]
        ):
            raise ReplayError("seed segment/input is invalid")
        segment_ids.add(segment["id"])
        portable_segments.append(
            {
                "id": segment["id"],
                "frame_limit": segment["frame_limit"],
                "capture_frame": segment["capture_frame"],
                "expected_frame_sha256": segment["expected_frame_sha256"],
                "expected_state": segment["expected_state"],
                "input_sha256": sha256_bytes(input_bytes),
                "input_base64": base64.b64encode(input_bytes).decode("ascii"),
            }
        )

    contract = {
        "policy_sha256": resolution.get("policy_sha256"),
        "package_schema_sha256": resolution.get("package_schema_sha256"),
        "semantic_manifest_sha256": resolution.get("semantic_manifest_sha256"),
        "semantic_schema_sha256": resolution.get("semantic_schema_sha256"),
    }
    if any(SHA256_RE.fullmatch(value or "") is None for value in contract.values()):
        raise ReplayError("resolution schema provenance is invalid")
    replay = {
        "schema": "gbrecompiled.data-mod-replay",
        "version": 1,
        "game": game,
        "build": {
            "executable_sha256": sha256_bytes(args.executable.read_bytes()),
            "generation_receipt_sha256": sha256_bytes(receipt_bytes),
            "source_inventory_sha256": source_inventory_sha256,
        },
        "mods": {
            "artifact_sha256": sha256_bytes(artifact),
            "artifact_base64": base64.b64encode(artifact).decode("ascii"),
            "package_set_sha256": package_set_sha256,
            "contract": contract,
            "load_order": [package["id"] for package in packages],
            "packages": packages,
        },
        "configuration": configuration,
        "configuration_sha256": sha256_bytes(canonical(configuration)),
        "seed": {
            "source_sha256": sha256_bytes(seed_path.read_bytes()),
            "segments": portable_segments,
        },
    }
    replay["portable_seed_sha256"] = sha256_bytes(
        canonical(
            {
                "game": replay["game"],
                "mods": replay["mods"],
                "configuration": replay["configuration"],
                "segments": replay["seed"]["segments"],
            }
        )
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(replay, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(json.dumps({
        "passed": True,
        "output": str(args.output),
        "replay_sha256": sha256_bytes(args.output.read_bytes()),
        "portable_seed_sha256": replay["portable_seed_sha256"],
    }, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, TypeError, ValueError, ReplayError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
