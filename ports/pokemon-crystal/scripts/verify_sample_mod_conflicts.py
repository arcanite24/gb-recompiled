#!/usr/bin/env python3
"""Capture explicit and semantic conflict diagnostics for Crystal sample mods."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path


class VerificationError(RuntimeError):
    pass


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(value, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def run(command: list[str], root: Path, label: str) -> subprocess.CompletedProcess[str]:
    completed = subprocess.run(command, text=True, capture_output=True, check=False)
    (root / f"{label}.stdout").write_text(completed.stdout, encoding="utf-8")
    (root / f"{label}.stderr").write_text(completed.stderr, encoding="utf-8")
    return completed


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    mods = root / "ports/pokemon-crystal/mods"
    output = args.output.resolve()
    if output.exists():
        if not output.is_dir() or output.is_symlink():
            raise VerificationError("output must be a directory")
        shutil.rmtree(output)
    output.mkdir(parents=True)

    guide_root = mods / "samples/route29-encounter-guide"
    difficulty_manifest = mods / "samples/route29-level-five/package.json"
    guide = json.loads((guide_root / "package.json").read_text(encoding="utf-8"))
    content = (guide_root / "accessibility.json").read_bytes()
    duplicate_root = output / "duplicate-guide"
    duplicate_root.mkdir()
    (duplicate_root / "accessibility.json").write_bytes(content)
    duplicate = json.loads(json.dumps(guide))
    duplicate["package"]["id"] = "org.gbrecompiled.crystal.route29-encounter-guide-alt"
    duplicate["load"]["order"] = 201
    duplicate_manifest = duplicate_root / "package.json"
    write_json(duplicate_manifest, duplicate)

    common = [
        sys.executable,
        str(root / "tools/validate_data_mods.py"),
        "--policy",
        str(mods / "target-policy.json"),
        "--package-schema",
        str(mods / "package-schema.json"),
        "--semantic-package",
        str(root / "ports/pokemon-crystal/semantic/package.json"),
        "--semantic-schema",
        str(root / "ports/pokemon-crystal/semantic/package-schema.json"),
    ]
    collision_resolution = output / "semantic-collision-resolution.json"
    collision_validate = run(
        common
        + [
            "--manifest",
            str(guide_root / "package.json"),
            "--manifest",
            str(duplicate_manifest),
            "--output",
            str(collision_resolution),
        ],
        output,
        "semantic-collision-validate",
    )
    if collision_validate.returncode != 0:
        raise VerificationError("independent manifests did not reach semantic compiler")
    stale_artifact = output / "semantic-collision.gbdm"
    stale_report = output / "semantic-collision-report.json"
    stale_artifact.write_bytes(b"stale")
    stale_report.write_text("stale\n", encoding="utf-8")
    collision_compile = run(
        [
            sys.executable,
            str(root / "tools/compile_crystal_data_mod.py"),
            "--resolution",
            str(collision_resolution),
            "--rom",
            str(args.rom.resolve()),
            "--output",
            str(stale_artifact),
            "--report",
            str(stale_report),
        ],
        output,
        "semantic-collision-compile",
    )
    semantic_message = collision_compile.stdout.strip()
    if (
        collision_compile.returncode == 0
        or "overlay conflict for information-sign:ROUTE_29:WEST_SIGN"
        not in semantic_message
        or "route29-encounter-guide:route29-encounter-guide conflicts with "
        "org.gbrecompiled.crystal.route29-encounter-guide-alt:"
        "route29-encounter-guide"
        not in semantic_message
        or stale_artifact.exists()
        or stale_report.exists()
    ):
        raise VerificationError("semantic conflict was not actionable/fail-closed")

    explicit_root = output / "explicit-conflict-guide"
    explicit_root.mkdir()
    (explicit_root / "accessibility.json").write_bytes(content)
    explicit = json.loads(json.dumps(guide))
    explicit["load"]["conflicts"] = [
        "org.gbrecompiled.crystal.route29-level-five"
    ]
    explicit_manifest = explicit_root / "package.json"
    write_json(explicit_manifest, explicit)
    explicit_resolution = output / "explicit-conflict-resolution.json"
    explicit_validate = run(
        common
        + [
            "--manifest",
            str(difficulty_manifest),
            "--manifest",
            str(explicit_manifest),
            "--output",
            str(explicit_resolution),
        ],
        output,
        "explicit-conflict-validate",
    )
    explicit_message = explicit_validate.stdout.strip()
    if (
        explicit_validate.returncode == 0
        or "org.gbrecompiled.crystal.route29-encounter-guide: conflicts with "
        "installed org.gbrecompiled.crystal.route29-level-five"
        not in explicit_message
        or explicit_resolution.exists()
    ):
        raise VerificationError("declared conflict was not actionable/fail-closed")

    result = {
        "schema": "gbrecompiled.pokemon-crystal.sample-mod-conflict-proof",
        "version": 1,
        "passed": True,
        "semantic_overlap": {
            "stage": "compile",
            "exit_code": collision_compile.returncode,
            "diagnostic": semantic_message,
            "identity": "information-sign:ROUTE_29:WEST_SIGN",
            "stale_artifact_removed": True,
            "stale_report_removed": True,
            "guest_processes_started": 0,
        },
        "declared_package_conflict": {
            "stage": "validation",
            "exit_code": explicit_validate.returncode,
            "diagnostic": explicit_message,
            "stale_resolution_absent": True,
            "guest_processes_started": 0,
        },
        "inputs": {
            "difficulty_manifest_sha256": sha256(difficulty_manifest),
            "information_manifest_sha256": sha256(
                guide_root / "package.json"
            ),
            "information_content_sha256": sha256(
                guide_root / "accessibility.json"
            ),
        },
    }
    write_json(output / "result.json", result)
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, TypeError, ValueError, VerificationError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
