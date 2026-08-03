#!/usr/bin/env python3
"""Verify the M8 wild-level tracer against a fresh vanilla executable."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path
from typing import Any

from validate_route import cycle_input, parse_fallback_log


ROM_SHA256 = "fdcc3c8c43813cf8731fc037d2a6d191bac75439c34b24ba1c27526e6acdc8a2"
ROM_SIZE = 2_097_152
WILD_FUNCTION_ID = "gbfn:v1:000f:68eb"
EXPECTED_ORIGINAL_LEVEL = 2
EXPECTED_CHALLENGE_LEVEL = 8
EXPECTED_REFERENCE_LEVEL = 5
WRAM_BASE = 0xD000
U8_FIELDS = {
    "party_level": 0xD143,
    "enemy_level": 0xD213,
    "battle_mode": 0xD22D,
}
U16_FIELDS = {
    "max_hp": 0xD218,
    "attack": 0xD21A,
    "defense": 0xD21C,
    "speed": 0xD21E,
    "special_attack": 0xD220,
    "special_defense": 0xD222,
}


class VerificationError(RuntimeError):
    pass


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load(path: Path) -> dict[str, Any]:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise VerificationError(f"cannot read JSON {path.name}: {error}") from error
    if not isinstance(payload, dict):
        raise VerificationError(f"JSON root must be an object: {path.name}")
    return payload


def validate_receipt(path: Path, expect_patch: bool) -> dict[str, Any]:
    receipt = load(path)
    rom = receipt.get("rom")
    native_patch = receipt.get("native_patch")
    if (
        receipt.get("schema") != "crystal-recompiled.generation"
        or receipt.get("version") != 1
        or rom != {
            "name": "pokemon_crystal.gbc",
            "sha256": ROM_SHA256,
            "size": ROM_SIZE,
        }
        or not isinstance(native_patch, dict)
    ):
        raise VerificationError("generation receipt has the wrong identity")
    kind = native_patch.get("kind")
    if (expect_patch and kind != "file") or (not expect_patch and kind != "none"):
        raise VerificationError("generation receipt has the wrong patch mode")
    return receipt


def run_segment(
    executable: Path,
    root: Path,
    persistence: Path,
    input_path: Path,
    frames: int,
    dump_state: bool,
    host_configuration: Path | None = None,
) -> Path | None:
    root.mkdir()
    state_path = root / "state.json"
    log_path = root / "runtime.log"
    command = [
        str(executable),
        "--headless",
        "--no-audio",
        "--limit-frames",
        str(frames),
        "--input",
        cycle_input(input_path),
        "--save-dir",
        str(persistence),
        "--log-file",
        str(log_path),
        "--rtc-unix-time",
        "1700000000",
        "--ignore-rtc-persistence",
        "--log-frame-fallbacks",
        "--report-interpreter-hotspots",
    ]
    if dump_state:
        command.extend(["--dump-state", str(state_path)])
    if host_configuration is not None:
        command.extend(["--host-configuration", str(host_configuration)])
    completed = subprocess.run(command, text=True, capture_output=True)
    (root / "launcher.stdout").write_text(completed.stdout, encoding="utf-8")
    (root / "launcher.stderr").write_text(completed.stderr, encoding="utf-8")
    if completed.returncode != 0:
        raise VerificationError(f"{root.parent.name}/{root.name} exited nonzero")
    if host_configuration is not None:
        log_text = log_path.read_text(encoding="utf-8") if log_path.is_file() else ""
        diagnostics = completed.stdout + completed.stderr + log_text
        if "[HOST-CONFIG] status=applied policy=challenge-v1 hash=" not in diagnostics:
            raise VerificationError(f"{root.parent.name}/{root.name} omitted configuration identity")
        if str(host_configuration.resolve()) in diagnostics:
            raise VerificationError(f"{root.parent.name}/{root.name} leaked configuration path")
    fallback = parse_fallback_log(log_path)
    if fallback["sites"] or fallback["summary"]["fallbacks"] != 0:
        raise VerificationError(f"{root.parent.name}/{root.name} used fallback")
    if dump_state and not state_path.is_file():
        raise VerificationError(f"{root.parent.name}/{root.name} omitted state")
    return state_path if dump_state else None


def battle_values(state_path: Path) -> tuple[dict[str, int], dict[str, Any]]:
    state = load(state_path)
    wram = state.get("wram_bank_1_d000_dfff")
    if (
        not isinstance(wram, list)
        or len(wram) != 4096
        or any(not isinstance(value, int) or not 0 <= value <= 255 for value in wram)
    ):
        raise VerificationError("state omitted byte-complete bank-1 WRAM")
    values = {
        name: wram[address - WRAM_BASE] for name, address in U8_FIELDS.items()
    }
    values.update(
        {
            name: (wram[address - WRAM_BASE] << 8)
            | wram[address - WRAM_BASE + 1]
            for name, address in U16_FIELDS.items()
        }
    )
    party_count = wram[0xDCD7 - WRAM_BASE]
    if party_count > 6:
        raise VerificationError("state has an invalid player party count")
    strongest = 0
    for index in range(party_count):
        mon = 0xDCDF + index * 48
        level = wram[mon + 31 - WRAM_BASE]
        hp = (wram[mon + 34 - WRAM_BASE] << 8) | wram[
            mon + 35 - WRAM_BASE
        ]
        if hp != 0:
            strongest = max(strongest, level)
    values["strongest_conscious_level"] = strongest
    values["badge_count"] = sum(
        value.bit_count() for value in wram[0xD857 - WRAM_BASE : 0xD859 - WRAM_BASE]
    )
    transaction = state.get("semantic_transaction")
    if transaction is None:
        transaction = {"outcome": "none", "dirty_ranges": []}
    if not isinstance(transaction, dict):
        raise VerificationError("state has malformed semantic transaction metadata")
    return values, transaction


def persistence_hashes(root: Path) -> dict[str, str]:
    files = sorted(path for path in root.iterdir() if path.is_file())
    if not files:
        raise VerificationError("route did not create persistence")
    return {path.name: sha256(path) for path in files}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--vanilla-executable", required=True, type=Path)
    parser.add_argument("--challenge-executable", required=True, type=Path)
    parser.add_argument("--vanilla-receipt", required=True, type=Path)
    parser.add_argument("--challenge-receipt", required=True, type=Path)
    parser.add_argument("--challenge-manifest", required=True, type=Path)
    parser.add_argument("--challenge-configuration", required=True, type=Path)
    parser.add_argument("--inputs", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    output = args.output.resolve()
    if output.exists():
        if not output.is_dir() or any(output.iterdir()):
            raise VerificationError("output must be absent or empty")
    else:
        output.mkdir(parents=True)

    vanilla_executable = args.vanilla_executable.resolve()
    challenge_executable = args.challenge_executable.resolve()
    for executable in (vanilla_executable, challenge_executable):
        if not executable.is_file():
            raise VerificationError("generated executable is missing")
    vanilla_receipt = validate_receipt(args.vanilla_receipt.resolve(), False)
    challenge_receipt = validate_receipt(args.challenge_receipt.resolve(), True)
    manifest = load(args.challenge_manifest.resolve())
    bindings = manifest.get("bindings")
    wild_binding = next(
        (
            binding
            for binding in bindings
            if isinstance(binding, dict)
            and binding.get("function") == WILD_FUNCTION_ID
        ),
        None,
    ) if isinstance(bindings, list) else None
    if (
        manifest.get("schema") != "gbrecomp.native-patch"
        or manifest.get("version") != 1
        or manifest.get("rom") != {"sha256": ROM_SHA256, "size": ROM_SIZE}
        or not isinstance(wild_binding, dict)
        or wild_binding.get("pre") != "crystal_challenge_wild_level"
    ):
        raise VerificationError("challenge manifest is not the reviewed wild hook")

    inputs = args.inputs.resolve()
    challenge_configuration = args.challenge_configuration.resolve()
    if not challenge_configuration.is_file():
        raise VerificationError("challenge configuration is missing")
    modes = {
        "vanilla": (vanilla_executable, vanilla_receipt, None),
        "challenge": (
            challenge_executable,
            challenge_receipt,
            challenge_configuration,
        ),
    }
    mode_results: dict[str, dict[str, Any]] = {}
    for name, (executable, receipt, host_configuration) in modes.items():
        mode_root = output / name
        mode_root.mkdir()
        persistence = mode_root / "persistence"
        persistence.mkdir()
        run_segment(
            executable,
            mode_root / "01-new-game",
            persistence,
            inputs / "new-game.json",
            12_050,
            False,
            host_configuration,
        )
        state_path = run_segment(
            executable,
            mode_root / "02-wild-battle",
            persistence,
            inputs / "adventure.json",
            16_000,
            True,
            host_configuration,
        )
        if state_path is None:
            raise VerificationError("wild segment omitted state")
        values, transaction = battle_values(state_path)
        mode_results[name] = {
            "executable_sha256": sha256(executable),
            "generation_receipt_sha256": sha256(
                args.vanilla_receipt.resolve()
                if name == "vanilla"
                else args.challenge_receipt.resolve()
            ),
            "metadata_sha256": receipt["generated"]["metadata_sha256"],
            "source_inventory_sha256": receipt["generated"][
                "source_inventory_sha256"
            ],
            "state_sha256": sha256(state_path),
            "host_configuration": load(state_path).get("host_configuration"),
            "battle": values,
            "semantic_transaction": transaction,
            "fallback_sites": 0,
            "persistence": persistence_hashes(persistence),
        }

    vanilla = mode_results["vanilla"]
    challenge = mode_results["challenge"]
    vanilla_battle = vanilla["battle"]
    challenge_battle = challenge["battle"]
    if (
        vanilla_battle["battle_mode"] != 1
        or challenge_battle["battle_mode"] != 1
        or vanilla_battle["party_level"] != EXPECTED_ORIGINAL_LEVEL
        or vanilla_battle["enemy_level"] != EXPECTED_ORIGINAL_LEVEL
        or challenge_battle["party_level"] != EXPECTED_CHALLENGE_LEVEL
        or challenge_battle["enemy_level"] != EXPECTED_CHALLENGE_LEVEL
        or vanilla_battle["strongest_conscious_level"]
        != EXPECTED_REFERENCE_LEVEL
        or challenge_battle["strongest_conscious_level"]
        != EXPECTED_REFERENCE_LEVEL
        or vanilla_battle["badge_count"] != 0
        or challenge_battle["badge_count"] != 0
    ):
        raise VerificationError("wild level mutation did not match the locked route")
    stat_fields = tuple(U16_FIELDS)
    if (
        challenge_battle["max_hp"] <= vanilla_battle["max_hp"]
        or sum(
            challenge_battle[field] != vanilla_battle[field]
            for field in stat_fields
        )
        != len(stat_fields)
    ):
        raise VerificationError("original body did not coherently derive enemy stats")
    transaction = challenge["semantic_transaction"]
    if transaction.get("outcome") != "committed" or transaction.get(
        "dirty_ranges"
    ) != [{"space": 3, "bank": 1, "address": 0xD143, "width": 1}]:
        raise VerificationError("challenge write escaped the reviewed field")
    if challenge["host_configuration"] != {
        "present": True,
        "applied": True,
        "enabled": True,
        "policy_id": "challenge-v1",
        "sha256": sha256(challenge_configuration),
    }:
        raise VerificationError("challenge state has the wrong configuration identity")
    if vanilla["persistence"] != challenge["persistence"]:
        raise VerificationError("wild tracer changed guest persistence")

    result = {
        "schema": "crystal-recompiled.challenge-wild-proof",
        "version": 1,
        "passed": True,
        "rom": {"sha256": ROM_SHA256, "size": ROM_SIZE},
        "hook": WILD_FUNCTION_ID,
        "event": "crystal.wild-level.v1",
        "rule": {
            "id": "challenge-v1",
            "formula": "max(original, strongest + floor(badges / 4) + offset)",
            "offset": 3,
            "minimum": 1,
            "maximum": 100,
        },
        "challenge_manifest_sha256": sha256(args.challenge_manifest.resolve()),
        "challenge_configuration_sha256": sha256(challenge_configuration),
        "inputs_sha256": {
            name: sha256(inputs / name) for name in ("new-game.json", "adventure.json")
        },
        "modes": mode_results,
    }
    result_path = output / "result.json"
    result_path.write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print("ok  Challenge Mode wild tracer")
    print(f"    vanilla_level={vanilla_battle['enemy_level']}")
    print(f"    challenge_level={challenge_battle['enemy_level']}")
    print(f"    result_sha256={sha256(result_path)}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except VerificationError as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
