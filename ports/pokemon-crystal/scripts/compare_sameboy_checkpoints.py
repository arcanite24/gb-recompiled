#!/usr/bin/env python3
"""Compare selected Crystal route checkpoints with the pinned SameBoy oracle."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path
from typing import Any


class ComparisonError(RuntimeError):
    pass


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_json(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ComparisonError(f"cannot read JSON {path}: {error}") from error


def ppm_q5(path: Path) -> list[tuple[int, int, int]]:
    try:
        header, dimensions, maximum, payload = path.read_bytes().split(b"\n", 3)
    except (OSError, ValueError) as error:
        raise ComparisonError(f"cannot parse PPM {path}: {error}") from error
    if header != b"P6" or dimensions != b"160 144" or maximum != b"255":
        raise ComparisonError(f"unsupported PPM header: {path}")
    if len(payload) != 160 * 144 * 3:
        raise ComparisonError(f"unexpected PPM payload size: {path}")
    values = [round(component * 31 / 255) for component in payload]
    return [tuple(values[index : index + 3]) for index in range(0, len(values), 3)]


def compare_frame(
    sameboy_path: Path,
    gbrecomp_path: Path,
    allowed_rect: tuple[int, int, int, int] | None = None,
    allowed_pixels: set[tuple[int, int]] | None = None,
) -> dict[str, Any]:
    sameboy = ppm_q5(sameboy_path)
    gbrecomp = ppm_q5(gbrecomp_path)
    differences = [
        (index % 160, index // 160)
        for index, (left, right) in enumerate(zip(sameboy, gbrecomp))
        if left != right
    ]
    outside = []
    for x, y in differences:
        in_rect = (
            allowed_rect is not None
            and allowed_rect[0] <= x <= allowed_rect[2]
            and allowed_rect[1] <= y <= allowed_rect[3]
        )
        in_pixels = allowed_pixels is not None and (x, y) in allowed_pixels
        if not in_rect and not in_pixels:
            outside.append((x, y))
    if outside:
        raise ComparisonError(
            f"unexplained normalized frame differences at {outside[:8]} "
            f"({len(outside)} outside the declared animation region)"
        )
    if allowed_pixels is not None and set(differences) != allowed_pixels:
        raise ComparisonError(
            f"declared animation pixels changed: expected {sorted(allowed_pixels)}, "
            f"got {differences[:16]}"
        )
    if allowed_rect is None and allowed_pixels is None and differences:
        raise ComparisonError(
            f"normalized frames differ at {len(differences)} pixel(s)"
        )
    return {
        "sameboy_path": str(sameboy_path),
        "sameboy_sha256": sha256(sameboy_path),
        "gbrecompiled_path": str(gbrecomp_path),
        "gbrecompiled_sha256": sha256(gbrecomp_path),
        "normalization": "round(component * 31 / 255)",
        "different_pixels": len(differences),
        "allowed_rect": list(allowed_rect) if allowed_rect else None,
        "allowed_pixels": (
            [list(item) for item in sorted(allowed_pixels)]
            if allowed_pixels is not None
            else None
        ),
        "unexplained_pixels": 0,
        "passed": True,
    }


def gbrecomp_anchor(state: dict[str, Any], offset: int) -> int:
    memory = state.get("wram_bank_1_d000_dfff")
    if not isinstance(memory, list) or offset >= len(memory):
        raise ComparisonError(f"GB Recompiled state lacks WRAM bank 1 offset {offset}")
    value = memory[offset]
    if not isinstance(value, int):
        raise ComparisonError(f"GB Recompiled WRAM offset {offset} is not an integer")
    return value


def sameboy_anchor(state: dict[str, Any], offset: int) -> int:
    memory = state.get("wram_bank_1_d000_dfff")
    value = memory.get(str(offset)) if isinstance(memory, dict) else None
    if not isinstance(value, int):
        raise ComparisonError(f"SameBoy state lacks WRAM bank 1 offset {offset}")
    return value


def compare_state(
    sameboy_path: Path, gbrecomp_path: Path, offsets: list[int]
) -> dict[str, Any]:
    sameboy = load_json(sameboy_path)
    gbrecomp = load_json(gbrecomp_path)
    if not isinstance(sameboy, dict) or not isinstance(gbrecomp, dict):
        raise ComparisonError("state roots must be objects")
    comparisons = []
    for offset in offsets:
        left = sameboy_anchor(sameboy, offset)
        right = gbrecomp_anchor(gbrecomp, offset)
        if left != right:
            raise ComparisonError(
                f"WRAM bank 1 offset {offset} differs: SameBoy={left}, "
                f"GB Recompiled={right}"
            )
        comparisons.append(
            {
                "space": "wram_bank_1_d000_dfff",
                "offset": offset,
                "sameboy": left,
                "gbrecompiled": right,
                "passed": True,
            }
        )
    for register in ("pc", "sp"):
        left = sameboy.get(register)
        right = gbrecomp.get(register)
        if not isinstance(left, int) or not isinstance(right, int) or left != right:
            raise ComparisonError(
                f"{register.upper()} differs: SameBoy={left}, GB Recompiled={right}"
            )
        comparisons.append(
            {
                "register": register,
                "sameboy": left,
                "gbrecompiled": right,
                "passed": True,
            }
        )
    return {
        "sameboy_path": str(sameboy_path),
        "sameboy_sha256": sha256(sameboy_path),
        "gbrecompiled_path": str(gbrecomp_path),
        "gbrecompiled_sha256": sha256(gbrecomp_path),
        "comparisons": comparisons,
        "passed": True,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--sameboy-new-game", required=True, type=Path)
    parser.add_argument("--sameboy-restart", required=True, type=Path)
    parser.add_argument("--gbrecompiled-route", required=True, type=Path)
    parser.add_argument("--gbrecompiled-overworld-state", required=True, type=Path)
    parser.add_argument("--gbrecompiled-continue-state", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    sameboy_new = args.sameboy_new_game.resolve()
    sameboy_restart = args.sameboy_restart.resolve()
    route = args.gbrecompiled_route.resolve()
    overworld_state = args.gbrecompiled_overworld_state.resolve()
    continue_state = args.gbrecompiled_continue_state.resolve()
    output = args.output.resolve()

    checkpoints = [
        {
            "id": "title",
            "sameboy_frame": 770,
            "gbrecompiled_frame": 750,
            "interpretation": (
                "SameBoy's open boot ROM and GB Recompiled's postboot initialization "
                "enter the title animation at different host-frame phases; after "
                "5-bit normalization only two sparkle pixels differ."
            ),
            "frame": compare_frame(
                sameboy_new / "frame_00770.ppm",
                route / "01-new-game/frame_00750.ppm",
                allowed_pixels={(77, 18), (93, 18)},
            ),
        },
        {
            "id": "new_game",
            "sameboy_frame": 2509,
            "gbrecompiled_frame": 2500,
            "interpretation": (
                "The stable Professor Oak text frame is offset by nine host frames; "
                "the normalized CGB image agrees exactly."
            ),
            "frame": compare_frame(
                sameboy_new / "frame_02509.ppm",
                route / "01-new-game/frame_02500.ppm",
            ),
        },
        {
            "id": "overworld",
            "sameboy_frame": 8250,
            "gbrecompiled_frame": 8250,
            "interpretation": (
                "The background agrees exactly after 5-bit normalization. The only "
                "different pixels are the animated 22x16 player-sprite region, while "
                "PC/SP and semantic WRAM anchors agree."
            ),
            "frame": compare_frame(
                sameboy_new / "frame_08250.ppm",
                route / "01-new-game/frame_08250.ppm",
                allowed_rect=(35, 60, 56, 75),
            ),
            "state": compare_state(
                sameboy_new / "state_08250.json",
                overworld_state,
                [557, 2124, 2442, 3253, 3254, 3255, 3256],
            ),
        },
        {
            "id": "continue",
            "sameboy_frame": 1500,
            "gbrecompiled_frame": 1500,
            "interpretation": (
                "SameBoy independently accepts the GB Recompiled battery file; the "
                "normalized Continue frame, PC/SP, and semantic WRAM anchors agree."
            ),
            "frame": compare_frame(
                sameboy_restart / "frame_01500.ppm",
                route / "04-restart-continue/frame_01500.ppm",
            ),
            "state": compare_state(
                sameboy_restart / "state_01500.json",
                continue_state,
                [557, 2124, 2442, 3253, 3254, 3255, 3256],
            ),
        },
    ]
    report = {
        "schema": "crystal-recompiled.sameboy-checkpoint-comparison",
        "version": 1,
        "passed": True,
        "sameboy_new_game_result_sha256": sha256(sameboy_new / "result.json"),
        "sameboy_restart_result_sha256": sha256(sameboy_restart / "result.json"),
        "checkpoints": checkpoints,
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(f"PASS checkpoints={len(checkpoints)} unexplained_differences=0")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ComparisonError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
