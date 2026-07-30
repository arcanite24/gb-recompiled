#!/usr/bin/env python3
"""Verify the original/native Pokédex route pair and its explicit differences."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


SCHEMA = "gbrecompiled.pokemon-crystal.route-result"
EXPECTED_NATIVE_EVENTS = [
    {"module": "crystal-workbench", "state": "shown", "surface": "native-pokedex"},
    {"module": "crystal-workbench", "species": 156, "surface": "native-pokedex"},
    {"module": "crystal-workbench", "species": 157, "surface": "native-pokedex"},
    {"module": "crystal-workbench", "species": 156, "surface": "native-pokedex"},
    {"module": "crystal-workbench", "state": "hidden", "surface": "native-pokedex"},
]
ALLOWED_CHECKPOINT_DIFFERENCES: set[tuple[str, str]] = set()


def load(path: Path) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"cannot read route result {path}: {error}") from error
    if (
        not isinstance(value, dict)
        or value.get("schema") != SCHEMA
        or value.get("passed") is not True
    ):
        raise ValueError(f"route result did not pass: {path}")
    return value


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def selected_presentation(result: dict) -> str | None:
    args = result.get("runtime_args")
    if not isinstance(args, list):
        return None
    for index, value in enumerate(args[:-1]):
        if value == "--native-presentation":
            return args[index + 1]
    return None


def segment_map(result: dict) -> dict[str, dict]:
    segments = result.get("segments")
    if not isinstance(segments, list):
        raise ValueError("route result has no segments")
    mapped = {segment.get("id"): segment for segment in segments}
    if len(mapped) != len(segments) or not all(isinstance(key, str) for key in mapped):
        raise ValueError("route result has invalid or duplicate segment ids")
    return mapped


def assertions_without_cycles(segment: dict) -> dict[str, object]:
    assertions = segment.get("final_state")
    if not isinstance(assertions, list):
        raise ValueError(f"segment {segment.get('id')} has no final-state assertions")
    return {
        item["path"]: item.get("actual")
        for item in assertions
        if item.get("path") != "total_cycles"
    }


def checkpoint_map(segment: dict) -> dict[str, str]:
    checkpoints = segment.get("checkpoints")
    if not isinstance(checkpoints, list):
        raise ValueError(f"segment {segment.get('id')} has no checkpoints")
    return {
        checkpoint["id"]: checkpoint["frame_sha256"]
        for checkpoint in checkpoints
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--original", type=Path, required=True)
    parser.add_argument("--native", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    original = load(args.original)
    native = load(args.native)
    if selected_presentation(original) != "original":
        raise ValueError("original result did not select original presentation")
    if selected_presentation(native) != "native":
        raise ValueError("native result did not select native presentation")
    for field in ("executable_sha256", "generation_receipt_sha256"):
        if original.get(field) != native.get(field):
            raise ValueError(f"route pair differs at {field}")
    if original.get("fallback_policy") != native.get("fallback_policy"):
        raise ValueError("route pair has different fallback evidence")
    fallback = original.get("fallback_policy")
    if (
        not isinstance(fallback, dict)
        or fallback.get("passed") is not True
        or fallback.get("observed_sites") != 0
    ):
        raise ValueError("route pair did not prove zero interpreter fallback")

    original_persistence = original.get("persistence")
    native_persistence = native.get("persistence")
    if (
        not isinstance(original_persistence, dict)
        or not isinstance(native_persistence, dict)
        or original_persistence.get("sav") != native_persistence.get("sav")
    ):
        raise ValueError("native presentation changed the persisted save")

    original_segments = segment_map(original)
    native_segments = segment_map(native)
    if original_segments.keys() != native_segments.keys():
        raise ValueError("route pair has different segment ids")

    observed_differences: set[tuple[str, str]] = set()
    for segment_id, original_segment in original_segments.items():
        native_segment = native_segments[segment_id]
        if assertions_without_cycles(original_segment) != assertions_without_cycles(
            native_segment
        ):
            raise ValueError(f"segment {segment_id} changed semantic final state")
        original_checkpoints = checkpoint_map(original_segment)
        native_checkpoints = checkpoint_map(native_segment)
        if original_checkpoints.keys() != native_checkpoints.keys():
            raise ValueError(f"segment {segment_id} has different checkpoint ids")
        for checkpoint_id, original_hash in original_checkpoints.items():
            if original_hash != native_checkpoints[checkpoint_id]:
                difference = (segment_id, checkpoint_id)
                observed_differences.add(difference)
                if difference not in ALLOWED_CHECKPOINT_DIFFERENCES:
                    raise ValueError(
                        f"unexpected frame difference at {segment_id}/{checkpoint_id}"
                    )

        original_port = original_segment.get("port")
        native_port = native_segment.get("port")
        if not isinstance(original_port, dict) or not isinstance(native_port, dict):
            raise ValueError(f"segment {segment_id} lacks port evidence")
        if (
            original_port.get("input_events") != 0
            or original_port.get("semantic_events") != []
            or original_port.get("last_command_count") != 0
        ):
            raise ValueError(f"original segment {segment_id} used native presentation")
        if segment_id == "adventure":
            if (
                native_port.get("input_events") != 5
                or native_port.get("semantic_events") != EXPECTED_NATIVE_EVENTS
                or native_port.get("last_command_count") != 0
            ):
                raise ValueError("native adventure lacks show, browse, and exit evidence")
        elif (
            native_port.get("input_events") != 0
            or native_port.get("semantic_events") != []
            or native_port.get("last_command_count") != 0
        ):
            raise ValueError(f"native segment {segment_id} unexpectedly used the surface")

    if observed_differences != ALLOWED_CHECKPOINT_DIFFERENCES:
        raise ValueError(
            "route pair did not exercise every declared presentation difference"
        )

    result = {
        "schema": "crystal-recompiled.native-pokedex-verification",
        "version": 1,
        "passed": True,
        "segments": len(original_segments),
        "checkpoints": sum(
            len(segment["checkpoints"]) for segment in original_segments.values()
        ),
        "native_events": len(EXPECTED_NATIVE_EVENTS),
        "semantic_state_equal": True,
        "save_equal": True,
        "rtc_equal": original_persistence.get("rtc") == native_persistence.get("rtc"),
        "fallback_equal": True,
        "allowed_checkpoint_differences": [
            f"{segment}/{checkpoint}"
            for segment, checkpoint in sorted(ALLOWED_CHECKPOINT_DIFFERENCES)
        ],
        "original_result_sha256": sha256(args.original),
        "native_result_sha256": sha256(args.native),
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
    except (KeyError, OSError, TypeError, ValueError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
