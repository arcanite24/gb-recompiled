#!/usr/bin/env python3
"""Eject the original Crystal port sources into a standalone repository."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import sys
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
PORT_DIR = SCRIPT_DIR.parent
REPO_ROOT = PORT_DIR.parent.parent
PUBLIC_FILES = {
    "BACKLOG.md",
    "LEGAL.md",
    "NATIVE_EXTENSIONS.md",
    "PACKAGING.md",
    "PLAN.md",
    "PRESENTATION.md",
    "README.md",
    "RELEASE.md",
    "REFERENCES.md",
    "SEMANTIC_ANCHORS.md",
    "THIRD_PARTY_NOTICES.md",
}
PUBLIC_DIRS = {
    "annotations",
    "assets",
    "evidence",
    "mods",
    "module",
    "native-extensions",
    "native-patches",
    "replay",
    "route",
    "scripts",
    "semantic",
    "standalone",
    "tests",
    "tools",
}
REFERENCE_FILES = {
    "references/.gitignore",
    "references/README.md",
    "references/sources.lock.json",
}
ROOT_TOOLS = {
    "compile_crystal_data_mod.py",
    "create_data_mod_replay.py",
    "validate_data_mods.py",
}
FORBIDDEN_SUFFIXES = {
    ".gb",
    ".gbc",
    ".sav",
    ".rtc",
    ".o",
    ".obj",
    ".a",
    ".so",
    ".dll",
    ".dylib",
    ".exe",
    ".pyc",
}


class EjectError(RuntimeError):
    pass


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def is_public(relative: Path) -> bool:
    posix = relative.as_posix()
    if posix in PUBLIC_FILES or posix in REFERENCE_FILES:
        return True
    if not relative.parts or relative.parts[0] not in PUBLIC_DIRS:
        return False
    if relative.parts[0] == "references":
        return False
    return not (
        "__pycache__" in relative.parts
        or relative.suffix == ".pyc"
        or ".DS_Store" in relative.parts
    )


def copy_file(source: Path, destination: Path) -> None:
    if source.is_symlink():
        raise EjectError(f"public source may not be a symlink: {source}")
    if source.suffix.lower() in FORBIDDEN_SUFFIXES:
        raise EjectError(f"forbidden release file: {source}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def source_inventory(root: Path) -> list[dict[str, object]]:
    items = []
    for path in sorted(candidate for candidate in root.rglob("*") if candidate.is_file()):
        relative = path.relative_to(root).as_posix()
        if relative == "SOURCE-MANIFEST.json":
            continue
        items.append(
            {
                "path": relative,
                "size": path.stat().st_size,
                "sha256": sha256(path),
            }
        )
    return items


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.expanduser().resolve()
    if output.exists():
        raise EjectError(f"output already exists: {output}")
    output.mkdir(parents=True)

    copied = 0
    for source in sorted(candidate for candidate in PORT_DIR.rglob("*") if candidate.is_file()):
        relative = source.relative_to(PORT_DIR)
        if not is_public(relative):
            continue
        copy_file(source, output / "ports" / "pokemon-crystal" / relative)
        copied += 1
    if copied == 0:
        raise EjectError("public source allowlist selected no files")

    for name in sorted(ROOT_TOOLS):
        copy_file(REPO_ROOT / "tools" / name, output / "tools" / name)
    copy_file(REPO_ROOT / "LICENSE", output / "LICENSE")
    shutil.copy2(
        PORT_DIR / "standalone" / "root.gitignore",
        output / ".gitignore",
    )
    shutil.copy2(
        PORT_DIR / "standalone" / "README.md",
        output / "README.md",
    )

    files = source_inventory(output)
    forbidden = [
        item["path"]
        for item in files
        if Path(str(item["path"])).suffix.lower() in FORBIDDEN_SUFFIXES
        or "/references/vendor/" in f"/{item['path']}/"
        or "/references/generated/" in f"/{item['path']}/"
    ]
    if forbidden:
        raise EjectError(f"forbidden source escaped allowlist: {forbidden[0]}")
    payload = {
        "schema": "crystal-recompiled.source-tree",
        "version": 1,
        "file_count": len(files),
        "files": files,
    }
    manifest = output / "SOURCE-MANIFEST.json"
    manifest.write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(f"ok  standalone source tree: {output}")
    print(f"    files={len(files)}")
    print(f"    manifest_sha256={sha256(manifest)}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (EjectError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
