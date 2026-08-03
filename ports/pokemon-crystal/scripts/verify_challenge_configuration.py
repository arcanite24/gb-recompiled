#!/usr/bin/env python3
"""Verify Challenge Mode host configuration, restart, and preflight behavior."""

from __future__ import annotations

import argparse
import hashlib
import json
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
        raise VerificationError("state root is not an object")
    return value


def run_case(
    executable: Path,
    root: Path,
    configuration: Path,
    expected_status: str,
    expected_exit: int,
) -> dict[str, Any]:
    root.mkdir()
    persistence = root / "persistence"
    persistence.mkdir()
    state = root / "state.json"
    log = root / "runtime.log"
    command = [
        str(executable),
        "--headless",
        "--no-audio",
        "--limit-frames",
        "1",
        "--save-dir",
        str(persistence),
        "--rtc-unix-time",
        "1700000000",
        "--ignore-rtc-persistence",
        "--host-configuration",
        str(configuration),
        "--dump-state",
        str(state),
        "--log-file",
        str(log),
    ]
    completed = subprocess.run(command, text=True, capture_output=True)
    (root / "launcher.stdout").write_text(completed.stdout, encoding="utf-8")
    (root / "launcher.stderr").write_text(completed.stderr, encoding="utf-8")
    log_text = log.read_text(encoding="utf-8") if log.is_file() else ""
    diagnostics = completed.stdout + completed.stderr + log_text
    if completed.returncode != expected_exit:
        raise VerificationError(f"{root.name} exited {completed.returncode}")
    if f"status={expected_status}" not in diagnostics:
        raise VerificationError(f"{root.name} omitted stable status")
    if str(configuration.resolve()) in diagnostics:
        raise VerificationError(f"{root.name} leaked the configuration path")

    if expected_exit != 0:
        if state.exists() or any(persistence.iterdir()):
            raise VerificationError(f"{root.name} executed guest state before rejection")
        return {
            "exit": completed.returncode,
            "status": expected_status,
            "diagnostics_sha256": hashlib.sha256(
                diagnostics.encode("utf-8")
            ).hexdigest(),
            "guest_execution": False,
        }

    if not state.is_file():
        raise VerificationError(f"{root.name} omitted state")
    state_value = load(state)
    identity = state_value.get("host_configuration")
    if not isinstance(identity, dict):
        raise VerificationError(f"{root.name} omitted configuration identity")
    return {
        "exit": completed.returncode,
        "status": expected_status,
        "state_sha256": sha256(state),
        "identity": identity,
        "guest_execution": True,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--configuration", required=True, type=Path)
    parser.add_argument("--disabled-configuration", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    executable = args.executable.resolve()
    configuration = args.configuration.resolve()
    disabled = args.disabled_configuration.resolve()
    if not executable.is_file() or not configuration.is_file() or not disabled.is_file():
        raise VerificationError("required configuration proof input is missing")
    output = args.output.resolve()
    if output.exists():
        if not output.is_dir() or any(output.iterdir()):
            raise VerificationError("output must be absent or empty")
    else:
        output.mkdir(parents=True)

    canonical = configuration.read_text(encoding="utf-8")
    cases_dir = output / "configurations"
    cases_dir.mkdir()
    invalid = {
        "malformed": ("{}\n", "malformed"),
        "non-canonical": (canonical.replace('"offset":3', '"offset":+3'), "non-canonical"),
        "schema-mismatch": (
            canonical.replace("gbrecomp.host-configuration", "wrong.schema"),
            "schema-mismatch",
        ),
        "policy-mismatch": (
            canonical.replace("challenge-v1", "wrong-v1"),
            "policy-mismatch",
        ),
        "out-of-range": (canonical.replace('"offset":3', '"offset":6'), "out-of-range"),
    }
    invalid_paths: dict[str, tuple[Path, str]] = {}
    for name, (content, status) in invalid.items():
        path = cases_dir / f"{name}.json"
        path.write_text(content, encoding="utf-8")
        invalid_paths[name] = (path, status)
    unapplied = cases_dir / "unapplied.json"
    unapplied.write_text(
        canonical.replace('"applied":true', '"applied":false'), encoding="utf-8"
    )

    results: dict[str, Any] = {}
    results["missing"] = run_case(
        executable,
        output / "missing",
        cases_dir / "missing.json",
        "missing",
        0,
    )
    results["applied_first"] = run_case(
        executable, output / "applied-first", configuration, "applied", 0
    )
    results["applied_restart"] = run_case(
        executable, output / "applied-restart", configuration, "applied", 0
    )
    results["disabled"] = run_case(
        executable, output / "disabled", disabled, "applied", 0
    )
    results["unapplied"] = run_case(
        executable, output / "unapplied", unapplied, "applied", 0
    )
    for name, (path, status) in invalid_paths.items():
        results[name] = run_case(
            executable, output / name, path, status, 1
        )

    expected_hash = sha256(configuration)
    for name in ("applied_first", "applied_restart"):
        identity = results[name]["identity"]
        if identity != {
            "present": True,
            "applied": True,
            "enabled": True,
            "policy_id": "challenge-v1",
            "sha256": expected_hash,
        }:
            raise VerificationError(f"{name} has the wrong applied identity")
    if results["applied_first"]["state_sha256"] != results["applied_restart"][
        "state_sha256"
    ]:
        raise VerificationError("restart changed applied configuration state")
    if results["missing"]["identity"].get("present") is not False:
        raise VerificationError("missing configuration did not disable the mode")
    if results["disabled"]["identity"].get("enabled") is not False:
        raise VerificationError("disabled configuration became active")
    if results["unapplied"]["identity"].get("applied") is not False:
        raise VerificationError("unapplied draft became active")

    result = {
        "schema": "crystal-recompiled.challenge-configuration-proof",
        "version": 1,
        "passed": True,
        "executable_sha256": sha256(executable),
        "configuration_sha256": expected_hash,
        "disabled_configuration_sha256": sha256(disabled),
        "cases": results,
    }
    result_path = output / "result.json"
    result_path.write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print("ok  Challenge Mode host configuration")
    print(f"    cases={len(results)}")
    print(f"    configuration_sha256={expected_hash}")
    print(f"    result_sha256={sha256(result_path)}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, json.JSONDecodeError, VerificationError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
