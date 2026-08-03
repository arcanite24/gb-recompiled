#!/usr/bin/env python3
"""Test the controller verifier's fail-closed, path-free parsing contract."""

from __future__ import annotations

import argparse
import importlib.util
import subprocess
import sys
from pathlib import Path


def load_module(path: Path):
    spec = importlib.util.spec_from_file_location("controller_verifier", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load controller verifier")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--verifier", type=Path, required=True)
    args = parser.parse_args()
    verifier = args.verifier.resolve()
    module = load_module(verifier)

    counts = module.input_action_counts("c10:UA:20,c40:DLRB:30,c80:ST:40\n")
    expected = {
        "U": 1,
        "D": 1,
        "L": 1,
        "R": 1,
        "A": 1,
        "B": 1,
        "S": 1,
        "T": 1,
    }
    if counts != expected:
        raise RuntimeError(f"unexpected action counts: {counts}")
    if module.input_action_counts("p10-20/5:A:2"):
        raise RuntimeError("record parser accepted a non-recorded periodic token")
    if module.input_action_counts("c10:A:/private/rom.gbc"):
        raise RuntimeError("record parser accepted malformed/private content")

    controller = module.find_controller_line(
        b"",
        b"[SDL] Controller: 8BitDo Ultimate 2C Wired Controller [Unknown]\n",
    )
    if controller is None or controller.group(1) != b"Unknown":
        raise RuntimeError("controller verifier did not inspect the runtime log")
    if module.find_controller_line(b"[SDL] Controller: malformed\n") is not None:
        raise RuntimeError("controller verifier accepted a malformed controller line")

    rejected = subprocess.run(
        [
            sys.executable,
            str(verifier),
            "--package-root",
            "/must/not/exist/package",
            "--cache",
            "/must/not/exist/cache",
            "--output",
            "/must/not/exist/result.json",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
        text=True,
    )
    if rejected.returncode == 0 or "attestation is required" not in rejected.stderr:
        raise RuntimeError("controller verifier did not require operator attestation")

    print("controller verifier is fail-closed and parses all required actions")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
