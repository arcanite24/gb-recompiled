#!/usr/bin/env python3
"""Render and verify the bounded New Bark / Route 29 widescreen prototype."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path
from typing import Any


ROM_SHA256 = "fdcc3c8c43813cf8731fc037d2a6d191bac75439c34b24ba1c27526e6acdc8a2"


class VerificationError(RuntimeError):
    pass


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_object(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise VerificationError(f"cannot read {path}: {error}") from error
    if not isinstance(value, dict):
        raise VerificationError(f"JSON root must be an object: {path}")
    return value


def run_probe(
    probe: Path, rom: Path, wram: Path, image: Path
) -> dict[str, Any]:
    completed = subprocess.run(
        [str(probe), str(rom), str(wram), str(image)],
        text=True,
        capture_output=True,
        check=False,
    )
    image.with_suffix(".stdout").write_text(
        completed.stdout, encoding="utf-8"
    )
    image.with_suffix(".stderr").write_text(
        completed.stderr, encoding="utf-8"
    )
    if completed.returncode != 0:
        raise VerificationError(
            f"probe exited {completed.returncode}: {completed.stderr.strip()}"
        )
    try:
        payload = json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        raise VerificationError("probe did not return JSON") from error
    if not isinstance(payload, dict):
        raise VerificationError("probe JSON root is not an object")
    return payload


def ppm_pixels(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    first, width_height, maximum, pixels = data.split(b"\n", 3)
    if first != b"P6" or maximum != b"255":
        raise VerificationError("probe output is not binary RGB PPM")
    width, height = (int(value) for value in width_height.split())
    if len(pixels) != width * height * 3:
        raise VerificationError("PPM pixel payload has the wrong size")
    return width, height, pixels


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--state", type=Path, required=True)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    rom = args.rom.resolve()
    state_path = args.state.resolve()
    probe = args.probe.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if not rom.is_file() or rom.stat().st_size != 2097152:
        raise VerificationError("ROM is missing or has the wrong size")
    if sha256(rom) != ROM_SHA256:
        raise VerificationError("unsupported ROM identity")
    if not probe.is_file():
        raise VerificationError("widescreen probe is missing")
    before_rom = sha256(rom)
    before_state = sha256(state_path)
    state = load_object(state_path)
    wram_value = state.get("wram_bank_1_d000_dfff")
    if (
        not isinstance(wram_value, list)
        or len(wram_value) != 4096
        or any(
            not isinstance(value, int) or value < 0 or value > 255
            for value in wram_value
        )
    ):
        raise VerificationError("state omits the complete bank-1 WRAM capture")
    location = wram_value[0xCB5 : 0xCB9]
    if location[:2] != [24, 4]:
        raise VerificationError(
            f"checkpoint is not New Bark Town: group/map={location[:2]}"
        )
    wram = output / "wram1.bin"
    wram.write_bytes(bytes(wram_value))
    first_image = output / "new-bark-route29-a.ppm"
    second_image = output / "new-bark-route29-b.ppm"
    first = run_probe(probe, rom, wram, first_image)
    second = run_probe(probe, rom, wram, second_image)
    if first != second or sha256(first_image) != sha256(second_image):
        raise VerificationError("widescreen composition is nondeterministic")
    expected = {
        "scene": "crystal.new-bark-route29",
        "width": 256,
        "height": 144,
        "regions": 2,
        "blocks": 360,
        "connection": "route29-west",
        "compose_status": 0,
    }
    for key, value in expected.items():
        if first.get(key) != value:
            raise VerificationError(
                f"unexpected probe field {key}: {first.get(key)!r}"
            )
    width, height, pixels = ppm_pixels(first_image)
    if (width, height) != (256, 144):
        raise VerificationError("prototype is not wider than 160-by-144")
    row_bytes = width * 3
    left = b"".join(
        pixels[row * row_bytes : row * row_bytes + 128 * 3]
        for row in range(height)
    )
    right = b"".join(
        pixels[row * row_bytes + 128 * 3 : (row + 1) * row_bytes]
        for row in range(height)
    )
    if hashlib.sha256(left).digest() == hashlib.sha256(right).digest():
        raise VerificationError("connected Route 29 and New Bark halves agree")
    colors = {
        pixels[index : index + 3] for index in range(0, len(pixels), 3)
    }
    if len(colors) < 4:
        raise VerificationError("prototype output lacks block/sprite detail")
    if sha256(rom) != before_rom or sha256(state_path) != before_state:
        raise VerificationError("presentation proof mutated guest inputs")
    result = {
        "schema": "crystal-recompiled.widescreen-proof",
        "version": 1,
        "rom_sha256": before_rom,
        "state_sha256": before_state,
        "wram_sha256": sha256(wram),
        "image_sha256": sha256(first_image),
        "repeat_image_sha256": sha256(second_image),
        "width": width,
        "height": height,
        "unique_rgb_colors": len(colors),
        "location": {
            "map_group": location[0],
            "map_number": location[1],
            "y": location[2],
            "x": location[3],
        },
        "probe": first,
        "guest_inputs_unchanged": True,
    }
    result_path = output / "result.json"
    result_path.write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(f"ok  widescreen overworld proof: {result_path}")
    print(f"    image_sha256={result['image_sha256']}")
    print(f"    colors={result['unique_rgb_colors']}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except VerificationError as error:
        print(f"error: {error}")
        raise SystemExit(1)
