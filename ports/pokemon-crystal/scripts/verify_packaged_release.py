#!/usr/bin/env python3
"""Run the exact-ROM packaged release gate on one clean host."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import subprocess
import sys
from pathlib import Path


COMMAND_TIMEOUT_SECONDS = 3600
FAILURE_OUTPUT_LIMIT_BYTES = 12 * 1024


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
    stage: str,
    redactions: tuple[Path, ...],
    capture: bool = False,
    env: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[bytes]:
    try:
        completed = subprocess.run(
            command,
            cwd=cwd,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
            timeout=COMMAND_TIMEOUT_SECONDS,
            env=env,
        )
    except subprocess.TimeoutExpired as error:
        detail = failure_output_tail(error.stdout or b"", redactions)
        raise RuntimeError(
            f"packaged release stage timed out: {stage}{detail}"
        ) from None
    if completed.returncode != 0:
        detail = failure_output_tail(completed.stdout, redactions)
        raise RuntimeError(
            "packaged release stage failed: "
            f"{stage} (exit {completed.returncode}){detail}"
        )
    if not capture:
        completed.stdout = b""
    return completed


def failure_output_tail(output: bytes, redactions: tuple[Path, ...]) -> str:
    if not output:
        return ""
    text = output[-FAILURE_OUTPUT_LIMIT_BYTES:].decode("utf-8", errors="replace")
    path_strings = set()
    for path in redactions:
        rendered = str(path)
        path_strings.update(
            {
                rendered,
                rendered.replace("\\", "/"),
                rendered.replace("/", "\\"),
            }
        )
    for rendered in sorted(path_strings, key=len, reverse=True):
        if rendered:
            text = re.sub(re.escape(rendered), "<private-path>", text, flags=re.I)
    return "\n--- redacted command output tail ---\n" + text.rstrip()


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
    redactions = (package_root, rom, cache)
    direct_game_env = None
    if os.name == "nt":
        direct_game_env = os.environ.copy()
        direct_game_env["PATH"] = (
            str(package_root / "sdk" / "gb-recompiled")
            + os.pathsep
            + direct_game_env.get("PATH", "")
        )
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
        stage="prepare generated Crystal build",
        redactions=redactions,
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
        stage="vanilla headless smoke",
        redactions=redactions,
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
        stage="four-segment route verification",
        redactions=redactions,
        env=direct_game_env,
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
        stage="data-mod validation",
        redactions=redactions,
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
        stage="data-mod compilation",
        redactions=redactions,
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
        stage="data-mod headless smoke",
        redactions=redactions,
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
        stage="vanilla recovery smoke",
        redactions=redactions,
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
