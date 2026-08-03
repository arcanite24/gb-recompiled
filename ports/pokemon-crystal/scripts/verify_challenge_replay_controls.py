#!/usr/bin/env python3
"""Run exact-build negative controls against Challenge replay preflight."""

from __future__ import annotations

import argparse
import copy
import json
import sys
from pathlib import Path
from typing import Any, Callable

from validate_route import (
    GENERATION_SCHEMA,
    ValidationError,
    load_json,
    sha256,
    validate_challenge_replay_preflight,
    validate_manifest,
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--generation-receipt", required=True, type=Path)
    parser.add_argument("--native-patch-manifest", required=True, type=Path)
    parser.add_argument("--host-configuration", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    manifest_path = args.manifest.resolve()
    route_root = manifest_path.parent
    executable = args.executable.resolve()
    receipt_path = args.generation_receipt.resolve()
    patch_path = args.native_patch_manifest.resolve()
    configuration_path = args.host_configuration.resolve()
    output = args.output.resolve()
    if output.exists():
        if not output.is_dir() or any(output.iterdir()):
            raise ValidationError("output must be absent or empty")
    for path in (
        manifest_path,
        executable,
        receipt_path,
        patch_path,
        configuration_path,
    ):
        if not path.is_file():
            raise ValidationError(f"missing replay-control input: {path.name}")
    manifest = validate_manifest(load_json(manifest_path))
    receipt = load_json(receipt_path)
    if (
        not isinstance(receipt, dict)
        or receipt.get("schema") != GENERATION_SCHEMA
        or receipt.get("version") != 1
        or receipt.get("rom", {}).get("sha256") != manifest["rom_sha256"]
    ):
        raise ValidationError("generation receipt does not match replay manifest")
    baseline = validate_challenge_replay_preflight(
        manifest,
        route_root,
        executable,
        receipt_path,
        receipt,
        patch_path,
        configuration_path,
    )
    if baseline is None:
        raise ValidationError("replay-control manifest is not Challenge v2")

    mutations: dict[str, Callable[[dict[str, Any]], None]] = {
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
    }
    if any(segment.get("input_overlay") for segment in manifest["segments"]):
        mutations["input_overlay"] = lambda value: next(
            segment
            for segment in value["segments"]
            if segment.get("input_overlay")
        ).__setitem__("input_overlay_sha256", "0" * 64)

    controls: dict[str, dict[str, object]] = {}
    for name, mutate in mutations.items():
        changed = copy.deepcopy(manifest)
        mutate(changed)
        try:
            validate_challenge_replay_preflight(
                changed,
                route_root,
                executable,
                receipt_path,
                receipt,
                patch_path,
                configuration_path,
            )
        except ValidationError as error:
            controls[name] = {
                "rejected_before_guest": True,
                "error": str(error),
            }
        else:
            raise ValidationError(f"{name} mismatch passed replay preflight")

    output.mkdir(parents=True)
    result = {
        "schema": "crystal-recompiled.challenge-replay-controls",
        "version": 1,
        "passed": True,
        "manifest_sha256": sha256(manifest_path),
        "executable_sha256": sha256(executable),
        "generation_receipt_sha256": sha256(receipt_path),
        "baseline": baseline,
        "controls": controls,
    }
    result_path = output / "result.json"
    result_path.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    print(f"PASS controls={len(controls)}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ValidationError as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
