#!/usr/bin/env python3
"""Prove packaged-release failures are useful without exposing private paths."""

from __future__ import annotations

import argparse
import importlib.util
import sys
import tempfile
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--verifier", type=Path, required=True)
    args = parser.parse_args()

    spec = importlib.util.spec_from_file_location(
        "crystal_packaged_release_verifier",
        args.verifier,
    )
    if spec is None or spec.loader is None:
        raise RuntimeError("could not load packaged release verifier")
    verifier = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(verifier)

    with tempfile.TemporaryDirectory(prefix="crystal-verifier-diagnostics-") as raw:
        private_root = Path(raw).resolve()
        private_marker = private_root / "private-rom.gbc"
        command = [
            sys.executable,
            "-c",
            (
                "import sys; "
                f"print('failure at {private_marker}'); "
                "raise SystemExit(7)"
            ),
        ]
        try:
            verifier.run(
                command,
                cwd=private_root,
                stage="diagnostic probe",
                redactions=(private_root, private_marker),
            )
        except RuntimeError as error:
            message = str(error)
        else:
            raise RuntimeError("failing diagnostic probe unexpectedly passed")

        if (
            "diagnostic probe (exit 7)" not in message
            or "redacted command output tail" not in message
            or "<private-path>" not in message
            or str(private_root) in message
            or str(private_marker) in message
        ):
            raise RuntimeError(f"unsafe or incomplete verifier diagnostic: {message}")

    print("Crystal packaged verifier diagnostics are bounded and path-redacted")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
