#!/usr/bin/env python3
"""Fail-closed controls for Crystal's persistence-structure verifier."""

from __future__ import annotations

import copy
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path


def offset(bank: int, address: int) -> int:
    return bank * 0x2000 + address - 0xA000


def fixture_save() -> bytes:
    data = bytearray([0xFF] * 32768)
    payload = bytes((index * 17) & 0xFF for index in range(0xB7A))
    for bank, start, checksum_address, check1, check2 in (
        (1, 0xA009, 0xAD0D, 0xA008, 0xAD0F),
        (0, 0xB209, 0xBF0D, 0xB208, 0xBF0F),
    ):
        data[offset(bank, start) : offset(bank, start) + len(payload)] = payload
        value = sum(payload) & 0xFFFF
        data[offset(bank, checksum_address) : offset(bank, checksum_address) + 2] = (
            value.to_bytes(2, "little")
        )
        data[offset(bank, check1)] = 99
        data[offset(bank, check2)] = 127
    box = offset(1, 0xAD10)
    data[box] = 1
    data[box + 1] = 25
    data[box + 2] = 0xFF
    return bytes(data)


def fixture_rtc() -> bytes:
    data = bytearray(40)
    data[0:4] = (0x47525443).to_bytes(4, "little")
    data[4:8] = (2).to_bytes(4, "little")
    data[8:16] = (1700000000).to_bytes(8, "little")
    data[24:29] = bytes([4, 3, 2, 1, 0])
    return bytes(data)


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    verifier = (
        root
        / "ports/pokemon-crystal/scripts/verify_transactional_persistence.py"
    )
    with tempfile.TemporaryDirectory() as raw:
        directory = Path(raw)
        save_path = directory / "pokemon_crystal.sav"
        rtc_path = directory / "pokemon_crystal.rtc"
        result_path = directory / "route.json"
        receipt_path = directory / "receipt.json"
        output_path = directory / "verification.json"
        save = fixture_save()
        rtc = fixture_rtc()
        save_path.write_bytes(save)
        rtc_path.write_bytes(rtc)
        route = {
            "schema": "gbrecompiled.pokemon-crystal.route-result",
            "version": 1,
            "passed": True,
            "persistence": {
                "sav": {"sha256": hashlib.sha256(save).hexdigest()},
                "rtc": {"sha256": hashlib.sha256(rtc).hexdigest()},
            },
        }
        receipt = {
            "schema": "crystal-recompiled.generation",
            "version": 1,
            "rom": {
                "name": "pokemon_crystal.gbc",
                "size": 2097152,
                "sha256": "fdcc3c8c43813cf8731fc037d2a6d191bac75439c34b24ba1c27526e6acdc8a2",
            },
        }
        result_path.write_text(json.dumps(route), encoding="utf-8")
        receipt_path.write_text(json.dumps(receipt), encoding="utf-8")

        def run() -> int:
            return subprocess.run(
                [
                    sys.executable,
                    str(verifier),
                    "--route-result",
                    str(result_path),
                    "--generation-receipt",
                    str(receipt_path),
                    "--save",
                    str(save_path),
                    "--rtc",
                    str(rtc_path),
                    "--output",
                    str(output_path),
                ],
                capture_output=True,
                check=False,
            ).returncode

        if run() != 0:
            raise AssertionError("verifier rejected a valid redundant save")
        bad_checksum = bytearray(save)
        bad_checksum[offset(1, 0xA009)] ^= 1
        save_path.write_bytes(bad_checksum)
        route["persistence"]["sav"]["sha256"] = hashlib.sha256(bad_checksum).hexdigest()
        result_path.write_text(json.dumps(route), encoding="utf-8")
        if run() == 0:
            raise AssertionError("verifier accepted an invalid primary checksum")
        save_path.write_bytes(save)
        route["persistence"]["sav"]["sha256"] = hashlib.sha256(save).hexdigest()
        bad_rtc = bytearray(rtc)
        bad_rtc[4] = 1
        rtc_path.write_bytes(bad_rtc)
        route["persistence"]["rtc"]["sha256"] = hashlib.sha256(bad_rtc).hexdigest()
        result_path.write_text(json.dumps(route), encoding="utf-8")
        if run() == 0:
            raise AssertionError("verifier accepted legacy RTC as committed v2")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
