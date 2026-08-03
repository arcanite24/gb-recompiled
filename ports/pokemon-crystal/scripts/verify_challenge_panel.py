#!/usr/bin/env python3
"""Drive Challenge Mode's renderer-neutral panel and verify Apply/Cancel."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any


class VerificationError(RuntimeError):
    pass


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise VerificationError(f"{path.name} is not an object")
    return value


def frame_text(port_state: dict[str, Any]) -> list[str]:
    commands = port_state.get("frame", {}).get("commands", [])
    if not isinstance(commands, list):
        raise VerificationError("port frame commands are missing")
    return [
        command["text"]
        for command in commands
        if isinstance(command, dict)
        and command.get("type") == "text"
        and isinstance(command.get("text"), str)
    ]


def run_case(
    executable: Path,
    root: Path,
    *,
    initial_configuration: Path | None,
    inputs: list[str],
    frames: int,
) -> tuple[dict[str, Any], dict[str, Any], Path, str]:
    root.mkdir()
    persistence = root / "persistence"
    persistence.mkdir()
    configuration = root / "challenge.json"
    if initial_configuration is not None:
        shutil.copyfile(initial_configuration, configuration)
    port_state = root / "port-state.json"
    state = root / "state.json"
    log = root / "runtime.log"
    command = [
        str(executable),
        "--headless",
        "--no-audio",
        "--ignore-rtc-persistence",
        "--rtc-unix-time",
        "1700000000",
        "--save-dir",
        str(persistence),
        "--host-configuration",
        str(configuration),
        "--port-ui-open",
    ]
    for item in inputs:
        command.extend(("--port-input-frame", item))
    command.extend(
        (
            "--limit-frames",
            str(frames),
            "--port-state",
            str(port_state),
            "--dump-state",
            str(state),
            "--log-file",
            str(log),
        )
    )
    completed = subprocess.run(command, text=True, capture_output=True)
    (root / "launcher.stdout").write_text(completed.stdout, encoding="utf-8")
    (root / "launcher.stderr").write_text(completed.stderr, encoding="utf-8")
    diagnostics = completed.stdout + completed.stderr
    if log.is_file():
        diagnostics += log.read_text(encoding="utf-8")
    if completed.returncode != 0:
        raise VerificationError(f"{root.name} exited {completed.returncode}")
    if str(configuration.resolve()) in diagnostics:
        raise VerificationError(f"{root.name} leaked its configuration path")
    return load(port_state), load(state), configuration, diagnostics


def require_text(texts: list[str], expected: str, label: str) -> None:
    if expected not in texts:
        raise VerificationError(f"{label} omitted: {expected}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    executable = args.executable.resolve()
    if not executable.is_file():
        raise VerificationError("generated executable is missing")
    output = args.output.resolve()
    if output.exists():
        if not output.is_dir() or any(output.iterdir()):
            raise VerificationError("output must be absent or empty")
    else:
        output.mkdir(parents=True)

    applied_port, applied_state, applied_file, applied_diagnostics = run_case(
        executable,
        output / "apply",
        initial_configuration=None,
        inputs=["1:right", "2:down", "3:right", "4:down", "5:accept"],
        frames=6,
    )
    expected_applied = (
        '{"schema":"gbrecomp.host-configuration","version":1,'
        '"policy_id":"challenge-v1","applied":true,"enabled":true,'
        '"offset":4,"minimum":1,"maximum":100}\n'
    )
    if applied_file.read_text(encoding="utf-8") != expected_applied:
        raise VerificationError("Apply did not persist canonical enabled settings")
    applied_hash = sha256(applied_file)
    if applied_state.get("host_configuration") != {
        "present": True,
        "applied": True,
        "enabled": True,
        "policy_id": "challenge-v1",
        "sha256": applied_hash,
    }:
        raise VerificationError("Apply state identity is wrong")
    if applied_port.get("input_captured") is not True:
        raise VerificationError("open panel did not capture controller input")
    applied_text = frame_text(applied_port)
    for expected in (
        "Crystal Recompiled - Challenge Mode",
        "  Enabled  ON",
        "  Offset  +4  (allowed -5..+5)",
        "> Apply settings for the next battle",
        "Rule: max(original, strongest + floor(badges/4) + offset)",
        "Final level is clamped to 1..100",
        "Applied: ON  offset +4  policy challenge-v1",
        "Applied for the next battle",
    ):
        require_text(applied_text, expected, "applied panel")
    if "Challenge settings applied for the next battle" not in applied_diagnostics:
        raise VerificationError("Apply omitted its stable diagnostic")

    restart_port, restart_state, restart_file, _ = run_case(
        executable,
        output / "restart",
        initial_configuration=applied_file,
        inputs=[],
        frames=1,
    )
    if sha256(restart_file) != applied_hash or restart_state.get(
        "host_configuration"
    ) != applied_state.get("host_configuration"):
        raise VerificationError("restart changed the applied configuration")
    require_text(
        frame_text(restart_port),
        "Applied: ON  offset +4  policy challenge-v1",
        "restart panel",
    )

    cancel_port, _, cancel_file, _ = run_case(
        executable,
        output / "cancel",
        initial_configuration=applied_file,
        inputs=["1:down", "2:left", "3:back"],
        frames=4,
    )
    if sha256(cancel_file) != applied_hash:
        raise VerificationError("Cancel changed the applied file")
    cancel_text = frame_text(cancel_port)
    require_text(cancel_text, "> Offset  +4  (allowed -5..+5)", "cancel panel")
    require_text(
        cancel_text,
        "Draft canceled; applied settings unchanged",
        "cancel panel",
    )

    disabled_port, disabled_state, disabled_file, _ = run_case(
        executable,
        output / "disable",
        initial_configuration=applied_file,
        inputs=["1:accept", "2:down", "3:down", "4:accept"],
        frames=5,
    )
    disabled = load(disabled_file)
    if disabled.get("enabled") is not False or disabled.get("applied") is not True:
        raise VerificationError("Disable did not persist an applied inactive mode")
    disabled_hash = sha256(disabled_file)
    identity = disabled_state.get("host_configuration")
    if not isinstance(identity, dict) or identity.get("enabled") is not False or identity.get(
        "sha256"
    ) != disabled_hash:
        raise VerificationError("Disable state identity is wrong")
    require_text(
        frame_text(disabled_port),
        "Applied: OFF  offset +4  policy challenge-v1",
        "disabled panel",
    )

    closed_port, _, closed_file, _ = run_case(
        executable,
        output / "close",
        initial_configuration=applied_file,
        inputs=["1:close"],
        frames=2,
    )
    if closed_port.get("input_captured") is not False or frame_text(closed_port):
        raise VerificationError("closed panel retained capture or draw commands")
    if sha256(closed_file) != applied_hash:
        raise VerificationError("Close changed the applied configuration")

    result = {
        "schema": "crystal-recompiled.challenge-panel-proof",
        "version": 1,
        "passed": True,
        "executable_sha256": sha256(executable),
        "applied_configuration_sha256": applied_hash,
        "disabled_configuration_sha256": disabled_hash,
        "cases": {
            "apply": {
                "input_events": applied_port["input_events"],
                "command_count": applied_port["last_command_count"],
                "input_captured": applied_port["input_captured"],
            },
            "restart": {"configuration_sha256": sha256(restart_file)},
            "cancel": {"configuration_sha256": sha256(cancel_file)},
            "disable": {"configuration_sha256": disabled_hash},
            "close": {
                "configuration_sha256": sha256(closed_file),
                "input_captured": closed_port["input_captured"],
                "command_count": closed_port["last_command_count"],
            },
        },
    }
    result_path = output / "result.json"
    result_path.write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print("ok  Challenge Mode controller panel")
    print(f"    cases={len(result['cases'])}")
    print(f"    applied_configuration_sha256={applied_hash}")
    print(f"    result_sha256={sha256(result_path)}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, json.JSONDecodeError, VerificationError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
