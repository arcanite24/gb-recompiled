#!/usr/bin/env python3
"""Validate Crystal's durable save/RTC identity and internal save redundancy."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


ROM_SHA256 = "fdcc3c8c43813cf8731fc037d2a6d191bac75439c34b24ba1c27526e6acdc8a2"
ROM_SIZE = 2_097_152
SAVE_SIZE = 32_768
RTC_SIZE = 40
RTC_MAGIC = 0x47525443
RTC_VERSION = 2


def load_json(path: Path) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"cannot read JSON {path}: {error}") from error
    if not isinstance(value, dict):
        raise ValueError(f"expected JSON object: {path}")
    return value


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def offset(bank: int, address: int) -> int:
    return bank * 0x2000 + address - 0xA000


def checksum(data: bytes, bank: int, start: int, end: int) -> int:
    return sum(data[offset(bank, start) : offset(bank, end)]) & 0xFFFF


def verify_save(data: bytes) -> dict:
    if len(data) != SAVE_SIZE:
        raise ValueError(f"battery save must contain exactly {SAVE_SIZE} bytes")
    regions = {
        "primary": {
            "bank": 1,
            "start": 0xA009,
            "end": 0xAB83,
            "checksum": 0xAD0D,
            "check1": 0xA008,
            "check2": 0xAD0F,
        },
        "backup": {
            "bank": 0,
            "start": 0xB209,
            "end": 0xBD83,
            "checksum": 0xBF0D,
            "check1": 0xB208,
            "check2": 0xBF0F,
        },
    }
    reports = {}
    payloads = {}
    for name, region in regions.items():
        bank = region["bank"]
        calculated = checksum(data, bank, region["start"], region["end"])
        stored_offset = offset(bank, region["checksum"])
        stored = int.from_bytes(data[stored_offset : stored_offset + 2], "little")
        check1 = data[offset(bank, region["check1"])]
        check2 = data[offset(bank, region["check2"])]
        if check1 != 99 or check2 != 127:
            raise ValueError(f"{name} save check values are invalid")
        if calculated != stored:
            raise ValueError(f"{name} save checksum is invalid")
        payload = data[
            offset(bank, region["start"]) : offset(bank, region["end"])
        ]
        payloads[name] = payload
        reports[name] = {
            "checksum": stored,
            "check_value_1": check1,
            "check_value_2": check2,
            "payload_sha256": hashlib.sha256(payload).hexdigest(),
        }
    if payloads["primary"] != payloads["backup"]:
        raise ValueError("primary and backup game-data payloads differ")

    box_start = offset(1, 0xAD10)
    box_end = offset(1, 0xB15E)
    box = data[box_start:box_end]
    box_count = box[0]
    if box_count > 20:
        raise ValueError("active box count exceeds Crystal's capacity")
    species = list(box[1 : 1 + box_count])
    if box[1 + box_count] != 0xFF:
        raise ValueError("active box species list lacks its terminator")
    return {
        "bytes": len(data),
        "primary": reports["primary"],
        "backup": reports["backup"],
        "primary_backup_equal": True,
        "active_box": {
            "count": box_count,
            "species": species,
            "sha256": hashlib.sha256(box).hexdigest(),
        },
    }


def verify_rtc(data: bytes) -> dict:
    if len(data) != RTC_SIZE:
        raise ValueError(f"RTC must contain exactly {RTC_SIZE} bytes")
    magic = int.from_bytes(data[0:4], "little")
    version = int.from_bytes(data[4:8], "little")
    if magic != RTC_MAGIC or version != RTC_VERSION:
        raise ValueError("RTC magic or serialization version is invalid")
    seconds, minutes, hours, day_low, day_high = data[24:29]
    if seconds >= 60 or minutes >= 60 or hours >= 24:
        raise ValueError("RTC register range is invalid")
    if day_high & ~0xC1:
        raise ValueError("RTC day-high contains unsupported bits")
    if any(data[35:]):
        raise ValueError("RTC v2 reserved bytes are not zero")
    return {
        "bytes": len(data),
        "magic": f"0x{magic:08x}",
        "version": version,
        "saved_unix_time": int.from_bytes(data[8:16], "little"),
        "cycle_remainder": int.from_bytes(data[16:24], "little"),
        "registers": {
            "seconds": seconds,
            "minutes": minutes,
            "hours": hours,
            "day_low": day_low,
            "day_high": day_high,
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--route-result", type=Path, required=True)
    parser.add_argument("--generation-receipt", type=Path, required=True)
    parser.add_argument("--save", type=Path, required=True)
    parser.add_argument("--rtc", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    route = load_json(args.route_result)
    receipt = load_json(args.generation_receipt)
    if (
        route.get("schema") != "gbrecompiled.pokemon-crystal.route-result"
        or route.get("passed") is not True
    ):
        raise ValueError("route result did not pass")
    rom = receipt.get("rom")
    if (
        receipt.get("schema") != "crystal-recompiled.generation"
        or not isinstance(rom, dict)
        or rom.get("sha256") != ROM_SHA256
        or rom.get("size") != ROM_SIZE
        or rom.get("name") != "pokemon_crystal.gbc"
    ):
        raise ValueError("generation receipt has the wrong Crystal identity")
    if args.save.name != "pokemon_crystal.sav" or args.rtc.name != "pokemon_crystal.rtc":
        raise ValueError("persistence filenames do not match stable game identity")
    persistence = route.get("persistence")
    if not isinstance(persistence, dict):
        raise ValueError("route result has no persistence evidence")
    if persistence.get("sav", {}).get("sha256") != sha256(args.save):
        raise ValueError("route result battery hash does not match the file")
    if persistence.get("rtc", {}).get("sha256") != sha256(args.rtc):
        raise ValueError("route result RTC hash does not match the file")

    save_report = verify_save(args.save.read_bytes())
    rtc_report = verify_rtc(args.rtc.read_bytes())
    result = {
        "schema": "crystal-recompiled.transactional-persistence-verification",
        "version": 1,
        "passed": True,
        "rom_sha256": ROM_SHA256,
        "storage_id": "pokemon_crystal",
        "save_sha256": sha256(args.save),
        "rtc_sha256": sha256(args.rtc),
        "save": save_report,
        "rtc": rtc_report,
        "route_result_sha256": sha256(args.route_result),
        "generation_receipt_sha256": sha256(args.generation_receipt),
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, TypeError, ValueError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
