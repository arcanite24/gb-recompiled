#!/usr/bin/env python3
"""Three-way validation for port-authored Crystal writable-save fixtures."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path
from typing import Any


PRIMARY = {
    "check1": 0x2008,
    "start": 0x2009,
    "end": 0x2B83,
    "checksum": 0x2D0D,
    "check2": 0x2D0F,
    "delta": 0,
}
BACKUP = {
    "check1": 0x1208,
    "start": 0x1209,
    "end": 0x1D83,
    "checksum": 0x1F0D,
    "check2": 0x1F0F,
    "delta": -0xE00,
}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def checksum_valid(data: bytes, layout: dict[str, int]) -> bool:
    calculated = sum(data[layout["start"] : layout["end"]]) & 0xFFFF
    stored = int.from_bytes(
        data[layout["checksum"] : layout["checksum"] + 2], "little"
    )
    return (
        data[layout["check1"]] == 99
        and data[layout["check2"]] == 127
        and stored == calculated
    )


def selected_copy(data: bytes) -> tuple[str, dict[str, int]]:
    if checksum_valid(data, PRIMARY):
        return "primary", PRIMARY
    if checksum_valid(data, BACKUP):
        return "backup", BACKUP
    raise ValueError("both Crystal save copies are invalid")


def decode_name(raw: bytes) -> str:
    output: list[str] = []
    for value in raw:
        if value == 0x50:
            break
        if 0x80 <= value <= 0x99:
            output.append(chr(ord("A") + value - 0x80))
        elif 0xA0 <= value <= 0xB9:
            output.append(chr(ord("a") + value - 0xA0))
        elif 0xF6 <= value <= 0xFF:
            output.append(chr(ord("0") + value - 0xF6))
        elif value == 0x7F:
            output.append(" ")
        else:
            output.append(f"\\x{value:02x}")
    return "".join(output)


def decode_party(data: bytes, delta: int) -> list[dict[str, Any]]:
    record = data[0x2865 + delta : 0x2865 + delta + 428]
    count = record[0]
    if count > 6:
        raise ValueError("invalid party count")
    result = []
    for slot in range(count):
        mon = record[8 + slot * 48 : 8 + (slot + 1) * 48]
        result.append(
            {
                "species": mon[0],
                "nickname": decode_name(
                    record[362 + slot * 11 : 373 + slot * 11]
                ),
                "original_trainer": decode_name(
                    record[296 + slot * 11 : 307 + slot * 11]
                ),
                "level": mon[31],
                "held_item": mon[1],
                "moves": list(mon[2:6]),
            }
        )
    return result


def decode_active_box(
    data: bytes, delta: int
) -> tuple[int, list[dict[str, Any]]]:
    current = data[0x2700 + delta]
    if current > 13:
        raise ValueError("invalid current box")
    offset = (
        (2 + current // 7) * 0x2000
        + (current % 7) * 1102
    )
    record = data[offset : offset + 1102]
    count = record[0]
    if count > 20:
        raise ValueError("invalid box count")
    result = []
    for slot in range(count):
        mon = record[22 + slot * 32 : 22 + (slot + 1) * 32]
        result.append(
            {
                "species": mon[0],
                "nickname": decode_name(
                    record[882 + slot * 11 : 893 + slot * 11]
                ),
                "original_trainer": decode_name(
                    record[662 + slot * 11 : 673 + slot * 11]
                ),
                "level": mon[31],
                "held_item": mon[1],
                "moves": list(mon[2:6]),
            }
        )
    return current, result


def cycle_input(path: Path) -> str:
    actions = load_json(path)
    return ",".join(
        f"c{action['cycle']}:{action['buttons']}:{action['duration']}"
        for action in actions
    )


def comparable_pokemon(pokemon: list[dict[str, Any]]) -> list[dict[str, Any]]:
    keys = (
        "species",
        "nickname",
        "original_trainer",
        "level",
        "held_item",
        "moves",
    )
    return [{key: entry[key] for key in keys} for entry in pokemon]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--save", type=Path, required=True)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--accessor-dir", type=Path, required=True)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--dotnet", type=Path, required=True)
    parser.add_argument("--pkhex-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[3]
    save = args.save.resolve()
    rom = args.rom.resolve()
    accessor_dir = args.accessor_dir.resolve()
    executable = args.executable.resolve()
    output_dir = args.output_dir.resolve()
    fixture_manifest = load_json(
        root
        / "ports/pokemon-crystal/tests/fixtures/writable-save-fixtures.json"
    )
    if (
        save.stat().st_size != 0x8000
        or rom.stat().st_size != 0x200000
        or not executable.is_file()
        or fixture_manifest.get("version") != 1
    ):
        raise ValueError("invalid verifier input")
    output_dir.mkdir(parents=True, exist_ok=True)

    compiler = os.environ.get("CC") or shutil.which("cc")
    if compiler is None:
        raise RuntimeError("no C compiler found")
    fixture_probe = output_dir / "writable-save-fixture-probe"
    subprocess.run(
        [
            compiler,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(root / "runtime/include"),
            "-I",
            str(accessor_dir),
            str(root / "runtime/src/gbrt_semantic.c"),
            str(accessor_dir / "crystal_semantic.c"),
            str(
                root
                / "ports/pokemon-crystal/tools/"
                "writable_save_fixture_probe.c"
            ),
            "-o",
            str(fixture_probe),
        ],
        check=True,
    )

    rendered_input = cycle_input(
        root / "ports/pokemon-crystal/route/inputs/restart-continue.json"
    )
    fixture_results = []
    baseline_state: Path | None = None
    for fixture in fixture_manifest["fixtures"]:
        fixture_id = fixture["id"]
        fixture_dir = output_dir / fixture_id
        fixture_dir.mkdir(parents=True, exist_ok=True)
        fixture_save = fixture_dir / "fixture.sav"
        export = json.loads(
            subprocess.check_output(
                [
                    str(fixture_probe),
                    str(save),
                    str(rom),
                    fixture_id,
                    str(fixture_save),
                ],
                text=True,
            )
        )
        raw = fixture_save.read_bytes()
        selected, layout = selected_copy(raw)
        if selected != fixture["expected_selected_copy"]:
            raise AssertionError(
                f"{fixture_id}: selected {selected}, expected "
                f"{fixture['expected_selected_copy']}"
            )

        persistence = fixture_dir / "guest-persistence"
        persistence.mkdir(exist_ok=True)
        shutil.copy2(fixture_save, persistence / "pokemon_crystal.sav")
        state = fixture_dir / "guest-state.json"
        runtime_log = fixture_dir / "guest-runtime.log"
        subprocess.run(
            [
                str(executable),
                "--headless",
                "--limit-frames",
                "3500",
                "--input",
                rendered_input,
                "--dump-state",
                str(state),
                "--save-dir",
                str(persistence),
                "--log-file",
                str(runtime_log),
                "--no-audio",
                "--rtc-unix-time",
                "1700000000",
                "--ignore-rtc-persistence",
                "--log-frame-fallbacks",
                "--report-interpreter-hotspots",
                "--interpreter-hotspot-limit",
                "16",
            ],
            check=True,
        )
        state_data = load_json(state)
        if (
            state_data.get("dispatch_fallbacks") != 0
            or "[INTERP] No interpreter fallback recorded."
            not in runtime_log.read_text(encoding="utf-8")
        ):
            raise AssertionError(f"{fixture_id}: guest used fallback")
        if baseline_state is None:
            baseline_state = state

        port_dir = fixture_dir / "port-accessors"
        subprocess.run(
            [
                "python3",
                str(
                    root
                    / "ports/pokemon-crystal/scripts/"
                    "verify_semantic_accessors.py"
                ),
                "--state",
                str(state),
                "--save",
                str(fixture_save),
                "--rom",
                str(rom),
                "--accessor-dir",
                str(accessor_dir),
                "--output-dir",
                str(port_dir),
            ],
            check=True,
            stdout=subprocess.DEVNULL,
        )
        port_result = load_json(port_dir / "result.json")
        if port_result["selected_save_copy"] != selected:
            raise AssertionError(f"{fixture_id}: accessor selection drift")

        repaired_save = persistence / "pokemon_crystal.sav"
        pkhex_save = (
            repaired_save
            if fixture["pkhex_input"] == "guest-repaired-fixture"
            else fixture_save
        )
        pkhex_dir = fixture_dir / "pkhex"
        subprocess.run(
            [
                "python3",
                str(
                    root
                    / "ports/pokemon-crystal/scripts/run_pkhex_oracle.py"
                ),
                "--save",
                str(pkhex_save),
                "--pkhex-dir",
                str(args.pkhex_dir.resolve()),
                "--dotnet",
                str(args.dotnet.resolve()),
                "--output-dir",
                str(pkhex_dir),
            ],
            check=True,
            stdout=subprocess.DEVNULL,
        )
        pkhex = load_json(pkhex_dir / "result.json")
        if not pkhex["accepted"]:
            raise AssertionError(f"{fixture_id}: PKHeX rejected selected save")

        party = decode_party(raw, layout["delta"])
        current_box, active_box = decode_active_box(raw, layout["delta"])
        pkhex_box = pkhex["boxes"][current_box]["pokemon"]
        if (
            comparable_pokemon(pkhex["party"])
            != comparable_pokemon(party)
            or comparable_pokemon(pkhex_box)
            != comparable_pokemon(active_box)
            or pkhex["current_box"] != current_box
            or len(pkhex["pokedex"]["caught"])
            != port_result["values"]["pokedex"]["caught_count"]
            or len(pkhex["pokedex"]["seen"])
            != port_result["values"]["pokedex"]["seen_count"]
            or pkhex["player"]["badges"]
            != (
                port_result["values"]["badges"]["johto_bits"]
                | port_result["values"]["badges"]["kanto_bits"] << 8
            )
        ):
            raise AssertionError(f"{fixture_id}: three-way decode mismatch")

        player_offset = 0x2009 + layout["delta"]
        player = {
            "trainer_id": int.from_bytes(
                raw[player_offset : player_offset + 2], "big"
            ),
            "name": decode_name(raw[player_offset + 2 : player_offset + 13]),
            "money": int.from_bytes(
                raw[0x23DC + layout["delta"] : 0x23DF + layout["delta"]],
                "big",
            ),
        }
        if any(pkhex["player"][key] != value for key, value in player.items()):
            raise AssertionError(f"{fixture_id}: player-data mismatch")

        repaired = repaired_save.read_bytes()
        repaired_checksums = {
            "primary": checksum_valid(repaired, PRIMARY),
            "backup": checksum_valid(repaired, BACKUP),
        }
        if not all(repaired_checksums.values()):
            raise AssertionError(
                f"{fixture_id}: guest did not leave both copies valid"
            )
        fixture_results.append(
            {
                "id": fixture_id,
                "operation": fixture["operation"],
                "fixture_sha256": sha256(fixture_save),
                "selected_copy": selected,
                "guest_state_sha256": sha256(state),
                "guest_repaired_save_sha256": sha256(repaired_save),
                "guest_fallbacks": 0,
                "port_accessor_result_sha256": sha256(
                    port_dir / "result.json"
                ),
                "pkhex_input": fixture["pkhex_input"],
                "pkhex_result_sha256": sha256(pkhex_dir / "result.json"),
                "pkhex_accepted": True,
                "party_count": len(party),
                "active_box_count": len(active_box),
                "pokedex_caught_count": len(pkhex["pokedex"]["caught"]),
                "pokedex_seen_count": len(pkhex["pokedex"]["seen"]),
                "player": player,
                "repaired_checksums": repaired_checksums,
                "export": export,
            }
        )

    if baseline_state is None:
        raise AssertionError("fixture matrix is empty")
    invalid_dir = output_dir / "both-checksums-invalid"
    invalid_dir.mkdir(exist_ok=True)
    invalid = bytearray(save.read_bytes())
    invalid[PRIMARY["checksum"]] ^= 1
    invalid[BACKUP["checksum"]] ^= 1
    invalid_save = invalid_dir / "fixture.sav"
    invalid_save.write_bytes(invalid)
    rejected = subprocess.run(
        [
            "python3",
            str(
                root
                / "ports/pokemon-crystal/scripts/"
                "verify_semantic_accessors.py"
            ),
            "--state",
            str(baseline_state),
            "--save",
            str(invalid_save),
            "--rom",
            str(rom),
            "--accessor-dir",
            str(accessor_dir),
            "--output-dir",
            str(invalid_dir / "port-accessors"),
        ],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    if rejected.returncode == 0:
        raise AssertionError("both-invalid negative control was accepted")

    result = {
        "schema": "crystal-recompiled.writable-save-verification",
        "version": 1,
        "passed": True,
        "rom_sha256": sha256(rom),
        "base_save_sha256": sha256(save),
        "accessor_source_sha256": sha256(
            accessor_dir / "crystal_semantic.c"
        ),
        "executable_sha256": sha256(executable),
        "fixture_manifest_sha256": sha256(
            root
            / "ports/pokemon-crystal/tests/fixtures/"
            "writable-save-fixtures.json"
        ),
        "fixtures": fixture_results,
        "negative_controls": {
            "both_checksums_invalid_rejected": True,
        },
        "legal_boundary": {
            "rom_committed": False,
            "base_save_committed": False,
            "pkhex_linked_into_runtime_or_port": False,
            "pkhex_process_only": True,
        },
    }
    (output_dir / "result.json").write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
