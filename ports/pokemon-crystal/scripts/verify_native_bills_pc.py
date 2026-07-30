#!/usr/bin/env python3
"""Verify the exact-ROM BillsPC replacement across original/native routes."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from verify_writable_saves import BACKUP, PRIMARY, checksum_valid


ROUTE_SCHEMA = "gbrecompiled.pokemon-crystal.route-result"
FUNCTION_ID = "gbfn:v1:0005:5668"
PATCH_ID = "org.gbrecompiled.crystal.bills-pc"
EXPECTED_EVENT = {
    "module": "crystal-workbench",
    "surface": "native-pc",
    "state": "shown",
}


def load(path: Path) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"cannot read JSON {path}: {error}") from error
    if not isinstance(value, dict):
        raise ValueError(f"JSON root is not an object: {path}")
    return value


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def presentation(result: dict) -> str | None:
    runtime_args = result.get("runtime_args")
    if not isinstance(runtime_args, list):
        return None
    for index, value in enumerate(runtime_args[:-1]):
        if value == "--native-presentation":
            return runtime_args[index + 1]
    return None


def segments(result: dict) -> dict[str, dict]:
    values = result.get("segments")
    if not isinstance(values, list):
        raise ValueError("route result has no segment list")
    mapped = {value.get("id"): value for value in values}
    if len(mapped) != len(values) or not all(isinstance(key, str) for key in mapped):
        raise ValueError("route result has invalid or duplicate segment IDs")
    return mapped


def semantic_state(segment: dict) -> dict[str, object]:
    values = segment.get("final_state")
    if not isinstance(values, list):
        raise ValueError(f"segment {segment.get('id')} has no final state")
    return {
        value["path"]: value.get("actual")
        for value in values
        if value.get("path") != "total_cycles"
    }


def checkpoints(segment: dict) -> dict[str, str]:
    values = segment.get("checkpoints")
    if not isinstance(values, list):
        raise ValueError(f"segment {segment.get('id')} has no checkpoints")
    return {value["id"]: value["frame_sha256"] for value in values}


def valid_route(path: Path, expected_presentation: str) -> dict:
    result = load(path)
    if (
        result.get("schema") != ROUTE_SCHEMA
        or result.get("passed") is not True
        or presentation(result) != expected_presentation
    ):
        raise ValueError(f"route did not pass as {expected_presentation}: {path}")
    fallback = result.get("fallback_policy")
    if (
        not isinstance(fallback, dict)
        or fallback.get("passed") is not True
        or fallback.get("observed_sites") != 0
    ):
        raise ValueError(f"route lacks zero-fallback proof: {path}")
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--original", type=Path, required=True)
    parser.add_argument("--native", type=Path, required=True)
    parser.add_argument("--original-save", type=Path, required=True)
    parser.add_argument("--native-save", type=Path, required=True)
    parser.add_argument("--metadata", type=Path, required=True)
    parser.add_argument("--patch-manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    original = valid_route(args.original, "original")
    native = valid_route(args.native, "native")
    for field in ("executable_sha256", "generation_receipt_sha256"):
        if original.get(field) != native.get(field):
            raise ValueError(f"route pair differs at {field}")
    if original.get("persistence") != native.get("persistence"):
        raise ValueError("native binding changed persisted save or RTC")

    original_segments = segments(original)
    native_segments = segments(native)
    if original_segments.keys() != native_segments.keys():
        raise ValueError("route pair has different segment IDs")
    if "restart-continue" not in original_segments:
        raise ValueError("route pair lacks the restart/continue process boundary")
    for segment_id, original_segment in original_segments.items():
        native_segment = native_segments[segment_id]
        if semantic_state(original_segment) != semantic_state(native_segment):
            raise ValueError(f"native binding changed {segment_id} semantic state")
        if checkpoints(original_segment) != checkpoints(native_segment):
            raise ValueError(f"native binding changed {segment_id} checkpoints")
        original_port = original_segment.get("port")
        native_port = native_segment.get("port")
        if not isinstance(original_port, dict) or not isinstance(native_port, dict):
            raise ValueError(f"segment {segment_id} lacks port evidence")
        if (
            original_port.get("input_events") != 0
            or original_port.get("semantic_events") != []
            or original_port.get("last_command_count") != 0
        ):
            raise ValueError(f"original mode used native UI in {segment_id}")
        if segment_id in {"adventure", "pc-save"}:
            if (
                native_port.get("module_version") != 8
                or native_port.get("input_events") != 2
                or native_port.get("semantic_events")
                != [EXPECTED_EVENT, EXPECTED_EVENT]
                or not isinstance(native_port.get("last_command_count"), int)
                or native_port["last_command_count"] <= 0
            ):
                raise ValueError("native mode did not bind BillsPC to the native PC")
        elif (
            native_port.get("input_events") != 0
            or native_port.get("semantic_events") != []
            or native_port.get("last_command_count") != 0
        ):
            raise ValueError(f"native mode unexpectedly opened UI in {segment_id}")

    original_save = args.original_save.read_bytes()
    native_save = args.native_save.read_bytes()
    if original_save != native_save:
        raise ValueError("route save files differ")
    if (
        not checksum_valid(original_save, PRIMARY)
        or not checksum_valid(original_save, BACKUP)
    ):
        raise ValueError("route save has an invalid primary or backup checksum")

    metadata = load(args.metadata)
    functions = metadata.get("functions")
    matches = [
        function
        for function in functions
        if isinstance(function, dict) and function.get("id") == FUNCTION_ID
    ] if isinstance(functions, list) else []
    if len(matches) != 1 or matches[0] != {
        **matches[0],
        "bank": 5,
        "address": "0x5668",
        "patchable": True,
        "emitted_name": "sym_BillsPC",
        "source_symbol": "BillsPC",
    }:
        raise ValueError("metadata does not confirm the patchable BillsPC identity")

    patch = load(args.patch_manifest)
    bindings = patch.get("bindings")
    if (
        patch.get("patch_id") != PATCH_ID
        or not isinstance(bindings, list)
        or len(bindings) != 1
        or bindings[0].get("function") != FUNCTION_ID
        or bindings[0].get("replace") != "crystal_native_bills_pc"
        or bindings[0].get("entry_contract") != "return-stack"
    ):
        raise ValueError("native patch manifest does not bind only BillsPC")

    result = {
        "schema": "crystal-recompiled.native-bills-pc-verification",
        "version": 1,
        "passed": True,
        "function_id": FUNCTION_ID,
        "patch_id": PATCH_ID,
        "segments": len(original_segments),
        "checkpoints": sum(
            len(segment["checkpoints"]) for segment in original_segments.values()
        ),
        "native_events": 4,
        "semantic_state_equal": True,
        "save_equal": True,
        "rtc_equal": True,
        "primary_backup_checksums_valid": True,
        "restart_segment_passed": True,
        "original_result_sha256": sha256(args.original),
        "native_result_sha256": sha256(args.native),
        "save_sha256": sha256(args.native_save),
        "metadata_sha256": sha256(args.metadata),
        "patch_manifest_sha256": sha256(args.patch_manifest),
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
