#!/usr/bin/env python3
"""Compare generated accessors with an independent Crystal save decoder."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path


LIVE = {
    "location": (0xCB5, 4),
    "party": (0xCD7, 296),
    "badges": (0x857, 2),
    "pokedex": (0xE99, 64),
}
PRIMARY_SAVE = {
    "location": (0x2843, 4),
    "party": (0x2865, 296),
    "badges": (0x23E5, 2),
    "pokedex": (0x2A27, 64),
}
BACKUP_SAVE = {
    "location": (0x1A43, 4),
    "party": (0x1A65, 296),
    "badges": (0x15E5, 2),
    "pokedex": (0x1C27, 64),
}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def checksum_valid(
    data: bytes,
    check1: int,
    start: int,
    end: int,
    checksum: int,
    check2: int,
) -> bool:
    return (
        data[check1] == 99
        and data[check2] == 127
        and int.from_bytes(data[checksum : checksum + 2], "little")
        == sum(data[start:end]) & 0xFFFF
    )


def select_save_copy(data: bytes) -> tuple[str, dict[str, tuple[int, int]]]:
    primary = checksum_valid(
        data, 0x2008, 0x2009, 0x2B83, 0x2D0D, 0x2D0F
    )
    backup = checksum_valid(
        data, 0x1208, 0x1209, 0x1D83, 0x1F0D, 0x1F0F
    )
    if primary:
        return "primary", PRIMARY_SAVE
    if backup:
        return "backup", BACKUP_SAVE
    raise ValueError("both Crystal save copies are invalid")


def decode(data: bytes, layout: dict[str, tuple[int, int]]) -> dict:
    chunks = {
        name: data[offset : offset + width]
        for name, (offset, width) in layout.items()
    }
    if any(len(chunks[name]) != width for name, (_, width) in layout.items()):
        raise ValueError("semantic input is truncated")
    location = chunks["location"]
    party = chunks["party"]
    badges = chunks["badges"]
    pokedex = chunks["pokedex"]
    if party[0] > 6:
        raise ValueError("invalid party count")
    return {
        "location": {
            "map_group": location[0],
            "map_number": location[1],
            "y": location[2],
            "x": location[3],
        },
        "party": {
            "count": party[0],
            "species": list(party[1:7]),
        },
        "badges": {
            "johto_bits": badges[0],
            "kanto_bits": badges[1],
            "johto_count": badges[0].bit_count(),
            "kanto_count": badges[1].bit_count(),
            "total_count": badges[0].bit_count() + badges[1].bit_count(),
        },
        "pokedex": {
            "caught_count": sum(value.bit_count() for value in pokedex[:32]),
            "seen_count": sum(value.bit_count() for value in pokedex[32:]),
        },
    }


def decode_species(rom: bytes, species_id: int) -> dict:
    if len(rom) != 0x200000 or not 1 <= species_id <= 251:
        raise ValueError("invalid Crystal ROM or species")
    base_offset = 20 * 0x4000 + (0x5424 - 0x4000) + (species_id - 1) * 32
    base = rom[base_offset : base_offset + 32]
    if len(base) != 32 or base[0] != species_id:
        raise ValueError("invalid species base-data record")
    pointer_offset = (
        16 * 0x4000 + (0x65B1 - 0x4000) + (species_id - 1) * 2
    )
    pointer = int.from_bytes(rom[pointer_offset : pointer_offset + 2], "little")
    if not 0x4000 <= pointer < 0x8000:
        raise ValueError("invalid evolution pointer")
    cursor = 16 * 0x4000 + (pointer - 0x4000)
    evolutions: list[dict[str, int]] = []
    while rom[cursor] != 0:
        method = rom[cursor]
        cursor += 1
        if not 1 <= method <= 5 or len(evolutions) >= 4:
            raise ValueError("invalid evolution record")
        payload_size = 3 if method == 5 else 2
        payload = rom[cursor : cursor + payload_size]
        cursor += payload_size
        evolutions.append(
            {
                "method": method,
                "parameter": payload[0],
                "condition": payload[1] if method == 5 else 0,
                "target_species": payload[2] if method == 5 else payload[1],
            }
        )
    cursor += 1
    level_moves: list[dict[str, int]] = []
    while rom[cursor] != 0:
        if len(level_moves) >= 32:
            raise ValueError("invalid level-up move list")
        level_moves.append(
            {"level": rom[cursor], "move_id": rom[cursor + 1]}
        )
        cursor += 2
    return {
        "species_id": species_id,
        "hp": base[1],
        "attack": base[2],
        "defense": base[3],
        "speed": base[4],
        "special_attack": base[5],
        "special_defense": base[6],
        "primary_type": base[7],
        "secondary_type": base[8],
        "catch_rate": base[9],
        "base_experience": base[10],
        "encounter_knowledge": 1,
        "evolutions": evolutions,
        "level_moves": level_moves,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--state", type=Path, required=True)
    parser.add_argument("--save", type=Path, required=True)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--accessor-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[3]
    state = json.loads(args.state.read_text(encoding="utf-8"))
    wram = bytes(state["wram_bank_1_d000_dfff"])
    save = args.save.read_bytes()
    rom = args.rom.read_bytes()
    if (
        len(wram) != 0x1000
        or len(save) != 0x8000
        or len(rom) != 0x200000
    ):
        raise ValueError("unexpected live WRAM or save size")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    wram_path = args.output_dir / "wram-bank-1.bin"
    wram_path.write_bytes(wram)

    compiler = os.environ.get("CC") or shutil.which("cc")
    if compiler is None:
        raise RuntimeError("no C compiler found")
    probe = args.output_dir / "semantic-probe"
    subprocess.run(
        [
            compiler,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-DCRYSTAL_SEMANTIC_STANDALONE",
            "-I",
            str(root / "runtime/include"),
            "-I",
            str(args.accessor_dir),
            str(root / "runtime/src/gbrt_semantic.c"),
            str(args.accessor_dir / "crystal_semantic.c"),
            str(root / "ports/pokemon-crystal/tools/semantic_probe.c"),
            "-o",
            str(probe),
        ],
        check=True,
    )
    accessor_result = json.loads(
        subprocess.check_output(
            [str(probe), str(wram_path), str(args.save), str(args.rom)],
            text=True,
        )
    )
    selected_copy, selected_layout = select_save_copy(save)
    independent = {
        "live": decode(wram, LIVE),
        "save": decode(save, selected_layout),
    }
    live_species_id = independent["live"]["party"]["species"][0]
    saved_species_id = independent["save"]["party"]["species"][0]
    independent["live"]["species"] = decode_species(rom, live_species_id)
    independent["save"]["species"] = decode_species(rom, saved_species_id)
    if accessor_result != independent:
        raise AssertionError("generated accessors disagree with independent decoder")
    if accessor_result["live"] != accessor_result["save"]:
        raise AssertionError("live and save-backed semantic views disagree")

    (args.output_dir / "accessor-result.json").write_text(
        json.dumps(accessor_result, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    (args.output_dir / "independent-result.json").write_text(
        json.dumps(independent, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    result = {
        "schema": "crystal-recompiled.semantic-accessor-verification",
        "version": 1,
        "passed": True,
        "state_sha256": sha256(args.state),
        "save_sha256": sha256(args.save),
        "rom_sha256": sha256(args.rom),
        "accessor_source_sha256": sha256(
            args.accessor_dir / "crystal_semantic.c"
        ),
        "live_save_equal": True,
        "independent_decoder_equal": True,
        "selected_save_copy": selected_copy,
        "values": accessor_result["live"],
    }
    (args.output_dir / "result.json").write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
