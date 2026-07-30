#!/usr/bin/env python3
"""Fail-closed package-set tests for the v1 data-mod contract."""

from __future__ import annotations

import copy
import argparse
import hashlib
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path


ROM = {
    "size": 2097152,
    "sha256": "fdcc3c8c43813cf8731fc037d2a6d191bac75439c34b24ba1c27526e6acdc8a2",
}
SEMANTIC = {
    "package_id": "crystal-recompiled",
    "package_version": 4,
    "manifest_sha256": "2eb33327d10fecae903e79d18fad7c9b90f3535b27c9dceb2d2454fb3041c566",
    "schema_sha256": "b1e8cb8a2c2bec9b221570238f9ff81edde9f81efb036ddb0d36c7a9905aade9",
}


def manifest(
    package_id: str,
    version: str,
    order: int,
    content_hash: str,
    *,
    dependencies: list[dict[str, str]] | None = None,
    conflicts: list[str] | None = None,
) -> dict:
    return {
        "schema": "gbrecompiled.data-mod-package",
        "schema_version": 1,
        "package": {"id": package_id, "version": version},
        "runtime_abi": {"name": "gbrecomp.data-mod", "version": 1},
        "compatibility": {
            "port": {"id": "crystal-recompiled", "version": 1},
            "rom": ROM,
            "semantic": SEMANTIC,
        },
        "load": {
            "order": order,
            "dependencies": dependencies or [],
            "conflicts": conflicts or [],
        },
        "provenance": {
            "license": "MIT",
            "authors": ["GB Recompiled tests"],
            "source": "https://example.invalid/mod",
            "content_origin": "project-original",
        },
        "content": [
            {
                "id": "rules",
                "target": "crystal.rules.v1",
                "path": "content/rules.json",
                "sha256": content_hash,
            }
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--evidence-output", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    validator = root / "tools/validate_data_mods.py"
    policy = root / "ports/pokemon-crystal/mods/target-policy.json"
    package_schema = root / "ports/pokemon-crystal/mods/package-schema.json"
    semantic_package = root / "ports/pokemon-crystal/semantic/package.json"
    semantic_schema = root / "ports/pokemon-crystal/semantic/package-schema.json"
    with tempfile.TemporaryDirectory() as raw:
        directory = Path(raw)
        content = directory / "content/rules.json"
        content.parent.mkdir()
        content.write_text('{"level_cap": 20}\\n', encoding="utf-8")
        content_hash = hashlib.sha256(content.read_bytes()).hexdigest()
        base = manifest("example.base", "1.0.0", 10, content_hash)
        addon = manifest(
            "example.addon",
            "2.1.0",
            20,
            content_hash,
            dependencies=[{"id": "example.base", "version": "1.0.0"}],
        )
        base_path = directory / "base.json"
        addon_path = directory / "addon.json"
        output = directory / "result.json"

        def run(
            base_value: dict,
            addon_value: dict | None = addon,
            *,
            addon_manifest_path: Path = addon_path,
        ) -> int:
            base_path.write_text(json.dumps(base_value), encoding="utf-8")
            manifests = [base_path]
            if addon_value is not None:
                addon_manifest_path.write_text(
                    json.dumps(addon_value), encoding="utf-8"
                )
                manifests.append(addon_manifest_path)
            command = [
                sys.executable,
                str(validator),
                "--policy",
                str(policy),
                "--package-schema",
                str(package_schema),
                "--semantic-package",
                str(semantic_package),
                "--semantic-schema",
                str(semantic_schema),
                "--output",
                str(output),
            ]
            for path in manifests:
                command.extend(["--manifest", str(path)])
            return subprocess.run(
                command, capture_output=True, check=False
            ).returncode

        if run(base, addon) != 0:
            raise AssertionError("validator rejected a compatible ordered package set")
        resolved = json.loads(output.read_text(encoding="utf-8"))
        if resolved["load_order"] != ["example.base", "example.addon"]:
            raise AssertionError("validator did not emit deterministic load order")

        controls: list[tuple[str, dict, dict | None]] = []
        unknown = copy.deepcopy(base)
        unknown["unknown"] = True
        controls.append(("unknown field", unknown, None))
        bad_rom = copy.deepcopy(base)
        bad_rom["compatibility"]["rom"]["sha256"] = "0" * 64
        controls.append(("wrong ROM", bad_rom, None))
        bad_schema = copy.deepcopy(base)
        bad_schema["compatibility"]["semantic"]["schema_sha256"] = "0" * 64
        controls.append(("wrong semantic schema", bad_schema, None))
        bad_abi = copy.deepcopy(base)
        bad_abi["runtime_abi"]["version"] = 2
        controls.append(("wrong ABI", bad_abi, None))
        bad_hash = copy.deepcopy(base)
        bad_hash["content"][0]["sha256"] = "0" * 64
        controls.append(("wrong content hash", bad_hash, None))
        bad_target = copy.deepcopy(base)
        bad_target["content"][0]["target"] = "crystal.unknown.v1"
        controls.append(("unknown target", bad_target, None))
        missing_dependency = copy.deepcopy(addon)
        controls.append(("missing dependency", missing_dependency, None))
        wrong_order = copy.deepcopy(addon)
        wrong_order["load"]["order"] = 5
        controls.append(("dependency order", base, wrong_order))
        conflict = copy.deepcopy(addon)
        conflict["load"]["conflicts"] = ["example.base"]
        conflict["load"]["dependencies"] = []
        controls.append(("installed conflict", base, conflict))
        bad_provenance = copy.deepcopy(base)
        bad_provenance["provenance"]["source"] = "file:///private/mod"
        controls.append(("bad provenance", bad_provenance, None))
        escape = copy.deepcopy(base)
        outside = directory / "outside.json"
        outside.write_text("{}", encoding="utf-8")
        escape["content"][0]["path"] = "../outside.json"
        escape["content"][0]["sha256"] = hashlib.sha256(
            outside.read_bytes()
        ).hexdigest()
        controls.append(("escaping path", escape, None))
        for label, base_value, addon_value in controls:
            if run(base_value, addon_value) == 0:
                raise AssertionError(f"validator accepted {label}")
            if output.exists():
                raise AssertionError(f"validator retained stale output for {label}")

        duplicate = copy.deepcopy(base)
        duplicate["package"]["version"] = "2.0.0"
        duplicate_path = directory / "duplicate.json"
        if run(base, duplicate, addon_manifest_path=duplicate_path) == 0:
            raise AssertionError("validator accepted duplicate package IDs")

        if hasattr(os, "symlink"):
            content.unlink()
            content.symlink_to(outside)
            symlink_manifest = manifest(
                "example.symlink",
                "1.0.0",
                1,
                hashlib.sha256(outside.read_bytes()).hexdigest(),
            )
            if run(symlink_manifest, None) == 0:
                raise AssertionError("validator accepted symlink content")
            if output.exists():
                raise AssertionError("validator retained stale output for symlink")
        if args.evidence_output:
            evidence = {
                "schema": "gbrecompiled.data-mod-contract-probe",
                "version": 1,
                "passed": True,
                "valid_package_count": 2,
                "deterministic_load_order": [
                    "example.base",
                    "example.addon",
                ],
                "rejected_controls": [
                    label for label, _, _ in controls
                ] + ["duplicate package ID", "symlink content"],
                "stale_resolution_removed": True,
            }
            args.evidence_output.parent.mkdir(parents=True, exist_ok=True)
            args.evidence_output.write_text(
                json.dumps(evidence, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
