#!/usr/bin/env python3
"""Capture a path-free physical-controller acceptance result for one package."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import subprocess
import sys
from collections import Counter
from pathlib import Path


REQUIRED_ACTIONS = frozenset("UDLRABST")
CONTROLLER_LINE = re.compile(
    rb"^\[SDL\] Controller: .+ \[([^\]\r\n]+)\]\r?$",
    re.MULTILINE,
)
INPUT_ENTRY = re.compile(r"(?:^|,)[cCfF]\d+:([UDLRABST]+):\d+(?=,|$)")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def input_action_counts(text: str) -> Counter[str]:
    counts: Counter[str] = Counter()
    for buttons in INPUT_ENTRY.findall(text.strip()):
        counts.update(buttons)
    return counts


def load_object(path: Path, label: str) -> dict[str, object]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise RuntimeError(f"cannot read {label}") from error
    if not isinstance(value, dict):
        raise RuntimeError(f"{label} is not an object")
    return value


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--package-root", type=Path, required=True)
    parser.add_argument(
        "--cache",
        type=Path,
        required=True,
        help="existing cache prepared by verify_packaged_release.py",
    )
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--duration-frames", type=int, default=7200)
    parser.add_argument(
        "--attest-controller-only",
        action="store_true",
        help="attest that all recorded gameplay input will come from the controller",
    )
    args = parser.parse_args()

    if not args.attest_controller_only:
        raise RuntimeError("operator controller-only attestation is required")
    if args.duration_frames < 600 or args.duration_frames > 18000:
        raise RuntimeError("duration must be between 600 and 18000 frames")

    package_root = args.package_root.expanduser().resolve()
    cache = args.cache.expanduser().resolve()
    output = args.output.expanduser().resolve()
    package_manifest = package_root / "crystal-release.json"
    receipt_path = cache / "first-run.json"
    package = load_object(package_manifest, "package manifest")
    receipt = load_object(receipt_path, "first-run receipt")
    if (
        package.get("schema") != "crystal-recompiled.release"
        or package.get("version") != 1
        or receipt.get("schema") != "crystal-recompiled.first-run"
        or receipt.get("version") != 1
    ):
        raise RuntimeError("unsupported package or first-run receipt")

    executable_name = "pokemon_crystal.exe" if os.name == "nt" else "pokemon_crystal"
    executable = (
        cache / "generated" / "crystal-rev1-v1" / "build" / executable_name
    )
    executable_receipt = receipt.get("generated")
    if (
        not executable.is_file()
        or not isinstance(executable_receipt, dict)
        or executable_receipt.get("executable_sha256") != sha256(executable)
    ):
        raise RuntimeError("prepared executable does not match first-run receipt")

    private_dir = cache / "verification" / "controller"
    if private_dir.exists() or output.exists():
        raise RuntimeError("controller evidence destination already exists")
    private_dir.mkdir(parents=True, mode=0o700)
    save_dir = private_dir / "user-data"
    save_dir.mkdir(mode=0o700)
    input_record = private_dir / "controller.input"
    runtime_log = private_dir / "runtime.log"

    print(
        "Controller acceptance will open Crystal for up to "
        f"{args.duration_frames} frames.\n"
        "Use only the physical controller. Press D-pad Up/Down/Left/Right, "
        "A, B, Start, and Select at least once, then continue briefly or close "
        "the window.",
        flush=True,
    )
    environment = os.environ.copy()
    if os.name == "nt":
        environment["PATH"] = (
            str(package_root / "sdk" / "gb-recompiled")
            + os.pathsep
            + environment.get("PATH", "")
        )
    completed = subprocess.run(
        [
            str(executable),
            "--record-input",
            str(input_record),
            "--save-dir",
            str(save_dir),
            "--log-file",
            str(runtime_log),
            "--limit-frames",
            str(args.duration_frames),
        ],
        cwd=executable.parent,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
        timeout=360,
        env=environment,
    )
    captured = completed.stdout
    controller = CONTROLLER_LINE.search(captured)
    if completed.returncode != 0:
        raise RuntimeError("packaged controller gameplay exited unsuccessfully")
    if controller is None:
        raise RuntimeError("SDL did not report an accepted controller")
    if not input_record.is_file():
        raise RuntimeError("controller gameplay did not produce an input record")

    counts = input_action_counts(input_record.read_text(encoding="utf-8"))
    missing = REQUIRED_ACTIONS - counts.keys()
    if missing:
        raise RuntimeError(
            "controller gameplay did not exercise required actions: "
            + "".join(sorted(missing))
        )

    controller_line = controller.group(0)
    result = {
        "schema": "crystal-recompiled.controller-verification",
        "version": 1,
        "passed": True,
        "operator_attested_controller_only": True,
        "host": {
            "system": platform.system(),
            "machine": platform.machine(),
        },
        "package": package["release"],
        "package_manifest_sha256": sha256(package_manifest),
        "executable_sha256": sha256(executable),
        "controller_detected": True,
        "controller_profile": controller.group(1).decode(
            "utf-8", errors="replace"
        ),
        "controller_identity_sha256": hashlib.sha256(controller_line).hexdigest(),
        "required_actions": sorted(REQUIRED_ACTIONS),
        "action_counts": {
            action: counts[action] for action in sorted(REQUIRED_ACTIONS)
        },
        "input_record_sha256": sha256(input_record),
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print("packaged physical-controller verification passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
