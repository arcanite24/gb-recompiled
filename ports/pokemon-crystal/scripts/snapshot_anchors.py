#!/usr/bin/env python3
"""Classify every documented Crystal anchor from generated metadata."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path


PORT_DIR = Path(__file__).resolve().parent.parent
DEFAULT_ANCHORS = PORT_DIR / "SEMANTIC_ANCHORS.md"
EXPECTED_ROM_SHA256 = (
    "fdcc3c8c43813cf8731fc037d2a6d191bac75439c34b24ba1c27526e6acdc8a2"
)
SECTION = re.compile(r"^## (Function-hook candidates|Read-only state candidates)$")
HEX_FIELD = re.compile(r"^[0-9a-fA-F]+:[0-9a-fA-F]+$")


@dataclass(frozen=True)
class Candidate:
    category: str
    symbol: str
    bank: int
    address: int
    function_id: str | None


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def unquote(field: str) -> str:
    value = field.strip()
    if value.startswith("`") and value.endswith("`"):
        return value[1:-1]
    return value


def load_candidates(path: Path) -> list[Candidate]:
    category: str | None = None
    candidates: list[Candidate] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        section = SECTION.match(line)
        if section:
            category = "function" if section.group(1).startswith("Function") else "state"
            continue
        if category is None or not line.startswith("|"):
            continue
        fields = [unquote(field) for field in line.strip().strip("|").split("|")]
        if len(fields) < 2 or not HEX_FIELD.fullmatch(fields[1]):
            continue
        bank_text, address_text = fields[1].split(":", maxsplit=1)
        function_id = fields[2] if category == "function" and len(fields) >= 3 else None
        if category == "function" and not function_id.startswith("gbfn:v1:"):
            raise RuntimeError(f"missing function ID for {fields[0]}")
        candidates.append(
            Candidate(
                category=category,
                symbol=fields[0],
                bank=int(bank_text, 16),
                address=int(address_text, 16),
                function_id=function_id,
            )
        )
    if not candidates:
        raise RuntimeError(f"no semantic candidates found in {path}")
    identities = [(item.category, item.symbol) for item in candidates]
    if len(identities) != len(set(identities)):
        raise RuntimeError("duplicate semantic candidate")
    return candidates


def require_metadata(path: Path) -> dict[str, object]:
    payload = json.loads(path.read_text(encoding="utf-8"))
    if payload.get("schema") != "gbrecomp.metadata" or payload.get("schema_version") != 2:
        raise RuntimeError("unsupported GB Recompiled metadata schema")
    rom = payload.get("rom")
    if not isinstance(rom, dict) or rom.get("sha256") != EXPECTED_ROM_SHA256:
        raise RuntimeError("metadata does not identify supported Pokémon Crystal ROM")
    for key in ("functions", "data_symbols"):
        if not isinstance(payload.get(key), list):
            raise RuntimeError(f"metadata is missing {key}")
    return payload


def address_text(address: int) -> str:
    return f"0x{address:04x}"


def metadata_subset(entry: dict[str, object]) -> dict[str, object]:
    allowed = (
        "id",
        "id_u32",
        "bank",
        "address",
        "patchable",
        "emitted_name",
        "emitted_constant",
        "kind",
        "provenance",
        "source_symbol",
    )
    return {key: entry[key] for key in allowed if key in entry}


def matching_address(
    entries: list[dict[str, object]], candidate: Candidate
) -> list[dict[str, object]]:
    return [
        metadata_subset(entry)
        for entry in entries
        if entry.get("bank") == candidate.bank
        and entry.get("address") == address_text(candidate.address)
    ]


def classify(
    candidate: Candidate,
    functions: list[dict[str, object]],
    data_symbols: list[dict[str, object]],
) -> dict[str, object]:
    entries = functions if candidate.category == "function" else data_symbols
    exact = [
        entry for entry in entries if entry.get("source_symbol") == candidate.symbol
    ]
    if len(exact) > 1:
        raise RuntimeError(f"ambiguous metadata for {candidate.symbol}")

    result: dict[str, object] = {
        "category": candidate.category,
        "symbol": candidate.symbol,
        "bank": candidate.bank,
        "address": address_text(candidate.address),
    }
    if candidate.function_id is not None:
        result["expected_function_id"] = candidate.function_id

    if not exact:
        result["status"] = "unresolved"
        result["metadata"] = None
        result["address_matches"] = matching_address(entries, candidate)
        return result

    entry = exact[0]
    if entry.get("bank") != candidate.bank or entry.get("address") != address_text(
        candidate.address
    ):
        raise RuntimeError(f"address mismatch for {candidate.symbol}")
    if candidate.category == "function":
        if entry.get("id") != candidate.function_id:
            raise RuntimeError(f"stable function ID mismatch for {candidate.symbol}")
        if not isinstance(entry.get("patchable"), bool):
            raise RuntimeError(f"missing patchability for {candidate.symbol}")
        result["status"] = "patchable" if entry["patchable"] else "unpatchable"
    else:
        if entry.get("kind") != "data":
            raise RuntimeError(f"non-data metadata for state anchor {candidate.symbol}")
        result["status"] = "resolved-read-only"
    result["metadata"] = metadata_subset(entry)
    result["address_matches"] = matching_address(entries, candidate)
    return result


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--metadata", type=Path, required=True)
    parser.add_argument("--anchors", type=Path, default=DEFAULT_ANCHORS)
    parser.add_argument("--json-out", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    metadata_path = args.metadata.resolve()
    anchors_path = args.anchors.resolve()
    if not metadata_path.is_file():
        raise RuntimeError(f"missing metadata: {metadata_path}")
    if not anchors_path.is_file():
        raise RuntimeError(f"missing anchor document: {anchors_path}")

    metadata = require_metadata(metadata_path)
    candidates = load_candidates(anchors_path)
    functions = metadata["functions"]
    data_symbols = metadata["data_symbols"]
    assert isinstance(functions, list)
    assert isinstance(data_symbols, list)
    classifications = [
        classify(candidate, functions, data_symbols) for candidate in candidates
    ]
    counts = Counter(str(item["status"]) for item in classifications)
    payload = {
        "schema": "crystal-recompiled.anchor-snapshot",
        "version": 1,
        "rom": metadata["rom"],
        "metadata": {
            "schema": metadata["schema"],
            "schema_version": metadata["schema_version"],
            "sha256": sha256(metadata_path),
        },
        "anchors": {
            "sha256": sha256(anchors_path),
            "candidate_count": len(candidates),
        },
        "summary": dict(sorted(counts.items())),
        "candidates": classifications,
    }
    rendered = json.dumps(payload, indent=2, sort_keys=True) + "\n"
    if args.json_out is not None:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(rendered, encoding="utf-8")
    print(rendered, end="")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
