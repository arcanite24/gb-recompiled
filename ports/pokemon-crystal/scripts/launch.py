#!/usr/bin/env python3
"""Prepare and launch a packaged Crystal Recompiled checkout."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

from first_run import DEFAULT_NATIVE_PATCH, OUTPUT_NAME, default_cache_dir


SCRIPT_DIR = Path(__file__).resolve().parent
PORT_DIR = SCRIPT_DIR.parent
REPO_ROOT = PORT_DIR.parent.parent
EMBEDDED_DISTRIBUTION = REPO_ROOT / "sdk" / "gb-recompiled"
BOOTSTRAP = SCRIPT_DIR / "bootstrap.py"
FIRST_RUN = SCRIPT_DIR / "first_run.py"
LAUNCH_SCHEMA = "crystal-recompiled.launch-progress"


def host_configuration_path(cache: Path) -> Path:
    return cache / "configuration" / "challenge-v1.json"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def emit(event: str, stage: str, completed: int, *, code: str | None = None) -> None:
    payload: dict[str, object] = {
        "schema": LAUNCH_SCHEMA,
        "schema_version": 1,
        "event": event,
        "stage": stage,
        "completed": completed,
        "total": 3,
    }
    if code is not None:
        payload["code"] = code
    print(json.dumps(payload, separators=(",", ":"), sort_keys=True), flush=True)


def run_silent(command: list[str]) -> bool:
    try:
        completed = subprocess.run(
            command,
            cwd=REPO_ROOT,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )
    except OSError:
        return False
    return completed.returncode == 0


def dependencies_ready() -> bool:
    executable_name = "gbrecomp.exe" if os.name == "nt" else "gbrecomp"
    return (
        (REPO_ROOT / "build" / "bin" / executable_name).is_file()
        and (REPO_ROOT / "runtime" / "include" / "gbrt.h").is_file()
        and (
            PORT_DIR
            / "references"
            / "cache"
            / "pokecrystal-symbols"
            / "pokecrystal11.sym"
        ).is_file()
    )


def verify_existing(cache: Path) -> Path | None:
    receipt_path = cache / "first-run.json"
    generation_receipt_path = (
        cache / "generated" / OUTPUT_NAME / "crystal-generation.json"
    )
    dependencies_path = REPO_ROOT / ".crystal" / "dependencies.json"
    if not receipt_path.is_file():
        return None
    try:
        receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
        generation_receipt = json.loads(
            generation_receipt_path.read_text(encoding="utf-8")
        )
        dependencies = json.loads(dependencies_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None
    executable_name = "pokemon_crystal.exe" if os.name == "nt" else "pokemon_crystal"
    executable = cache / "generated" / OUTPUT_NAME / "build" / executable_name
    if (
        receipt.get("schema") != "crystal-recompiled.first-run"
        or receipt.get("version") != 1
        or receipt.get("generated", {}).get("id") != OUTPUT_NAME
        or receipt.get("generated", {}).get("executable_name") != executable_name
        or not executable.is_file()
        or sha256_file(executable)
        != receipt.get("generated", {}).get("executable_sha256")
        or generation_receipt.get("recompiler", {}).get("sha256")
        != dependencies.get("cli_sha256")
        or generation_receipt.get("runtime", {}).get("source_tree_sha256")
        != dependencies.get("runtime_tree_sha256")
        or generation_receipt.get("native_patch")
        != {
            "kind": "file",
            "name": DEFAULT_NATIVE_PATCH.name,
            "sha256": sha256_file(DEFAULT_NATIVE_PATCH),
        }
    ):
        return None
    return executable


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--rom", type=Path)
    parser.add_argument("--cache-dir", type=Path, default=default_cache_dir())
    parser.add_argument("--prepare-only", action="store_true")
    parser.add_argument(
        "--headless-smoke",
        action="store_true",
        help="run 120 headless frames after preparation",
    )
    parser.add_argument("--data-mod", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    emit("stage", "dependencies", 0)
    if not dependencies_ready():
        if not (EMBEDDED_DISTRIBUTION / "gbrecomp-release.json").is_file():
            emit("failure", "dependencies", 0, code="embedded-sdk-missing")
            return 2
        if not run_silent(
            [
                sys.executable,
                str(BOOTSTRAP),
                "--distribution",
                str(EMBEDDED_DISTRIBUTION),
                "--fetch-references",
            ]
        ):
            emit("failure", "dependencies", 0, code="dependency-setup-failed")
            return 2
    if not dependencies_ready():
        emit("failure", "dependencies", 0, code="dependencies-incomplete")
        return 2
    emit("stage", "dependencies", 1)

    cache = args.cache_dir.expanduser().resolve()
    executable = verify_existing(cache)
    if executable is None:
        command = [
            sys.executable,
            str(FIRST_RUN),
            "--cache-dir",
            str(cache),
        ]
        if args.rom is not None:
            command.extend(("--rom", str(args.rom.expanduser().resolve())))
        completed = subprocess.run(command, cwd=REPO_ROOT, check=False)
        if completed.returncode != 0:
            emit("failure", "local-build", 1, code="first-run-failed")
            return 3
        executable = verify_existing(cache)
        if executable is None:
            emit("failure", "local-build", 1, code="first-run-receipt-invalid")
            return 3
    emit("stage", "local-build", 2)

    if args.prepare_only:
        emit("complete", "complete", 3)
        return 0

    old_umask = os.umask(0o077)
    try:
        user_data = cache / "user-data"
        user_data.mkdir(parents=True, exist_ok=True, mode=0o700)
        configuration = host_configuration_path(cache)
        configuration.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
        if os.name != "nt":
            user_data.chmod(0o700)
            configuration.parent.chmod(0o700)
    finally:
        os.umask(old_umask)

    game_command = [
        str(executable),
        "--save-dir",
        str(user_data),
        "--host-configuration",
        str(configuration),
    ]
    if args.data_mod is not None:
        game_command.extend(("--data-mod", str(args.data_mod.expanduser().resolve())))
    if args.headless_smoke:
        game_command.extend(("--headless", "--no-audio", "--limit-frames", "120"))
    environment = os.environ.copy()
    if os.name == "nt":
        environment["PATH"] = (
            str(EMBEDDED_DISTRIBUTION)
            + os.pathsep
            + environment.get("PATH", "")
        )
    try:
        completed = subprocess.run(
            game_command, cwd=cache, env=environment, check=False
        )
    except OSError:
        emit("failure", "game", 2, code="game-launch-failed")
        return 4
    if completed.returncode != 0:
        emit("failure", "game", 2, code="game-exited-with-error")
        return 4
    emit("complete", "complete", 3)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
