#!/usr/bin/env python3
"""Replay a Crystal route repeatedly and compare stable generated behavior."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path
from typing import Any


class VerificationError(RuntimeError):
    pass


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def json_sha256(payload: object) -> str:
    encoded = json.dumps(
        payload, sort_keys=True, separators=(",", ":"), ensure_ascii=True
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def load_json(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise VerificationError(f"cannot read JSON {path}: {error}") from error


def normalized_command(command: object) -> list[object]:
    if not isinstance(command, list) or any(
        not isinstance(item, str) for item in command
    ):
        raise VerificationError("route result contains an invalid command")
    path_values = {
        "--dump-state",
        "--log-file",
        "--save-dir",
        "--screenshot-prefix",
    }
    normalized: list[object] = ["$EXECUTABLE"]
    index = 1
    while index < len(command):
        option = command[index]
        if option == "--input":
            if index + 1 >= len(command):
                raise VerificationError("route command has an incomplete --input")
            normalized.extend(
                [
                    "--input-sha256",
                    hashlib.sha256(command[index + 1].encode("utf-8")).hexdigest(),
                ]
            )
            index += 2
            continue
        if option in path_values:
            if index + 1 >= len(command):
                raise VerificationError(f"route command has an incomplete {option}")
            normalized.extend([option, "$ARTIFACT"])
            index += 2
            continue
        normalized.append(option)
        index += 1
    return normalized


def persistence_inventory(run_dir: Path) -> list[dict[str, object]]:
    persistence = run_dir / "persistence"
    if not persistence.is_dir():
        raise VerificationError(f"run has no persistence directory: {run_dir}")
    inventory = []
    for path in sorted(persistence.iterdir()):
        if path.suffix == ".rtc":
            continue
        if path.is_file():
            inventory.append(
                {
                    "name": path.name,
                    "bytes": path.stat().st_size,
                    "sha256": sha256(path),
                }
            )
    if not inventory:
        raise VerificationError(f"run has no comparable persistence: {run_dir}")
    return inventory


def comparable_result(payload: object, run_dir: Path) -> dict[str, object]:
    if (
        not isinstance(payload, dict)
        or payload.get("schema") != "gbrecompiled.pokemon-crystal.route-result"
        or payload.get("version") != 1
        or payload.get("passed") is not True
        or not isinstance(payload.get("segments"), list)
        or not payload["segments"]
    ):
        raise VerificationError(f"route result did not pass: {run_dir}")

    segments: list[dict[str, object]] = []
    for segment in payload["segments"]:
        if not isinstance(segment, dict) or segment.get("passed") is not True:
            raise VerificationError(f"route segment did not pass: {run_dir}")
        pcm = segment.get("pcm")
        if (
            not isinstance(pcm, dict)
            or not isinstance(pcm.get("bytes"), int)
            or pcm["bytes"] <= 0
            or not isinstance(pcm.get("sha256"), str)
        ):
            raise VerificationError(
                f"route segment has no deterministic PCM evidence: {run_dir}"
            )
        segments.append(
            {
                "id": segment.get("id"),
                "input": segment.get("input"),
                "input_sha256": segment.get("input_sha256"),
                "runtime_options": normalized_command(segment.get("command")),
                "checkpoints": segment.get("checkpoints"),
                "final_state": segment.get("final_state"),
                "pcm": pcm,
            }
        )
    return {
        "manifest_sha256": payload.get("manifest_sha256"),
        "executable_sha256": payload.get("executable_sha256"),
        "generation_receipt_sha256": payload.get("generation_receipt_sha256"),
        "segments": segments,
        "persistence": persistence_inventory(run_dir),
    }


def first_difference(
    expected: object, actual: object, path: str = "comparison"
) -> str | None:
    if type(expected) is not type(actual):
        return f"{path}: type {type(expected).__name__} != {type(actual).__name__}"
    if isinstance(expected, dict):
        if expected.keys() != actual.keys():
            return f"{path}: keys {sorted(expected)} != {sorted(actual)}"
        for key in expected:
            difference = first_difference(
                expected[key], actual[key], f"{path}.{key}"
            )
            if difference:
                return difference
        return None
    if isinstance(expected, list):
        if len(expected) != len(actual):
            return f"{path}: length {len(expected)} != {len(actual)}"
        for index, value in enumerate(expected):
            difference = first_difference(value, actual[index], f"{path}.{index}")
            if difference:
                return difference
        return None
    if expected != actual:
        return f"{path}: {expected!r} != {actual!r}"
    return None


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--route-validator",
        type=Path,
        default=Path(__file__).with_name("validate_route.py"),
    )
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--generation-receipt", required=True, type=Path)
    parser.add_argument("--evidence-dir", required=True, type=Path)
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--pcm-seconds", type=int, default=10)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.runs < 3:
        raise VerificationError("runs must be at least 3")
    if args.pcm_seconds <= 0:
        raise VerificationError("pcm-seconds must be greater than zero")

    manifest = args.manifest.resolve()
    executable = args.executable.resolve()
    receipt_path = args.generation_receipt.resolve()
    route_validator = args.route_validator.resolve()
    for label, path in (
        ("manifest", manifest),
        ("executable", executable),
        ("generation receipt", receipt_path),
        ("route validator", route_validator),
    ):
        if not path.is_file():
            raise VerificationError(f"missing {label}: {path}")

    evidence = args.evidence_dir.resolve()
    if evidence.exists():
        if not evidence.is_dir() or any(evidence.iterdir()):
            raise VerificationError(
                f"evidence directory must be absent or empty: {evidence}"
            )
    else:
        evidence.mkdir(parents=True)

    receipt = load_json(receipt_path)
    if (
        not isinstance(receipt, dict)
        or receipt.get("schema") != "crystal-recompiled.generation"
        or receipt.get("version") != 1
    ):
        raise VerificationError("unsupported generation receipt")

    comparisons: list[dict[str, object]] = []
    run_results: list[dict[str, object]] = []
    for run_number in range(1, args.runs + 1):
        run_dir = evidence / f"run-{run_number:02d}"
        command = [
            sys.executable,
            str(route_validator),
            "--manifest",
            str(manifest),
            "--executable",
            str(executable),
            "--generation-receipt",
            str(receipt_path),
            "--evidence-dir",
            str(run_dir),
            "--pcm-seconds",
            str(args.pcm_seconds),
        ]
        completed = subprocess.run(
            command, text=True, capture_output=True, check=False
        )
        (evidence / f"run-{run_number:02d}.stdout").write_text(
            completed.stdout, encoding="utf-8"
        )
        (evidence / f"run-{run_number:02d}.stderr").write_text(
            completed.stderr, encoding="utf-8"
        )
        if completed.returncode != 0:
            raise VerificationError(
                f"route validation run {run_number} exited with "
                f"status {completed.returncode}"
            )
        result_path = run_dir / "result.json"
        route_result = load_json(result_path)
        comparison = comparable_result(route_result, run_dir)
        if comparisons:
            difference = first_difference(comparisons[0], comparison)
            if difference:
                message = (
                    f"run {run_number} differs from run 1 at {difference}"
                )
                (evidence / "failure.json").write_text(
                    json.dumps(
                        {
                            "schema": (
                                "gbrecompiled.pokemon-crystal.determinism-failure"
                            ),
                            "version": 1,
                            "passed": False,
                            "error": message,
                            "baseline_run": 1,
                            "mismatching_run": run_number,
                        },
                        indent=2,
                        sort_keys=True,
                    )
                    + "\n",
                    encoding="utf-8",
                )
                raise VerificationError(message)
        comparisons.append(comparison)
        run_results.append(
            {
                "run": run_number,
                "result": str(result_path.relative_to(evidence)),
                "result_sha256": sha256(result_path),
                "comparison_sha256": json_sha256(comparison),
            }
        )

    generation_identity = {
        key: receipt.get(key)
        for key in (
            "rom",
            "recompiler",
            "runtime",
            "generated",
            "build_profile",
            "native_patch",
            "references",
        )
    }
    report = {
        "schema": "gbrecompiled.pokemon-crystal.determinism-result",
        "version": 1,
        "passed": True,
        "runs": args.runs,
        "pcm_seconds_per_segment": args.pcm_seconds,
        "route_validator_sha256": sha256(route_validator),
        "generation_identity": generation_identity,
        "rtc_policy": {
            "comparable": False,
            "excluded_suffix": ".rtc",
            "mode": "isolated-real-time",
            "reason": (
                "Each run starts from empty persistence. RTC files may encode "
                "host elapsed time and are excluded from deterministic byte "
                "comparison; route frames and asserted guest state remain compared."
            ),
        },
        "comparison": comparisons[0],
        "comparison_sha256": json_sha256(comparisons[0]),
        "run_results": run_results,
    }
    (evidence / "result.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    checkpoint_count = sum(
        len(segment["checkpoints"]) for segment in comparisons[0]["segments"]
    )
    print(
        f"PASS runs={args.runs} segments={len(comparisons[0]['segments'])} "
        f"checkpoints={checkpoint_count}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, VerificationError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
