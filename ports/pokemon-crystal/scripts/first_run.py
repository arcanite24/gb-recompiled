#!/usr/bin/env python3
"""Select, validate, generate, and build Crystal in a private local cache."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

from verify_rom import EXPECTED, identify


SCRIPT_DIR = Path(__file__).resolve().parent
PORT_DIR = SCRIPT_DIR.parent
REPO_ROOT = PORT_DIR.parent.parent
GENERATE_SCRIPT = SCRIPT_DIR / "generate.py"
DEFAULT_GBRECOMP = (
    REPO_ROOT
    / "build"
    / "bin"
    / ("gbrecomp.exe" if os.name == "nt" else "gbrecomp")
)
DEFAULT_RUNTIME = REPO_ROOT / "runtime"
OUTPUT_NAME = "crystal-rev1-v1"
PROGRESS_SCHEMA = "crystal-recompiled.first-run-progress"
FAILURE_OUTPUT_LIMIT_BYTES = 12 * 1024


def default_cache_dir() -> Path:
    if sys.platform == "darwin":
        return Path.home() / "Library" / "Application Support" / "Crystal Recompiled"
    if os.name == "nt":
        local_app_data = os.environ.get("LOCALAPPDATA")
        if local_app_data:
            return Path(local_app_data) / "Crystal Recompiled"
        return Path.home() / "AppData" / "Local" / "Crystal Recompiled"
    xdg_cache = os.environ.get("XDG_CACHE_HOME")
    base = Path(xdg_cache) if xdg_cache else Path.home() / ".cache"
    return base / "crystal-recompiled"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


class SafeProgress:
    def __init__(self) -> None:
        self._handle = None
        self._history: list[str] = []

    def attach(self, path: Path) -> None:
        self._handle = path.open("w", encoding="utf-8")
        for encoded in self._history:
            self._handle.write(encoded + "\n")
        self._handle.flush()

    def emit(self, event: str, stage: str, completed: int, *, code: str | None = None) -> None:
        payload: dict[str, object] = {
            "schema": PROGRESS_SCHEMA,
            "schema_version": 1,
            "event": event,
            "stage": stage,
            "completed": completed,
            "total": 5,
        }
        if code is not None:
            payload["code"] = code
        encoded = json.dumps(payload, separators=(",", ":"), sort_keys=True)
        self._history.append(encoded)
        print(encoded, flush=True)
        if self._handle is not None:
            self._handle.write(encoded + "\n")
            self._handle.flush()

    def close(self) -> None:
        if self._handle is not None:
            self._handle.close()
            self._handle = None


def choose_rom() -> Path | None:
    try:
        import tkinter as tk
        from tkinter import filedialog
    except ImportError:
        return None
    root = tk.Tk()
    root.withdraw()
    try:
        selected = filedialog.askopenfilename(
            title="Select Pokémon Crystal US/Europe Rev 1",
            filetypes=(
                ("Game Boy Color ROM", "*.gbc"),
                ("Game Boy ROM", "*.gb"),
                ("All files", "*"),
            ),
        )
    finally:
        root.destroy()
    return Path(selected) if selected else None


def is_inside(path: Path, parent: Path) -> bool:
    try:
        path.relative_to(parent)
    except ValueError:
        return False
    return True


def private_output_tail(output: bytes, redactions: tuple[Path, ...]) -> str:
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
    return "--- redacted private command output tail ---\n" + text.rstrip()


def run_private(command: list[str], *, redactions: tuple[Path, ...]) -> None:
    completed = subprocess.run(
        command,
        cwd=REPO_ROOT,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if completed.returncode != 0:
        detail = private_output_tail(completed.stdout, redactions)
        if detail:
            print(detail, file=sys.stderr)
        completed.check_returncode()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Create Crystal Recompiled from a locally selected exact ROM."
    )
    parser.add_argument("--rom", type=Path, help="headless alternative to the file picker")
    parser.add_argument("--cache-dir", type=Path, default=default_cache_dir())
    parser.add_argument("--gbrecomp", type=Path, default=DEFAULT_GBRECOMP)
    parser.add_argument("--runtime", type=Path, default=DEFAULT_RUNTIME)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    progress = SafeProgress()
    progress.emit("stage", "rom-selection", 0)
    rom = args.rom.expanduser() if args.rom is not None else choose_rom()
    if rom is None:
        progress.emit("failure", "rom-selection", 0, code="selection-cancelled")
        return 2
    if not rom.is_file():
        progress.emit("failure", "rom-validation", 0, code="rom-unreadable")
        return 2

    try:
        actual = identify(rom)
    except OSError:
        progress.emit("failure", "rom-validation", 0, code="rom-unreadable")
        return 2
    if actual != EXPECTED:
        progress.emit("failure", "rom-validation", 0, code="unsupported-rom")
        return 2
    progress.emit("stage", "rom-validated", 1)

    cache = args.cache_dir.expanduser().resolve()
    source_root = REPO_ROOT.resolve()
    if is_inside(cache, source_root) or cache == source_root:
        progress.emit("failure", "cache-validation", 1, code="cache-inside-source")
        return 2
    output = cache / "generated" / OUTPUT_NAME
    redactions = (rom.resolve(), cache, source_root)
    if output.exists():
        progress.emit("failure", "cache-validation", 1, code="output-already-exists")
        return 2

    old_umask = os.umask(0o077)
    try:
        cache.mkdir(parents=True, exist_ok=True, mode=0o700)
        cache.chmod(0o700)
        status_dir = cache / "status"
        status_dir.mkdir(mode=0o700, exist_ok=True)
        progress.attach(status_dir / "first-run-progress.jsonl")
        recompiler_progress = status_dir / "gbrecomp-progress.jsonl"

        generate_command = [
            sys.executable,
            str(GENERATE_SCRIPT),
            "--rom",
            str(rom.resolve()),
            "--gbrecomp",
            str(args.gbrecomp.expanduser().resolve()),
            "--runtime",
            str(args.runtime.expanduser().resolve()),
            "--output",
            str(output),
            "--private-cache-output",
            "--progress-json",
            str(recompiler_progress),
        ]
        try:
            run_private(generate_command, redactions=redactions)
        except (OSError, subprocess.CalledProcessError):
            if output.exists():
                shutil.rmtree(output)
            progress.emit("failure", "local-generation", 1, code="generation-failed")
            return 3
        progress.emit("stage", "local-generation", 2)

        build = output / "build"
        profile = output / "crystal-build-profile.cmake"
        try:
            run_private(
                [
                    "cmake",
                    "-G",
                    "Ninja",
                    "-C",
                    str(profile),
                    "-S",
                    str(output),
                    "-B",
                    str(build),
                ],
                redactions=redactions,
            )
        except (OSError, subprocess.CalledProcessError):
            progress.emit("failure", "configure", 2, code="configure-failed")
            return 4
        progress.emit("stage", "configure", 3)

        try:
            run_private(
                ["ninja", "-C", str(build)],
                redactions=redactions,
            )
        except (OSError, subprocess.CalledProcessError):
            progress.emit("failure", "build", 3, code="build-failed")
            return 4
        progress.emit("stage", "build", 4)

        executable_name = "pokemon_crystal.exe" if os.name == "nt" else "pokemon_crystal"
        executable = build / executable_name
        if not executable.is_file():
            progress.emit("failure", "build", 4, code="executable-missing")
            return 4
        generation_receipt = output / "crystal-generation.json"
        if not generation_receipt.is_file():
            progress.emit("failure", "build", 4, code="receipt-missing")
            return 4
        receipt = {
            "schema": "crystal-recompiled.first-run",
            "version": 1,
            "rom": {
                "identity": "pokemon-crystal-ue-rev1",
                "size": actual["size"],
                "sha256": actual["sha256"],
            },
            "generated": {
                "id": OUTPUT_NAME,
                "generation_receipt_sha256": sha256_file(generation_receipt),
                "executable_name": executable_name,
                "executable_sha256": sha256_file(executable),
            },
            "privacy": {
                "telemetry": False,
                "source_path_retained": False,
                "save_included": False,
            },
        }
        receipt_path = cache / "first-run.json"
        receipt_path.write_text(
            json.dumps(receipt, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        progress.emit("complete", "complete", 5)
        return 0
    finally:
        progress.close()
        os.umask(old_umask)


if __name__ == "__main__":
    raise SystemExit(main())
