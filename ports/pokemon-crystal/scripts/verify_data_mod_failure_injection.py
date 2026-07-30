#!/usr/bin/env python3
"""Exercise generated-runtime data-overlay rejection before guest execution."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path


class VerificationError(RuntimeError):
    pass


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(
    executable: Path,
    root: Path,
    artifact: Path,
    expected_status: int,
    expected_text: str,
) -> dict:
    root.mkdir(parents=True)
    persistence = root / "persistence"
    persistence.mkdir()
    log = root / "runtime.log"
    state = root / "state.json"
    completed = subprocess.run(
        [
            str(executable),
            "--headless",
            "--limit-frames",
            "1",
            "--no-audio",
            "--save-dir",
            str(persistence),
            "--log-file",
            str(log),
            "--dump-state",
            str(state),
            "--data-mod",
            str(artifact),
        ],
        cwd=root,
        text=True,
        capture_output=True,
        check=False,
    )
    (root / "launcher.stdout").write_text(completed.stdout, encoding="utf-8")
    (root / "launcher.stderr").write_text(completed.stderr, encoding="utf-8")
    text = log.read_text(encoding="utf-8") if log.is_file() else ""
    if (
        completed.returncode != expected_status
        or expected_text not in text
        or (expected_status != 0 and state.exists())
        or (expected_status != 0 and "[GBRT][port:" in text)
        or (expected_status != 0 and "[LIMIT]" in text)
    ):
        raise VerificationError(f"unexpected fail-closed result for {root.name}")
    return {
        "exit_status": completed.returncode,
        "runtime_log_sha256": sha256(log),
        "state_written": state.exists(),
        "guest_limit_reached": "[LIMIT]" in text,
        "port_activated": "[GBRT][port:" in text,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--artifact", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    executable = args.executable.resolve()
    source = args.artifact.resolve()
    output = args.output.resolve()
    if output.exists():
        raise VerificationError("output must not exist")
    output.mkdir(parents=True)
    if not executable.is_file() or not source.is_file():
        raise VerificationError("missing executable or artifact")
    original = source.read_bytes()

    wrong_rom = bytearray(original)
    wrong_rom[24] ^= 1
    wrong_rom_path = output / "wrong-rom.gbdm"
    wrong_rom_path.write_bytes(wrong_rom)

    wrong_source = bytearray(original)
    wrong_source[100] ^= 1
    wrong_source_path = output / "wrong-source.gbdm"
    wrong_source_path.write_bytes(wrong_source)

    trailing_path = output / "trailing.gbdm"
    trailing_path.write_bytes(original + b"\0")

    controls = {
        "valid": run(
            executable,
            output / "valid",
            source,
            0,
            "[DATA-MOD] Active entries=42",
        ),
        "wrong_rom": run(
            executable,
            output / "wrong-rom",
            wrong_rom_path,
            1,
            "Data-mod activation failed: ROM mismatch",
        ),
        "wrong_source": run(
            executable,
            output / "wrong-source",
            wrong_source_path,
            1,
            "Data-mod activation failed: source byte mismatch",
        ),
        "trailing": run(
            executable,
            output / "trailing",
            trailing_path,
            1,
            "Data-mod activation failed: invalid artifact",
        ),
    }
    if not controls["valid"]["state_written"]:
        raise VerificationError("valid control did not reach guest shutdown")
    result = {
        "schema": "gbrecompiled.pokemon-crystal.data-mod-failure-injection",
        "version": 1,
        "passed": True,
        "executable_sha256": sha256(executable),
        "source_artifact_sha256": sha256(source),
        "controls": controls,
    }
    result_path = output / "result.json"
    result_path.write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, VerificationError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
