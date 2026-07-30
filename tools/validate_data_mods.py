#!/usr/bin/env python3
"""Fail-closed validation and deterministic ordering for data-mod packages."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path
from typing import Any


PACKAGE_ID_RE = re.compile(r"[a-z][a-z0-9]*(?:[.-][a-z0-9]+)+")
SEMVER_RE = re.compile(r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)")
SHA256_RE = re.compile(r"[0-9a-f]{64}")
CONTENT_ID_RE = re.compile(r"[a-z][a-z0-9]*(?:[._-][a-z0-9]+)*")
SPDX_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9.+-]*(?: OR [A-Za-z0-9][A-Za-z0-9.+-]*)*")


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


def exact_keys(value: object, expected: set[str], label: str) -> dict[str, Any]:
    if not isinstance(value, dict) or set(value) != expected:
        raise ValidationError(f"{label} fields are missing or unknown")
    return value


def exact_string(value: object, pattern: re.Pattern[str], label: str) -> str:
    if not isinstance(value, str) or pattern.fullmatch(value) is None:
        raise ValidationError(f"{label} has invalid syntax")
    return value


def validate_policy(path: Path) -> tuple[dict[str, Any], dict[str, dict[str, Any]]]:
    policy = load(path)
    exact_keys(
        policy,
        {"schema", "schema_version", "port", "rom", "semantic", "targets"},
        "target policy",
    )
    if (
        policy["schema"] != "gbrecompiled.data-mod-target-policy"
        or policy["schema_version"] != 1
    ):
        raise ValidationError("unsupported target policy schema")
    port = exact_keys(policy["port"], {"id", "version"}, "policy port")
    rom = exact_keys(policy["rom"], {"size", "sha256"}, "policy ROM")
    semantic = exact_keys(
        policy["semantic"],
        {
            "package_id",
            "package_version",
            "manifest_sha256",
            "schema_sha256",
        },
        "policy semantic contract",
    )
    if (
        not isinstance(port["id"], str)
        or not isinstance(port["version"], int)
        or port["version"] < 1
        or not isinstance(rom["size"], int)
        or rom["size"] <= 0
    ):
        raise ValidationError("policy identity fields are invalid")
    exact_string(rom["sha256"], SHA256_RE, "policy ROM hash")
    exact_string(semantic["manifest_sha256"], SHA256_RE, "semantic manifest hash")
    exact_string(semantic["schema_sha256"], SHA256_RE, "semantic schema hash")
    if (
        not isinstance(semantic["package_id"], str)
        or not isinstance(semantic["package_version"], int)
        or semantic["package_version"] < 1
    ):
        raise ValidationError("policy semantic identity is invalid")
    targets = policy["targets"]
    if not isinstance(targets, list) or not targets:
        raise ValidationError("policy must define at least one target")
    mapped: dict[str, dict[str, Any]] = {}
    for index, target_value in enumerate(targets):
        target = exact_keys(
            target_value,
            {"id", "media_type", "extensions", "max_bytes", "description"},
            f"policy target {index}",
        )
        target_id = exact_string(target["id"], CONTENT_ID_RE, "target ID")
        extensions = target["extensions"]
        if (
            target_id in mapped
            or not isinstance(target["media_type"], str)
            or not target["media_type"]
            or not isinstance(extensions, list)
            or not extensions
            or any(
                not isinstance(extension, str)
                or re.fullmatch(r"\.[a-z0-9]+", extension) is None
                for extension in extensions
            )
            or len(set(extensions)) != len(extensions)
            or not isinstance(target["max_bytes"], int)
            or target["max_bytes"] <= 0
            or not isinstance(target["description"], str)
            or not target["description"]
        ):
            raise ValidationError(f"invalid or duplicate policy target {target_id}")
        mapped[target_id] = target
    return policy, mapped


def confined_content(manifest: Path, relative: object) -> Path:
    if (
        not isinstance(relative, str)
        or not relative
        or "\\" in relative
        or Path(relative).is_absolute()
        or any(part in {"", ".", ".."} for part in Path(relative).parts)
    ):
        raise ValidationError("content path is absolute, escaping, or noncanonical")
    root = manifest.parent.resolve()
    candidate = root.joinpath(relative)
    if candidate.is_symlink():
        raise ValidationError(f"content path must not be a symlink: {relative}")
    resolved = candidate.resolve()
    try:
        resolved.relative_to(root)
    except ValueError as error:
        raise ValidationError(f"content path escapes package: {relative}") from error
    if not resolved.is_file():
        raise ValidationError(f"content file is missing: {relative}")
    return resolved


def validate_manifest(
    path: Path,
    policy: dict[str, Any],
    targets: dict[str, dict[str, Any]],
) -> dict[str, Any]:
    value = load(path)
    exact_keys(
        value,
        {
            "schema",
            "schema_version",
            "package",
            "runtime_abi",
            "compatibility",
            "load",
            "provenance",
            "content",
        },
        "package manifest",
    )
    if (
        value["schema"] != "gbrecompiled.data-mod-package"
        or value["schema_version"] != 1
    ):
        raise ValidationError("unsupported data-mod schema")
    package = exact_keys(value["package"], {"id", "version"}, "package")
    package_id = exact_string(package["id"], PACKAGE_ID_RE, "package ID")
    exact_string(package["version"], SEMVER_RE, "package version")
    if value["runtime_abi"] != {"name": "gbrecomp.data-mod", "version": 1}:
        raise ValidationError(f"{package_id}: unsupported data-mod runtime ABI")

    compatibility = exact_keys(
        value["compatibility"], {"port", "rom", "semantic"}, "compatibility"
    )
    if compatibility["port"] != policy["port"]:
        raise ValidationError(f"{package_id}: incompatible port identity")
    if compatibility["rom"] != policy["rom"]:
        raise ValidationError(f"{package_id}: incompatible exact ROM")
    expected_semantic = {
        "package_id": policy["semantic"]["package_id"],
        "package_version": policy["semantic"]["package_version"],
        "manifest_sha256": policy["semantic"]["manifest_sha256"],
        "schema_sha256": policy["semantic"]["schema_sha256"],
    }
    if compatibility["semantic"] != expected_semantic:
        raise ValidationError(f"{package_id}: incompatible semantic schema")

    load_contract = exact_keys(
        value["load"], {"order", "dependencies", "conflicts"}, "load contract"
    )
    order = load_contract["order"]
    if not isinstance(order, int) or isinstance(order, bool) or not 0 <= order <= 65535:
        raise ValidationError(f"{package_id}: invalid load order")
    dependencies = load_contract["dependencies"]
    if not isinstance(dependencies, list):
        raise ValidationError(f"{package_id}: dependencies must be an array")
    dependency_ids: set[str] = set()
    for dependency_value in dependencies:
        dependency = exact_keys(
            dependency_value, {"id", "version"}, "dependency"
        )
        dependency_id = exact_string(
            dependency["id"], PACKAGE_ID_RE, "dependency ID"
        )
        exact_string(dependency["version"], SEMVER_RE, "dependency version")
        if dependency_id == package_id or dependency_id in dependency_ids:
            raise ValidationError(f"{package_id}: invalid duplicate/self dependency")
        dependency_ids.add(dependency_id)
    conflicts = load_contract["conflicts"]
    if (
        not isinstance(conflicts, list)
        or any(
            not isinstance(conflict, str)
            or PACKAGE_ID_RE.fullmatch(conflict) is None
            for conflict in conflicts
        )
        or len(set(conflicts)) != len(conflicts)
        or package_id in conflicts
        or dependency_ids.intersection(conflicts)
    ):
        raise ValidationError(f"{package_id}: invalid conflicts")

    provenance = exact_keys(
        value["provenance"],
        {"license", "authors", "source", "content_origin"},
        "provenance",
    )
    exact_string(provenance["license"], SPDX_RE, "license")
    authors = provenance["authors"]
    if (
        not isinstance(authors, list)
        or not authors
        or any(not isinstance(author, str) or not author.strip() for author in authors)
        or len(set(authors)) != len(authors)
        or not isinstance(provenance["source"], str)
        or not provenance["source"].startswith("https://")
        or provenance["content_origin"]
        not in {"project-original", "compatible-license"}
    ):
        raise ValidationError(f"{package_id}: invalid provenance")

    content = value["content"]
    if not isinstance(content, list) or not content:
        raise ValidationError(f"{package_id}: content must not be empty")
    content_ids: set[str] = set()
    content_report: list[dict[str, Any]] = []
    for content_value in content:
        item = exact_keys(
            content_value, {"id", "target", "path", "sha256"}, "content item"
        )
        content_id = exact_string(item["id"], CONTENT_ID_RE, "content ID")
        target_id = exact_string(item["target"], CONTENT_ID_RE, "content target")
        expected_hash = exact_string(item["sha256"], SHA256_RE, "content hash")
        if content_id in content_ids:
            raise ValidationError(f"{package_id}: duplicate content ID {content_id}")
        content_ids.add(content_id)
        target = targets.get(target_id)
        if target is None:
            raise ValidationError(f"{package_id}: unknown content target {target_id}")
        content_path = confined_content(path, item["path"])
        if (
            content_path.suffix not in target["extensions"]
            or content_path.stat().st_size > target["max_bytes"]
        ):
            raise ValidationError(f"{package_id}: content violates target policy")
        actual_hash = sha256(content_path)
        if actual_hash != expected_hash:
            raise ValidationError(f"{package_id}: content hash mismatch for {content_id}")
        content_report.append(
            {
                "id": content_id,
                "target": target_id,
                "path": item["path"],
                "bytes": content_path.stat().st_size,
                "sha256": actual_hash,
            }
        )
    return {
        "id": package_id,
        "version": package["version"],
        "order": order,
        "dependencies": dependencies,
        "conflicts": conflicts,
        "manifest": str(path.resolve()),
        "manifest_sha256": sha256(path),
        "content": content_report,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, action="append", required=True)
    parser.add_argument("--policy", type=Path, required=True)
    parser.add_argument("--package-schema", type=Path, required=True)
    parser.add_argument("--semantic-package", type=Path, required=True)
    parser.add_argument("--semantic-schema", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.output and args.output.exists():
        if not args.output.is_file() or args.output.is_symlink():
            raise ValidationError("output must be an ordinary file path")
        args.output.unlink()

    policy, targets = validate_policy(args.policy)
    package_schema = load(args.package_schema)
    if (
        package_schema.get("$schema")
        != "https://json-schema.org/draft/2020-12/schema"
        or package_schema.get("$id")
        != "https://gbrecompiled.example/schemas/data-mod-package-v1.json"
        or package_schema.get("type") != "object"
    ):
        raise ValidationError("unsupported data-mod package schema document")
    if (
        sha256(args.semantic_package) != policy["semantic"]["manifest_sha256"]
        or sha256(args.semantic_schema) != policy["semantic"]["schema_sha256"]
    ):
        raise ValidationError("installed semantic contract does not match policy")
    packages = [
        validate_manifest(path.resolve(), policy, targets)
        for path in args.manifest
    ]
    by_id = {package["id"]: package for package in packages}
    if len(by_id) != len(packages):
        raise ValidationError("package set contains duplicate package IDs")
    for package in packages:
        for dependency in package["dependencies"]:
            installed = by_id.get(dependency["id"])
            if installed is None or installed["version"] != dependency["version"]:
                raise ValidationError(
                    f"{package['id']}: missing/incompatible dependency "
                    f"{dependency['id']}@{dependency['version']}"
                )
            if installed["order"] >= package["order"]:
                raise ValidationError(
                    f"{package['id']}: dependency {installed['id']} "
                    "does not load first"
                )
        for conflict in package["conflicts"]:
            if conflict in by_id:
                raise ValidationError(
                    f"{package['id']}: conflicts with installed {conflict}"
                )
    ordered = sorted(packages, key=lambda package: (package["order"], package["id"]))
    result = {
        "schema": "gbrecompiled.data-mod-resolution",
        "version": 1,
        "passed": True,
        "policy_sha256": sha256(args.policy),
        "package_schema_sha256": sha256(args.package_schema),
        "semantic_manifest_sha256": sha256(args.semantic_package),
        "semantic_schema_sha256": sha256(args.semantic_schema),
        "packages": ordered,
        "load_order": [package["id"] for package in ordered],
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
    except (OSError, TypeError, ValidationError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
