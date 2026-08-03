#!/usr/bin/env python3
"""Prove BillsPC fails closed when its required port module is unavailable."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path

from validate_route import cycle_input
from verify_writable_saves import BACKUP, PRIMARY, checksum_valid


EXPECTED_FAILURE = "native BillsPC requires the exact-ROM Crystal port module"
NO_FALLBACK = "[INTERP] No interpreter fallback recorded."


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--save", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[3]
    output = args.output_dir.resolve()
    if output.exists() and any(output.iterdir()):
        raise ValueError(f"output directory is not empty: {output}")
    output.mkdir(parents=True, exist_ok=True)
    input_script = cycle_input(
        root / "ports/pokemon-crystal/route/inputs/pc-save.json"
    )
    source_hash = sha256(args.save)

    def run(mode: str) -> dict:
        directory = output / mode
        persistence = directory / "persistence"
        persistence.mkdir(parents=True)
        save = persistence / "pokemon_crystal.sav"
        shutil.copy2(args.save, save)
        log = directory / "runtime.log"
        completed = subprocess.run(
            [
                str(args.executable.resolve()),
                "--headless",
                "--no-audio",
                "--limit-frames",
                "15000",
                "--input",
                input_script,
                "--save-dir",
                str(persistence),
                "--rtc-unix-time",
                "1700000000",
                "--ignore-rtc-persistence",
                "--native-presentation",
                mode,
                "--disable-port-module",
                "--log-file",
                str(log),
                "--log-frame-fallbacks",
                "--report-interpreter-hotspots",
            ],
            cwd=directory,
            text=True,
            capture_output=True,
            check=False,
        )
        (directory / "launcher.stdout").write_text(
            completed.stdout, encoding="utf-8"
        )
        (directory / "launcher.stderr").write_text(
            completed.stderr, encoding="utf-8"
        )
        return {
            "exit_code": completed.returncode,
            "save_sha256": sha256(save),
            "log_sha256": sha256(log),
            "log": log.read_text(encoding="utf-8", errors="replace"),
        }

    original = run("original")
    native = run("native")
    original_save = (
        output / "original/persistence/pokemon_crystal.sav"
    ).read_bytes()
    if (
        original["exit_code"] != 0
        or EXPECTED_FAILURE in original["log"]
        or NO_FALLBACK not in original["log"]
        or not checksum_valid(original_save, PRIMARY)
        or not checksum_valid(original_save, BACKUP)
    ):
        raise ValueError(
            "original mode did not bypass the unavailable host surface"
        )
    if (
        native["exit_code"] == 0
        or native["save_sha256"] != source_hash
        or EXPECTED_FAILURE not in native["log"]
    ):
        raise ValueError(
            "native mode did not fail closed without changing the save"
        )

    result = {
        "schema": "crystal-recompiled.bills-pc-failure-injection",
        "version": 1,
        "passed": True,
        "injection": "port-module-disabled",
        "original_exit_code": original["exit_code"],
        "native_exit_code": native["exit_code"],
        "original_completed_without_fallback": True,
        "original_checksums_valid": True,
        "original_save_sha256": original["save_sha256"],
        "native_failed_closed": True,
        "native_save_unchanged": True,
        "source_save_sha256": source_hash,
        "original_log_sha256": original["log_sha256"],
        "native_log_sha256": native["log_sha256"],
    }
    (output / "result.json").write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, TypeError, ValueError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
