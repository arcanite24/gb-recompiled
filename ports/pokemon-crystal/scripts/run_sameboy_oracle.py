#!/usr/bin/env python3
"""Replay one Crystal route segment in pinned SameBoy and capture checkpoints."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path
from typing import Any


BUTTON_BITS = {
    "R": 1 << 0,
    "L": 1 << 1,
    "U": 1 << 2,
    "D": 1 << 3,
    "A": 1 << 4,
    "B": 1 << 5,
    "T": 1 << 6,
    "S": 1 << 7,
}
EXPECTED_ROM_SHA256 = (
    "fdcc3c8c43813cf8731fc037d2a6d191bac75439c34b24ba1c27526e6acdc8a2"
)


class OracleError(RuntimeError):
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
        raise OracleError(f"cannot read JSON {path}: {error}") from error


def button_mask(text: object) -> int:
    if not isinstance(text, str) or not text:
        raise OracleError(f"invalid button string: {text!r}")
    mask = 0
    for character in text:
        if character not in BUTTON_BITS:
            raise OracleError(f"unknown button {character!r}")
        mask |= BUTTON_BITS[character]
    return mask


def expand_pulses(payload: object) -> list[tuple[int, int, int]]:
    if not isinstance(payload, list) or not payload:
        raise OracleError("route input must be a non-empty array")
    pulses: list[tuple[int, int, int]] = []
    for action in payload:
        if not isinstance(action, dict):
            raise OracleError("route input action must be an object")
        duration = action.get("duration")
        if not isinstance(duration, int) or isinstance(duration, bool) or duration <= 0:
            raise OracleError(f"invalid input duration: {action!r}")
        if "buttons_sequence" in action:
            sequence = action.get("buttons_sequence")
            start = action.get("start_cycle")
            step = action.get("step_cycles")
            count = action.get("count")
            if (
                not isinstance(sequence, list)
                or not sequence
                or not isinstance(start, int)
                or isinstance(start, bool)
                or start < 0
                or not isinstance(step, int)
                or isinstance(step, bool)
                or step <= 0
                or not isinstance(count, int)
                or isinstance(count, bool)
                or count <= 0
            ):
                raise OracleError(f"invalid input sequence: {action!r}")
            for index in range(count):
                pulse_start = start + index * step
                pulses.append(
                    (
                        pulse_start,
                        pulse_start + duration,
                        button_mask(sequence[index % len(sequence)]),
                    )
                )
        elif "cycle" in action:
            start = action.get("cycle")
            if not isinstance(start, int) or isinstance(start, bool) or start < 0:
                raise OracleError(f"invalid input cycle: {action!r}")
            pulses.append((start, start + duration, button_mask(action.get("buttons"))))
        else:
            start = action.get("start_cycle")
            end = action.get("end_cycle")
            step = action.get("step_cycles")
            if (
                not isinstance(start, int)
                or isinstance(start, bool)
                or start < 0
                or not isinstance(end, int)
                or isinstance(end, bool)
                or end < start
                or not isinstance(step, int)
                or isinstance(step, bool)
                or step <= 0
                or duration >= step
            ):
                raise OracleError(f"invalid periodic input: {action!r}")
            for pulse_start in range(start, end + 1, step):
                pulses.append(
                    (
                        pulse_start,
                        pulse_start + duration,
                        button_mask(action.get("buttons")),
                    )
                )
        if len(pulses) > 250_000:
            raise OracleError("expanded input exceeds 250,000 pulses")
    return sorted(pulses)


def compile_oracle(source: Path, sameboy: Path, executable: Path) -> list[str]:
    objects = sorted((sameboy / "build/obj/Core").glob("*.c.o"))
    if not objects:
        raise OracleError("SameBoy Core objects are missing; run `make tester` first")
    command = [
        os.environ.get("CC", "cc"),
        "-std=gnu11",
        "-D_GNU_SOURCE",
        "-DGB_VERSION=\"oracle\"",
        "-DGB_COPYRIGHT_YEAR=\"2026\"",
        "-I",
        str(sameboy),
        str(source),
        *(str(path) for path in objects),
        "-o",
        str(executable),
        "-lc",
        "-lm",
        "-ldl",
    ]
    completed = subprocess.run(command, text=True, capture_output=True, check=False)
    (executable.parent / "compile.stdout").write_text(
        completed.stdout, encoding="utf-8"
    )
    (executable.parent / "compile.stderr").write_text(
        completed.stderr, encoding="utf-8"
    )
    if completed.returncode != 0:
        raise OracleError(f"oracle compile failed with status {completed.returncode}")
    return command


def main() -> int:
    parser = argparse.ArgumentParser()
    script_dir = Path(__file__).resolve().parent
    port_dir = script_dir.parent
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument(
        "--sameboy",
        type=Path,
        default=port_dir / "references/vendor/sameboy",
    )
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--battery", type=Path)
    parser.add_argument("--frames", required=True)
    parser.add_argument("--frame-limit", required=True, type=int)
    parser.add_argument("--rtc-unix-time", required=True, type=int)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()

    rom = args.rom.resolve()
    sameboy = args.sameboy.resolve()
    input_path = args.input.resolve()
    battery = args.battery.resolve() if args.battery else None
    output = args.output_dir.resolve()
    if not rom.is_file():
        raise OracleError(f"missing ROM: {rom}")
    rom_hash = sha256(rom)
    if rom_hash != EXPECTED_ROM_SHA256:
        raise OracleError(
            f"unsupported ROM SHA-256: expected {EXPECTED_ROM_SHA256}, got {rom_hash}"
        )
    if not input_path.is_file():
        raise OracleError(f"missing route input: {input_path}")
    if battery is not None and not battery.is_file():
        raise OracleError(f"missing battery: {battery}")
    try:
        checkpoints = [int(item) for item in args.frames.split(",")]
    except ValueError as error:
        raise OracleError("--frames must be comma-separated integers") from error
    if (
        not checkpoints
        or checkpoints != sorted(set(checkpoints))
        or checkpoints[0] <= 0
        or checkpoints[-1] > args.frame_limit
    ):
        raise OracleError("checkpoint frames must be unique, sorted, and within limit")
    if output.exists():
        if not output.is_dir() or any(output.iterdir()):
            raise OracleError(f"output directory must be absent or empty: {output}")
    else:
        output.mkdir(parents=True)

    boot_rom = sameboy / "build/bin/BootROMs/cgb_boot.bin"
    boot_rom_source = sameboy / "BootROMs/cgb_boot.asm"
    source = port_dir / "tools/sameboy_route_oracle.c"
    if not boot_rom.is_file():
        raise OracleError("SameBoy boot ROM is missing; run `make bootroms` first")
    if not source.is_file():
        raise OracleError(f"missing oracle source: {source}")
    if not boot_rom_source.is_file():
        raise OracleError(f"missing SameBoy boot ROM source: {boot_rom_source}")

    lock = load_json(port_dir / "references/sources.lock.json")
    sources = lock.get("sources") if isinstance(lock, dict) else None
    sameboy_lock = next(
        (
            item
            for item in sources
            if isinstance(item, dict) and item.get("name") == "sameboy"
        ),
        None,
    ) if isinstance(sources, list) else None
    expected_commit = (
        sameboy_lock.get("commit") if isinstance(sameboy_lock, dict) else None
    )
    actual_commit = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=sameboy,
        text=True,
        capture_output=True,
        check=True,
    ).stdout.strip()
    if not isinstance(expected_commit, str) or actual_commit != expected_commit:
        raise OracleError(
            f"SameBoy checkout does not match lock: expected {expected_commit}, "
            f"got {actual_commit}"
        )

    pulses = expand_pulses(load_json(input_path))
    schedule = output / "schedule.txt"
    schedule.write_text(
        "".join(f"{start} {end} {keys:02x}\n" for start, end, keys in pulses),
        encoding="utf-8",
    )
    checkpoint_file = output / "checkpoints.txt"
    checkpoint_file.write_text(
        "".join(f"{frame}\n" for frame in checkpoints), encoding="utf-8"
    )
    executable = output / "sameboy_route_oracle"
    compile_command = compile_oracle(source, sameboy, executable)

    command = [
        str(executable),
        str(rom),
        str(boot_rom),
        str(battery) if battery is not None else "-",
        str(schedule),
        str(checkpoint_file),
        str(output),
        str(args.frame_limit),
        str(args.rtc_unix_time),
    ]
    completed = subprocess.run(command, text=True, capture_output=True, check=False)
    (output / "oracle.stdout").write_text(completed.stdout, encoding="utf-8")
    (output / "oracle.stderr").write_text(completed.stderr, encoding="utf-8")
    if completed.returncode != 0:
        raise OracleError(f"SameBoy oracle failed with status {completed.returncode}")

    report = {
        "schema": "crystal-recompiled.sameboy-route-oracle",
        "version": 1,
        "rom_sha256": rom_hash,
        "input_sha256": sha256(input_path),
        "battery_sha256": sha256(battery) if battery is not None else None,
        "sameboy_commit": actual_commit,
        "sameboy_boot_rom_sha256": sha256(boot_rom),
        "sameboy_boot_rom_source_sha256": sha256(boot_rom_source),
        "oracle_source_sha256": sha256(source),
        "oracle_driver_sha256": sha256(Path(__file__).resolve()),
        "oracle_executable_sha256": sha256(executable),
        "rgbasm_version": subprocess.run(
            ["rgbasm", "--version"],
            text=True,
            capture_output=True,
            check=True,
        ).stdout.strip(),
        "compiler_version": subprocess.run(
            [compile_command[0], "--version"],
            text=True,
            capture_output=True,
            check=True,
        ).stdout.splitlines()[0],
        "compile_command": compile_command,
        "command": command,
        "rtc_unix_time": args.rtc_unix_time,
        "frame_limit": args.frame_limit,
        "checkpoints": [
            {
                "frame": frame,
                "frame_sha256": sha256(output / f"frame_{frame:05d}.ppm"),
                "state_sha256": sha256(output / f"state_{frame:05d}.json"),
            }
            for frame in checkpoints
        ],
    }
    (output / "result.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(
        f"PASS frames={args.frame_limit} checkpoints={len(checkpoints)} "
        f"sameboy={report['sameboy_commit']}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, OracleError, subprocess.SubprocessError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
