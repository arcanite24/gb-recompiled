#!/usr/bin/env python3
"""Create a deterministic, ROM-free Crystal Recompiled platform package."""

from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import shutil
import stat
import sys
import tarfile
import zipfile
from pathlib import Path
from typing import Any


SOURCE_MANIFEST = "SOURCE-MANIFEST.json"
SDK_MANIFEST = "gbrecomp-release.json"
PACKAGE_MANIFEST = "crystal-release.json"
ARCHIVE_ROOT = "crystal-recompiled"
FORBIDDEN_SOURCE_SUFFIXES = {
    ".a",
    ".dll",
    ".dylib",
    ".exe",
    ".gb",
    ".gbc",
    ".o",
    ".obj",
    ".rtc",
    ".sav",
    ".so",
}


class ReleaseError(RuntimeError):
    pass


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_object(path: Path, label: str) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ReleaseError(f"cannot read {label}") from error
    if not isinstance(value, dict):
        raise ReleaseError(f"{label} is not an object")
    return value


def verify_inventory(
    root: Path,
    manifest: dict[str, Any],
    *,
    manifest_name: str,
) -> None:
    entries = manifest.get("files")
    if not isinstance(entries, list) or not entries:
        raise ReleaseError("input inventory is empty")
    expected: set[str] = set()
    for entry in entries:
        if (
            not isinstance(entry, dict)
            or set(entry) != {"path", "size", "sha256"}
            or not isinstance(entry["path"], str)
            or not isinstance(entry["size"], int)
            or not isinstance(entry["sha256"], str)
        ):
            raise ReleaseError("input inventory entry is malformed")
        relative = Path(entry["path"])
        if relative.is_absolute() or ".." in relative.parts:
            raise ReleaseError("input inventory path is unsafe")
        path = root / relative
        if (
            entry["path"] in expected
            or not path.is_file()
            or path.is_symlink()
            or path.stat().st_size != entry["size"]
            or sha256(path) != entry["sha256"]
        ):
            raise ReleaseError("input inventory file mismatch")
        expected.add(entry["path"])
    actual = {
        path.relative_to(root).as_posix()
        for path in root.rglob("*")
        if path.is_file() and path.name != manifest_name
    }
    if actual != expected:
        raise ReleaseError("input inventory has missing or unexpected files")


def package_inventory(root: Path) -> list[dict[str, object]]:
    entries = []
    for path in sorted(candidate for candidate in root.rglob("*") if candidate.is_file()):
        relative = path.relative_to(root).as_posix()
        if relative == PACKAGE_MANIFEST:
            continue
        if path.is_symlink():
            raise ReleaseError("package may not contain symlinks")
        entries.append(
            {
                "path": relative,
                "size": path.stat().st_size,
                "sha256": sha256(path),
            }
        )
    return entries


def normalized_mode(path: Path) -> int:
    if path.is_dir():
        return 0o755
    if path.name in {"launch-crystal.sh", "launch-crystal.command"}:
        return 0o755
    if path.name in {"gbrecomp", "gbrecomp.exe"}:
        return 0o755
    return 0o644


def write_tar_gz(root: Path, archive: Path) -> None:
    with archive.open("wb") as raw:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode="w", format=tarfile.PAX_FORMAT) as tar:
                paths = [root, *sorted(root.rglob("*"))]
                for path in paths:
                    relative = Path(ARCHIVE_ROOT)
                    if path != root:
                        relative /= path.relative_to(root)
                    info = tar.gettarinfo(str(path), arcname=relative.as_posix())
                    info.uid = 0
                    info.gid = 0
                    info.uname = ""
                    info.gname = ""
                    info.mtime = 0
                    info.mode = normalized_mode(path)
                    if path.is_file():
                        with path.open("rb") as handle:
                            tar.addfile(info, handle)
                    else:
                        tar.addfile(info)


