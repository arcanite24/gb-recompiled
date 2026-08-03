#!/usr/bin/env python3
"""Compile validated Crystal encounter content into a ROM-bound overlay."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path
from typing import Any


ROM_SHA256 = "fdcc3c8c43813cf8731fc037d2a6d191bac75439c34b24ba1c27526e6acdc8a2"
ROM_SIZE = 2_097_152
ARTIFACT_MAGIC = b"GBDMOD1\0"
ARTIFACT_ABI = 1
ARTIFACT_HEADER_SIZE = 92
POLICY_SHA256 = "089191ee2ca10dd1d434d7cab3af5eea6134c54f52edf7bbdfc2aef51b5ec142"
PACKAGE_SCHEMA_SHA256 = (
    "493f3af3aa688567722137eff608a91845bbae9f4ebace2be09edac6fdc123eb"
)
SEMANTIC_MANIFEST_SHA256 = (
    "2eb33327d10fecae903e79d18fad7c9b90f3535b27c9dceb2d2454fb3041c566"
)
SEMANTIC_SCHEMA_SHA256 = (
    "b1e8cb8a2c2bec9b221570238f9ff81edde9f81efb036ddb0d36c7a9905aade9"
)

# These are semantic identities, not author-controlled physical addresses.
# Expanding coverage requires a reviewed mapping tied to the exact ROM.
ENCOUNTER_TABLES = {
    "ROUTE_29": {
        "offset": 0x2ADFD,
        "map_group": 24,
        "map_number": 3,
        "rates": {"morning": 25, "day": 25, "night": 25},
    },
}
TIME_OFFSETS = {"morning": 5, "day": 19, "night": 33}
SPECIES = {
    "PIDGEY": 0x10,
    "RATTATA": 0x13,
    "SENTRET": 0xA1,
    "HOOTHOOT": 0xA3,
    "HOPPIP": 0xBB,
}
CHARMAP = {
    " ": 0x7F,
    **{chr(ord("A") + index): 0x80 + index for index in range(26)},
    **{str(index): 0xF6 + index for index in range(10)},
    ":": 0x9C,
    "-": 0xE3,
    ".": 0xE8,
    "/": 0xF3,
}
INFORMATION_SIGNS = {
    ("ROUTE_29", "WEST_SIGN"): {
        "offset": 0x1A15B9,
        "size": 43,
        "original": [
            "ROUTE 29",
            "CHERRYGROVE CITY -",
            "NEW BARK TOWN",
        ],
    },
}


class CompileError(RuntimeError):
    pass


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise CompileError(f"cannot read JSON {path}: {error}") from error
    if not isinstance(value, dict):
        raise CompileError(f"JSON root must be an object: {path}")
    return value


def exact_keys(value: object, expected: set[str], label: str) -> dict[str, Any]:
    if not isinstance(value, dict) or set(value) != expected:
        raise CompileError(f"{label} fields are missing or unknown")
    return value


def confined_content(manifest_path: Path, relative: object) -> Path:
    if (
        not isinstance(relative, str)
        or not relative
        or "\\" in relative
        or Path(relative).is_absolute()
        or any(part in {"", ".", ".."} for part in Path(relative).parts)
    ):
        raise CompileError("resolved content path is escaping or noncanonical")
    root = manifest_path.parent.resolve()
    candidate = root.joinpath(relative)
    if candidate.is_symlink():
        raise CompileError("resolved content must not be a symlink")
    resolved = candidate.resolve()
    try:
        resolved.relative_to(root)
    except ValueError as error:
        raise CompileError("resolved content path escapes package") from error
    if not resolved.is_file():
        raise CompileError("resolved content file is missing")
    return resolved


def add_patch(
    patches: dict[int, tuple[int, str, str]],
    offset: int,
    replacement: int,
    source_label: str,
    semantic_identity: str,
) -> None:
    prior = patches.get(offset)
    if prior is not None:
        raise CompileError(
            f"overlay conflict for {semantic_identity} at 0x{offset:06x}: "
            f"{prior[1]} conflicts with {source_label}"
        )
    patches[offset] = (replacement, source_label, semantic_identity)


def compile_encounters(
    content_path: Path,
    rom: bytes,
    patches: dict[int, tuple[int, str, str]],
    source_label: str,
) -> int:
    content = read_json(content_path)
    exact_keys(content, {"schema", "version", "changes"}, source_label)
    if content["schema"] != "crystal.encounters" or content["version"] != 1:
        raise CompileError(f"{source_label}: unsupported encounter schema")
    changes = content["changes"]
    if not isinstance(changes, list) or not changes:
        raise CompileError(f"{source_label}: changes must be a nonempty array")
    seen: set[tuple[str, str, int]] = set()
    for index, raw_change in enumerate(changes):
        change = exact_keys(
            raw_change, {"map", "time", "slot", "level", "species"},
            f"{source_label} change {index}",
        )
        map_id = change["map"]
        time_id = change["time"]
        slot = change["slot"]
        level = change["level"]
        species_name = change["species"]
        identity = (map_id, time_id, slot)
        time_offsets = (
            tuple(TIME_OFFSETS.values())
            if time_id == "all"
            else (TIME_OFFSETS[time_id],)
            if time_id in TIME_OFFSETS
            else ()
        )
        if (
            map_id not in ENCOUNTER_TABLES
            or not time_offsets
            or not isinstance(slot, int)
            or isinstance(slot, bool)
            or not 0 <= slot < 7
            or not isinstance(level, int)
            or isinstance(level, bool)
            or not 1 <= level <= 100
            or species_name not in SPECIES
            or identity in seen
        ):
            raise CompileError(f"{source_label}: invalid/duplicate encounter identity")
        seen.add(identity)
        table = ENCOUNTER_TABLES[map_id]
        base = table["offset"]
        if (
            rom[base] != table["map_group"]
            or rom[base + 1] != table["map_number"]
            or any(
                rom[base + 2 + rate_index] != table["rates"][rate_name]
                for rate_index, rate_name in enumerate(("morning", "day", "night"))
            )
        ):
            raise CompileError(f"{source_label}: exact encounter table signature changed")
        for time_offset in time_offsets:
            slot_offset = base + time_offset + slot * 2
            expanded_time = next(
                name for name, offset in TIME_OFFSETS.items()
                if offset == time_offset
            )
            semantic_prefix = (
                f"encounter:{map_id}:{expanded_time}:slot-{slot}"
            )
            add_patch(
                patches,
                slot_offset,
                level,
                source_label,
                f"{semantic_prefix}:level",
            )
            add_patch(
                patches,
                slot_offset + 1,
                SPECIES[species_name],
                source_label,
                f"{semantic_prefix}:species",
            )
    return len(changes)


def encode_text(text: object, label: str) -> bytes:
    if (
        not isinstance(text, str)
        or not text
        or len(text) > 18
        or any(character not in CHARMAP for character in text)
    ):
        raise CompileError(
            f"{label}: text must be 1-18 supported uppercase characters"
        )
    return bytes(CHARMAP[character] for character in text)


def encode_sign(lines: list[str], size: int, label: str) -> bytes:
    encoded = bytearray([0x00])
    encoded.extend(encode_text(lines[0], f"{label} title"))
    encoded.append(0x51)
    encoded.extend(encode_text(lines[1], f"{label} first line"))
    encoded.append(0x4F)
    encoded.extend(encode_text(lines[2], f"{label} second line"))
    encoded.append(0x57)
    if len(encoded) > size:
        raise CompileError(f"{label}: encoded sign exceeds its reviewed allocation")
    encoded[-1:-1] = bytes([CHARMAP[" "]]) * (size - len(encoded))
    return bytes(encoded)


def compile_accessibility(
    content_path: Path,
    rom: bytes,
    patches: dict[int, tuple[int, str, str]],
    source_label: str,
) -> int:
    content = read_json(content_path)
    exact_keys(content, {"schema", "version", "signs"}, source_label)
    if (
        content["schema"] != "crystal.accessibility"
        or content["version"] != 1
    ):
        raise CompileError(f"{source_label}: unsupported accessibility schema")
    signs = content["signs"]
    if not isinstance(signs, list) or not signs:
        raise CompileError(f"{source_label}: signs must be a nonempty array")
    seen: set[tuple[str, str]] = set()
    for index, raw_sign in enumerate(signs):
        sign = exact_keys(
            raw_sign,
            {"map", "sign", "title", "lines"},
            f"{source_label} sign {index}",
        )
        identity = (sign["map"], sign["sign"])
        lines = sign["lines"]
        if (
            identity not in INFORMATION_SIGNS
            or identity in seen
            or not isinstance(lines, list)
            or len(lines) != 2
            or any(not isinstance(line, str) for line in lines)
        ):
            raise CompileError(
                f"{source_label}: invalid/duplicate information-sign identity"
            )
        seen.add(identity)
        target = INFORMATION_SIGNS[identity]
        original = encode_sign(
            target["original"],
            target["size"],
            f"{source_label} reviewed original",
        )
        offset = target["offset"]
        if rom[offset : offset + target["size"]] != original:
            raise CompileError(
                f"{source_label}: exact information-sign signature changed"
            )
        replacement = encode_sign(
            [sign["title"], *lines],
            target["size"],
            source_label,
        )
        semantic_prefix = f"information-sign:{identity[0]}:{identity[1]}"
        for byte_index, byte in enumerate(replacement):
            add_patch(
                patches,
                offset + byte_index,
                byte,
                source_label,
                semantic_prefix,
            )
    return len(signs)


def package_set_digest(resolution: dict[str, Any]) -> bytes:
    identities = []
    for package in resolution["packages"]:
        identities.append(
            {
                "id": package["id"],
                "version": package["version"],
                "order": package["order"],
                "manifest_sha256": package["manifest_sha256"],
                "content": [
                    {
                        "id": item["id"],
                        "target": item["target"],
                        "sha256": item["sha256"],
                    }
                    for item in package["content"]
                ],
            }
        )
    canonical = json.dumps(
        identities, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")
    return hashlib.sha256(canonical).digest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--resolution", type=Path, required=True)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()

    if args.output.exists():
        if not args.output.is_file() or args.output.is_symlink():
            raise CompileError("output must be an ordinary file")
        args.output.unlink()
    if args.report and args.report.exists():
        if not args.report.is_file() or args.report.is_symlink():
            raise CompileError("report must be an ordinary file")
        args.report.unlink()

    rom = args.rom.read_bytes()
    if len(rom) != ROM_SIZE or sha256_bytes(rom) != ROM_SHA256:
        raise CompileError("unsupported ROM: exact Crystal Rev 1 is required")
    resolution = read_json(args.resolution)
    exact_keys(
        resolution,
        {
            "schema",
            "version",
            "passed",
            "policy_sha256",
            "package_schema_sha256",
            "semantic_manifest_sha256",
            "semantic_schema_sha256",
            "packages",
            "load_order",
        },
        "resolution",
    )
    packages = resolution["packages"]
    if (
        resolution["schema"] != "gbrecompiled.data-mod-resolution"
        or resolution["version"] != 1
        or resolution["passed"] is not True
        or resolution["policy_sha256"] != POLICY_SHA256
        or resolution["package_schema_sha256"] != PACKAGE_SCHEMA_SHA256
        or resolution["semantic_manifest_sha256"] !=
            SEMANTIC_MANIFEST_SHA256
        or resolution["semantic_schema_sha256"] != SEMANTIC_SCHEMA_SHA256
        or not isinstance(packages, list)
        or not packages
        or resolution["load_order"] != [package.get("id") for package in packages]
    ):
        raise CompileError("resolution is invalid or unsuccessful")
    if (
        any(not isinstance(package, dict) for package in packages)
        or len({package.get("id") for package in packages}) != len(packages)
        or packages != sorted(
            packages, key=lambda package: (package.get("order"), package.get("id"))
        )
    ):
        raise CompileError("resolution package order/identity is invalid")

    patches: dict[int, tuple[int, str, str]] = {}
    change_count = 0
    information_sign_count = 0
    for package in packages:
        manifest_path = Path(package["manifest"])
        manifest_bytes = manifest_path.read_bytes()
        if sha256_bytes(manifest_bytes) != package["manifest_sha256"]:
            raise CompileError(f"{package['id']}: manifest changed after validation")
        for item in package["content"]:
            content_path = confined_content(manifest_path, item["path"])
            content_bytes = content_path.read_bytes()
            if sha256_bytes(content_bytes) != item["sha256"]:
                raise CompileError(f"{package['id']}: content changed after validation")
            if item["target"] == "crystal.encounters.v1":
                change_count += compile_encounters(
                    content_path,
                    rom,
                    patches,
                    f"{package['id']}:{item['id']}",
                )
            elif item["target"] == "crystal.accessibility.v1":
                information_sign_count += compile_accessibility(
                    content_path,
                    rom,
                    patches,
                    f"{package['id']}:{item['id']}",
                )
            elif item["target"] in {
                "crystal.trainers.v1",
                "crystal.rules.v1",
                "crystal.host-assets.v1",
            }:
                raise CompileError(
                    f"{package['id']}:{item['id']}: target application is not "
                    "implemented in overlay ABI v1"
                )
            else:
                raise CompileError(f"{package['id']}: unknown target")
    if not patches:
        raise CompileError("resolution produced no data-overlay entries")

    rom_digest = hashlib.sha256(rom).digest()
    set_digest = package_set_digest(resolution)
    artifact = bytearray()
    artifact.extend(ARTIFACT_MAGIC)
    artifact.extend(struct.pack("<IIQ", ARTIFACT_ABI, ARTIFACT_HEADER_SIZE, len(rom)))
    artifact.extend(rom_digest)
    artifact.extend(set_digest)
    artifact.extend(struct.pack("<I", len(patches)))
    for offset, (replacement, _source, _identity) in sorted(patches.items()):
        artifact.extend(struct.pack("<II", offset, 1))
        artifact.append(rom[offset])
        artifact.append(replacement)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_name(args.output.name + ".tmp")
    temporary.write_bytes(artifact)
    temporary.replace(args.output)
    report = {
        "schema": "gbrecompiled.data-mod-compile-report",
        "version": 1,
        "passed": True,
        "rom": {"size": len(rom), "sha256": ROM_SHA256},
        "resolution_sha256": sha256_bytes(args.resolution.read_bytes()),
        "package_set_sha256": set_digest.hex(),
        "artifact": {
            "abi_version": ARTIFACT_ABI,
            "bytes": len(artifact),
            "entry_count": len(patches),
            "sha256": sha256_bytes(artifact),
        },
        "encounter_change_count": change_count,
        "information_sign_count": information_sign_count,
        "overlaid_offsets": [f"0x{offset:06x}" for offset in sorted(patches)],
        "original_rom_sha256_after_compile": sha256_bytes(rom),
    }
    rendered = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        report_temporary = args.report.with_name(args.report.name + ".tmp")
        report_temporary.write_text(rendered, encoding="utf-8")
        report_temporary.replace(args.report)
    print(rendered, end="")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (CompileError, KeyError, OSError, TypeError, ValueError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
