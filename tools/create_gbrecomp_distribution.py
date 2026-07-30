#!/usr/bin/env python3
"""Create a versioned, provenance-complete GB Recompiled distribution."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any


MANIFEST_NAME = "gbrecomp-release.json"
IGNORED_NAMES = {".DS_Store", "__pycache__", "build", "build_*"}


class DistributionError(RuntimeError):
    pass


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def normalized_text_bytes(path: Path) -> bytes:
    content = path.read_bytes()
    if b"\0" not in content:
        content = content.replace(b"\r\n", b"\n")
    return content


def tree_sha256(root: Path, *, clean_source: bool = False) -> str:
    digest = hashlib.sha256()
    for path in sorted(candidate for candidate in root.rglob("*") if candidate.is_file()):
        relative = path.relative_to(root)
        if ".DS_Store" in relative.parts or "__pycache__" in relative.parts:
            continue
        if clean_source and any(
            part == "build" or part.startswith("build_")
            for part in relative.parts
        ):
            continue
        if path.suffix == ".pyc":
            continue
        content = normalized_text_bytes(path)
        encoded = relative.as_posix().encode("utf-8")
        digest.update(len(encoded).to_bytes(4, "big"))
        digest.update(encoded)
        digest.update(len(content).to_bytes(8, "big"))
        digest.update(hashlib.sha256(content).digest())
    return digest.hexdigest()


def normalize_runtime_text_files(root: Path) -> None:
    for path in sorted(candidate for candidate in root.rglob("*") if candidate.is_file()):
        content = path.read_bytes()
        normalized = content.replace(b"\r\n", b"\n") if b"\0" not in content else content
        if normalized != content:
            path.write_bytes(normalized)


def require_file(path: Path, label: str, *, executable: bool = False) -> Path:
    resolved = path.expanduser().resolve()
    if not resolved.is_file():
        raise DistributionError(f"missing {label}: {resolved}")
    if resolved.is_symlink():
        raise DistributionError(f"{label} may not be a symlink: {resolved}")
    if executable and not os.access(resolved, os.X_OK):
        raise DistributionError(f"{label} is not executable: {resolved}")
    return resolved


def ignore_runtime(_directory: str, names: list[str]) -> set[str]:
    ignored: set[str] = set()
    for name in names:
        if (
            name == ".DS_Store"
            or name == "__pycache__"
            or name == "build"
            or name.startswith("build_")
            or name.endswith(".pyc")
        ):
            ignored.add(name)
    return ignored


def load_version(executable: Path) -> dict[str, Any]:
    completed = subprocess.run(
        [str(executable), "--version-json"],
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if completed.returncode != 0:
        raise DistributionError(
            f"gbrecomp --version-json exited {completed.returncode}: "
            f"{completed.stderr.strip()}"
        )
    try:
        value = json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        raise DistributionError("gbrecomp returned malformed version JSON") from error
    expected_keys = {"schema", "schema_version", "version", "abis", "features"}
    if (
        not isinstance(value, dict)
        or set(value) != expected_keys
        or value.get("schema") != "gbrecomp.version"
        or value.get("schema_version") != 1
        or not isinstance(value.get("version"), str)
        or not isinstance(value.get("abis"), dict)
        or not isinstance(value.get("features"), list)
    ):
        raise DistributionError("gbrecomp returned unsupported version identity")
    return value


def inventory(root: Path) -> list[dict[str, object]]:
    files = []
    for path in sorted(candidate for candidate in root.rglob("*") if candidate.is_file()):
        relative = path.relative_to(root).as_posix()
        if relative == MANIFEST_NAME:
            continue
        if path.is_symlink():
            raise DistributionError(f"distribution contains a symlink: {relative}")
        files.append(
            {
                "path": relative,
                "size": path.stat().st_size,
                "sha256": sha256(path),
            }
        )
    return files


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--gbrecomp", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--license", type=Path, required=True)
    parser.add_argument("--readme", type=Path)
    parser.add_argument(
        "--extra-file",
        action="append",
        default=[],
        metavar="SOURCE=DESTINATION",
        help="copy an additional platform file into a safe relative path",
    )
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--platform", dest="platform_name")
    parser.add_argument("--architecture")
    args = parser.parse_args()

    gbrecomp = require_file(args.gbrecomp, "gbrecomp", executable=True)
    runtime = args.runtime.expanduser().resolve()
    if not runtime.is_dir() or runtime.is_symlink():
        raise DistributionError(f"invalid runtime source: {runtime}")
    required_runtime = (
        runtime / "CMakeLists.txt",
        runtime / "include/gbrt.h",
        runtime / "include/gbrt_presentation.h",
        runtime / "src/gbrt.c",
        runtime / "src/gbrt_presentation.c",
        runtime / "vendor/imgui/LICENSE.txt",
    )
    for path in required_runtime:
        require_file(path, "runtime file")
    license_path = require_file(args.license, "license")
    readme = require_file(args.readme, "readme") if args.readme else None
    version = load_version(gbrecomp)

    output = args.output.expanduser().resolve()
    if output.exists():
        raise DistributionError(f"output already exists: {output}")
    output.mkdir(parents=True)
    executable_name = "gbrecomp.exe" if gbrecomp.suffix.lower() == ".exe" else "gbrecomp"
    copied_executable = output / executable_name
    shutil.copy2(gbrecomp, copied_executable)
    copied_executable.chmod(copied_executable.stat().st_mode | 0o111)
    shutil.copytree(runtime, output / "runtime", ignore=ignore_runtime)
    normalize_runtime_text_files(output / "runtime")
    shutil.copy2(license_path, output / "LICENSE")
    if readme is not None:
        shutil.copy2(readme, output / "README.md")
    for specification in args.extra_file:
        if "=" not in specification:
            raise DistributionError("--extra-file requires SOURCE=DESTINATION")
        source_raw, destination_raw = specification.split("=", 1)
        source = require_file(Path(source_raw), "extra file")
        destination_relative = Path(destination_raw)
        if (
            destination_relative.is_absolute()
            or ".." in destination_relative.parts
            or not destination_relative.parts
        ):
            raise DistributionError("extra-file destination is unsafe")
        destination = output / destination_relative
        if destination.exists():
            raise DistributionError(f"extra-file destination exists: {destination}")
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)

    source_runtime_hash = tree_sha256(runtime, clean_source=True)
    copied_runtime_hash = tree_sha256(output / "runtime")
    if source_runtime_hash != copied_runtime_hash:
        raise DistributionError("copied runtime differs from source runtime")

    payload = {
        "schema": "gb-recompiled.release",
        "version": 1,
        "release": {
            "tool_version": version["version"],
            "platform": args.platform_name or sys.platform,
            "architecture": args.architecture or platform.machine(),
        },
        "cli": {
            "path": executable_name,
            "sha256": sha256(copied_executable),
            "identity": version,
        },
        "runtime": {
            "path": "runtime",
            "tree_sha256": copied_runtime_hash,
        },
        "files": inventory(output),
    }
    manifest = output / MANIFEST_NAME
    manifest.write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(f"ok  distribution: {output}")
    print(f"    version={version['version']}")
    print(f"    runtime_tree_sha256={copied_runtime_hash}")
    print(f"    manifest_sha256={sha256(manifest)}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (DistributionError, OSError, subprocess.SubprocessError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
