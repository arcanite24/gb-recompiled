#!/usr/bin/env python3
"""Validate Crystal semantic anchors and optional generated metadata."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from typing import Any


EXPECTED_ROM_SHA256 = (
    "fdcc3c8c43813cf8731fc037d2a6d191bac75439c34b24ba1c27526e6acdc8a2"
)
EXPECTED_RTC_SELECTORS = {
    "seconds": 0x08,
    "minutes": 0x09,
    "hours": 0x0A,
    "day_low": 0x0B,
    "day_high": 0x0C,
}
REQUIRED_METADATA_SPACES = {
    "physical_rom",
    "vram",
    "external_ram",
    "wram",
    "banked_wram",
    "hram",
    "mmio",
    "constant",
}
ADDRESS_SPACES = {
    "physical_rom",
    "vram",
    "external_ram",
    "wram",
    "banked_wram",
    "hram",
    "mmio",
}
ID_PATTERN = re.compile(r"^[a-z][a-z0-9_.-]*$")
ADDRESS_PATTERN = re.compile(r"^0x[0-9a-f]{4}$")
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")


def load_json(path: Path, label: str) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise RuntimeError(f"invalid {label}: {path}") from error
    if not isinstance(value, dict):
        raise RuntimeError(f"{label} must be a JSON object")
    return value


def validate_schema_document(schema: dict[str, Any]) -> None:
    if (
        schema.get("$schema")
        != "https://json-schema.org/draft/2020-12/schema"
        or schema.get("type") != "object"
        or not isinstance(schema.get("$defs"), dict)
        or "anchor" not in schema["$defs"]
    ):
        raise RuntimeError("invalid anchor schema document")


def validate_provenance(value: Any, anchor_id: str) -> None:
    if (
        not isinstance(value, dict)
        or set(value) != {"kind", "source"}
        or value.get("kind")
        not in {"pinned-disassembly", "hardware-reference"}
        or not isinstance(value.get("source"), str)
        or not value["source"]
    ):
        raise RuntimeError(f"{anchor_id} has malformed provenance")


def validate_manifest_shape(manifest: dict[str, Any]) -> None:
    if set(manifest) != {"schema", "version", "rom_sha256", "anchors"}:
        raise RuntimeError("anchor manifest has unexpected root fields")
    if (
        manifest.get("schema") != "crystal-recompiled.semantic-anchors"
        or manifest.get("version") != 1
        or not isinstance(manifest.get("rom_sha256"), str)
        or SHA256_PATTERN.fullmatch(manifest["rom_sha256"]) is None
        or not isinstance(manifest.get("anchors"), list)
        or not manifest["anchors"]
    ):
        raise RuntimeError("anchor manifest schema violation")

    address_fields = {
        "id",
        "symbol",
        "memory_space",
        "bank",
        "address",
        "width",
        "provenance",
    }
    rtc_fields = {
        "id",
        "register",
        "memory_space",
        "selector",
        "width",
        "provenance",
    }
    for anchor in manifest["anchors"]:
        if not isinstance(anchor, dict):
            raise RuntimeError("anchor manifest contains a non-object anchor")
        anchor_id = anchor.get("id")
        if (
            not isinstance(anchor_id, str)
            or ID_PATTERN.fullmatch(anchor_id) is None
        ):
            raise RuntimeError("anchor manifest contains an invalid ID")
        if anchor.get("memory_space") == "rtc":
            if set(anchor) != rtc_fields:
                raise RuntimeError(f"{anchor_id} has invalid RTC fields")
            if (
                anchor.get("register") not in EXPECTED_RTC_SELECTORS
                or not isinstance(anchor.get("selector"), int)
                or not 8 <= anchor["selector"] <= 12
                or anchor.get("width") != 1
            ):
                raise RuntimeError(f"{anchor_id} has invalid RTC values")
        else:
            if set(anchor) != address_fields:
                raise RuntimeError(f"{anchor_id} has invalid address fields")
            if (
                anchor.get("memory_space") not in ADDRESS_SPACES
                or not isinstance(anchor.get("symbol"), str)
                or not anchor["symbol"]
                or not isinstance(anchor.get("bank"), int)
                or not 0 <= anchor["bank"] <= 0x1FF
                or not isinstance(anchor.get("address"), str)
                or ADDRESS_PATTERN.fullmatch(anchor["address"]) is None
                or not isinstance(anchor.get("width"), int)
                or not 1 <= anchor["width"] <= 0x10000
            ):
                raise RuntimeError(f"{anchor_id} has invalid address values")
        validate_provenance(anchor.get("provenance"), anchor_id)


def valid_address_range(
    memory_space: str, bank: int, address: int, width: int
) -> bool:
    end = address + width - 1
    if end > 0xFFFF:
        return False
    if memory_space == "physical_rom":
        return (
            (bank == 0 and 0x0000 <= address <= end <= 0x3FFF)
            or (1 <= bank <= 0x1FF and 0x4000 <= address <= end <= 0x7FFF)
        )
    if memory_space == "vram":
        return bank in (0, 1) and 0x8000 <= address <= end <= 0x9FFF
    if memory_space == "external_ram":
        return 0 <= bank <= 3 and 0xA000 <= address <= end <= 0xBFFF
    if memory_space == "wram":
        return bank == 0 and 0xC000 <= address <= end <= 0xCFFF
    if memory_space == "banked_wram":
        return 1 <= bank <= 7 and 0xD000 <= address <= end <= 0xDFFF
    if memory_space == "hram":
        return bank == 0 and 0xFF80 <= address <= end <= 0xFFFE
    if memory_space == "mmio":
        return (
            bank == 0
            and width == 1
            and (0xFF00 <= address <= 0xFF7F or address == 0xFFFF)
        )
    return False


def validate_anchor_semantics(manifest: dict[str, Any]) -> dict[str, Any]:
    if manifest["rom_sha256"] != EXPECTED_ROM_SHA256:
        raise RuntimeError("semantic anchors target an unsupported ROM")

    ids: set[str] = set()
    occupied: dict[tuple[str, int], list[tuple[int, int, str]]] = {}
    rtc_selectors: dict[int, str] = {}

    for anchor in manifest["anchors"]:
        anchor_id = anchor["id"]
        if anchor_id in ids:
            raise RuntimeError(f"duplicate semantic anchor ID: {anchor_id}")
        ids.add(anchor_id)

        memory_space = anchor["memory_space"]
        if memory_space == "rtc":
            register = anchor["register"]
            selector = anchor["selector"]
            if EXPECTED_RTC_SELECTORS.get(register) != selector:
                raise RuntimeError(
                    f"{anchor_id} uses the wrong MBC3 RTC selector"
                )
            if selector in rtc_selectors:
                raise RuntimeError(
                    f"RTC selector {selector:#04x} overlaps "
                    f"{rtc_selectors[selector]} and {anchor_id}"
                )
            rtc_selectors[selector] = anchor_id
            continue

        bank = anchor["bank"]
        address = int(anchor["address"], 16)
        width = anchor["width"]
        if not valid_address_range(memory_space, bank, address, width):
            raise RuntimeError(
                f"{anchor_id} has an impossible {memory_space} range"
            )
        end = address + width - 1
        key = (memory_space, bank)
        for other_start, other_end, other_id in occupied.setdefault(key, []):
            if address <= other_end and other_start <= end:
                raise RuntimeError(
                    f"semantic anchors overlap: {other_id} and {anchor_id}"
                )
        occupied[key].append((address, end, anchor_id))

    if set(rtc_selectors) != set(EXPECTED_RTC_SELECTORS.values()):
        raise RuntimeError("semantic anchors do not cover all MBC3 RTC registers")

    return {
        "anchor_count": len(ids),
        "address_anchor_count": sum(len(ranges) for ranges in occupied.values()),
        "rtc_anchor_count": len(rtc_selectors),
    }


def metadata_entries(metadata: dict[str, Any]) -> list[dict[str, Any]]:
    result: list[dict[str, Any]] = []
    for section in (
        "functions",
        "labels",
        "data_symbols",
        "rom_data_symbols",
        "builtin_address_constants",
    ):
        entries = metadata.get(section)
        if not isinstance(entries, list):
            raise RuntimeError(f"metadata {section} must be an array")
        if any(not isinstance(entry, dict) for entry in entries):
            raise RuntimeError(f"metadata {section} has a malformed entry")
        result.extend(entries)
    return result


def validate_generated_metadata(
    manifest: dict[str, Any], metadata: dict[str, Any]
) -> dict[str, Any]:
    if (
        metadata.get("schema") != "gbrecomp.metadata"
        or metadata.get("schema_version") != 2
    ):
        raise RuntimeError("unsupported generated metadata schema")

    address_entries = metadata_entries(metadata)
    constants = metadata.get("constants")
    if not isinstance(constants, list):
        raise RuntimeError("metadata constants must be an array")
    if len(constants) != 164:
        raise RuntimeError(
            f"expected 164 imported constants, found {len(constants)}"
        )
    constant_names: set[str] = set()
    for constant in constants:
        if (
            not isinstance(constant, dict)
            or not isinstance(constant.get("name"), str)
            or constant.get("memory_space") != "constant"
            or constant.get("provenance") != "imported"
        ):
            raise RuntimeError("malformed imported constant metadata")
        if constant["name"] in constant_names:
            raise RuntimeError(f"duplicate metadata constant: {constant['name']}")
        constant_names.add(constant["name"])

    spaces = {
        entry.get("memory_space")
        for entry in address_entries
        if isinstance(entry.get("memory_space"), str)
    }
    spaces.update(constant.get("memory_space") for constant in constants)
    missing_spaces = sorted(REQUIRED_METADATA_SPACES - spaces)
    if missing_spaces:
        raise RuntimeError(
            f"generated metadata is missing memory spaces: {missing_spaces}"
        )

    indexed: dict[tuple[int, str], list[dict[str, Any]]] = {}
    function_addresses: set[tuple[int, str]] = set()
    for section in ("functions", "labels", "data_symbols", "rom_data_symbols"):
        for entry in metadata[section]:
            bank = entry.get("bank")
            address = entry.get("address")
            if isinstance(bank, int) and isinstance(address, str):
                indexed.setdefault((bank, address), []).append(entry)
                if section == "functions":
                    function_addresses.add((bank, address))

    checked_symbols = 0
    for anchor in manifest["anchors"]:
        if anchor["memory_space"] == "rtc":
            continue
        key = (anchor["bank"], anchor["address"])
        candidates = indexed.get(key, [])
        matching = [
            entry
            for entry in candidates
            if anchor["symbol"]
            in entry.get(
                "source_symbols",
                [entry.get("source_symbol")]
                if entry.get("source_symbol")
                else [],
            )
            and entry.get("memory_space") == anchor["memory_space"]
        ]
        if not matching:
            raise RuntimeError(
                f"generated metadata does not match anchor {anchor['id']}"
            )
        if anchor["memory_space"] != "physical_rom" and key in function_addresses:
            raise RuntimeError(
                f"non-ROM anchor {anchor['id']} was exposed as a function ID"
            )
        checked_symbols += 1

    return {
        "metadata_address_entry_count": len(address_entries),
        "metadata_constant_count": len(constants),
        "metadata_spaces": sorted(spaces),
        "matched_address_anchors": checked_symbols,
    }


def validate(
    manifest_path: Path,
    schema_path: Path,
    metadata_path: Path | None = None,
) -> dict[str, Any]:
    schema = load_json(schema_path, "anchor schema")
    manifest = load_json(manifest_path, "anchor manifest")
    validate_schema_document(schema)
    validate_manifest_shape(manifest)

    result = {
        "schema": "crystal-recompiled.semantic-anchor-validation",
        "version": 1,
        "status": "pass",
        **validate_anchor_semantics(manifest),
    }
    if metadata_path is not None:
        metadata = load_json(metadata_path, "generated metadata")
        result.update(validate_generated_metadata(manifest, metadata))
    return result


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--schema", type=Path, required=True)
    parser.add_argument("--metadata", type=Path)
    parser.add_argument("--output", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    result = validate(
        args.manifest.resolve(),
        args.schema.resolve(),
        args.metadata.resolve() if args.metadata else None,
    )
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(
            json.dumps(result, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
    print(
        "PASS "
        f"anchors={result['anchor_count']} "
        f"address={result['address_anchor_count']} "
        f"rtc={result['rtc_anchor_count']}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(f"FAIL {error}")
        raise SystemExit(1)
