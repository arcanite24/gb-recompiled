#!/usr/bin/env python3
"""Verify the M8 trainer-party tracer against a fresh vanilla executable."""

from __future__ import annotations

import argparse
import json
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from typing import Any

from verify_challenge_wild import (
    ROM_SHA256,
    ROM_SIZE,
    VerificationError,
    load,
    persistence_hashes,
    run_segment,
    sha256,
    validate_receipt,
)


TRAINER_FUNCTION_ID = "gbfn:v1:0003:588c"
WILD_FUNCTION_ID = "gbfn:v1:000f:68eb"
EXPECTED_SPECIES = 158
EXPECTED_ORIGINAL_LEVEL = 5
EXPECTED_CHALLENGE_LEVEL = 10
EXPECTED_REFERENCE_LEVEL = 7
WRAM_BASE = 0xD000
U8_FIELDS = {
    "current_party_level": 0xD143,
    "battle_mode": 0xD22D,
    "battle_type": 0xD230,
    "opponent_party_count": 0xD280,
    "species": 0xD288,
    "level": 0xD2A7,
}
U16_FIELDS = {
    "max_hp": 0xD2AC,
    "attack": 0xD2AE,
    "defense": 0xD2B0,
    "speed": 0xD2B2,
    "special_attack": 0xD2B4,
    "special_defense": 0xD2B6,
}


def trainer_values(state_path: Path) -> tuple[dict[str, int], dict[str, Any]]:
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
    functions = {
        binding.get("function")
        for binding in bindings
        if isinstance(binding, dict)
    } if isinstance(bindings, list) else set()
    if (
        manifest.get("schema") != "gbrecomp.native-patch"
        or manifest.get("version") != 1
        or manifest.get("rom") != {"sha256": ROM_SHA256, "size": ROM_SIZE}
        or functions != {TRAINER_FUNCTION_ID}
    ):
        raise VerificationError("challenge manifest lacks the reviewed battle hooks")

    inputs = args.inputs.resolve()
    challenge_configuration = args.challenge_configuration.resolve()
    if not challenge_configuration.is_file():
        raise VerificationError("challenge configuration is missing")
    modes = {
        "vanilla": (
            vanilla_executable,
            vanilla_receipt,
            args.vanilla_receipt.resolve(),
            None,
        ),
        "challenge": (
            challenge_executable,
            challenge_receipt,
            args.challenge_receipt.resolve(),
            challenge_configuration,
        ),
    }

    def run_mode(name: str) -> tuple[str, dict[str, Any]]:
        executable, receipt, receipt_path, host_configuration = modes[name]
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
            mode_root / "02-trainer-battle",
            persistence,
            inputs / "adventure.json",
            488_001,
            True,
            host_configuration,
        )
        if state_path is None:
            raise VerificationError("trainer segment omitted state")
        values, transaction = trainer_values(state_path)
        return name, {
            "executable_sha256": sha256(executable),
            "generation_receipt_sha256": sha256(receipt_path),
            "metadata_sha256": receipt["generated"]["metadata_sha256"],
            "source_inventory_sha256": receipt["generated"][
                "source_inventory_sha256"
            ],
            "state_sha256": sha256(state_path),
            "host_configuration": load(state_path).get("host_configuration"),
            "trainer": values,
            "semantic_transaction": transaction,
            "fallback_sites": 0,
            "persistence": persistence_hashes(persistence),
            "persistence_sizes": {
                path.name: path.stat().st_size
                for path in sorted(persistence.iterdir())
                if path.is_file()
            },
        }

    with ThreadPoolExecutor(max_workers=2) as executor:
        mode_results = dict(executor.map(run_mode, modes))

    vanilla = mode_results["vanilla"]
    challenge = mode_results["challenge"]
    vanilla_trainer = vanilla["trainer"]
    challenge_trainer = challenge["trainer"]
    for values in (vanilla_trainer, challenge_trainer):
        if (
            values["battle_mode"] != 2
            or values["battle_type"] != 1
            or values["opponent_party_count"] != 1
            or values["species"] != EXPECTED_SPECIES
        ):
            raise VerificationError("locked route did not reach the reviewed trainer")
    if (
        vanilla_trainer["current_party_level"] != EXPECTED_ORIGINAL_LEVEL
        or vanilla_trainer["level"] != EXPECTED_ORIGINAL_LEVEL
        or challenge_trainer["current_party_level"] != EXPECTED_CHALLENGE_LEVEL
        or challenge_trainer["level"] != EXPECTED_CHALLENGE_LEVEL
        or vanilla_trainer["strongest_conscious_level"]
        != EXPECTED_REFERENCE_LEVEL
        or challenge_trainer["strongest_conscious_level"]
        != EXPECTED_REFERENCE_LEVEL
        or vanilla_trainer["badge_count"] != 0
        or challenge_trainer["badge_count"] != 0
    ):
        raise VerificationError("trainer mutation did not match the locked route")
    stat_fields = tuple(U16_FIELDS)
    if (
        challenge_trainer["max_hp"] <= vanilla_trainer["max_hp"]
        or sum(
            challenge_trainer[field] != vanilla_trainer[field]
            for field in stat_fields
        )
        != len(stat_fields)
    ):
        raise VerificationError("original body did not derive trainer stats")
    transaction = challenge["semantic_transaction"]
    if transaction.get("outcome") != "committed" or transaction.get(
        "dirty_ranges"
    ) != [{"space": 3, "bank": 1, "address": 0xD143, "width": 1}]:
        raise VerificationError("trainer write escaped the reviewed field")
    if challenge["host_configuration"] != {
        "present": True,
        "applied": True,
        "enabled": True,
        "policy_id": "challenge-v1",
        "sha256": sha256(challenge_configuration),
    }:
        raise VerificationError("challenge state has the wrong configuration identity")
    if vanilla["persistence_sizes"] != challenge["persistence_sizes"]:
        raise VerificationError("battle tracers changed the persistence layout")

    result = {
        "schema": "crystal-recompiled.challenge-trainer-proof",
        "version": 1,
        "passed": True,
        "rom": {"sha256": ROM_SHA256, "size": ROM_SIZE},
        "hook": TRAINER_FUNCTION_ID,
        "event": "crystal.trainer-party-level.v1",
        "guards": {
            "battle_type_allowed": [0, 1],
            "mon_type_low_nibble": 1,
            "link_mode": 0,
            "battle_tower_bit": 0,
            "opponent_party_count_max_exclusive": 6,
        },
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
    print("ok  Challenge Mode trainer tracer")
    print(f"    vanilla_level={vanilla_trainer['level']}")
    print(f"    challenge_level={challenge_trainer['level']}")
    print(f"    result_sha256={sha256(result_path)}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except VerificationError as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
