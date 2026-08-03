#!/usr/bin/env python3
"""Compare disabled, closed, and interactive Workbench route evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


def load(path: Path) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"cannot read route result {path}: {error}") from error
    if (
        not isinstance(value, dict)
        or value.get("schema") != "gbrecompiled.pokemon-crystal.route-result"
        or value.get("passed") is not True
    ):
        raise ValueError(f"route result did not pass: {path}")
    return value


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def guest_projection(result: dict) -> dict:
    return {
        "manifest_sha256": result.get("manifest_sha256"),
        "rtc_unix_time": result.get("rtc_unix_time"),
        "ignore_rtc_persistence": result.get("ignore_rtc_persistence"),
        "fallback_policy": result.get("fallback_policy"),
        "persistence": result.get("persistence"),
        "segments": [
            {
                "id": segment.get("id"),
                "input": segment.get("input"),
                "input_sha256": segment.get("input_sha256"),
                "checkpoints": segment.get("checkpoints"),
                "final_state": segment.get("final_state"),
                "state_sha256": segment.get("state_sha256"),
                "pcm": segment.get("pcm"),
                "fallbacks": segment.get("fallbacks"),
            }
            for segment in result.get("segments", [])
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--disabled", type=Path, required=True)
    parser.add_argument("--closed", type=Path, required=True)
    parser.add_argument("--interactive", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    disabled = load(args.disabled)
    closed = load(args.closed)
    interactive = load(args.interactive)
    if disabled.get("capture_port_state") is not False:
        raise ValueError("disabled route unexpectedly captured a port module")
    if closed.get("capture_port_state") is not True:
        raise ValueError("closed route did not capture its port module")
    if interactive.get("capture_port_state") is not True:
        raise ValueError("interactive route did not capture its port module")
    if guest_projection(disabled) != guest_projection(closed):
        raise ValueError("closed Workbench route changed guest-visible results")
    if guest_projection(disabled) != guest_projection(interactive):
        raise ValueError("interactive Workbench route changed guest-visible results")

    for segment in closed["segments"]:
        port = segment.get("port")
        if (
            not isinstance(port, dict)
            or port.get("module_id") != "crystal-workbench"
            or port.get("input_events") != 0
            or port.get("semantic_events") != []
            or port.get("last_command_count") != 0
        ):
            raise ValueError(
                f"closed segment {segment.get('id')} was not observationally closed"
            )
    event_count = 0
    for segment in interactive["segments"]:
        port = segment.get("port")
        if (
            not isinstance(port, dict)
            or port.get("module_id") != "crystal-workbench"
            or not isinstance(port.get("input_events"), int)
            or port["input_events"] < 5
            or not isinstance(port.get("semantic_events"), list)
            or len(port["semantic_events"]) != port["input_events"]
            or port.get("last_command_count", 0) <= 0
        ):
            raise ValueError(
                f"interactive segment {segment.get('id')} lacks toggle evidence"
            )
        states = {event.get("state") for event in port["semantic_events"]}
        if states != {"shown", "hidden"}:
            raise ValueError(
                f"interactive segment {segment.get('id')} did not open and close"
            )
        event_count += port["input_events"]

    result = {
        "schema": "crystal-recompiled.workbench-observational-verification",
        "version": 1,
        "passed": True,
        "segments": len(disabled["segments"]),
        "semantic_events": event_count,
        "guest_state_equal": True,
        "checkpoint_frames_equal": True,
        "pcm_equal": True,
        "save_equal": True,
        "rtc_equal": True,
        "fallback_equal": True,
        "disabled_result_sha256": sha256(args.disabled),
        "closed_result_sha256": sha256(args.closed),
        "interactive_result_sha256": sha256(args.interactive),
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
