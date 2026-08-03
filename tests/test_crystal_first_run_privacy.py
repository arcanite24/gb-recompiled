#!/usr/bin/env python3
"""Verify unsupported first-run input fails before cache creation."""

from __future__ import annotations

import argparse
import contextlib
import importlib.util
import io
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--first-run", type=Path, required=True)
    parser.add_argument("--privacy-verifier", type=Path, required=True)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="crystal-first-run-test-") as raw:
        root = Path(raw)
        secret = "private-user-selected-name"
        rom = root / f"{secret}.gbc"
        rom.write_bytes(b"\x00" * 2_097_152)
        cache = root / "private-cache"
        result = subprocess.run(
            [
                sys.executable,
                str(args.first_run),
                "--rom",
                str(rom),
                "--cache-dir",
                str(cache),
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=30,
        )
        if result.returncode != 2:
            raise RuntimeError(
                f"unsupported ROM returned {result.returncode}: {result.stderr}"
            )
        if cache.exists():
            raise RuntimeError("unsupported ROM created a private cache")
        retained = result.stdout + result.stderr
        if secret in retained or str(root) in retained:
            raise RuntimeError("unsupported-ROM diagnostics disclosed a private path")
        expected_code = '"code":"unsupported-rom"'
        if expected_code not in retained:
            raise RuntimeError(f"missing stable rejection code: {retained}")

        spec = importlib.util.spec_from_file_location(
            "crystal_first_run",
            args.first_run,
        )
        if spec is None or spec.loader is None:
            raise RuntimeError("could not load first-run module")
        first_run = importlib.util.module_from_spec(spec)
        sys.path.insert(0, str(args.first_run.parent))
        try:
            spec.loader.exec_module(first_run)
        finally:
            sys.path.pop(0)
        challenge_args = first_run.generation_feature_args()
        if (
            len(challenge_args) != 2
            or challenge_args[0] != "--native-patch"
            or Path(challenge_args[1]).name != "manifest.json"
            or not Path(challenge_args[1]).is_file()
            or Path(challenge_args[1]).parent.name != "challenge-mode"
        ):
            raise RuntimeError("first run does not compile the Challenge patch")

        launch_path = args.first_run.parent / "launch.py"
        launch_spec = importlib.util.spec_from_file_location(
            "crystal_launch",
            launch_path,
        )
        if launch_spec is None or launch_spec.loader is None:
            raise RuntimeError("could not load launch module")
        launch = importlib.util.module_from_spec(launch_spec)
        sys.path.insert(0, str(args.first_run.parent))
        try:
            launch_spec.loader.exec_module(launch)
        finally:
            sys.path.pop(0)
        configuration = launch.host_configuration_path(cache)
        if (
            configuration != cache / "configuration" / "challenge-v1.json"
            or configuration.exists()
        ):
            raise RuntimeError("launcher does not use an external Challenge config")

        diagnostics = io.StringIO()
        try:
            with contextlib.redirect_stderr(diagnostics):
                first_run.run_private(
                    [
                        sys.executable,
                        "-c",
                        (
                            "import sys; "
                            f"print('compile failed at {rom}'); "
                            "raise SystemExit(7)"
                        ),
                    ],
                    redactions=(root, rom),
                )
        except subprocess.CalledProcessError as error:
            if error.returncode != 7:
                raise
        else:
            raise RuntimeError("failing private command unexpectedly passed")
        diagnostic_text = diagnostics.getvalue()
        if (
            "redacted private command output tail" not in diagnostic_text
            or "<private-path>" not in diagnostic_text
            or secret in diagnostic_text
            or str(root) in diagnostic_text
        ):
            raise RuntimeError(
                f"unsafe or incomplete private command diagnostic: {diagnostic_text}"
            )

        privacy_spec = importlib.util.spec_from_file_location(
            "crystal_first_run_privacy",
            args.privacy_verifier,
        )
        if privacy_spec is None or privacy_spec.loader is None:
            raise RuntimeError("could not load first-run privacy verifier")
        privacy = importlib.util.module_from_spec(privacy_spec)
        privacy_spec.loader.exec_module(privacy)

        public_tree = root / "public-tree"
        public_tree.mkdir()
        (public_tree / "safe.txt").write_text("public data\n", encoding="utf-8")
        rom_bytes = b"exact-private-rom-payload"
        selected_path = str(rom).encode("utf-8")
        if privacy.audit_public_tree(
            public_tree,
            selected_path_bytes=selected_path,
            rom_bytes=rom_bytes,
        ) != 1:
            raise RuntimeError("privacy verifier miscounted safe public files")

        controls = (
            ("path.bin", b"prefix" + selected_path + b"suffix", "ROM path"),
            ("rom.bin", b"prefix" + rom_bytes + b"suffix", "ROM bytes"),
            ("save.sav", b"save", "private/derived artifact"),
        )
        for name, payload, expected in controls:
            control = public_tree / name
            control.write_bytes(payload)
            try:
                privacy.audit_public_tree(
                    public_tree,
                    selected_path_bytes=selected_path,
                    rom_bytes=rom_bytes,
                )
            except RuntimeError as error:
                if expected not in str(error):
                    raise RuntimeError(
                        f"privacy control {name} failed for the wrong reason: {error}"
                    ) from error
            else:
                raise RuntimeError(f"privacy verifier accepted {name}")
            control.unlink()

    print("first run rejects unsupported ROMs before cache creation without path disclosure")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