def write_zip(root: Path, archive: Path) -> None:
    with zipfile.ZipFile(
        archive, mode="w", compression=zipfile.ZIP_DEFLATED, compresslevel=9
    ) as bundle:
        for path in sorted(candidate for candidate in root.rglob("*") if candidate.is_file()):
            relative = Path(ARCHIVE_ROOT) / path.relative_to(root)
            info = zipfile.ZipInfo(relative.as_posix(), date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 3
            info.external_attr = (stat.S_IFREG | normalized_mode(path)) << 16
            bundle.writestr(info, path.read_bytes(), compress_type=zipfile.ZIP_DEFLATED)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-tree", type=Path, required=True)
    parser.add_argument("--distribution", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument(
        "--platform", choices=("linux", "macos", "windows"), required=True
    )
    parser.add_argument(
        "--architecture", choices=("x64", "arm64"), required=True
    )
    args = parser.parse_args()

    source = args.source_tree.resolve()
    distribution = args.distribution.resolve()
    output = args.output.resolve()
    archive = args.archive.resolve()
    if output.exists() or archive.exists():
        raise ReleaseError("output or archive already exists")
    source_manifest_path = source / SOURCE_MANIFEST
    sdk_manifest_path = distribution / SDK_MANIFEST
    source_manifest = load_object(source_manifest_path, "source manifest")
    sdk_manifest = load_object(sdk_manifest_path, "SDK manifest")
    if (
        source_manifest.get("schema") != "crystal-recompiled.source-tree"
        or source_manifest.get("version") != 1
        or sdk_manifest.get("schema") != "gb-recompiled.release"
        or sdk_manifest.get("version") != 1
    ):
        raise ReleaseError("unsupported source or SDK manifest")
    verify_inventory(source, source_manifest, manifest_name=SOURCE_MANIFEST)
    verify_inventory(distribution, sdk_manifest, manifest_name=SDK_MANIFEST)

    forbidden_source = [
        path
        for path in source.rglob("*")
        if path.is_file() and path.suffix.lower() in FORBIDDEN_SOURCE_SUFFIXES
    ]
    if forbidden_source:
        raise ReleaseError("source tree contains a private or generated artifact")
    target = sdk_manifest.get("release")
    if (
        not isinstance(target, dict)
        or target.get("platform") != args.platform
        or target.get("architecture") != args.architecture
    ):
        raise ReleaseError("SDK platform/architecture does not match package")
    contract = load_object(
        source
        / "ports"
        / "pokemon-crystal"
        / "standalone"
        / "gbrecomp-contract.json",
        "Crystal SDK contract",
    )
    cli_identity = sdk_manifest.get("cli", {}).get("identity")
    runtime = sdk_manifest.get("runtime")
    if (
        not isinstance(cli_identity, dict)
        or not isinstance(runtime, dict)
        or cli_identity.get("version") != contract.get("tool_version")
        or cli_identity.get("abis") != contract.get("abis")
        or not set(contract.get("required_features", []))
        .issubset(set(cli_identity.get("features", [])))
        or runtime.get("tree_sha256") != contract.get("runtime_tree_sha256")
    ):
        raise ReleaseError("SDK does not satisfy the Crystal contract")

    shutil.copytree(source, output)
    shutil.copytree(distribution, output / "sdk" / "gb-recompiled")
    shell = (
        "#!/bin/sh\n"
        'exec python3 "$(dirname "$0")/ports/pokemon-crystal/scripts/launch.py" "$@"\n'
    )
    (output / "launch-crystal.sh").write_text(shell, encoding="utf-8", newline="\n")
    (output / "launch-crystal.command").write_text(
        shell, encoding="utf-8", newline="\n"
    )
    batch = (
        "@echo off\r\n"
        'py -3 "%~dp0ports\\pokemon-crystal\\scripts\\launch.py" %*\r\n'
    )
    (output / "launch-crystal.bat").write_text(
        batch, encoding="utf-8", newline=""
    )
    for name in ("launch-crystal.sh", "launch-crystal.command"):
        path = output / name
        path.chmod(path.stat().st_mode | 0o111)

    payload = {
        "schema": "crystal-recompiled.release",
        "version": 1,
        "release": {
            "platform": args.platform,
            "architecture": args.architecture,
            "rom_included": False,
            "generated_game_included": False,
        },
        "entrypoints": {
            "posix": "launch-crystal.sh",
            "macos": "launch-crystal.command",
            "windows": "launch-crystal.bat",
        },
        "source_manifest_sha256": sha256(source_manifest_path),
        "sdk_manifest_sha256": sha256(sdk_manifest_path),
        "sdk_identity": cli_identity,
        "files": package_inventory(output),
    }
    manifest = output / PACKAGE_MANIFEST
    manifest.write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    archive.parent.mkdir(parents=True, exist_ok=True)
    if args.platform == "windows":
        write_zip(output, archive)
    else:
        write_tar_gz(output, archive)
    print("ok  Crystal Recompiled platform package")
    print(f"    files={len(payload['files'])}")
    print(f"    manifest_sha256={sha256(manifest)}")
    print(f"    archive_sha256={sha256(archive)}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ReleaseError, shutil.Error) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
