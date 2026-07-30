#!/usr/bin/env python3
"""Prove one Crystal encounter overlay in the same generated executable."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path

from validate_route import cycle_input, parse_fallback_log


EXPECTED_ROM_SHA256 = (
    "fdcc3c8c43813cf8731fc037d2a6d191bac75439c34b24ba1c27526e6acdc8a2"
)
EXPECTED_VANILLA_OVERWORLD = (
    "4fe548730b50523e70f4d7d71b54c94a9747896e368774d715165539570a9b7a"
)
EXPECTED_VANILLA_WILD_BATTLE = (
    "f017ebea694dd3c9e225717fa390fb002f74dfea1dff39c7e728e71da2fd2693"
)
HOPPIP = 187


class VerificationError(RuntimeError):
    pass


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load(path: Path) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise VerificationError(f"cannot read JSON {path}: {error}") from error
    if not isinstance(value, dict):
        raise VerificationError(f"JSON root must be an object: {path}")
    return value


def run_segment(
    executable: Path,
    segment_dir: Path,
    persistence: Path,
    input_path: Path,
    frame_limit: int,
    capture_frame: int,
    artifact: Path | None,
) -> dict:
    segment_dir.mkdir(parents=True)
    prefix = segment_dir / "frame"
    state_path = segment_dir / "state.json"
    log_path = segment_dir / "runtime.log"
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
        command,
        cwd=segment_dir,
        text=True,
        capture_output=True,
        check=False,
    )
    (segment_dir / "launcher.stdout").write_text(
        completed.stdout, encoding="utf-8"
    )
    (segment_dir / "launcher.stderr").write_text(
        completed.stderr, encoding="utf-8"
    )
    if completed.returncode != 0:
        raise VerificationError(
            f"{segment_dir.name} exited with status {completed.returncode}"
        )
    frame_path = segment_dir / f"frame_{capture_frame:05d}.ppm"
    if not frame_path.is_file() or not state_path.is_file():
        raise VerificationError(f"{segment_dir.name} omitted evidence output")
    fallback = parse_fallback_log(log_path)
    if fallback["sites"] or fallback["summary"]["fallbacks"] != 0:
        raise VerificationError(f"{segment_dir.name} used interpreter fallback")
    log = log_path.read_text(encoding="utf-8")
    if artifact is None:
        if "[DATA-MOD]" in log:
            raise VerificationError("vanilla mode unexpectedly activated a data mod")
    elif "[DATA-MOD] Active entries=42" not in log:
        raise VerificationError("modded mode did not activate all overlay entries")
    return {
        "frame": str(frame_path),
        "frame_sha256": sha256(frame_path),
        "state": load(state_path),
        "runtime_log_sha256": sha256(log_path),
        "fallback": fallback,
    }


def run_route(
    executable: Path,
    root: Path,
    inputs: Path,
    artifact: Path | None,
) -> dict:
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
    )
    if new_game["frame_sha256"] != EXPECTED_VANILLA_OVERWORLD:
        raise VerificationError("new-game path changed before the encounter overlay")
    adventure = run_segment(
        executable,
        root / "02-route29-wild-battle",
        persistence,
        inputs / "adventure.json",
        14500,
        14000,
        artifact,
    )
    wram = adventure["state"].get("wram_bank_1_d000_dfff")
    if not isinstance(wram, list) or len(wram) != 4096:
        raise VerificationError("adventure state omitted banked WRAM")
    return {
        "new_game_frame_sha256": new_game["frame_sha256"],
        "wild_battle_frame": adventure["frame"],
        "wild_battle_frame_sha256": adventure["frame_sha256"],
        "enemy_species": wram[0x206],
        "enemy_level": wram[0x213],
        "fallback_sites": len(adventure["fallback"]["sites"]),
        "runtime_log_sha256": adventure["runtime_log_sha256"],
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--generation-receipt", type=Path, required=True)
    parser.add_argument("--artifact", type=Path, required=True)
    parser.add_argument("--compile-report", type=Path, required=True)
    parser.add_argument("--inputs", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    executable = args.executable.resolve()
    receipt_path = args.generation_receipt.resolve()
    artifact = args.artifact.resolve()
    inputs = args.inputs.resolve()
    output = args.output.resolve()
    if output.exists():
        if not output.is_dir() or output.is_symlink():
            raise VerificationError("output must be a directory")
        shutil.rmtree(output)
    output.mkdir(parents=True)
    if not executable.is_file() or not artifact.is_file():
        raise VerificationError("missing executable or overlay artifact")
    receipt = load(receipt_path)
    if (
        receipt.get("schema") != "crystal-recompiled.generation"
        or receipt.get("version") != 1
        or receipt.get("rom", {}).get("sha256") != EXPECTED_ROM_SHA256
    ):
        raise VerificationError("generation receipt is not exact Crystal Rev 1")
    report = load(args.compile_report.resolve())
    if (
        report.get("passed") is not True
        or report.get("artifact", {}).get("entry_count") != 42
        or report.get("artifact", {}).get("sha256") != sha256(artifact)
        or report.get("original_rom_sha256_after_compile") != EXPECTED_ROM_SHA256
    ):
        raise VerificationError("overlay compile report is inconsistent")

    vanilla = run_route(executable, output / "vanilla", inputs, None)
    modded = run_route(executable, output / "modded", inputs, artifact)
    if vanilla["wild_battle_frame_sha256"] != EXPECTED_VANILLA_WILD_BATTLE:
        raise VerificationError("inactive mode no longer matches the vanilla route")
    if (
        modded["enemy_species"] != HOPPIP
        or modded["enemy_level"] != 5
        or (vanilla["enemy_species"], vanilla["enemy_level"]) ==
            (modded["enemy_species"], modded["enemy_level"])
    ):
        raise VerificationError("overlay did not produce the level-5 Hoppip battle")

    result = {
        "schema": "gbrecompiled.pokemon-crystal.data-mod-route-proof",
        "version": 1,
        "passed": True,
        "executable_sha256": sha256(executable),
        "generation_receipt_sha256": sha256(receipt_path),
        "artifact_sha256": sha256(artifact),
        "same_executable": True,
        "rom_recompiled_between_modes": False,
        "battle_state_changed": True,
        "vanilla": vanilla,
        "modded": modded,
    }
    result_path = output / "result.json"
    result_path.write_text(
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
