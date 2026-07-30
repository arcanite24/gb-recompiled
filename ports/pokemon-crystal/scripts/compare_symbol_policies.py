#!/usr/bin/env python3
"""Fail-closed comparison of Crystal names-only and annotated generations."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


RECEIPT_NAME = "crystal-generation.json"
METADATA_NAME = "pokemon_crystal_metadata.json"
EXPECTED_SCHEMA = "crystal-recompiled.generation"
EXPECTED_METADATA_SCHEMA = "gbrecomp.metadata"
RICH_ALIAS_ADDRESS = (1, "0xdcd7")
RICH_ALIASES = ["wCurMapDataEnd", "wPartyCount", "wPokemonData"]


def load_json(path: Path, label: str) -> dict[str, Any]:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise RuntimeError(f"invalid {label}: {path}") from error
    if not isinstance(payload, dict):
        raise RuntimeError(f"{label} must be a JSON object: {path}")
    return payload


def function_index(metadata: dict[str, Any]) -> dict[str, dict[str, Any]]:
    functions = metadata.get("functions")
    if not isinstance(functions, list):
        raise RuntimeError("metadata functions must be an array")
    result: dict[str, dict[str, Any]] = {}
    for entry in functions:
        if not isinstance(entry, dict) or not isinstance(entry.get("id"), str):
            raise RuntimeError("malformed function metadata entry")
        function_id = entry["id"]
        if function_id in result:
            raise RuntimeError(f"duplicate function ID: {function_id}")
        result[function_id] = entry
    return result


def find_address_entry(
    metadata: dict[str, Any], bank: int, address: str
) -> dict[str, Any]:
    matches: list[dict[str, Any]] = []
    for section in ("functions", "labels", "data_symbols", "rom_data_symbols"):
        entries = metadata.get(section)
        if not isinstance(entries, list):
            raise RuntimeError(f"metadata {section} must be an array")
        for entry in entries:
            if (
                isinstance(entry, dict)
                and entry.get("bank") == bank
                and entry.get("address") == address
            ):
                matches.append(entry)
    if not matches:
        raise RuntimeError(f"missing metadata address {bank:02x}:{address[2:]}")
    for entry in matches:
        if entry.get("source_symbols") == RICH_ALIASES:
            return entry
    raise RuntimeError(
        f"address {bank:02x}:{address[2:]} did not preserve expected aliases"
    )


def compare(
    names_dir: Path,
    annotated_dir: Path,
    expected_function_id: str,
    expected_source_symbol: str,
) -> dict[str, Any]:
    names_receipt = load_json(names_dir / RECEIPT_NAME, "names-only receipt")
    annotated_receipt = load_json(
        annotated_dir / RECEIPT_NAME, "annotated receipt"
    )
    names_metadata = load_json(names_dir / METADATA_NAME, "names-only metadata")
    annotated_metadata = load_json(
        annotated_dir / METADATA_NAME, "annotated metadata"
    )

    for label, receipt in (
        ("names-only", names_receipt),
        ("annotated", annotated_receipt),
    ):
        if (
            receipt.get("schema") != EXPECTED_SCHEMA
            or receipt.get("version") != 1
        ):
            raise RuntimeError(f"unsupported {label} receipt schema")
        analysis = receipt.get("analysis")
        if (
            not isinstance(analysis, dict)
            or analysis.get("symbol_policy") != "names-only"
        ):
            raise RuntimeError(f"{label} generation was not names-only")

    for label, metadata in (
        ("names-only", names_metadata),
        ("annotated", annotated_metadata),
    ):
        if metadata.get("schema") != EXPECTED_METADATA_SCHEMA:
            raise RuntimeError(f"unsupported {label} metadata schema")

    identity_fields = (
        ("rom", "sha256"),
        ("references", "symbols"),
        ("analysis", "scope"),
        ("analysis", "scan"),
        ("analysis", "entry_points"),
    )
    for parent, child in identity_fields:
        names_value = names_receipt.get(parent, {}).get(child)
        annotated_value = annotated_receipt.get(parent, {}).get(child)
        if names_value != annotated_value:
            raise RuntimeError(
                f"generation identity differs at {parent}.{child}"
            )

    names_annotations = names_receipt.get("references", {}).get("annotations")
    annotated_annotations = annotated_receipt.get("references", {}).get(
        "annotations"
    )
    if names_annotations != {"kind": "none"}:
        raise RuntimeError("names-only control unexpectedly used annotations")
    if (
        not isinstance(annotated_annotations, dict)
        or annotated_annotations.get("kind") != "file"
    ):
        raise RuntimeError("annotated generation did not identify its annotation file")

    names_functions = function_index(names_metadata)
    annotated_functions = function_index(annotated_metadata)
    added = sorted(set(annotated_functions) - set(names_functions))
    removed = sorted(set(names_functions) - set(annotated_functions))
    if added != [expected_function_id] or removed:
        raise RuntimeError(
            f"unexpected boundary delta: added={added}, removed={removed}"
        )

    target = annotated_functions[expected_function_id]
    if (
        target.get("provenance") != "annotation"
        or target.get("source_symbol") != expected_source_symbol
        or target.get("source_symbols") != [expected_source_symbol]
    ):
        raise RuntimeError("added boundary lacks exact annotation provenance")

    changed_common: list[str] = []
    for function_id in sorted(names_functions.keys() & annotated_functions.keys()):
        before = names_functions[function_id]
        after = annotated_functions[function_id]
        projected_before = {
            key: before.get(key)
            for key in (
                "bank",
                "address",
                "emitted_name",
                "provenance",
                "source_symbol",
                "source_symbols",
            )
        }
        projected_after = {
            key: after.get(key)
            for key in projected_before
        }
        if projected_before != projected_after:
            changed_common.append(function_id)
    if changed_common:
        raise RuntimeError(
            f"common function naming/provenance changed: {changed_common[:8]}"
        )

    rich_alias_entry = find_address_entry(
        names_metadata, *RICH_ALIAS_ADDRESS
    )
    find_address_entry(annotated_metadata, *RICH_ALIAS_ADDRESS)

    return {
        "schema": "crystal-recompiled.symbol-policy-comparison",
        "version": 1,
        "status": "pass",
        "names_only_function_count": len(names_functions),
        "annotated_function_count": len(annotated_functions),
        "added_function_ids": added,
        "removed_function_ids": removed,
        "changed_common_function_ids": changed_common,
        "added_boundary": target,
        "rich_alias_address": {
            "bank": rich_alias_entry["bank"],
            "address": rich_alias_entry["address"],
            "source_symbols": rich_alias_entry["source_symbols"],
        },
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--names-only", type=Path, required=True)
    parser.add_argument("--annotated", type=Path, required=True)
    parser.add_argument("--expected-function-id", required=True)
    parser.add_argument("--expected-source-symbol", required=True)
    parser.add_argument("--output", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    result = compare(
        args.names_only.resolve(),
        args.annotated.resolve(),
        args.expected_function_id,
        args.expected_source_symbol,
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(
        "PASS "
        f"functions={result['names_only_function_count']}+"
        f"{len(result['added_function_ids'])} "
        f"boundary={result['added_function_ids'][0]}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(f"FAIL {error}")
        raise SystemExit(1)
