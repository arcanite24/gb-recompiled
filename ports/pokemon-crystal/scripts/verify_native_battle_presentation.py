#!/usr/bin/env python3
"""Prove deterministic high-resolution Crystal battle presentation."""

from __future__ import annotations

import argparse
import hashlib
import json
import platform
import statistics
import subprocess
import time
from pathlib import Path
from typing import Any

from validate_route import cycle_input, parse_fallback_log


ROM_SHA256 = "fdcc3c8c43813cf8731fc037d2a6d191bac75439c34b24ba1c27526e6acdc8a2"
BATTLE_FRAME_SHA256 = (
    "f017ebea694dd3c9e225717fa390fb002f74dfea1dff39c7e728e71da2fd2693"
)


class VerificationError(RuntimeError):
    pass


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise VerificationError(f"JSON root must be an object: {path}")
    return value


def run_segment(
    executable: Path,
    root: Path,
    persistence: Path,
    input_path: Path,
    frames: int,
    capture: int,
) -> Path:
    root.mkdir(parents=True)
    state = root / "state.json"
    log = root / "runtime.log"
    prefix = root / "frame"
    command = [
        str(executable),
        "--headless",
        "--limit-frames",
        str(frames),
        "--input",
        cycle_input(input_path),
        "--dump-frames",
        str(capture),
        "--screenshot-prefix",
        str(prefix),
        "--dump-state",
        str(state),
        "--save-dir",
        str(persistence),
        "--log-file",
        str(log),
        "--no-audio",
        "--rtc-unix-time",
        "1700000000",
        "--ignore-rtc-persistence",
        "--log-frame-fallbacks",
        "--report-interpreter-hotspots",
    ]
    completed = subprocess.run(command, text=True, capture_output=True)
    (root / "launcher.stdout").write_text(completed.stdout, encoding="utf-8")
    (root / "launcher.stderr").write_text(completed.stderr, encoding="utf-8")
    if completed.returncode != 0:
        raise VerificationError(f"{root.name} exited {completed.returncode}")
    fallback = parse_fallback_log(log)
    if fallback["sites"] or fallback["summary"]["fallbacks"] != 0:
        raise VerificationError(f"{root.name} used interpreter fallback")
    frame = root / f"frame_{capture:05d}.ppm"
    if not frame.is_file() or not state.is_file():
        raise VerificationError(f"{root.name} omitted capture evidence")
    return state


def validate_assets(manifest_path: Path) -> tuple[Path, Path, str]:
    manifest = load(manifest_path)
    if (
        manifest.get("schema") != "gbrecompiled.presentation-assets"
        or manifest.get("version") != 1
        or manifest.get("rom")
        != {"size": 2097152, "sha256": ROM_SHA256}
        or manifest.get("configuration")
        != {
            "mode": "native",
            "width": 1280,
            "height": 720,
            "effect_seed": 1129466195,
        }
    ):
        raise VerificationError("unsupported presentation asset manifest")
    assets = manifest.get("assets")
    if not isinstance(assets, list) or len(assets) != 2:
        raise VerificationError("presentation asset set is incomplete")
    resolved: dict[str, Path] = {}
    for asset in assets:
        if (
            not isinstance(asset, dict)
            or asset.get("license") != "CC0-1.0"
            or asset.get("source") != "Crystal Recompiled project"
        ):
            raise VerificationError("asset provenance is incomplete")
        path = (manifest_path.parent / str(asset.get("path"))).resolve()
        path.relative_to(manifest_path.parent.resolve())
        if not path.is_file() or sha256(path) != asset.get("sha256"):
            raise VerificationError("presentation asset hash disagreed")
        resolved[str(asset.get("id"))] = path
    return (
        resolved["crystal.ui.panel-v1"],
        resolved["crystal.battle.aura-v1"],
        sha256(manifest_path),
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--inputs", type=Path, required=True)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True)
    persistence = output / "persistence"
    persistence.mkdir()
    run_segment(
        args.executable.resolve(),
        output / "01-new-game",
        persistence,
        args.inputs / "new-game.json",
        12050,
        8250,
    )
    battle_state = run_segment(
        args.executable.resolve(),
        output / "02-wild-battle",
        persistence,
        args.inputs / "adventure.json",
        14501,
        14000,
    )
    battle_frame = output / "02-wild-battle/frame_14000.ppm"
    if sha256(battle_frame) != BATTLE_FRAME_SHA256:
        raise VerificationError("accurate battle frame changed")
    state = load(battle_state)
    wram0 = state.get("wram_bank_0_c000_cfff")
    wram1 = state.get("wram_bank_1_d000_dfff")
    if not isinstance(wram0, list) or len(wram0) != 4096:
        raise VerificationError("battle state omitted WRAM0")
    if not isinstance(wram1, list) or len(wram1) != 4096:
        raise VerificationError("battle state omitted bank-1 WRAM")
    wram0_path = output / "wram0.bin"
    wram1_path = output / "wram1.bin"
    wram0_path.write_bytes(bytes(wram0))
    wram1_path.write_bytes(bytes(wram1))
    panel, aura, manifest_hash = validate_assets(args.assets.resolve())
    images = [output / "native-battle-a.ppm", output / "native-battle-b.ppm"]
    payloads: list[dict[str, Any]] = []
    durations: list[float] = []
    for image in images:
        started = time.perf_counter()
        completed = subprocess.run(
            [
                str(args.probe.resolve()),
                str(wram0_path),
                str(wram1_path),
                str(panel),
                str(aura),
                str(image),
            ],
            text=True,
            capture_output=True,
        )
        durations.append((time.perf_counter() - started) * 1000)
        image.with_suffix(".stderr").write_text(
            completed.stderr, encoding="utf-8"
        )
        if completed.returncode != 0:
            raise VerificationError(
                f"battle probe exited {completed.returncode}: "
                f"{completed.stderr.strip()}"
            )
        payloads.append(json.loads(completed.stdout))
    if payloads[0] != payloads[1] or sha256(images[0]) != sha256(images[1]):
        raise VerificationError("native battle output is nondeterministic")
    if (
        payloads[0].get("width") != 1280
        or payloads[0].get("height") != 720
        or payloads[0].get("assets") != 2
        or payloads[0].get("player_species", 0) == 0
        or payloads[0].get("enemy_species", 0) == 0
    ):
        raise VerificationError("native battle scene is incomplete")
    result = {
        "schema": "crystal-recompiled.native-battle-proof",
        "version": 1,
        "passed": True,
        "accurate_battle_frame_sha256": sha256(battle_frame),
        "native_battle_frame_sha256": sha256(images[0]),
        "repeat_native_frame_sha256": sha256(images[1]),
        "asset_manifest_sha256": manifest_hash,
        "panel_asset_sha256": sha256(panel),
        "aura_asset_sha256": sha256(aura),
        "scene": payloads[0],
        "performance": {
            "profile": "deterministic CPU reference plus PPM write",
            "graphics_stack": "no GPU; C CPU raster surface",
            "host": platform.platform(),
            "runs_ms": durations,
            "median_ms": statistics.median(durations),
        },
    }
    result_path = output / "result.json"
    result_path.write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(f"ok  native battle presentation: {result_path}")
    print(f"    native_frame_sha256={result['native_battle_frame_sha256']}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, KeyError, ValueError, json.JSONDecodeError, VerificationError) as error:
        print(f"error: {error}")
        raise SystemExit(1)
