#!/usr/bin/env python3
"""Prove Crystal platform packages are deterministic, inventoried, and relocatable."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zipfile
from pathlib import Path


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run(command: list[str], *, expect_success: bool = True) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        command,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=180,
    )
    if expect_success != (result.returncode == 0):
        raise RuntimeError(
            f"unexpected command result {result.returncode}: {result.stdout}{result.stderr}"
        )
    return result


def target_identity() -> tuple[str, str]:
    if sys.platform == "darwin":
        target_platform = "macos"
    elif os.name == "nt":
        target_platform = "windows"
    else:
        target_platform = "linux"
    machine = platform.machine().lower()
    architecture = "arm64" if machine in {"arm64", "aarch64"} else "x64"
    return target_platform, architecture


def verify_package(root: Path) -> dict[str, object]:
    manifest = json.loads(
        (root / "crystal-release.json").read_text(encoding="utf-8")
    )
    if (
        manifest.get("schema") != "crystal-recompiled.release"
        or manifest.get("version") != 1
        or manifest.get("release", {}).get("rom_included") is not False
        or manifest.get("release", {}).get("generated_game_included") is not False
    ):
        raise RuntimeError("invalid Crystal release manifest")
    expected = set()
    for entry in manifest["files"]:
        path = root / entry["path"]
        if (
            entry["path"] in expected
            or not path.is_file()
            or path.stat().st_size != entry["size"]
            or sha256(path) != entry["sha256"]
        ):
            raise RuntimeError("Crystal release inventory mismatch")
        expected.add(entry["path"])
    actual = {
        path.relative_to(root).as_posix()
        for path in root.rglob("*")
        if path.is_file() and path.name != "crystal-release.json"
    }
    if actual != expected:
        raise RuntimeError("Crystal release contains uninventoried files")
    forbidden = [
        path
        for path in root.rglob("*")
        if path.is_file()
        and path.suffix.lower() in {".gb", ".gbc", ".sav", ".rtc", ".o", ".obj"}
    ]
    if forbidden:
        raise RuntimeError("Crystal release contains private/derived game content")
    required_notices = {
        "ports/pokemon-crystal/LEGAL.md",
        "ports/pokemon-crystal/RELEASE.md",
        "ports/pokemon-crystal/THIRD_PARTY_NOTICES.md",
        "sdk/gb-recompiled/runtime/vendor/imgui/LICENSE.txt",
    }
    missing_notices = required_notices - expected
    if missing_notices:
        raise RuntimeError(
            f"Crystal release is missing required notices: {sorted(missing_notices)}"
        )
    if manifest["release"]["platform"] == "windows":
        windows_notices = {
            "sdk/gb-recompiled/SDL2.dll",
            "sdk/gb-recompiled/THIRD_PARTY/SDL2-LICENSE.txt",
        }
        missing_windows = windows_notices - expected
        if missing_windows:
            raise RuntimeError(
                f"Windows release is missing SDL2 runtime/notices: {sorted(missing_windows)}"
            )
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--gbrecomp", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--license", type=Path, required=True)
    parser.add_argument("--readme", type=Path, required=True)
    parser.add_argument("--distribution-tool", type=Path, required=True)
    parser.add_argument("--eject-script", type=Path, required=True)
    parser.add_argument("--packager", type=Path, required=True)
    args = parser.parse_args()
    target_platform, architecture = target_identity()

    with tempfile.TemporaryDirectory(prefix="crystal-release-package-") as raw:
        root = Path(raw)
        distribution = root / "distribution"
        source = root / "source"
        run(
            [
                sys.executable,
                str(args.distribution_tool),
                "--gbrecomp",
                str(args.gbrecomp),
                "--runtime",
                str(args.runtime),
                "--license",
                str(args.license),
                "--readme",
                str(args.readme),
                "--output",
                str(distribution),
                "--platform",
                target_platform,
                "--architecture",
                architecture,
            ]
        )
        run(
            [
                sys.executable,
                str(args.eject_script),
                "--output",
                str(source),
            ]
        )

        archives = []
        packages = []
        extension = ".zip" if target_platform == "windows" else ".tar.gz"
        for index in range(2):
            package = root / f"package-{index}"
            archive = root / f"crystal-{index}{extension}"
            run(
                [
                    sys.executable,
                    str(args.packager),
                    "--source-tree",
                    str(source),
                    "--distribution",
                    str(distribution),
                    "--output",
                    str(package),
                    "--archive",
                    str(archive),
                    "--platform",
                    target_platform,
                    "--architecture",
                    architecture,
                ]
            )
            verify_package(package)
            packages.append(package)
            archives.append(archive)
        if sha256(archives[0]) != sha256(archives[1]):
            raise RuntimeError("equivalent Crystal packages produced different archives")

        extracted = root / "extracted"
        extracted.mkdir()
        if target_platform == "windows":
            with zipfile.ZipFile(archives[0]) as bundle:
                bundle.extractall(extracted)
        else:
            with tarfile.open(archives[0], mode="r:gz") as bundle:
                bundle.extractall(extracted)
        relocated = root / "relocated" / "crystal-recompiled"
        shutil.copytree(extracted / "crystal-recompiled", relocated)
        verify_package(relocated)
        launch = (
            relocated
            / "ports"
            / "pokemon-crystal"
            / "scripts"
            / "launch.py"
        )
        help_result = run([sys.executable, str(launch), "--help"])
        if "--prepare-only" not in help_result.stdout:
            raise RuntimeError("relocated launcher did not expose its CLI")
        verifier = (
            relocated
            / "ports"
            / "pokemon-crystal"
            / "scripts"
            / "verify_packaged_release.py"
        )
        verifier_help = run([sys.executable, str(verifier), "--help"])
        if "--package-root" not in verifier_help.stdout:
            raise RuntimeError("relocated exact-ROM verifier did not expose its CLI")
        controller_verifier = (
            relocated
            / "ports"
            / "pokemon-crystal"
            / "scripts"
            / "verify_controller_release.py"
        )
        controller_help = run(
            [sys.executable, str(controller_verifier), "--help"]
        )
        if "--attest-controller-only" not in controller_help.stdout:
            raise RuntimeError(
                "relocated physical-controller verifier did not expose its CLI"
            )

        tampered = root / "tampered-source"
        shutil.copytree(source, tampered)
        with (tampered / "README.md").open("a", encoding="utf-8") as handle:
            handle.write("\ntampered\n")
        run(
            [
                sys.executable,
                str(args.packager),
                "--source-tree",
                str(tampered),
                "--distribution",
                str(distribution),
                "--output",
                str(root / "must-not-exist"),
                "--archive",
                str(root / f"must-not-exist{extension}"),
                "--platform",
                target_platform,
                "--architecture",
                architecture,
            ],
            expect_success=False,
        )
        if (root / "must-not-exist").exists():
            raise RuntimeError("tampered source created a partial package")

    print("Crystal release package is deterministic, inventoried, and relocatable")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
