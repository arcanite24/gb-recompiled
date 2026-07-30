#!/usr/bin/env python3
"""Run the exact-ROM packaged release gate on one clean host."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import subprocess
import sys
from pathlib import Path


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run(
    command: list[str],
    *,
    cwd: Path,
    capture: bool = False,
) -> subprocess.CompletedProcess[bytes]:
    completed = subprocess.run(
        command,
        cwd=cwd,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE if capture else subprocess.DEVNULL,
        stderr=subprocess.STDOUT if capture else subprocess.DEVNULL,
        check=False,
        timeout=900,
    )
    if completed.returncode != 0:
        raise RuntimeError("packaged release command failed")
    return completed


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--package-root", type=Path, required=True)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--cache", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    package_root = args.package_root.resolve()
    rom = args.rom.resolve()
    cache = args.cache.resolve()
    if cache.exists():
        raise RuntimeError("verification cache already exists")
    os.umask(0o077)
    launch = (
        package_root
        / "ports"
        / "pokemon-crystal"
        / "scripts"
        / "launch.py"
    )
    crystal = package_root / "ports" / "pokemon-crystal"
    if not launch.is_file() or not (package_root / "crystal-release.json").is_file():
        raise RuntimeError("incomplete package")

    run(
        [
            sys.executable,
            str(launch),
            "--rom",
            str(rom),
            "--cache-dir",
            str(cache),
            "--prepare-only",
        ],
        cwd=package_root,
    )
    run(
        [
            sys.executable,
            str(launch),
            "--cache-dir",
            str(cache),
            "--headless-smoke",
        ],
        cwd=package_root,
    )

    executable_name = "pokemon_crystal.exe" if os.name == "nt" else "pokemon_crystal"
    generated = cache / "generated" / "crystal-rev1-v1"
    executable = generated / "build" / executable_name
    route_dir = cache / "verification" / "route"
    run(
        [
            sys.executable,
            str(crystal / "scripts" / "validate_route.py"),
            "--manifest",
            str(crystal / "route" / "manifest.json"),
            "--executable",
            str(executable),
            "--generation-receipt",
            str(generated / "crystal-generation.json"),
            "--evidence-dir",
            str(route_dir),
            "--fallback-policy",
            str(crystal / "route" / "fallback-policy.json"),
            "--rtc-unix-time",
            "1700000000",
        ],
        cwd=package_root,
    )
    route = json.loads((route_dir / "result.json").read_text(encoding="utf-8"))
    if route.get("passed") is not True or len(route.get("segments", [])) != 4:
        raise RuntimeError("packaged route did not pass")

    mod_dir = cache / "verification" / "mod"
    mod_dir.mkdir(parents=True)
    resolution = mod_dir / "resolution.json"
    artifact = mod_dir / "route29-level-five.gbdm"
    compile_report = mod_dir / "compile-report.json"
    run(
        [
            sys.executable,
            str(package_root / "tools" / "validate_data_mods.py"),
            "--manifest",
            str(
                crystal
                / "mods"
                / "samples"
                / "route29-level-five"
                / "package.json"
            ),
            "--policy",
            str(crystal / "mods" / "target-policy.json"),
            "--package-schema",
            str(crystal / "mods" / "package-schema.json"),
            "--semantic-package",
            str(crystal / "semantic" / "package.json"),
            "--semantic-schema",
            str(crystal / "semantic" / "package-schema.json"),
            "--output",
            str(resolution),
        ],
        cwd=package_root,
    )
    run(
        [
            sys.executable,
            str(package_root / "tools" / "compile_crystal_data_mod.py"),
            "--resolution",
            str(resolution),
            "--rom",
            str(rom),
            "--output",
            str(artifact),
            "--report",
            str(compile_report),
        ],
        cwd=package_root,
    )
    save = cache / "user-data" / "pokemon_crystal.sav"
    before_save = sha256(save)
    mod_run = run(
        [
            sys.executable,
            str(launch),
            "--cache-dir",
            str(cache),
            "--data-mod",
            str(artifact),
            "--headless-smoke",
        ],
        cwd=package_root,
        capture=True,
    )
    if b"[DATA-MOD] Active entries=42" not in mod_run.stdout:
        raise RuntimeError("packaged data mod was not active")
    if sha256(save) != before_save:
        raise RuntimeError("packaged data mod changed the save")
    run(
        [
            sys.executable,
            str(launch),
            "--cache-dir",
            str(cache),
            "--headless-smoke",
        ],
        cwd=package_root,
    )
    if sha256(save) != before_save:
        raise RuntimeError("vanilla recovery changed the save")

    receipt = json.loads((cache / "first-run.json").read_text(encoding="utf-8"))
    package = json.loads(
        (package_root / "crystal-release.json").read_text(encoding="utf-8")
    )
    result = {
        "schema": "crystal-recompiled.packaged-release-verification",
        "version": 1,
        "passed": True,
        "host": {
            "system": platform.system(),
            "machine": platform.machine(),
        },
        "package": package["release"],
        "archive_manifest_sha256": sha256(
            package_root / "crystal-release.json"
        ),
        "first_run_receipt_sha256": sha256(cache / "first-run.json"),
        "generation_receipt_sha256": sha256(
            generated / "crystal-generation.json"
        ),
        "executable_sha256": sha256(executable),
        "route_result_sha256": sha256(route_dir / "result.json"),
        "route_segments": 4,
        "route_checkpoints": sum(
            len(segment["checkpoints"]) for segment in route["segments"]
        ),
        "fallback_entries": sum(
            segment["fallbacks"]["summary"]["interpreter_entries"]
            for segment in route["segments"]
        ),
        "mod_artifact_sha256": sha256(artifact),
        "mod_entries": 42,
        "save_sha256": before_save,
        "save_preserved": True,
        "rom": {
            "size": receipt["rom"]["size"],
            "sha256": receipt["rom"]["sha256"],
        },
    }
    args.output.write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print("packaged release verification passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, json.JSONDecodeError, subprocess.SubprocessError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
