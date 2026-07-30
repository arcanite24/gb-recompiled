#!/usr/bin/env python3
"""Synthetic contract tests for the Crystal encounter overlay compiler."""

from __future__ import annotations

import importlib.util
import json
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools" / "compile_crystal_data_mod.py"
SPEC = importlib.util.spec_from_file_location("compile_crystal_data_mod", SCRIPT)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def content(change: dict) -> dict:
    return {
        "schema": "crystal.encounters",
        "version": 1,
        "changes": [change],
    }


def expect_failure(path: Path, rom: bytes, payload: dict, needle: str) -> None:
    path.write_text(json.dumps(payload), encoding="utf-8")
    try:
        MODULE.compile_encounters(path, rom, {}, "fixture")
    except MODULE.CompileError as error:
        if needle not in str(error):
            raise AssertionError(f"unexpected failure: {error}") from error
    else:
        raise AssertionError("invalid encounter content was accepted")


def main() -> int:
    table = MODULE.ENCOUNTER_TABLES["ROUTE_29"]
    rom = bytearray(table["offset"] + 47)
    base = table["offset"]
    rom[base] = table["map_group"]
    rom[base + 1] = table["map_number"]
    rom[base + 2 : base + 5] = bytes((25, 25, 25))
    rom[base + 5 : base + 19] = bytes(
        (2, 0x10, 2, 0xA1, 3, 0x10, 3, 0xA1, 2, 0x13, 3, 0xBB, 3, 0xBB)
    )
    change = {
        "map": "ROUTE_29",
        "time": "morning",
        "slot": 0,
        "level": 5,
        "species": "HOPPIP",
    }
    with tempfile.TemporaryDirectory() as temporary:
        path = Path(temporary) / "encounters.json"
        path.write_text(json.dumps(content(change)), encoding="utf-8")
        patches: dict[int, tuple[int, str, str]] = {}
        if MODULE.compile_encounters(path, bytes(rom), patches, "fixture") != 1:
            raise AssertionError("valid encounter change count disagreed")
        expected = {
            base + 5: (
                5,
                "fixture",
                "encounter:ROUTE_29:morning:slot-0:level",
            ),
            base + 6: (
                0xBB,
                "fixture",
                "encounter:ROUTE_29:morning:slot-0:species",
            ),
        }
        if patches != expected:
            raise AssertionError(f"semantic identity mapped incorrectly: {patches}")

        all_times = dict(change)
        all_times["time"] = "all"
        path.write_text(json.dumps(content(all_times)), encoding="utf-8")
        all_patches: dict[int, tuple[int, str, str]] = {}
        MODULE.compile_encounters(path, bytes(rom), all_patches, "fixture")
        if len(all_patches) != 6:
            raise AssertionError("all-time identity did not expand to three slots")

        unknown = dict(change)
        unknown["address"] = base + 5
        expect_failure(path, bytes(rom), content(unknown), "fields")
        unsupported = dict(change)
        unsupported["map"] = "ROUTE_30"
        expect_failure(path, bytes(rom), content(unsupported), "invalid")
        duplicate = content(change)
        duplicate["changes"].append(change)
        expect_failure(path, bytes(rom), duplicate, "duplicate")
        changed_rom = bytearray(rom)
        changed_rom[base] ^= 1
        expect_failure(path, bytes(changed_rom), content(change), "signature")

        sign = MODULE.INFORMATION_SIGNS[("ROUTE_29", "WEST_SIGN")]
        sign_rom = bytearray(sign["offset"] + sign["size"])
        sign_rom[sign["offset"] :] = MODULE.encode_sign(
            sign["original"], sign["size"], "fixture"
        )
        accessibility_path = Path(temporary) / "accessibility.json"
        accessibility = {
            "schema": "crystal.accessibility",
            "version": 1,
            "signs": [
                {
                    "map": "ROUTE_29",
                    "sign": "WEST_SIGN",
                    "title": "ROUTE 29",
                    "lines": ["AM/DAY: PIDGEY", "NITE: HOOTHOOT"],
                }
            ],
        }
        accessibility_path.write_text(
            json.dumps(accessibility), encoding="utf-8"
        )
        information_patches: dict[int, tuple[int, str, str]] = {}
        if (
            MODULE.compile_accessibility(
                accessibility_path,
                bytes(sign_rom),
                information_patches,
                "guide-package:guide",
            )
            != 1
            or len(information_patches) != sign["size"]
        ):
            raise AssertionError("valid information sign was not compiled")
        replacement = bytes(
            information_patches[sign["offset"] + index][0]
            for index in range(sign["size"])
        )
        if replacement == bytes(sign_rom[sign["offset"] :]):
            raise AssertionError("information sign did not change")
        try:
            MODULE.compile_accessibility(
                accessibility_path,
                bytes(sign_rom),
                information_patches,
                "other-package:guide",
            )
        except MODULE.CompileError as error:
            message = str(error)
            if (
                "overlay conflict for information-sign:ROUTE_29:WEST_SIGN"
                not in message
                or "guide-package:guide conflicts with other-package:guide"
                not in message
            ):
                raise AssertionError(
                    f"conflict diagnostic was not actionable: {error}"
                ) from error
        else:
            raise AssertionError("overlapping semantic sign claims were accepted")

        unsupported_text = json.loads(json.dumps(accessibility))
        unsupported_text["signs"][0]["lines"][0] = "lowercase"
        accessibility_path.write_text(
            json.dumps(unsupported_text), encoding="utf-8"
        )
        try:
            MODULE.compile_accessibility(
                accessibility_path, bytes(sign_rom), {}, "fixture"
            )
        except MODULE.CompileError as error:
            if "supported uppercase" not in str(error):
                raise AssertionError(f"unexpected text failure: {error}") from error
        else:
            raise AssertionError("unsupported information text was accepted")

        changed_sign_rom = bytearray(sign_rom)
        changed_sign_rom[sign["offset"]] ^= 1
        accessibility_path.write_text(
            json.dumps(accessibility), encoding="utf-8"
        )
        try:
            MODULE.compile_accessibility(
                accessibility_path, bytes(changed_sign_rom), {}, "fixture"
            )
        except MODULE.CompileError as error:
            if "signature" not in str(error):
                raise AssertionError(f"unexpected sign failure: {error}") from error
        else:
            raise AssertionError("changed information-sign source was accepted")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
