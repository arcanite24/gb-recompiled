#!/usr/bin/env python3
"""Prove the two Crystal sample mods alone, composed, and removed."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import struct
import subprocess
from pathlib import Path
from typing import Any

from validate_route import cycle_input, parse_fallback_log


ROM_SHA256 = "fdcc3c8c43813cf8731fc037d2a6d191bac75439c34b24ba1c27526e6acdc8a2"
VANILLA_OVERWORLD_SHA256 = (
    "4fe548730b50523e70f4d7d71b54c94a9747896e368774d715165539570a9b7a"
)
VANILLA_BATTLE_SHA256 = (
    "f017ebea694dd3c9e225717fa390fb002f74dfea1dff39c7e728e71da2fd2693"
)
SENTRET = 161


class VerificationError(RuntimeError):
    pass


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise VerificationError(f"cannot read JSON {path}: {error}") from error
    if not isinstance(value, dict):
        raise VerificationError(f"JSON root must be an object: {path}")
    return value


def artifact_entries(path: Path) -> dict[int, tuple[int, int]]:
    data = path.read_bytes()
    if (
        len(data) < 92
        or data[:8] != b"GBDMOD1\0"
        or struct.unpack_from("<I", data, 8)[0] != 1
        or struct.unpack_from("<I", data, 12)[0] != 92
        or data[24:56].hex() != ROM_SHA256
    ):
        raise VerificationError(f"invalid exact-ROM artifact: {path}")
    count = struct.unpack_from("<I", data, 88)[0]
    cursor = 92
    entries: dict[int, tuple[int, int]] = {}
    for _ in range(count):
        if cursor + 8 > len(data):
            raise VerificationError(f"truncated artifact entry: {path}")
        offset, size = struct.unpack_from("<II", data, cursor)
        cursor += 8
        if size != 1 or cursor + 2 > len(data) or offset in entries:
            raise VerificationError(f"noncanonical sample artifact: {path}")
        entries[offset] = (data[cursor], data[cursor + 1])
        cursor += 2
    if cursor != len(data):
        raise VerificationError(f"artifact has trailing bytes: {path}")
    return entries


def check_report(path: Path, artifact: Path, entry_count: int) -> dict[str, Any]:
    report = load(path)
    if (
        report.get("passed") is not True
        or report.get("rom") != {"size": 2097152, "sha256": ROM_SHA256}
        or report.get("original_rom_sha256_after_compile") != ROM_SHA256
        or report.get("artifact", {}).get("entry_count") != entry_count
        or report.get("artifact", {}).get("sha256") != sha256(artifact)
    ):
        raise VerificationError(f"compile report does not bind artifact: {path}")
    return report


def run_segment(
    executable: Path,
    root: Path,
    persistence: Path,
    input_path: Path,
    frame_limit: int,
    capture_frame: int,
    artifact: Path | None,
    entry_count: int,
) -> dict[str, Any]:
    root.mkdir(parents=True)
    state_path = root / "state.json"
    log_path = root / "runtime.log"
    prefix = root / "frame"
    command = [
        str(executable),
        "--headless",
        "--limit-frames",
        str(frame_limit),
        "--input",
        cycle_input(input_path),
        "--dump-frames",
        str(capture_frame),
        "--screenshot-prefix",
        str(prefix),
        "--dump-state",
        str(state_path),
        "--save-dir",
        str(persistence),
        "--log-file",
        str(log_path),
        "--no-audio",
        "--rtc-unix-time",
        "1700000000",
        "--ignore-rtc-persistence",
        "--log-frame-fallbacks",
        "--report-interpreter-hotspots",
        "--interpreter-hotspot-limit",
        "16",
    ]
    if artifact is not None:
        command.extend(["--data-mod", str(artifact)])
    completed = subprocess.run(
        command, cwd=root, text=True, capture_output=True, check=False
    )
    (root / "launcher.stdout").write_text(completed.stdout, encoding="utf-8")
    (root / "launcher.stderr").write_text(completed.stderr, encoding="utf-8")
    if completed.returncode != 0:
        raise VerificationError(f"{root.name} exited with {completed.returncode}")
    frame = root / f"frame_{capture_frame:05d}.ppm"
    if not frame.is_file() or not state_path.is_file() or not log_path.is_file():
        raise VerificationError(f"{root.name} omitted required evidence")
    fallback = parse_fallback_log(log_path)
    if fallback["sites"] or fallback["summary"]["fallbacks"] != 0:
        raise VerificationError(f"{root.name} used interpreter fallback")
    log = log_path.read_text(encoding="utf-8")
    marker = f"[DATA-MOD] Active entries={entry_count}"
    if (artifact is None and "[DATA-MOD]" in log) or (
        artifact is not None and marker not in log
    ):
        raise VerificationError(f"{root.name} data-mod activation disagreed")
    return {
        "frame_sha256": sha256(frame),
        "state": load(state_path),
        "runtime_log_sha256": sha256(log_path),
        "fallback_sites": 0,
    }


def run_route(
    executable: Path,
    root: Path,
    inputs: Path,
    artifact: Path | None,
    entry_count: int,
) -> dict[str, Any]:
    persistence = root / "persistence"
    persistence.mkdir(parents=True)
    new_game = run_segment(
        executable,
        root / "01-new-game",
        persistence,
        inputs / "new-game.json",
        12050,
        8250,
        artifact,
        entry_count,
    )
    if new_game["frame_sha256"] != VANILLA_OVERWORLD_SHA256:
        raise VerificationError(f"{root.name} changed the pre-overlay route")
    battle = run_segment(
        executable,
        root / "02-route29-wild-battle",
        persistence,
        inputs / "adventure.json",
        14500,
        14000,
        artifact,
        entry_count,
    )
    wram = battle["state"].get("wram_bank_1_d000_dfff")
    save = persistence / "pokemon_crystal.sav"
    if (
        not isinstance(wram, list)
        or len(wram) != 4096
        or not save.is_file()
    ):
        raise VerificationError(f"{root.name} omitted battle/save state")
    return {
        "overworld_frame_sha256": new_game["frame_sha256"],
        "battle_frame_sha256": battle["frame_sha256"],
        "enemy_species": wram[0x206],
        "enemy_level": wram[0x213],
        "save_sha256": sha256(save),
        "fallback_sites": 0,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--generation-receipt", type=Path, required=True)
    parser.add_argument("--difficulty-artifact", type=Path, required=True)
    parser.add_argument("--difficulty-report", type=Path, required=True)
    parser.add_argument("--information-artifact", type=Path, required=True)
    parser.add_argument("--information-report", type=Path, required=True)
    parser.add_argument("--combined-artifact", type=Path, required=True)
    parser.add_argument("--combined-report", type=Path, required=True)
    parser.add_argument(
        "--combined-reproduction-artifact", type=Path, required=True
    )
    parser.add_argument(
        "--combined-reproduction-report", type=Path, required=True
    )
    parser.add_argument("--inputs", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    rom = args.rom.resolve()
    executable = args.executable.resolve()
    receipt_path = args.generation_receipt.resolve()
    inputs = args.inputs.resolve()
    output = args.output.resolve()
    artifacts = {
        "difficulty": args.difficulty_artifact.resolve(),
        "information": args.information_artifact.resolve(),
        "combined": args.combined_artifact.resolve(),
    }
    reports = {
        "difficulty": args.difficulty_report.resolve(),
        "information": args.information_report.resolve(),
        "combined": args.combined_report.resolve(),
    }
    reproduction_artifact = args.combined_reproduction_artifact.resolve()
    reproduction_report = args.combined_reproduction_report.resolve()
    if (
        not rom.is_file()
        or sha256(rom) != ROM_SHA256
        or not executable.is_file()
        or not receipt_path.is_file()
        or any(
            not path.is_file()
            for path in [
                *artifacts.values(),
                *reports.values(),
                reproduction_artifact,
                reproduction_report,
            ]
        )
    ):
        raise VerificationError("missing or incompatible verification input")
    receipt = load(receipt_path)
    if (
        receipt.get("schema") != "crystal-recompiled.generation"
        or receipt.get("version") != 1
        or receipt.get("rom", {}).get("sha256") != ROM_SHA256
    ):
        raise VerificationError("generation receipt is not exact Crystal Rev 1")

    entries = {name: artifact_entries(path) for name, path in artifacts.items()}
    expected_counts = {"difficulty": 42, "information": 43, "combined": 85}
    checked_reports = {
        name: check_report(reports[name], artifacts[name], expected_counts[name])
        for name in artifacts
    }
    check_report(reproduction_report, reproduction_artifact, 85)
    if (
        reproduction_artifact.read_bytes() != artifacts["combined"].read_bytes()
        or reproduction_report.read_bytes() != reports["combined"].read_bytes()
    ):
        raise VerificationError(
            "combined resolution/artifact did not reproduce byte-for-byte"
        )
    if set(entries["difficulty"]).intersection(entries["information"]):
        raise VerificationError("independent sample mods overlap")
    composed = dict(entries["difficulty"])
    composed.update(entries["information"])
    if entries["combined"] != dict(sorted(composed.items())):
        raise VerificationError("combined artifact is not the exact deterministic union")
    if (
        checked_reports["difficulty"].get("encounter_change_count") != 21
        or checked_reports["difficulty"].get("information_sign_count") != 0
        or checked_reports["information"].get("encounter_change_count") != 0
        or checked_reports["information"].get("information_sign_count") != 1
        or checked_reports["combined"].get("encounter_change_count") != 21
        or checked_reports["combined"].get("information_sign_count") != 1
    ):
        raise VerificationError("sample target counts disagree")
    sign_bytes = bytes(
        entries["information"][offset][1]
        for offset in range(0x1A15B9, 0x1A15B9 + 43)
    )
    expected_text_bytes = bytes.fromhex(
        "00918e9493847ff8ff51"
        "808cf38380989c7f8f8883868498"
        "4f"
        "8d8893849c7f878e8e93878e8e93"
        "7f7f7f"
        "57"
    )
    if sign_bytes != expected_text_bytes:
        raise VerificationError("information sample did not encode its promised sign")

    if output.exists():
        if not output.is_dir() or output.is_symlink():
            raise VerificationError("output must be a directory")
        shutil.rmtree(output)
    output.mkdir(parents=True)
    modes = {
        "vanilla": (None, 0),
        "difficulty": (artifacts["difficulty"], 42),
        "information": (artifacts["information"], 43),
        "combined": (artifacts["combined"], 85),
    }
    routes = {
        name: run_route(executable, output / name, inputs, artifact, count)
        for name, (artifact, count) in modes.items()
    }
    if (
        routes["vanilla"]["battle_frame_sha256"] != VANILLA_BATTLE_SHA256
        or (routes["vanilla"]["enemy_species"], routes["vanilla"]["enemy_level"])
        != (SENTRET, 2)
        or (routes["information"]["enemy_species"], routes["information"]["enemy_level"])
        != (SENTRET, 2)
        or (routes["difficulty"]["enemy_species"], routes["difficulty"]["enemy_level"])
        != (SENTRET, 5)
        or (routes["combined"]["enemy_species"], routes["combined"]["enemy_level"])
        != (SENTRET, 5)
    ):
        raise VerificationError("sample gameplay effects disagreed")
    save_hashes = {route["save_sha256"] for route in routes.values()}
    if len(save_hashes) != 1:
        raise VerificationError("sample mods changed save compatibility")

    removal_root = output / "removed"
    removal_persistence = removal_root / "persistence"
    shutil.copytree(output / "combined/persistence", removal_persistence)
    save_before = sha256(removal_persistence / "pokemon_crystal.sav")
    removed = run_segment(
        executable,
        removal_root / "01-vanilla-restart",
        removal_persistence,
        inputs / "restart-continue.json",
        3500,
        1500,
        None,
        0,
    )
    save_after = sha256(removal_persistence / "pokemon_crystal.sav")
    if save_before != save_after or save_before not in save_hashes:
        raise VerificationError("vanilla restart did not preserve the modded-run save")

    result = {
        "schema": "gbrecompiled.pokemon-crystal.sample-mod-proof",
        "version": 1,
        "passed": True,
        "rom_sha256": ROM_SHA256,
        "executable_sha256": sha256(executable),
        "generation_receipt_sha256": sha256(receipt_path),
        "artifacts": {
            name: {
                "sha256": sha256(artifacts[name]),
                "entry_count": len(entries[name]),
                "package_set_sha256": checked_reports[name]["package_set_sha256"],
            }
            for name in artifacts
        },
        "deterministic_composition": {
            "load_order": [
                "org.gbrecompiled.crystal.route29-level-five",
                "org.gbrecompiled.crystal.route29-encounter-guide",
            ],
            "individual_ranges_disjoint": True,
            "combined_entries_equal_ordered_union": True,
            "combined_artifact_reproduced_byte_for_byte": True,
            "combined_report_reproduced_byte_for_byte": True,
        },
        "routes": routes,
        "save_compatibility": {
            "all_modes_sha256": next(iter(save_hashes)),
            "byte_identical_across_modes": True,
            "removed_mod_restart_frame_sha256": removed["frame_sha256"],
            "removed_mod_restart_zero_fallback": True,
            "removed_mod_restart_save_unchanged": True,
        },
        "rom_rewritten": False,
        "generated_c_regenerated_between_modes": False,
    }
    (output / "result.json").write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, TypeError, VerificationError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
