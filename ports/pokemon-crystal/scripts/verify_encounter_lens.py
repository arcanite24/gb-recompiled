#!/usr/bin/env python3
"""Prove source-built Encounter Lens composition and live overlay reads."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
from pathlib import Path
from typing import Any

from validate_route import cycle_input, parse_fallback_log


ROM_SHA256 = "fdcc3c8c43813cf8731fc037d2a6d191bac75439c34b24ba1c27526e6acdc8a2"
OVERWORLD_SHA256 = (
    "4fe548730b50523e70f4d7d71b54c94a9747896e368774d715165539570a9b7a"
)
BATTLE_SHA256 = (
    "f017ebea694dd3c9e225717fa390fb002f74dfea1dff39c7e728e71da2fd2693"
)
EXTENSION_ID = "org.gbrecompiled.crystal.encounter-lens"


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


def run_segment(
    executable: Path,
    root: Path,
    persistence: Path,
    input_path: Path,
    frame_limit: int,
    capture_frame: int,
    *,
    lens: bool,
    artifact: Path | None,
) -> dict[str, Any]:
    root.mkdir(parents=True)
    state = root / "state.json"
    port_state = root / "port-state.json"
    log = root / "runtime.log"
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
        str(state),
        "--port-state",
        str(port_state),
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
        "--interpreter-hotspot-limit",
        "16",
    ]
    if lens:
        command.extend(["--port-input-frame", "1:encounters"])
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
    if not frame.is_file() or not state.is_file() or not port_state.is_file():
        raise VerificationError(f"{root.name} omitted evidence")
    fallback = parse_fallback_log(log)
    if fallback["sites"] or fallback["summary"]["fallbacks"] != 0:
        raise VerificationError(f"{root.name} used interpreter fallback")
    runtime_log = log.read_text(encoding="utf-8")
    if f"[port:{EXTENSION_ID}][info] extension activated" not in runtime_log:
        raise VerificationError(f"{root.name} did not activate the extension")
    if lens and f"[port:{EXTENSION_ID}][info] encounter lens shown" not in runtime_log:
        raise VerificationError(f"{root.name} did not accept Encounter Lens input")
    return {
        "frame_sha256": sha256(frame),
        "state": load(state),
        "state_sha256": sha256(state),
        "port": load(port_state),
        "runtime_log_sha256": sha256(log),
        "fallback_sites": 0,
    }


def run_route(
    executable: Path,
    root: Path,
    inputs: Path,
    *,
    lens: bool,
    artifact: Path | None,
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
        lens=False,
        artifact=artifact,
    )
    adventure = run_segment(
        executable,
        root / "02-route29-wild-battle",
        persistence,
        inputs / "adventure.json",
        14500,
        14000,
        lens=lens,
        artifact=artifact,
    )
    if (
        new_game["frame_sha256"] != OVERWORLD_SHA256
        or adventure["frame_sha256"] != BATTLE_SHA256
    ):
        raise VerificationError(f"{root.name} changed canonical guest frames")
    wram = adventure["state"].get("wram_bank_1_d000_dfff")
    if not isinstance(wram, list) or len(wram) != 4096:
        raise VerificationError(f"{root.name} omitted battle WRAM")
    extensions = adventure["port"].get("extensions")
    if extensions != [{"id": EXTENSION_ID, "version": 1, "priority": 200}]:
        raise VerificationError(f"{root.name} extension identity/order disagreed")
    texts = [
        command.get("text")
        for command in adventure["port"].get("frame", {}).get("commands", [])
        if command.get("type") == "text"
    ]
    slots: list[dict[str, int]] = []
    for text in texts:
        if not isinstance(text, str):
            continue
        match = re.fullmatch(
            r"Slot ([1-7])  species #([0-9]+)  level ([0-9]+)", text
        )
        if match:
            slots.append(
                {
                    "slot": int(match.group(1)),
                    "species": int(match.group(2)),
                    "level": int(match.group(3)),
                }
            )
    if lens:
        if (
            "Encounter Lens - live overlaid data" not in texts
            or not any(
                isinstance(text, str) and text.startswith("Route 29  ")
                for text in texts
            )
            or len(slots) != 7
        ):
            raise VerificationError(f"{root.name} did not render live encounters")
    elif texts or adventure["port"].get("last_command_count") != 0:
        raise VerificationError(f"{root.name} hidden lens rendered commands")
    return {
        "overworld_frame_sha256": new_game["frame_sha256"],
        "battle_frame_sha256": adventure["frame_sha256"],
        "enemy_species": wram[0x206],
        "enemy_level": wram[0x213],
        "guest_state_sha256": adventure["state_sha256"],
        "port_command_count": adventure["port"].get("last_command_count"),
        "extension_order": [extension["id"] for extension in extensions],
        "slots": slots,
        "fallback_sites": 0,
        "save_sha256": sha256(persistence / "pokemon_crystal.sav"),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--generation-receipt", type=Path, required=True)
    parser.add_argument("--extension-manifest", type=Path, required=True)
    parser.add_argument("--data-mod", type=Path, required=True)
    parser.add_argument("--data-mod-report", type=Path, required=True)
    parser.add_argument("--inputs", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists():
        if not output.is_dir() or output.is_symlink():
            raise VerificationError("output must be a directory")
        shutil.rmtree(output)
    rom = args.rom.resolve()
    executable = args.executable.resolve()
    receipt_path = args.generation_receipt.resolve()
    extension_manifest = args.extension_manifest.resolve()
    artifact = args.data_mod.resolve()
    report_path = args.data_mod_report.resolve()
    if (
        not rom.is_file()
        or sha256(rom) != ROM_SHA256
        or not executable.is_file()
        or not receipt_path.is_file()
        or not extension_manifest.is_file()
        or not artifact.is_file()
        or not report_path.is_file()
    ):
        raise VerificationError("missing or incompatible proof input")
    receipt = load(receipt_path)
    recorded = receipt.get("port_extensions", {})
    if (
        receipt.get("rom", {}).get("sha256") != ROM_SHA256
        or recorded.get("load_order") != [EXTENSION_ID]
        or len(recorded.get("extensions", [])) != 1
        or recorded["extensions"][0].get("manifest_sha256") !=
            sha256(extension_manifest)
    ):
        raise VerificationError("generation receipt does not bind Encounter Lens")
    report = load(report_path)
    if (
        report.get("artifact", {}).get("sha256") != sha256(artifact)
        or report.get("artifact", {}).get("entry_count") != 42
        or report.get("encounter_change_count") != 21
    ):
        raise VerificationError("difficulty artifact/report mismatch")

    output.mkdir(parents=True)
    hidden = run_route(
        executable,
        output / "hidden",
        args.inputs.resolve(),
        lens=False,
        artifact=None,
    )
    vanilla = run_route(
        executable,
        output / "vanilla-visible",
        args.inputs.resolve(),
        lens=True,
        artifact=None,
    )
    modded = run_route(
        executable,
        output / "modded-visible",
        args.inputs.resolve(),
        lens=True,
        artifact=artifact,
    )
    if (
        hidden["guest_state_sha256"] != vanilla["guest_state_sha256"]
        or hidden["enemy_level"] != vanilla["enemy_level"]
        or hidden["enemy_species"] != vanilla["enemy_species"]
        or hidden["save_sha256"] != vanilla["save_sha256"]
        or [slot["species"] for slot in vanilla["slots"]] !=
            [slot["species"] for slot in modded["slots"]]
        or all(slot["level"] == 5 for slot in vanilla["slots"])
        or not all(slot["level"] == 5 for slot in modded["slots"])
        or modded["enemy_level"] != 5
        or vanilla["enemy_level"] != 2
    ):
        raise VerificationError("lens observation/overlay behavior disagreed")

    result = {
        "schema": "gbrecompiled.pokemon-crystal.encounter-lens-proof",
        "version": 1,
        "passed": True,
        "rom_sha256": ROM_SHA256,
        "executable_sha256": sha256(executable),
        "generation_receipt_sha256": sha256(receipt_path),
        "extension_manifest_sha256": sha256(extension_manifest),
        "extension_load_order": [EXTENSION_ID],
        "source_built": True,
        "dynamic_loading": False,
        "sandboxed_bytecode": False,
        "hidden": hidden,
        "vanilla_visible": vanilla,
        "modded_visible": modded,
        "observational_guest_state_preserved": True,
        "active_data_overlay_observed": True,
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
