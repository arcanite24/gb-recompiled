#!/usr/bin/env python3
"""Validate and deterministically resolve source-built port extensions."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path
from typing import Any


ROM = {
    "size": 2097152,
    "sha256": "fdcc3c8c43813cf8731fc037d2a6d191bac75439c34b24ba1c27526e6acdc8a2",
}
HOST = {"id": "crystal-workbench", "version": 8, "abi_version": 2}
ID_RE = re.compile(r"[a-z][a-z0-9]*(?:[.-][a-z0-9]+)+")
SEMVER_RE = re.compile(r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)")
SHA_RE = re.compile(r"[0-9a-f]{64}")
SYMBOL_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
SOURCE_RE = re.compile(r"[a-z][a-z0-9_-]*\.(?:c|h)")
CAPABILITIES = {"host-draw", "host-input", "semantic-read"}


class ValidationError(RuntimeError):
    pass


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValidationError(f"cannot read JSON {path}: {error}") from error
    if not isinstance(value, dict):
        raise ValidationError(f"JSON root must be an object: {path}")
    return value


def exact(value: object, keys: set[str], label: str) -> dict[str, Any]:
    if not isinstance(value, dict) or set(value) != keys:
        raise ValidationError(f"{label} fields are missing or unknown")
    return value


def confined_source(manifest: Path, relative: object) -> Path:
    if not isinstance(relative, str) or SOURCE_RE.fullmatch(relative) is None:
        raise ValidationError("extension source path is invalid or escaping")
    root = manifest.parent.resolve()
    candidate = root / relative
    if candidate.is_symlink():
        raise ValidationError("extension source must not be a symlink")
    resolved = candidate.resolve()
    try:
        resolved.relative_to(root)
    except ValueError as error:
        raise ValidationError("extension source escapes package") from error
    if not resolved.is_file():
        raise ValidationError(f"extension source is missing: {relative}")
    return resolved


def validate_manifest(path: Path) -> dict[str, Any]:
    value = load(path)
    exact(
        value,
        {
            "schema",
            "version",
            "extension",
            "host",
            "rom",
            "load",
            "capabilities",
            "provenance",
            "entry_symbol",
            "sources",
        },
        "extension manifest",
    )
    if (
        value["schema"] != "gbrecompiled.port-extension"
        or value["version"] != 1
    ):
        raise ValidationError("unsupported port-extension schema")
    extension = exact(
        value["extension"],
        {"id", "version", "abi_version", "priority"},
        "extension",
    )
    extension_id = extension["id"]
    if (
        not isinstance(extension_id, str)
        or ID_RE.fullmatch(extension_id) is None
        or not isinstance(extension["version"], str)
        or SEMVER_RE.fullmatch(extension["version"]) is None
        or extension["abi_version"] != 1
        or not isinstance(extension["priority"], int)
        or isinstance(extension["priority"], bool)
        or not 0 <= extension["priority"] <= 65535
    ):
        raise ValidationError("invalid extension identity, ABI, or priority")
    if value["host"] != HOST:
        raise ValidationError(f"{extension_id}: incompatible host module")
    if value["rom"] != ROM:
        raise ValidationError(f"{extension_id}: incompatible exact ROM")
    load_contract = exact(
        value["load"], {"dependencies", "conflicts"}, "extension load contract"
    )
    dependencies = load_contract["dependencies"]
    conflicts = load_contract["conflicts"]
    if not isinstance(dependencies, list) or not isinstance(conflicts, list):
        raise ValidationError(f"{extension_id}: invalid load relationships")
    dependency_ids: set[str] = set()
    for dependency_value in dependencies:
        dependency = exact(
            dependency_value, {"id", "version"}, "extension dependency"
        )
        dependency_id = dependency["id"]
        if (
            not isinstance(dependency_id, str)
            or ID_RE.fullmatch(dependency_id) is None
            or not isinstance(dependency["version"], str)
            or SEMVER_RE.fullmatch(dependency["version"]) is None
            or dependency_id == extension_id
            or dependency_id in dependency_ids
        ):
            raise ValidationError(f"{extension_id}: invalid dependency")
        dependency_ids.add(dependency_id)
    if (
        any(
            not isinstance(conflict, str)
            or ID_RE.fullmatch(conflict) is None
            for conflict in conflicts
        )
        or len(set(conflicts)) != len(conflicts)
        or extension_id in conflicts
        or dependency_ids.intersection(conflicts)
    ):
        raise ValidationError(f"{extension_id}: invalid conflicts")
    capabilities = value["capabilities"]
    if (
        not isinstance(capabilities, list)
        or set(capabilities) != CAPABILITIES
        or capabilities != sorted(capabilities)
    ):
        raise ValidationError(
            f"{extension_id}: capabilities must be the bounded v1 set"
        )
    provenance = exact(
        value["provenance"],
        {"license", "authors", "source", "content_origin"},
        "extension provenance",
    )
    if (
        provenance["license"] != "MIT"
        or not isinstance(provenance["authors"], list)
        or not provenance["authors"]
        or any(
            not isinstance(author, str) or not author.strip()
            for author in provenance["authors"]
        )
        or not isinstance(provenance["source"], str)
        or not provenance["source"].startswith("https://")
        or provenance["content_origin"] != "project-original"
    ):
        raise ValidationError(f"{extension_id}: invalid provenance")
    entry_symbol = value["entry_symbol"]
    if not isinstance(entry_symbol, str) or SYMBOL_RE.fullmatch(entry_symbol) is None:
        raise ValidationError(f"{extension_id}: invalid entry symbol")
    sources = value["sources"]
    if not isinstance(sources, list) or not sources:
        raise ValidationError(f"{extension_id}: sources must not be empty")
    source_report: list[dict[str, str]] = []
    source_names: set[str] = set()
    entry_found = False
    for source_value in sources:
        source = exact(source_value, {"path", "sha256"}, "extension source")
        relative = source["path"]
        expected_hash = source["sha256"]
        if (
            relative in source_names
            or not isinstance(expected_hash, str)
            or SHA_RE.fullmatch(expected_hash) is None
        ):
            raise ValidationError(f"{extension_id}: invalid duplicate source")
        source_names.add(relative)
        source_path = confined_source(path, relative)
        if sha256(source_path) != expected_hash:
            raise ValidationError(f"{extension_id}: source hash mismatch")
        if source_path.suffix == ".c" and entry_symbol in source_path.read_text(
            encoding="utf-8"
        ):
            entry_found = True
        source_report.append({"path": relative, "sha256": expected_hash})
    if not entry_found:
        raise ValidationError(f"{extension_id}: entry symbol not found in C source")
    return {
        "id": extension_id,
        "version": extension["version"],
        "abi_version": extension["abi_version"],
        "priority": extension["priority"],
        "dependencies": dependencies,
        "conflicts": conflicts,
        "capabilities": capabilities,
        "entry_symbol": entry_symbol,
        "manifest": str(path.resolve()),
        "manifest_sha256": sha256(path),
        "sources": source_report,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, action="append", required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.output and args.output.exists():
        if not args.output.is_file() or args.output.is_symlink():
            raise ValidationError("output must be an ordinary file")
        args.output.unlink()
    extensions = [validate_manifest(path.resolve()) for path in args.manifest]
    by_id = {extension["id"]: extension for extension in extensions}
    if len(by_id) != len(extensions):
        raise ValidationError("extension set contains duplicate IDs")
    if len({extension["entry_symbol"] for extension in extensions}) != len(
        extensions
    ):
        raise ValidationError("extension set contains duplicate entry symbols")
    for extension in extensions:
        for dependency in extension["dependencies"]:
            installed = by_id.get(dependency["id"])
            if installed is None or installed["version"] != dependency["version"]:
                raise ValidationError(
                    f"{extension['id']}: missing/incompatible dependency "
                    f"{dependency['id']}@{dependency['version']}"
                )
            if installed["priority"] >= extension["priority"]:
                raise ValidationError(
                    f"{extension['id']}: dependency {installed['id']} "
                    "does not run first"
                )
        for conflict in extension["conflicts"]:
            if conflict in by_id:
                raise ValidationError(
                    f"{extension['id']}: conflicts with installed {conflict}"
                )
    ordered = sorted(
        extensions, key=lambda item: (item["priority"], item["id"])
    )
    result = {
        "schema": "gbrecompiled.port-extension-resolution",
        "version": 1,
        "passed": True,
        "extension_abi_version": 1,
        "extensions": ordered,
        "load_order": [extension["id"] for extension in ordered],
    }
    rendered = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        temporary = args.output.with_name(args.output.name + ".tmp")
        temporary.write_text(rendered, encoding="utf-8")
        temporary.replace(args.output)
    print(rendered, end="")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, TypeError, ValueError, ValidationError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
