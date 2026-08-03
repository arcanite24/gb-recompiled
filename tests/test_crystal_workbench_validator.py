#!/usr/bin/env python3
"""Exercise the Workbench evidence validator's positive and fail-closed paths."""

from __future__ import annotations

import copy
import json
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    validator = (
        root / "ports/pokemon-crystal/scripts/validate_workbench.py"
    )
    texts = [
        "Crystal Recompiled - Pokegear Workbench",
        "Location  Map 26:5  Position 8,1",
        "Party  1/6  #155",
        "Badges  0/16  (Johto 0/8, Kanto 0/8)",
        "Pokedex  caught 1/251  seen 6/251",
        "Species #155  HP 39  Atk 52  Def 43  Spd 65  SpA 60  SpD 50",
        "Encounters  unavailable - encounter tables are not modeled yet",
        "Evolution  method #1  parameter 14  -> species #156",
        "Level-up moves  L1:#33",
    ]
    port = {
        "schema": "gbrecompiled.port-state",
        "version": 2,
        "module_id": "crystal-workbench",
        "module_version": 2,
        "active": True,
        "headless": True,
        "frame": {
            "commands": [
                {"type": "panel"},
                *({"type": "text", "text": text} for text in texts),
            ]
        },
    }
    semantic = {
        "schema": "crystal-recompiled.semantic-accessor-verification",
        "passed": True,
        "live_save_equal": True,
        "independent_decoder_equal": True,
        "values": {
            "location": {
                "map_group": 26,
                "map_number": 5,
                "x": 8,
                "y": 1,
            },
            "party": {"count": 1, "species": [155, 0, 0, 0, 0, 0]},
            "badges": {
                "total_count": 0,
                "johto_count": 0,
                "kanto_count": 0,
            },
            "pokedex": {"caught_count": 1, "seen_count": 6},
            "species": {
                "species_id": 155,
                "hp": 39,
                "attack": 52,
                "defense": 43,
                "speed": 65,
                "special_attack": 60,
                "special_defense": 50,
                "encounter_knowledge": 1,
                "evolutions": [
                    {
                        "method": 1,
                        "parameter": 14,
                        "condition": 0,
                        "target_species": 156,
                    }
                ],
                "level_moves": [{"level": 1, "move_id": 33}],
            },
        },
    }
    with tempfile.TemporaryDirectory() as raw:
        directory = Path(raw)
        port_path = directory / "port.json"
        repeat_path = directory / "repeat.json"
        semantic_path = directory / "semantic.json"
        guest_path = directory / "guest.json"
        repeat_guest_path = directory / "repeat-guest.json"
        output_path = directory / "result.json"

        def write(path: Path, value: object) -> None:
            path.write_text(json.dumps(value), encoding="utf-8")

        write(port_path, port)
        write(repeat_path, port)
        write(semantic_path, semantic)
        guest_path.write_bytes(b"guest")
        repeat_guest_path.write_bytes(b"guest")
        command = [
            sys.executable,
            str(validator),
            "--port-state",
            str(port_path),
            "--repeat-port-state",
            str(repeat_path),
            "--guest-state",
            str(guest_path),
            "--repeat-guest-state",
            str(repeat_guest_path),
            "--semantic-result",
            str(semantic_path),
            "--output",
            str(output_path),
        ]
        subprocess.run(command, check=True, capture_output=True)
        invalid = copy.deepcopy(semantic)
        invalid["values"]["species"]["encounter_knowledge"] = 0
        write(semantic_path, invalid)
        if subprocess.run(command, check=False, capture_output=True).returncode == 0:
            raise AssertionError("validator accepted fabricated encounter knowledge")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
