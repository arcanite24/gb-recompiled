#!/usr/bin/env python3
"""Exercise the privacy-safe single-ROM generation progress contract."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path


def run(command: list[str], *, cwd: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        cwd=cwd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=120,
    )


def read_events(path: Path) -> list[dict[str, object]]:
    return [
        json.loads(line)
        for line in path.read_text(encoding="utf-8").splitlines()
        if line
    ]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--gbrecomp", type=Path, required=True)
    parser.add_argument("--runtime-dir", type=Path, required=True)
    parser.add_argument("--fixture-generator", type=Path, required=True)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="gbrecomp-progress-") as raw:
        root = Path(raw)
        secret = "private-user-rom-name"
        rom = root / f"{secret}.gb"
        output = root / "generated"
        progress = root / "progress.jsonl"
        fixture = run(
            [
                sys.executable,
                str(args.fixture_generator),
                "--mapper",
                "rom-only",
                "--output",
                str(rom),
            ],
            cwd=root,
        )
        if fixture.returncode != 0:
            raise RuntimeError(f"fixture generation failed: {fixture.stderr}")

        generated = run(
            [
                str(args.gbrecomp),
                str(rom),
                "--runtime-dir",
                str(args.runtime_dir),
                "--no-scan",
                "--output-prefix",
                "stable_game",
                "--progress-json",
                str(progress),
                "--output",
                str(output),
            ],
            cwd=root,
        )
        if generated.returncode != 0:
            raise RuntimeError(f"generation failed: {generated.stderr}")
        events = read_events(progress)
        expected = [
            ("stage", "rom-validated", 1),
            ("stage", "analysis-complete", 2),
            ("stage", "ir-complete", 3),
            ("stage", "code-generation-complete", 4),
            ("stage", "output-complete", 5),
            ("complete", "complete", 6),
        ]
        actual = [
            (event.get("event"), event.get("stage"), event.get("completed"))
            for event in events
        ]
        if actual != expected:
            raise RuntimeError(f"unexpected progress sequence: {actual}")
        for event in events:
            if (
                event.get("schema") != "gbrecomp.progress"
                or event.get("schema_version") != 1
                or event.get("total") != 6
            ):
                raise RuntimeError(f"invalid progress event: {event}")
        encoded = progress.read_text(encoding="utf-8")
        if secret in encoded or str(root) in encoded:
            raise RuntimeError("progress disclosed the ROM name or local path")
        metadata = json.loads(
            (output / "stable_game_metadata.json").read_text(encoding="utf-8")
        )
        if metadata.get("rom_name") != "stable_game":
            raise RuntimeError("stable output prefix did not replace the private ROM name")
        if any(secret in path.name for path in output.rglob("*")):
            raise RuntimeError("generated filenames disclosed the private ROM name")

        invalid = root / f"{secret}-unsupported.gbc"
        invalid.write_bytes(b"\x00" * 0x100)
        rejected_output = root / "must-not-exist"
        rejected_progress = root / "rejected.jsonl"
        rejected = run(
            [
                str(args.gbrecomp),
                str(invalid),
                "--runtime-dir",
                str(args.runtime_dir),
                "--progress-json",
                str(rejected_progress),
                "--output",
                str(rejected_output),
            ],
            cwd=root,
        )
        if rejected.returncode == 0:
            raise RuntimeError("invalid ROM unexpectedly passed")
        if rejected_output.exists():
            raise RuntimeError("invalid ROM created generated output")
        rejected_events = read_events(rejected_progress)
        if rejected_events != [
            {
                "schema": "gbrecomp.progress",
                "schema_version": 1,
                "event": "failure",
                "stage": "rom-validation",
                "completed": 0,
                "total": 6,
                "code": "rom-invalid",
            }
        ]:
            raise RuntimeError(f"unexpected rejection event: {rejected_events}")
        rejected_encoded = rejected_progress.read_text(encoding="utf-8")
        if secret in rejected_encoded or str(root) in rejected_encoded:
            raise RuntimeError("rejection diagnostics disclosed private input identity")

    print("generation progress is stable, fail-closed, and path-free")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
