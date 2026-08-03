#!/usr/bin/env python3
"""Validate Crystal hook-candidate stability and fail-closed binding status."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fail(message: str) -> None:
    raise ValueError(message)


def index_functions(metadata: dict) -> dict[str, dict]:
    return {record["id"]: record for record in metadata["functions"]}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--metadata", type=Path, required=True)
    parser.add_argument("--perturbed-metadata", type=Path, required=True)
    parser.add_argument("--receipt", type=Path, required=True)
    parser.add_argument("--perturbed-receipt", type=Path, required=True)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--evidence-root", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    try:
        manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
        metadata = json.loads(args.metadata.read_text(encoding="utf-8"))
        perturbed = json.loads(args.perturbed_metadata.read_text(encoding="utf-8"))
        receipt = json.loads(args.receipt.read_text(encoding="utf-8"))
        perturbed_receipt = json.loads(
            args.perturbed_receipt.read_text(encoding="utf-8")
        )
        if (
            manifest.get("schema")
            != "gbrecompiled.pokemon-crystal.hook-candidates"
            or manifest.get("version") != 1
        ):
            fail("unsupported hook-candidate manifest")
        rom_sha = manifest["rom_sha256"]
        for candidate_receipt in (receipt, perturbed_receipt):
            if candidate_receipt["rom"]["sha256"] != rom_sha:
                fail("hook generation used the wrong ROM")
            if candidate_receipt.get("native_patch") != {"kind": "none"}:
                fail("observational hook generation unexpectedly bound a patch")
        if perturbed_receipt["codegen"].get("single_function") is not True:
            fail("layout perturbation did not use single-function codegen")

        standard = index_functions(metadata)
        changed = index_functions(perturbed)
        checked = 0
        unbound = 0
        observational = 0
        bound = 0
        for candidate in manifest["candidates"]:
            function_id = candidate["function_id"]
            expected_bank = int(candidate["bank"])
            expected_address = int(candidate["address"], 0)
            left = standard.get(function_id)
            right = changed.get(function_id)
            if left is None or right is None:
                fail(f"candidate ID missing after regeneration: {function_id}")
            for record in (left, right):
                if (
                    int(record["bank"]) != expected_bank
                    or int(record["address"], 0) != expected_address
                    or record["memory_space"] != "physical_rom"
                ):
                    fail(f"candidate address changed: {function_id}")
            checked += 1

            status = candidate["binding_status"]
            if status == "unbound":
                unbound += 1
                if left["patchable"] or right["patchable"]:
                    fail(f"unbound candidate unexpectedly became patchable: {function_id}")
                if not candidate.get("known_caller") or not candidate.get("reason"):
                    fail(f"unbound candidate lacks caller or reason: {function_id}")
                source_path, source_line = candidate["source"].rsplit(":", 1)
                caller_path, caller_line = candidate["caller_source"].rsplit(":", 1)
                source_lines = (
                    args.source_root / source_path
                ).read_text(encoding="utf-8").splitlines()
                caller_lines = (
                    args.source_root / caller_path
                ).read_text(encoding="utf-8").splitlines()
                if candidate["symbol"] not in source_lines[int(source_line) - 1]:
                    fail(f"source label moved for {function_id}")
                if candidate["known_caller"] not in caller_lines[int(caller_line) - 1]:
                    fail(f"caller evidence moved for {function_id}")
                if candidate["replay_coverage"] != "none":
                    replay = args.evidence_root / candidate["replay_coverage"]
                    if not replay.exists():
                        fail(f"declared replay evidence is missing: {replay}")
            elif status == "observational":
                observational += 1
                if not candidate.get("reason"):
                    fail(f"observational candidate lacks reason: {function_id}")
            elif status == "bound":
                bound += 1
                if not left["patchable"] or not right["patchable"]:
                    fail(f"bound candidate is not patchable: {function_id}")
                replay_value = candidate.get("replay_coverage")
                if not isinstance(replay_value, str) or replay_value == "none":
                    fail(f"bound candidate lacks replay evidence: {function_id}")
                replay = args.evidence_root / replay_value
                if not replay.is_file():
                    fail(f"declared replay evidence is missing: {replay}")
                if not candidate.get("reason"):
                    fail(f"bound candidate lacks reason: {function_id}")
            else:
                fail(f"unsupported binding status: {status}")

        result = {
            "schema": "gbrecompiled.pokemon-crystal.hook-validation-result",
            "version": 1,
            "passed": True,
            "manifest_sha256": sha256(args.manifest),
            "metadata_sha256": sha256(args.metadata),
            "perturbed_metadata_sha256": sha256(args.perturbed_metadata),
            "candidate_count": checked,
            "stable_ids": checked,
            "unbound_candidates": unbound,
            "observational_candidates": observational,
            "bound_candidates": bound,
        }
        rendered = json.dumps(result, indent=2, sort_keys=True) + "\n"
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(rendered, encoding="utf-8")
        print(rendered, end="")
        return 0
    except (OSError, ValueError, KeyError, IndexError, json.JSONDecodeError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
