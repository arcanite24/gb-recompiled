#!/usr/bin/env python3
"""Compare replay reproductions and prove provenance mismatches fail preflight."""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import subprocess
from pathlib import Path


class ControlError(RuntimeError):
    pass


def load(path: Path) -> dict:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ControlError(f"JSON root must be an object: {path}")
    return value


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--result", type=Path, action="append", required=True)
    parser.add_argument("--verifier", type=Path, required=True)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--generation-receipt", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if len(args.result) != 2:
        raise ControlError("exactly two independent replay results are required")
    output = args.output.resolve()
    if output.exists():
        raise ControlError("output must not exist")
    output.mkdir(parents=True)

    results = [load(path.resolve()) for path in args.result]
    for result in results:
        if (
            result.get("schema") != "gbrecompiled.data-mod-replay-result"
            or result.get("passed") is not True
            or result.get("guest_processes_started") != 2
        ):
            raise ControlError("one replay result did not complete")
    if (
        results[0]["reproduction_sha256"] != results[1]["reproduction_sha256"]
        or results[0]["segments"] != results[1]["segments"]
        or results[0]["provenance"] != results[1]["provenance"]
    ):
        raise ControlError("matching replay inputs did not reproduce")

    baseline = load(args.manifest.resolve())
    mutations = {}
    value = copy.deepcopy(baseline)
    value["mods"]["load_order"] = []
    mutations["load_order"] = value
    value = copy.deepcopy(baseline)
    value["mods"]["packages"][0]["id"] += ".changed"
    value["mods"]["load_order"][0] += ".changed"
    mutations["package_identity"] = value
    value = copy.deepcopy(baseline)
    value["mods"]["packages"][0]["content"][0]["sha256"] = "0" * 64
    mutations["content_hash"] = value
    value = copy.deepcopy(baseline)
    value["mods"]["contract"]["semantic_schema_sha256"] = "0" * 64
    mutations["schema_hash"] = value
    value = copy.deepcopy(baseline)
    value["build"]["executable_sha256"] = "0" * 64
    mutations["executable_hash"] = value
    value = copy.deepcopy(baseline)
    value["configuration"]["rtc_unix_time"] += 1
    mutations["configuration"] = value
    value = copy.deepcopy(baseline)
    value["seed"]["segments"].reverse()
    mutations["segment_order"] = value

    controls = {}
    for name, mutation in mutations.items():
        manifest = output / f"{name}.json"
        manifest.write_text(
            json.dumps(mutation, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        forbidden_output = output / f"{name}-guest-output"
        completed = subprocess.run(
            [
                "python3",
                str(args.verifier.resolve()),
                "--manifest",
                str(manifest),
                "--rom",
                str(args.rom.resolve()),
                "--executable",
                str(args.executable.resolve()),
                "--generation-receipt",
                str(args.generation_receipt.resolve()),
                "--output",
                str(forbidden_output),
                "--preflight-only",
            ],
            text=True,
            capture_output=True,
            check=False,
        )
        (output / f"{name}.stdout").write_text(
            completed.stdout, encoding="utf-8"
        )
        (output / f"{name}.stderr").write_text(
            completed.stderr, encoding="utf-8"
        )
        if completed.returncode == 0 or forbidden_output.exists():
            raise ControlError(f"{name} mismatch reached replay execution")
        controls[name] = {
            "exit_status": completed.returncode,
            "guest_output_created": False,
        }

    report = {
        "schema": "gbrecompiled.data-mod-replay-controls",
        "version": 1,
        "passed": True,
        "manifest_sha256": sha256(args.manifest.resolve()),
        "matching_reproductions": 2,
        "reproduction_sha256": results[0]["reproduction_sha256"],
        "controls": controls,
    }
    (output / "result.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, TypeError, ValueError, ControlError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
