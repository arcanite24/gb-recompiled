#!/usr/bin/env python3
"""Validate deterministic Crystal Workbench presentation evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


def load(path: Path) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"cannot read JSON {path}: {error}") from error
    if not isinstance(value, dict):
        raise ValueError(f"JSON root must be an object: {path}")
    return value


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require_text(texts: list[str], expected: str) -> None:
    if expected not in texts:
        raise ValueError(f"missing Workbench text: {expected}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port-state", type=Path, required=True)
    parser.add_argument("--repeat-port-state", type=Path, required=True)
    parser.add_argument("--guest-state", type=Path, required=True)
    parser.add_argument("--repeat-guest-state", type=Path, required=True)
    parser.add_argument("--semantic-result", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    port = load(args.port_state)
    repeat_port = load(args.repeat_port_state)
    semantic = load(args.semantic_result)
    if port != repeat_port:
        raise ValueError("repeated Workbench host frames differ")
    if args.guest_state.read_bytes() != args.repeat_guest_state.read_bytes():
        raise ValueError("repeated Workbench guest states differ")
    if (
        port.get("schema") != "gbrecompiled.port-state"
        or port.get("version") != 2
        or port.get("module_id") != "crystal-workbench"
        or port.get("module_version") != 2
        or port.get("active") is not True
        or port.get("headless") is not True
    ):
        raise ValueError("invalid Workbench port-state identity")
    if (
        semantic.get("schema")
        != "crystal-recompiled.semantic-accessor-verification"
        or semantic.get("passed") is not True
        or semantic.get("live_save_equal") is not True
        or semantic.get("independent_decoder_equal") is not True
    ):
        raise ValueError("semantic live/save verification did not pass")

    values = semantic.get("values")
    frame = port.get("frame")
    if not isinstance(values, dict) or not isinstance(frame, dict):
        raise ValueError("missing semantic values or host frame")
    commands = frame.get("commands")
    if not isinstance(commands, list) or len(commands) < 10:
        raise ValueError("Workbench frame is incomplete")
    texts = [
        command["text"]
        for command in commands
        if isinstance(command, dict)
        and command.get("type") == "text"
        and isinstance(command.get("text"), str)
    ]
    location = values["location"]
    party = values["party"]
    badges = values["badges"]
    pokedex = values["pokedex"]
    species = values["species"]
    require_text(texts, "Crystal Recompiled - Pokegear Workbench")
    require_text(
        texts,
        f"Location  Map {location['map_group']}:{location['map_number']}  "
        f"Position {location['x']},{location['y']}",
    )
    party_text = f"Party  {party['count']}/6" + "".join(
        f"  #{species_id}"
        for species_id in party["species"][: party["count"]]
    )
    require_text(texts, party_text)
    require_text(
        texts,
        f"Badges  {badges['total_count']}/16  "
        f"(Johto {badges['johto_count']}/8, Kanto {badges['kanto_count']}/8)",
    )
    require_text(
        texts,
        f"Pokedex  caught {pokedex['caught_count']}/251  "
        f"seen {pokedex['seen_count']}/251",
    )
    require_text(
        texts,
        f"Species #{species['species_id']}  HP {species['hp']}  "
        f"Atk {species['attack']}  Def {species['defense']}  "
        f"Spd {species['speed']}  SpA {species['special_attack']}  "
        f"SpD {species['special_defense']}",
    )
    if species.get("encounter_knowledge") != 1:
        raise ValueError("expected explicitly unmodeled encounter knowledge")
    require_text(
        texts,
        "Encounters  unavailable - encounter tables are not modeled yet",
    )
    evolutions = species.get("evolutions")
    moves = species.get("level_moves")
    if not isinstance(evolutions, list) or not evolutions:
        raise ValueError("species page has no evolution information")
    if not isinstance(moves, list) or not moves:
        raise ValueError("species page has no move information")
    evolution = evolutions[0]
    require_text(
        texts,
        f"Evolution  method #{evolution['method']}  "
        f"parameter {evolution['parameter']}  "
        f"-> species #{evolution['target_species']}",
    )
    move_text = "Level-up moves" + "".join(
        f"  L{move['level']}:#{move['move_id']}" for move in moves[:8]
    )
    require_text(texts, move_text)

    result = {
        "schema": "crystal-recompiled.workbench-validation",
        "version": 1,
        "passed": True,
        "module_version": port["module_version"],
        "command_count": len(commands),
        "deterministic_host_frame": True,
        "deterministic_guest_state": True,
        "live_save_equal": True,
        "independent_decoder_equal": True,
        "port_state_sha256": sha256(args.port_state),
        "guest_state_sha256": sha256(args.guest_state),
        "semantic_result_sha256": sha256(args.semantic_result),
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
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
