#!/usr/bin/env python3
"""Fail-closed validation for a versioned exact-ROM semantic package."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

EXPECTED_ROM = "fdcc3c8c43813cf8731fc037d2a6d191bac75439c34b24ba1c27526e6acdc8a2"
EXPECTED_VIEWS = {
    "player.identity",
    "player.location",
    "player.badges",
    "player.pokedex",
    "player.party",
    "storage.active_box",
    "world.map_connections",
    "battle.player",
    "battle.active_slot",
    "battle.enemy",
    "battle.context",
    "clock.rtc",
    "species.base_data",
    "species.evolutions_and_moves",
}
BACKED_UP_VIEWS = {
    "player.identity",
    "player.location",
    "player.badges",
    "player.pokedex",
    "player.party",
}
BACKUP_LAYOUTS = {
    "player.identity": {
        "space": "external_ram",
        "bank": 0,
        "address": "0xb20b",
        "width": 11,
    },
    "player.location": {
        "space": "external_ram",
        "bank": 0,
        "address": "0xba43",
        "width": 4,
    },
    "player.badges": {
        "space": "external_ram",
        "bank": 0,
        "address": "0xb5e5",
        "width": 2,
    },
    "player.pokedex": {
        "space": "external_ram",
        "bank": 0,
        "address": "0xbc27",
        "width": 64,
    },
    "player.party": {
        "space": "external_ram",
        "bank": 0,
        "address": "0xba65",
        "width": 428,
    },
}
TYPES = {"u8", "u16le", "bytes", "bitset", "record", "table", "rtc"}
TRANSACTIONAL_WRITE_VIEWS = {"player.party", "storage.active_box"}
TRANSACTIONAL_LAYOUTS = {
    "player.party": {
        "memory": {
            "space": "banked_wram",
            "bank": 1,
            "address": "0xdcd7",
            "width": 428,
        },
        "save_memory": {
            "space": "external_ram",
            "bank": 1,
            "address": "0xa865",
            "width": 428,
        },
        "backup_memory": {
            "space": "external_ram",
            "bank": 0,
            "address": "0xba65",
            "width": 428,
        },
    },
    "storage.active_box": {
        "memory": {
            "space": "external_ram",
            "bank": 1,
            "address": "0xad10",
            "width": 1102,
        },
        "canonical_memory": {
            "space": "external_ram",
            "first_bank": 2,
            "bank_count": 2,
            "address": "0xa000",
            "stride": 1102,
            "items_per_bank": 7,
            "width": 1102,
            "selector_memory": {
                "space": "external_ram",
                "bank": 1,
                "address": "0xa700",
                "width": 1,
            },
            "selector_max": 13,
        },
    },
}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fail(message: str) -> None:
    raise ValueError(message)


def validate_range(space: str, bank: int, address: int, width: int) -> None:
    end = address + width
    if width <= 0 or end > 0x10000:
        fail("invalid semantic width or overflowing range")
    if space == "physical_rom":
        valid = (
            (bank == 0 and 0 <= address < end <= 0x4000)
            or (1 <= bank <= 127 and 0x4000 <= address < end <= 0x8000)
        )
    elif space == "external_ram":
        valid = 0 <= bank <= 3 and 0xA000 <= address < end <= 0xC000
    elif space == "wram":
        valid = bank == 0 and 0xC000 <= address < end <= 0xD000
    elif space == "banked_wram":
        valid = 1 <= bank <= 7 and 0xD000 <= address < end <= 0xE000
    else:
        valid = False
    if not valid:
        fail(f"impossible {space} range {bank}:{address:04x}+{width}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--schema", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        schema = json.loads(args.schema.read_text(encoding="utf-8"))
        manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
        if (
            schema.get("$schema")
            != "https://json-schema.org/draft/2020-12/schema"
            or schema.get("properties", {}).get("schema", {}).get("const")
            != "gbrecompiled.semantic-package"
        ):
            fail("unsupported semantic package schema")
        if (
            manifest.get("schema") != "gbrecompiled.semantic-package"
            or manifest.get("schema_version") != 1
            or manifest.get("package") != {"id": "crystal-recompiled", "version": 4}
            or manifest.get("runtime_abi")
            != {"name": "gbrecomp.semantic", "version": 1}
            or manifest.get("rom")
            != {"size": 2097152, "sha256": EXPECTED_ROM}
        ):
            fail("unsupported schema, ABI, package, or ROM identity")

        views = manifest.get("views")
        if not isinstance(views, list) or not views:
            fail("semantic views are empty")
        ids = [view.get("id") for view in views]
        if len(ids) != len(set(ids)) or set(ids) != EXPECTED_VIEWS:
            fail("semantic views are duplicate, missing, or unexpected")

        ranges: dict[tuple[str, str, int], list[tuple[int, int, str]]] = {}
        for view in views:
            expected_access = (
                "transactional_write"
                if view.get("id") in TRANSACTIONAL_WRITE_VIEWS
                else "read_only"
            )
            if (
                not isinstance(view.get("id"), str)
                or re.fullmatch(r"[a-z][a-z0-9_.-]*", view["id"]) is None
                or view.get("type") not in TYPES
                or view.get("access") != expected_access
            ):
                fail(f"invalid type or access policy for {view.get('id')}")
            provenance = view.get("provenance")
            if (
                not isinstance(provenance, dict)
                or provenance.get("kind")
                not in {"pinned-disassembly", "hardware-reference"}
                or not provenance.get("source")
            ):
                fail(f"invalid provenance for {view['id']}")
            memory = view.get("memory")
            if not isinstance(memory, dict):
                fail(f"missing memory contract for {view['id']}")
            width = memory.get("width")
            if not isinstance(width, int):
                fail(f"invalid width for {view['id']}")
            if (
                (view["id"] in BACKED_UP_VIEWS) !=
                ("backup_memory" in view)
            ):
                fail("only backed-up save views must declare backup memory")
            if (
                view["id"] in BACKUP_LAYOUTS
                and view.get("backup_memory") != BACKUP_LAYOUTS[view["id"]]
            ):
                fail(f"unsupported backup layout for {view['id']}")
            if (
                (view["id"] == "storage.active_box") !=
                ("canonical_memory" in view)
            ):
                fail("only storage.active_box must declare canonical memory")
            if view["id"] in TRANSACTIONAL_LAYOUTS:
                for contract_name, expected in TRANSACTIONAL_LAYOUTS[
                    view["id"]
                ].items():
                    if view.get(contract_name) != expected:
                        fail(
                            f"unsupported transactional layout for "
                            f"{view['id']} {contract_name}"
                        )
            canonical = view.get("canonical_memory")
            if canonical is not None:
                selector = canonical.get("selector_memory")
                if (
                    canonical.get("space") != "external_ram"
                    or canonical.get("first_bank") != 2
                    or canonical.get("bank_count") != 2
                    or canonical.get("address") != "0xa000"
                    or canonical.get("stride") != width
                    or canonical.get("items_per_bank") != 7
                    or canonical.get("width") != width
                    or canonical.get("selector_max") != 13
                    or selector
                    != {
                        "space": "external_ram",
                        "bank": 1,
                        "address": "0xa700",
                        "width": 1,
                    }
                ):
                    fail(f"invalid canonical memory for {view['id']}")
                last_address = (
                    int(canonical["address"], 0)
                    + (canonical["items_per_bank"] - 1)
                    * canonical["stride"]
                )
                validate_range(
                    "external_ram",
                    canonical["first_bank"],
                    last_address,
                    width,
                )
            for mode, contract in (
                ("live", memory),
                ("save", view.get("save_memory")),
                ("backup", view.get("backup_memory")),
            ):
                if contract is None:
                    continue
                if not isinstance(contract, dict):
                    fail(f"invalid {mode} memory contract for {view['id']}")
                space = contract.get("space")
                contract_width = contract.get("width")
                if contract_width != width:
                    fail(f"{mode} width mismatch for {view['id']}")
                if space == "rtc":
                    if (
                        mode != "live"
                        or view["type"] != "rtc"
                        or width != 5
                        or contract.get("selectors") != [8, 9, 10, 11, 12]
                        or "bank" in contract
                        or "address" in contract
                    ):
                        fail("invalid RTC semantic contract")
                    continue
                if (
                    space not in {"physical_rom", "external_ram", "wram", "banked_wram"}
                    or not isinstance(contract.get("bank"), int)
                    or not isinstance(contract.get("address"), str)
                    or re.fullmatch(r"0x[0-9a-f]{4}", contract["address"]) is None
                ):
                    fail(f"invalid {mode} memory space for {view['id']}")
                if mode in {"save", "backup"} and space != "external_ram":
                    fail(f"save contract is not external RAM for {view['id']}")
                bank = contract["bank"]
                address = int(contract["address"], 0)
                validate_range(space, bank, address, width)
                key = (mode, space, bank)
                for start, end, other in ranges.setdefault(key, []):
                    if address < end and start < address + width:
                        fail(
                            f"overlapping {mode} semantic views: "
                            f"{other} and {view['id']}"
                        )
                ranges[key].append((address, address + width, view["id"]))
            fields = view.get("fields")
            if not isinstance(fields, list) or not fields:
                fail(f"missing typed fields for {view['id']}")
            field_names: set[str] = set()
            for field in fields:
                if (
                    not isinstance(field.get("name"), str)
                    or field["name"] in field_names
                    or not isinstance(field.get("offset"), int)
                    or field["offset"] < 0
                    or field["offset"] >= width
                    or field.get("type") not in TYPES - {"rtc"}
                ):
                    fail(f"invalid field in {view['id']}")
                field_names.add(field["name"])

        result = {
            "schema": "gbrecompiled.semantic-package-validation",
            "version": 1,
            "passed": True,
            "manifest_sha256": sha256(args.manifest),
            "schema_sha256": sha256(args.schema),
            "runtime_abi_version": 1,
            "view_count": len(views),
            "read_only_views": sum(
                view["access"] == "read_only" for view in views
            ),
            "transactional_write_views": sum(
                view["access"] == "transactional_write" for view in views
            ),
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
