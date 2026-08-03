#!/usr/bin/env python3
"""Validate Crystal's reviewed annotations against pinned sources and metadata."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

ANNOTATION_RE = re.compile(
    r"^(?P<kind>function|data) "
    r"(?P<bank>[0-9a-f]{2}):(?P<address>[0-9a-f]{4}) "
    r"(?:(?P<width>[1-9][0-9]*) )?(?P<name>[A-Za-z_][A-Za-z0-9_]*)$"
)
SYM_RE = re.compile(
    r"^(?P<bank>[0-9A-Fa-f]+):(?P<address>[0-9A-Fa-f]+) "
    r"(?P<name>\S+)"
)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fail(message: str) -> None:
    raise ValueError(message)


def parse_annotations(path: Path) -> list[dict]:
    records: list[dict] = []
    for line_number, raw in enumerate(
        path.read_text(encoding="utf-8").splitlines(), 1
    ):
        body = raw.split(";", 1)[0].strip()
        if not body or body.startswith("#"):
            continue
        match = ANNOTATION_RE.fullmatch(body)
        if not match:
            fail(f"malformed annotation line {line_number}")
        kind = match.group("kind")
        width = int(match.group("width") or "1")
        if kind == "data" and match.group("width") is None:
            fail(f"data annotation lacks width on line {line_number}")
        records.append(
            {
                "kind": kind,
                "bank": int(match.group("bank"), 16),
                "address": int(match.group("address"), 16),
                "width": width,
                "name": match.group("name"),
            }
        )
    if not records:
        fail("annotation file is empty")
    identities = {
        (record["kind"], record["bank"], record["address"])
        for record in records
    }
    if len(identities) != len(records):
        fail("duplicate annotation identity")
    return records


def parse_symbols(path: Path) -> dict[tuple[int, int], set[str]]:
    symbols: dict[tuple[int, int], set[str]] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        match = SYM_RE.match(raw)
        if match:
            key = (
                int(match.group("bank"), 16),
                int(match.group("address"), 16),
            )
            symbols.setdefault(key, set()).add(match.group("name"))
    return symbols


def metadata_addresses(metadata: dict) -> dict[tuple[int, int], list[dict]]:
    result: dict[tuple[int, int], list[dict]] = {}
    for collection in ("functions", "labels", "data_symbols", "rom_data_symbols"):
        for record in metadata.get(collection, []):
            key = (int(record["bank"]), int(record["address"], 0))
            result.setdefault(key, []).append(record)
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--annotations", type=Path, required=True)
    parser.add_argument("--symbols", type=Path, required=True)
    parser.add_argument("--semantic-manifest", type=Path, required=True)
    parser.add_argument("--entry-points", type=Path, required=True)
    parser.add_argument("--metadata", type=Path, required=True)
    parser.add_argument("--baseline-metadata", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    try:
        annotations = parse_annotations(args.annotations)
        symbols = parse_symbols(args.symbols)
        semantic = json.loads(args.semantic_manifest.read_text(encoding="utf-8"))
        entry_payload = json.loads(args.entry_points.read_text(encoding="utf-8"))
        metadata = json.loads(args.metadata.read_text(encoding="utf-8"))
        baseline = json.loads(args.baseline_metadata.read_text(encoding="utf-8"))

        for record in annotations:
            names = symbols.get((record["bank"], record["address"]), set())
            if record["name"] not in names:
                fail(
                    "annotation does not match pinned symbols: "
                    f"{record['bank']:02x}:{record['address']:04x} "
                    f"{record['name']}"
                )

        semantic_addresses = {
            (int(anchor["bank"]), int(anchor["address"], 0)): anchor
            for anchor in semantic["anchors"]
            if "address" in anchor
        }
        data_annotations = [
            record for record in annotations if record["kind"] == "data"
        ]
        if len(data_annotations) != len(semantic_addresses):
            fail("data annotations do not exactly cover semantic address anchors")
        for record in data_annotations:
            anchor = semantic_addresses.get((record["bank"], record["address"]))
            if (
                anchor is None
                or anchor["symbol"] != record["name"]
                or int(anchor["width"]) != record["width"]
            ):
                fail(f"data annotation disagrees with semantic anchor {record}")

        entry_points = {
            (int(value.split(":")[0]), int(value.split(":")[1], 16))
            for value in entry_payload["entry_points"]
        }
        function_annotations = [
            record for record in annotations if record["kind"] == "function"
        ]
        for record in function_annotations:
            site = (record["bank"], record["address"])
            if site != (1, 0x403F) and site not in entry_points:
                fail(f"function is neither route-proven nor copied-RAM source: {site}")

        emitted = metadata_addresses(metadata)
        for record in annotations:
            matches = [
                candidate
                for candidate in emitted.get(
                    (record["bank"], record["address"]), []
                )
                if candidate.get("provenance") == "annotation"
                and record["name"] in candidate.get(
                    "source_symbols",
                    [candidate.get("source_symbol")],
                )
            ]
            if not matches:
                fail(f"annotation provenance was not emitted for {record}")
            if record["kind"] == "data" and not any(
                int(candidate["width"]) == record["width"]
                for candidate in matches
            ):
                fail(f"annotation width was not emitted for {record}")

        current_ids = {record["id"] for record in metadata["functions"]}
        baseline_ids = {record["id"] for record in baseline["functions"]}
        if current_ids != baseline_ids:
            fail("trusted annotations changed stable function IDs")

        result = {
            "schema": "gbrecompiled.pokemon-crystal.annotation-validation-result",
            "version": 1,
            "passed": True,
            "annotations_sha256": sha256(args.annotations),
            "metadata_sha256": sha256(args.metadata),
            "baseline_metadata_sha256": sha256(args.baseline_metadata),
            "annotation_count": len(annotations),
            "function_annotations": len(function_annotations),
            "data_annotations": len(data_annotations),
            "stable_function_ids": len(current_ids),
        }
        rendered = json.dumps(result, indent=2, sort_keys=True) + "\n"
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(rendered, encoding="utf-8")
        print(rendered, end="")
        return 0
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
