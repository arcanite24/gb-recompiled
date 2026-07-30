#!/usr/bin/env python3
"""Install a verified GB Recompiled distribution into a standalone checkout."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any


SCRIPT_DIR = Path(__file__).resolve().parent
PORT_DIR = SCRIPT_DIR.parent
REPO_ROOT = PORT_DIR.parent.parent
CONTRACT_PATH = PORT_DIR / "standalone" / "gbrecomp-contract.json"
RELEASE_MANIFEST = "gbrecomp-release.json"


class BootstrapError(RuntimeError):
    pass


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def tree_sha256(root: Path) -> str:
    digest = hashlib.sha256()
    for path in sorted(candidate for candidate in root.rglob("*") if candidate.is_file()):
        relative = path.relative_to(root)
        if ".DS_Store" in relative.parts or "__pycache__" in relative.parts:
            continue
        encoded = relative.as_posix().encode("utf-8")
        digest.update(len(encoded).to_bytes(4, "big"))
        digest.update(encoded)
        digest.update(path.stat().st_size.to_bytes(8, "big"))
        digest.update(bytes.fromhex(sha256(path)))
    return digest.hexdigest()


def load_object(path: Path, label: str) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise BootstrapError(f"cannot read {label}: {path}") from error
    if not isinstance(value, dict):
        raise BootstrapError(f"{label} root is not an object")
    return value


def verify_inventory(distribution: Path, manifest: dict[str, Any]) -> None:
    entries = manifest.get("files")
    if not isinstance(entries, list) or not entries:
        raise BootstrapError("release inventory is empty")
    expected: set[str] = set()
    for entry in entries:
        if (
            not isinstance(entry, dict)
            or set(entry) != {"path", "size", "sha256"}
            or not isinstance(entry["path"], str)
            or not isinstance(entry["size"], int)
            or not isinstance(entry["sha256"], str)
        ):
            raise BootstrapError("release inventory entry is malformed")
        relative = Path(entry["path"])
        if relative.is_absolute() or ".." in relative.parts:
            raise BootstrapError("release inventory path escapes distribution")
        path = distribution / relative
        if (
            entry["path"] in expected
            or not path.is_file()
            or path.is_symlink()
            or path.stat().st_size != entry["size"]
            or sha256(path) != entry["sha256"]
        ):
            raise BootstrapError(f"release file mismatch: {entry['path']}")
        expected.add(entry["path"])
    actual = {
        path.relative_to(distribution).as_posix()
        for path in distribution.rglob("*")
        if path.is_file() and path.name != RELEASE_MANIFEST
    }
    if actual != expected:
        raise BootstrapError("release inventory has missing or unexpected files")


def verify_identity(
    distribution: Path,
    manifest: dict[str, Any],
    contract: dict[str, Any],
) -> Path:
    if (
        manifest.get("schema") != "gb-recompiled.release"
        or manifest.get("version") != 1
        or contract.get("schema") != "crystal-recompiled.gbrecomp-contract"
        or contract.get("version") != 1
    ):
        raise BootstrapError("unsupported release or compatibility contract")
    verify_inventory(distribution, manifest)
    cli = manifest.get("cli")
    runtime = manifest.get("runtime")
    if not isinstance(cli, dict) or not isinstance(runtime, dict):
        raise BootstrapError("release omitted CLI or runtime identity")
    executable = distribution / str(cli.get("path", ""))
    runtime_dir = distribution / str(runtime.get("path", ""))
    if (
        not executable.is_file()
        or not os.access(executable, os.X_OK)
        or sha256(executable) != cli.get("sha256")
        or not runtime_dir.is_dir()
        or tree_sha256(runtime_dir) != runtime.get("tree_sha256")
    ):
        raise BootstrapError("release CLI or runtime does not match manifest")
    completed = subprocess.run(
        [str(executable), "--version-json"],
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    try:
        identity = json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        raise BootstrapError("release CLI returned malformed identity") from error
    required_features = contract.get("required_features")
    if (
        completed.returncode != 0
        or identity != cli.get("identity")
        or identity.get("version") != contract.get("tool_version")
        or identity.get("abis") != contract.get("abis")
        or not isinstance(required_features, list)
        or not set(required_features).issubset(set(identity.get("features", [])))
        or runtime.get("tree_sha256") != contract.get("runtime_tree_sha256")
    ):
        raise BootstrapError("release is incompatible with Crystal contract")
    return executable


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--distribution", type=Path, required=True)
    references = parser.add_mutually_exclusive_group()
    references.add_argument("--fetch-references", action="store_true")
    references.add_argument("--fetch-all-references", action="store_true")
    args = parser.parse_args()
    distribution = args.distribution.expanduser().resolve()
    if not distribution.is_dir():
        raise BootstrapError(f"missing distribution: {distribution}")
    manifest_path = distribution / RELEASE_MANIFEST
    manifest = load_object(manifest_path, "release manifest")
    contract = load_object(CONTRACT_PATH, "Crystal compatibility contract")
    executable = verify_identity(distribution, manifest, contract)

    target_executable = REPO_ROOT / "build" / "bin" / executable.name
    target_runtime = REPO_ROOT / "runtime"
    receipt_dir = REPO_ROOT / ".crystal"
    if target_executable.exists() or target_runtime.exists() or receipt_dir.exists():
        raise BootstrapError("local dependencies already exist; refusing hidden state")
    reference_scope = (
        "all"
        if args.fetch_all_references
        else "generation"
        if args.fetch_references
        else "none"
    )
    if reference_scope != "none":
        subprocess.run(
            [
                sys.executable,
                str(SCRIPT_DIR / "references.py"),
                "fetch",
                "--scope",
                reference_scope,
            ],
            cwd=REPO_ROOT,
            check=True,
        )
    target_executable.parent.mkdir(parents=True)
    shutil.copy2(executable, target_executable)
    target_executable.chmod(target_executable.stat().st_mode | 0o111)
    shutil.copytree(distribution / manifest["runtime"]["path"], target_runtime)
    receipt_dir.mkdir()
    receipt = {
        "schema": "crystal-recompiled.dependencies",
        "version": 1,
        "release_manifest_sha256": sha256(manifest_path),
        "tool_version": contract["tool_version"],
        "cli_sha256": sha256(target_executable),
        "runtime_tree_sha256": tree_sha256(target_runtime),
        "references_scope": reference_scope,
    }
    (receipt_dir / "dependencies.json").write_text(
        json.dumps(receipt, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(f"ok  compatible GB Recompiled {contract['tool_version']} installed")
    print(f"    release_manifest_sha256={receipt['release_manifest_sha256']}")
    print(f"    runtime_tree_sha256={receipt['runtime_tree_sha256']}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (BootstrapError, OSError, subprocess.SubprocessError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
