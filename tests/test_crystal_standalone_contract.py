#!/usr/bin/env python3
"""Exercise Crystal standalone ejection, SDK install, and tamper rejection."""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def run(command: list[str], *, expect_success: bool = True) -> subprocess.CompletedProcess[str]:
    completed = subprocess.run(
        command,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if expect_success != (completed.returncode == 0):
        raise AssertionError(
            f"unexpected exit {completed.returncode}: {' '.join(command)}\n"
            f"{completed.stdout}"
        )
    return completed


def eject(script: Path, output: Path) -> None:
    run([sys.executable, str(script), "--output", str(output)])
    manifest = json.loads((output / "SOURCE-MANIFEST.json").read_text())
    paths = {entry["path"] for entry in manifest["files"]}
    if (
        manifest.get("schema") != "crystal-recompiled.source-tree"
        or manifest.get("version") != 1
        or manifest.get("file_count") != len(paths)
        or not paths
        or any(
            Path(path).suffix.lower()
            in {".gb", ".gbc", ".sav", ".rtc", ".exe", ".pyc"}
            for path in paths
        )
        or any(
            "/references/vendor/" in f"/{path}/"
            or "/references/generated/" in f"/{path}/"
            for path in paths
        )
    ):
        raise AssertionError("ejected source inventory is unsafe or malformed")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--gbrecomp", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--license", type=Path, required=True)
    parser.add_argument("--readme", type=Path, required=True)
    parser.add_argument("--distribution-tool", type=Path, required=True)
    parser.add_argument("--eject-script", type=Path, required=True)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="crystal-standalone-contract-") as raw:
        root = Path(raw)
        distribution = root / "distribution"
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
            ]
        )
        release = json.loads(
            (distribution / "gbrecomp-release.json").read_text()
        )
        if (
            release.get("schema") != "gb-recompiled.release"
            or release.get("release", {}).get("tool_version") != "0.1.0"
            or release.get("cli", {}).get("identity", {}).get("abis")
            != {
                "data_mod": 1,
                "native_patch": 1,
                "port_extension": 1,
                "port_module": 2,
                "presentation": 1,
                "semantic": 1,
            }
        ):
            raise AssertionError("release identity does not match Crystal ABI")

        standalone = root / "standalone"
        eject(args.eject_script, standalone)
        bootstrap = standalone / "ports/pokemon-crystal/scripts/bootstrap.py"
        run(
            [
                sys.executable,
                str(bootstrap),
                "--distribution",
                str(distribution),
            ]
        )
        installed = standalone / "build/bin" / args.gbrecomp.name
        if not installed.is_file() or not (standalone / "runtime/include/gbrt.h").is_file():
            raise AssertionError("bootstrap omitted the verified SDK")
        version = json.loads(
            run([str(installed), "--version-json"]).stdout
        )
        if version != release["cli"]["identity"]:
            raise AssertionError("installed CLI identity changed")

        tampered_distribution = root / "tampered-distribution"
        shutil.copytree(distribution, tampered_distribution)
        with (tampered_distribution / "runtime/include/gbrt.h").open(
            "a", encoding="utf-8"
        ) as handle:
            handle.write("\n/* tampered */\n")
        tampered_tree = root / "tampered-tree"
        eject(args.eject_script, tampered_tree)
        rejected = run(
            [
                sys.executable,
                str(tampered_tree / "ports/pokemon-crystal/scripts/bootstrap.py"),
                "--distribution",
                str(tampered_distribution),
            ],
            expect_success=False,
        )
        if "release file mismatch" not in rejected.stdout:
            raise AssertionError("tampered release did not fail at inventory")
        if (tampered_tree / "runtime").exists():
            raise AssertionError("tampered release installed runtime state")

        wrong_abi_tree = root / "wrong-abi-tree"
        eject(args.eject_script, wrong_abi_tree)
        contract_path = (
            wrong_abi_tree
            / "ports/pokemon-crystal/standalone/gbrecomp-contract.json"
        )
        contract = json.loads(contract_path.read_text())
        contract["abis"]["presentation"] = 99
        contract_path.write_text(json.dumps(contract), encoding="utf-8")
        rejected = run(
            [
                sys.executable,
                str(wrong_abi_tree / "ports/pokemon-crystal/scripts/bootstrap.py"),
                "--distribution",
                str(distribution),
            ],
            expect_success=False,
        )
        if "incompatible with Crystal contract" not in rejected.stdout:
            raise AssertionError("wrong ABI did not fail at compatibility")
        if (wrong_abi_tree / "runtime").exists():
            raise AssertionError("incompatible release installed runtime state")

    print("Crystal standalone distribution, ejection, and bootstrap contract passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
