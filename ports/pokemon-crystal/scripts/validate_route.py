#!/usr/bin/env python3
"""Run the checked-in Pokémon Crystal route and verify declared checkpoints."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path
from typing import Any


SCHEMA = "gbrecompiled.pokemon-crystal.route"
GENERATION_SCHEMA = "crystal-recompiled.generation"
SHA256_RE = re.compile(r"[0-9a-f]{64}")
REQUIRED_CHECKPOINTS = (
    "title",
    "new_game",
    "overworld",
    "map_transition",
    "wild_battle",
    "trainer_battle",
    "start_menu",
    "pokedex",
    "pc",
    "save",
    "restart",
    "continue",
)
CHALLENGE_CHECKPOINTS = REQUIRED_CHECKPOINTS[:6]
CHALLENGE_PROFILE = "challenge-wild-trainer-v1"
FALLBACK_POLICY_SCHEMA = "gbrecompiled.pokemon-crystal.fallback-policy"
FALLBACK_INVENTORY_RE = re.compile(
    r"^\[INTERP\] Fallback inventory: sites=(?P<sites>\d+) "
    r"dropped=(?P<dropped>\d+) complete=(?P<complete>yes|no)$"
)
FALLBACK_SITE_RE = re.compile(
    r"^\[INTERP\] Fallback site #(?P<rank>\d+) "
    r"(?P<bank>[0-9A-F]{3}):(?P<addr>[0-9A-F]{4}) "
    r"reason=(?P<reason>[a-z_]+) entries=(?P<entries>\d+) "
    r"instructions=(?P<instructions>\d+) cycles=(?P<cycles>\d+) "
    r"first_frame=(?P<first_frame>\d+) last_frame=(?P<last_frame>\d+) "
    r"compiled_bank_variants=(?P<compiled_bank_variants>\d+)$"
)
FALLBACK_SUMMARY_RE = re.compile(
    r"^\[INTERP\] Summary: fallbacks=(?P<fallbacks>\d+) "
    r"interpreter_entries=(?P<entries>\d+) "
    r"interpreter_instructions=(?P<instructions>\d+) "
    r"interpreter_cycles=(?P<cycles>\d+)$"
)
NO_FALLBACK_RE = re.compile(r"^\[INTERP\] No interpreter fallback recorded\.$")
PORT_EVENT_RE = re.compile(
    r"^\[GBRT\]\[port:(?P<module>[a-z0-9_-]+)\]\[info\] "
    r"native UI (?P<state>shown|hidden)$"
)
NATIVE_POKEDEX_EVENT_RE = re.compile(
    r"^\[GBRT\]\[port:(?P<module>[a-z0-9_-]+)\]\[info\] "
    r"native Pokedex (?:(?P<state>shown|hidden)|species (?P<species>\d+))$"
)
NATIVE_PC_EVENT_RE = re.compile(
    r"^\[GBRT\]\[port:(?P<module>[a-z0-9_-]+)\]\[info\] "
    r"native PC (?P<state>shown|hidden)$"
)


class ValidationError(RuntimeError):
    pass


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def canonical_sha256(value: object) -> str:
    encoded = json.dumps(value, sort_keys=True, separators=(",", ":")).encode(
        "utf-8"
    )
    return hashlib.sha256(encoded).hexdigest()


def is_sha256(value: object) -> bool:
    return isinstance(value, str) and SHA256_RE.fullmatch(value) is not None


def exact_object(value: object, keys: set[str], label: str) -> dict[str, Any]:
    if not isinstance(value, dict) or set(value) != keys:
        raise ValidationError(f"{label} fields are missing or unknown")
    return value


def load_json(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValidationError(f"cannot read JSON {path}: {error}") from error


def confined_file(root: Path, relative: object) -> Path:
    if not isinstance(relative, str) or not relative:
        raise ValidationError("route file path must be a non-empty string")
    candidate = (root / relative).resolve()
    try:
        candidate.relative_to(root)
    except ValueError as error:
        raise ValidationError(f"route file escapes route directory: {relative}") from error
    if not candidate.is_file():
        raise ValidationError(f"missing route file: {relative}")
    return candidate


def cycle_input(
    path: Path,
    cycle_shift: object = None,
    prefix_actions: object = None,
    overlay_path: Path | None = None,
) -> str:
    actions = load_json(path)
    if not isinstance(actions, list) or not actions:
        raise ValidationError(f"input must contain at least one action: {path}")
    if prefix_actions is not None:
        if (
            not isinstance(prefix_actions, int)
            or isinstance(prefix_actions, bool)
            or prefix_actions <= 0
            or prefix_actions > len(actions)
        ):
            raise ValidationError(f"invalid input prefix action count: {path}")
        actions = actions[:prefix_actions]
    shift_start = len(actions)
    shift_delta = 0
    if cycle_shift is not None:
        shift = exact_object(
            cycle_shift, {"start_index", "delta"}, "input cycle shift"
        )
        shift_start = shift["start_index"]
        shift_delta = shift["delta"]
        if (
            not isinstance(shift_start, int)
            or isinstance(shift_start, bool)
            or shift_start < 0
            or shift_start >= len(actions)
            or not isinstance(shift_delta, int)
            or isinstance(shift_delta, bool)
            or shift_delta <= 0
        ):
            raise ValidationError(f"invalid input cycle shift: {path}")
    rendered: list[str] = []
    for action_index, source_action in enumerate(actions):
        action = (
            dict(source_action)
            if isinstance(source_action, dict)
            else source_action
        )
        if not isinstance(action, dict):
            raise ValidationError(f"input action must be an object: {path}")
        if action_index >= shift_start:
            for key in ("cycle", "start_cycle", "end_cycle"):
                if key in action:
                    value = action[key]
                    if not isinstance(value, int) or isinstance(value, bool):
                        raise ValidationError(
                            f"invalid shifted cycle input action: {action!r}"
                        )
                    action[key] = value + shift_delta
        buttons = action.get("buttons")
        duration = action.get("duration")
        if (
            not isinstance(duration, int)
            or isinstance(duration, bool)
            or duration <= 0
        ):
            raise ValidationError(f"invalid cycle input action: {action!r}")
        if "buttons_sequence" in action:
            sequence = action.get("buttons_sequence")
            start = action.get("start_cycle")
            step = action.get("step_cycles")
            count = action.get("count")
            if (
                not isinstance(sequence, list)
                or not sequence
                or any(not isinstance(item, str) or not item for item in sequence)
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
                raise ValidationError(f"invalid cycle input sequence: {action!r}")
            rendered.extend(
                f"c{start + index * step}:{sequence[index % len(sequence)]}:{duration}"
                for index in range(count)
            )
        elif "cycle" in action:
            cycle = action.get("cycle")
            if (
                not isinstance(buttons, str)
                or not buttons
                or not isinstance(cycle, int)
                or isinstance(cycle, bool)
                or cycle < 0
            ):
                raise ValidationError(f"invalid cycle input action: {action!r}")
            rendered.append(f"c{cycle}:{buttons}:{duration}")
        else:
            start = action.get("start_cycle")
            end = action.get("end_cycle")
            step = action.get("step_cycles")
            if (
                not isinstance(buttons, str)
                or not buttons
                or not isinstance(start, int)
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
                raise ValidationError(f"invalid cycle input range: {action!r}")
            rendered.append(f"p{start}-{end}/{step}:{buttons}:{duration}")
        if len(rendered) > 2048:
            raise ValidationError(f"input expands beyond 2048 actions: {path}")
    if overlay_path is not None:
        try:
            overlay_text = overlay_path.read_text(encoding="utf-8")
        except (OSError, UnicodeError) as error:
            raise ValidationError(
                f"cannot read cycle-input overlay: {overlay_path}"
            ) from error
        if (
            not overlay_text.endswith("\n")
            or overlay_text.count("\n") != 1
            or overlay_text[:-1].strip() != overlay_text[:-1]
            or not overlay_text[:-1]
        ):
            raise ValidationError(
                f"cycle-input overlay is not canonical: {overlay_path}"
            )
        overlay_tokens = overlay_text[:-1].split(",")
        cycle_token = re.compile(
            r"c(?P<cycle>\d+):(?P<cycle_buttons>[ABUDLRSE]+):(?P<cycle_duration>\d+)"
            r"|p(?P<start>\d+)-(?P<end>\d+)/(?P<step>\d+):"
            r"(?P<periodic_buttons>[ABUDLRSE]+):(?P<periodic_duration>\d+)"
        )
        for token in overlay_tokens:
            match = cycle_token.fullmatch(token)
            valid = match is not None
            if match is not None and match.group("cycle") is not None:
                buttons = match.group("cycle_buttons")
                duration = int(match.group("cycle_duration"))
                valid = duration > 0 and len(set(buttons)) == len(buttons)
            elif match is not None:
                start = int(match.group("start"))
                end = int(match.group("end"))
                step = int(match.group("step"))
                buttons = match.group("periodic_buttons")
                duration = int(match.group("periodic_duration"))
                valid = (
                    end >= start
                    and step > 0
                    and duration > 0
                    and duration < step
                    and len(set(buttons)) == len(buttons)
                )
            if not valid:
                raise ValidationError(
                    f"invalid cycle-input overlay token: {overlay_path}"
                )
        if len(rendered) + len(overlay_tokens) > 2048:
            raise ValidationError(
                f"input plus overlay expands beyond 2048 actions: {path}"
            )
        rendered.extend(overlay_tokens)
    return ",".join(rendered)


def state_value(state: object, dotted_path: str) -> object:
    value = state
    for component in dotted_path.split("."):
        if isinstance(value, dict) and component in value:
            value = value[component]
            continue
        if isinstance(value, list) and component.isdecimal():
            index = int(component)
            if index < len(value):
                value = value[index]
                continue
        raise ValidationError(f"state does not contain {dotted_path}")
    return value


def validate_manifest(payload: object) -> dict[str, Any]:
    if not isinstance(payload, dict):
        raise ValidationError("manifest root must be an object")
    version = payload.get("version")
    if payload.get("schema") != SCHEMA or version not in (1, 2):
        raise ValidationError("unsupported route manifest schema or version")
    if version == 2:
        exact_object(
            payload,
            {
                "schema",
                "version",
                "profile",
                "rom_sha256",
                "rtc_unix_time",
                "ignore_rtc_persistence",
                "build",
                "native_patch",
                "host_configuration",
                "mods",
                "segments",
                "portable_seed_sha256",
            },
            "v2 route manifest",
        )
        if payload["profile"] != CHALLENGE_PROFILE:
            raise ValidationError("unsupported v2 route profile")
        if not is_sha256(payload["portable_seed_sha256"]):
            raise ValidationError("v2 route portable seed hash is invalid")
    rom_hash = payload.get("rom_sha256")
    if (
        not isinstance(rom_hash, str)
        or len(rom_hash) != 64
        or any(character not in "0123456789abcdef" for character in rom_hash)
    ):
        raise ValidationError("manifest rom_sha256 must be lowercase SHA-256")
    segments = payload.get("segments")
    if not isinstance(segments, list) or not segments:
        raise ValidationError("manifest must contain at least one segment")
    checkpoint_ids: list[object] = []
    segment_ids: set[str] = set()
    segment_order: list[str] = []
    for index, segment in enumerate(segments):
        if not isinstance(segment, dict):
            raise ValidationError(f"segment {index} must be an object")
        segment_id = segment.get("id")
        if (
            not isinstance(segment_id, str)
            or re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_-]*", segment_id) is None
        ):
            raise ValidationError(f"segment {index} has unsafe id")
        if segment_id in segment_ids:
            raise ValidationError(f"duplicate segment id: {segment_id}")
        segment_ids.add(segment_id)
        segment_order.append(segment_id)
        if version == 2:
            input_hash = segment.get("input_sha256")
            if not is_sha256(input_hash):
                raise ValidationError(
                    f"segment {segment_id} has invalid input_sha256"
                )
            cycle_shift = segment.get("input_cycle_shift")
            if cycle_shift is not None:
                shift = exact_object(
                    cycle_shift,
                    {"start_index", "delta"},
                    f"segment {segment_id} input cycle shift",
                )
                if (
                    not isinstance(shift["start_index"], int)
                    or isinstance(shift["start_index"], bool)
                    or shift["start_index"] < 0
                    or not isinstance(shift["delta"], int)
                    or isinstance(shift["delta"], bool)
                    or shift["delta"] <= 0
                ):
                    raise ValidationError(
                        f"segment {segment_id} has invalid input cycle shift"
                    )
            prefix_actions = segment.get("input_prefix_actions")
            if prefix_actions is not None and (
                not isinstance(prefix_actions, int)
                or isinstance(prefix_actions, bool)
                or prefix_actions <= 0
            ):
                raise ValidationError(
                    f"segment {segment_id} has invalid input prefix action count"
                )
            overlay = segment.get("input_overlay")
            overlay_hash = segment.get("input_overlay_sha256")
            if (overlay is None) != (overlay_hash is None):
                raise ValidationError(
                    f"segment {segment_id} input overlay identity is incomplete"
                )
            if overlay is not None and (
                not isinstance(overlay, str)
                or not overlay
                or not is_sha256(overlay_hash)
            ):
                raise ValidationError(
                    f"segment {segment_id} input overlay identity is invalid"
                )
        frame_limit = segment.get("frame_limit")
        if (
            not isinstance(frame_limit, int)
            or isinstance(frame_limit, bool)
            or frame_limit <= 0
        ):
            raise ValidationError(f"segment {segment_id} has invalid frame_limit")
        checkpoints = segment.get("checkpoints")
        if not isinstance(checkpoints, list) or not checkpoints:
            raise ValidationError(f"segment {segment_id} has no checkpoints")
        for checkpoint in checkpoints:
            if not isinstance(checkpoint, dict):
                raise ValidationError(f"segment {segment_id} has an invalid checkpoint")
            checkpoint_id = checkpoint.get("id")
            frame = checkpoint.get("frame")
            frame_hash = checkpoint.get("frame_sha256")
            if not isinstance(checkpoint_id, str) or not checkpoint_id:
                raise ValidationError(f"segment {segment_id} has checkpoint without id")
            if (
                not isinstance(frame, int)
                or isinstance(frame, bool)
                or frame <= 0
            ):
                raise ValidationError(
                    f"checkpoint {checkpoint_id} has an invalid frame"
                )
            if frame > frame_limit:
                raise ValidationError(
                    f"checkpoint {checkpoint_id} frame {frame} exceeds frame_limit "
                    f"{frame_limit}"
                )
            if (
                not isinstance(frame_hash, str)
                or len(frame_hash) != 64
                or any(character not in "0123456789abcdef" for character in frame_hash)
            ):
                raise ValidationError(
                    f"checkpoint {checkpoint_id} has invalid frame hash"
                )
            checkpoint_ids.append(checkpoint_id)
    expected_checkpoints = (
        CHALLENGE_CHECKPOINTS if version == 2 else REQUIRED_CHECKPOINTS
    )
    if tuple(checkpoint_ids) != expected_checkpoints:
        raise ValidationError(
            "route checkpoints must be exactly: "
            + ", ".join(expected_checkpoints)
        )
    if version == 2 and segment_order != ["new-game", "adventure"]:
        raise ValidationError(
            "Challenge replay segments must be new-game then adventure"
        )
    return payload


def validate_challenge_replay_preflight(
    manifest: dict[str, Any],
    route_root: Path,
    executable: Path,
    receipt_path: Path,
    receipt: dict[str, Any],
    native_patch_path: Path | None,
    host_configuration_path: Path | None,
) -> dict[str, Any] | None:
    if manifest["version"] == 1:
        if native_patch_path is not None or host_configuration_path is not None:
            raise ValidationError(
                "native patch/configuration inputs require a v2 replay manifest"
            )
        return None
    if native_patch_path is None or host_configuration_path is None:
        raise ValidationError(
            "v2 Challenge replay requires native patch and host configuration"
        )
    if not native_patch_path.is_file():
        raise ValidationError("missing native patch manifest")
    if not host_configuration_path.is_file():
        raise ValidationError("missing host configuration")

    build = exact_object(
        manifest["build"],
        {
            "executable_sha256",
            "generation_receipt_sha256",
            "source_inventory_sha256",
        },
        "replay build",
    )
    if any(not is_sha256(value) for value in build.values()):
        raise ValidationError("replay build hashes are invalid")
    if (
        sha256(executable) != build["executable_sha256"]
        or sha256(receipt_path) != build["generation_receipt_sha256"]
        or receipt.get("generated", {}).get("source_inventory_sha256")
        != build["source_inventory_sha256"]
    ):
        raise ValidationError("replay build identity mismatch")

    native_patch = exact_object(
        manifest["native_patch"],
        {"kind", "patch_id", "manifest_sha256"},
        "replay native patch",
    )
    if (
        native_patch["kind"] != "file"
        or not isinstance(native_patch["patch_id"], str)
        or not native_patch["patch_id"]
        or not is_sha256(native_patch["manifest_sha256"])
    ):
        raise ValidationError("replay native patch identity is invalid")
    patch_payload = load_json(native_patch_path)
    receipt_patch = receipt.get("native_patch")
    if (
        not isinstance(patch_payload, dict)
        or patch_payload.get("schema") != "gbrecomp.native-patch"
        or patch_payload.get("version") != 1
        or patch_payload.get("patch_id") != native_patch["patch_id"]
        or patch_payload.get("rom", {}).get("sha256") != manifest["rom_sha256"]
        or sha256(native_patch_path) != native_patch["manifest_sha256"]
        or not isinstance(receipt_patch, dict)
        or receipt_patch.get("kind") != "file"
        or receipt_patch.get("sha256") != native_patch["manifest_sha256"]
    ):
        raise ValidationError("replay native patch mismatch")

    host_identity = exact_object(
        manifest["host_configuration"],
        {"schema", "version", "policy_id", "sha256"},
        "replay host configuration",
    )
    if (
        host_identity["schema"] != "gbrecomp.host-configuration"
        or host_identity["version"] != 1
        or not isinstance(host_identity["policy_id"], str)
        or not host_identity["policy_id"]
        or not is_sha256(host_identity["sha256"])
        or sha256(host_configuration_path) != host_identity["sha256"]
    ):
        raise ValidationError("replay host configuration identity mismatch")
    configuration = load_json(host_configuration_path)
    expected_config_keys = [
        "schema",
        "version",
        "policy_id",
        "applied",
        "enabled",
        "offset",
        "minimum",
        "maximum",
    ]
    contract = patch_payload.get("host_configuration")
    contract_offset_limit = (
        contract.get("offset_limit") if isinstance(contract, dict) else None
    )
    contract_minimum = (
        contract.get("value_minimum") if isinstance(contract, dict) else None
    )
    contract_maximum = (
        contract.get("value_maximum") if isinstance(contract, dict) else None
    )
    if (
        not isinstance(configuration, dict)
        or list(configuration) != expected_config_keys
        or set(configuration) != set(expected_config_keys)
        or configuration["schema"] != host_identity["schema"]
        or configuration["version"] != host_identity["version"]
        or configuration["policy_id"] != host_identity["policy_id"]
        or configuration["applied"] is not True
        or configuration["enabled"] is not True
        or not isinstance(configuration["offset"], int)
        or isinstance(configuration["offset"], bool)
        or not isinstance(configuration["minimum"], int)
        or isinstance(configuration["minimum"], bool)
        or not isinstance(configuration["maximum"], int)
        or isinstance(configuration["maximum"], bool)
        or not isinstance(contract, dict)
        or not isinstance(contract_offset_limit, int)
        or isinstance(contract_offset_limit, bool)
        or contract_offset_limit < 0
        or not isinstance(contract_minimum, int)
        or isinstance(contract_minimum, bool)
        or not isinstance(contract_maximum, int)
        or isinstance(contract_maximum, bool)
        or contract.get("schema") != configuration["schema"]
        or contract.get("version") != configuration["version"]
        or contract.get("policy_id") != configuration["policy_id"]
        or abs(configuration["offset"]) > contract_offset_limit
        or configuration["minimum"] != contract_minimum
        or configuration["maximum"] != contract_maximum
    ):
        raise ValidationError("replay host configuration violates its contract")
    canonical_configuration = (
        json.dumps(configuration, separators=(",", ":"), ensure_ascii=True) + "\n"
    ).encode("utf-8")
    if host_configuration_path.read_bytes() != canonical_configuration:
        raise ValidationError("replay host configuration is not canonical")

    mods = exact_object(manifest["mods"], {"kind"}, "replay mods")
    if mods != {"kind": "none"}:
        raise ValidationError("unsupported replay mod identity")

    for segment in manifest["segments"]:
        input_path = confined_file(route_root, segment["input"])
        if sha256(input_path) != segment["input_sha256"]:
            raise ValidationError(
                f"replay input hash mismatch: {segment['id']}"
            )
        overlay_relative = segment.get("input_overlay")
        overlay_path = (
            confined_file(route_root, overlay_relative)
            if overlay_relative is not None
            else None
        )
        if overlay_path is not None and sha256(overlay_path) != segment[
            "input_overlay_sha256"
        ]:
            raise ValidationError(
                f"replay input overlay hash mismatch: {segment['id']}"
            )
        cycle_input(
            input_path,
            segment.get("input_cycle_shift"),
            segment.get("input_prefix_actions"),
            overlay_path,
        )

    portable = dict(manifest)
    expected_portable_hash = portable.pop("portable_seed_sha256")
    if canonical_sha256(portable) != expected_portable_hash:
        raise ValidationError("replay portable seed hash mismatch")

    return {
        "portable_seed_sha256": expected_portable_hash,
        "build": build,
        "native_patch": native_patch,
        "host_configuration": host_identity,
        "mods": mods,
    }


def load_fallback_policy(path: Path) -> tuple[dict[str, Any], set[tuple[int, int, str]]]:
    payload = load_json(path)
    if (
        not isinstance(payload, dict)
        or payload.get("schema") != FALLBACK_POLICY_SCHEMA
        or payload.get("version") != 1
        or not isinstance(payload.get("allowed_sites"), list)
    ):
        raise ValidationError("unsupported fallback policy schema or version")

    allowed: set[tuple[int, int, str]] = set()
    for index, site in enumerate(payload["allowed_sites"]):
        if not isinstance(site, dict):
            raise ValidationError(f"fallback policy site {index} must be an object")
        bank = site.get("bank")
        address = site.get("address")
        reason = site.get("reason")
        correctness = site.get("correctness")
        rationale = site.get("rationale")
        if (
            not isinstance(bank, int)
            or isinstance(bank, bool)
            or bank < 0
            or bank > 0x1FF
            or not isinstance(address, str)
            or re.fullmatch(r"0x[0-9a-f]{4}", address) is None
            or not isinstance(reason, str)
            or re.fullmatch(r"[a-z_]+", reason) is None
            or correctness != "universal_interpreter"
            or not isinstance(rationale, str)
            or not rationale.strip()
        ):
            raise ValidationError(f"fallback policy site {index} is invalid")
        key = (bank, int(address, 16), reason)
        if key in allowed:
            raise ValidationError(f"duplicate fallback policy site: {bank:03X}:{address[2:]}")
        allowed.add(key)
    return payload, allowed


def parse_fallback_log(path: Path) -> dict[str, Any]:
    inventories: list[dict[str, object]] = []
    sites: list[dict[str, Any]] = []
    summaries: list[dict[str, int]] = []
    no_fallback = False
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = FALLBACK_INVENTORY_RE.fullmatch(line)
        if match:
            inventories.append(
                {
                    "sites": int(match.group("sites")),
                    "dropped": int(match.group("dropped")),
                    "complete": match.group("complete") == "yes",
                }
            )
            continue
        match = FALLBACK_SITE_RE.fullmatch(line)
        if match:
            sites.append(
                {
                    "bank": int(match.group("bank"), 16),
                    "address": f"0x{int(match.group('addr'), 16):04x}",
                    "reason": match.group("reason"),
                    "entries": int(match.group("entries")),
                    "instructions": int(match.group("instructions")),
                    "cycles": int(match.group("cycles")),
                    "first_frame": int(match.group("first_frame")),
                    "last_frame": int(match.group("last_frame")),
                    "compiled_bank_variants": int(
                        match.group("compiled_bank_variants")
                    ),
                }
            )
            continue
        match = FALLBACK_SUMMARY_RE.fullmatch(line)
        if match:
            summaries.append(
                {
                    "fallbacks": int(match.group("fallbacks")),
                    "interpreter_entries": int(match.group("entries")),
                    "interpreter_instructions": int(match.group("instructions")),
                    "interpreter_cycles": int(match.group("cycles")),
                }
            )
            continue
        if NO_FALLBACK_RE.fullmatch(line):
            no_fallback = True

    if len(inventories) != 1:
        raise ValidationError(
            f"fallback log must contain exactly one inventory: {path}"
        )
    inventory = inventories[0]
    if not inventory["complete"] or inventory["dropped"] != 0:
        raise ValidationError(f"fallback inventory is incomplete: {path}")
    if inventory["sites"] != len(sites):
        raise ValidationError(f"fallback inventory site count mismatch: {path}")
    if len(summaries) > 1:
        raise ValidationError(f"fallback log contains duplicate summaries: {path}")
    if sites and len(summaries) != 1:
        raise ValidationError(f"fallback log is missing its summary: {path}")
    if not sites and not no_fallback:
        raise ValidationError(f"fallback log is missing its no-fallback verdict: {path}")
    if sites and sum(site["entries"] for site in sites) != summaries[0]["fallbacks"]:
        raise ValidationError(f"fallback entry total does not match summary: {path}")
    return {
        "complete": True,
        "dropped_sites": 0,
        "sites": sites,
        "summary": summaries[0] if summaries else {
            "fallbacks": 0,
            "interpreter_entries": 0,
            "interpreter_instructions": 0,
            "interpreter_cycles": 0,
        },
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--generation-receipt", required=True, type=Path)
    parser.add_argument("--native-patch-manifest", type=Path)
    parser.add_argument("--host-configuration", type=Path)
    parser.add_argument("--evidence-dir", required=True, type=Path)
    parser.add_argument("--pcm-seconds", type=int, default=0)
    parser.add_argument("--fallback-policy", type=Path)
    parser.add_argument("--rtc-unix-time", type=int)
    parser.add_argument(
        "--runtime-arg",
        action="append",
        default=[],
        help="append one non-core generated-runtime argument",
    )
    parser.add_argument(
        "--capture-port-state",
        action="store_true",
        help="capture the compiled port module's final state per segment",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    manifest_path = args.manifest.resolve()
    route_root = manifest_path.parent
    manifest = validate_manifest(load_json(manifest_path))
    if args.pcm_seconds < 0:
        raise ValidationError("pcm-seconds must be zero or greater")
    if args.rtc_unix_time is not None and args.rtc_unix_time < 0:
        raise ValidationError("rtc-unix-time must be zero or greater")
    forbidden_runtime_args = {
        "--headless",
        "--limit-frames",
        "--input",
        "--dump-frames",
        "--screenshot-prefix",
        "--dump-state",
        "--save-dir",
        "--log-file",
        "--rtc-unix-time",
        "--port-state",
        "--host-configuration",
        "--data-mod",
        "--load-state-file",
    }
    if (
        any(
            not isinstance(value, str)
            or not value
            or value in forbidden_runtime_args
            for value in args.runtime_arg
        )
    ):
        raise ValidationError("runtime-arg is empty or overrides route evidence")
    manifest_rtc_time = manifest.get("rtc_unix_time")
    if manifest_rtc_time is not None and (
        not isinstance(manifest_rtc_time, int)
        or isinstance(manifest_rtc_time, bool)
        or manifest_rtc_time < 0
    ):
        raise ValidationError("manifest rtc_unix_time must be zero or greater")
    rtc_unix_time = (
        args.rtc_unix_time
        if args.rtc_unix_time is not None
        else manifest_rtc_time
    )
    ignore_rtc_persistence = manifest.get("ignore_rtc_persistence", False)
    if not isinstance(ignore_rtc_persistence, bool):
        raise ValidationError("manifest ignore_rtc_persistence must be boolean")
    executable = args.executable.resolve()
    if not executable.is_file():
        raise ValidationError(f"missing executable: {executable}")
    receipt_path = args.generation_receipt.resolve()
    receipt = load_json(receipt_path)
    if (
        not isinstance(receipt, dict)
        or receipt.get("schema") != GENERATION_SCHEMA
        or receipt.get("version") != 1
        or not isinstance(receipt.get("rom"), dict)
        or receipt["rom"].get("sha256") != manifest["rom_sha256"]
    ):
        raise ValidationError("generation receipt does not match the route ROM")
    native_patch_path = (
        args.native_patch_manifest.resolve()
        if args.native_patch_manifest is not None
        else None
    )
    host_configuration_path = (
        args.host_configuration.resolve()
        if args.host_configuration is not None
        else None
    )
    replay_preflight = validate_challenge_replay_preflight(
        manifest,
        route_root,
        executable,
        receipt_path,
        receipt,
        native_patch_path,
        host_configuration_path,
    )
    fallback_policy_path = args.fallback_policy.resolve() if args.fallback_policy else None
    fallback_policy = None
    allowed_fallbacks: set[tuple[int, int, str]] = set()
    if fallback_policy_path is not None:
        if not fallback_policy_path.is_file():
            raise ValidationError(f"missing fallback policy: {fallback_policy_path}")
        fallback_policy, allowed_fallbacks = load_fallback_policy(fallback_policy_path)

    evidence = args.evidence_dir.resolve()
    if evidence.exists():
        if not evidence.is_dir() or any(evidence.iterdir()):
            raise ValidationError(f"evidence directory must be absent or empty: {evidence}")
    else:
        evidence.mkdir(parents=True)
    persistence = evidence / "persistence"
    persistence.mkdir(exist_ok=True)
    segment_reports: list[dict[str, Any]] = []

    for index, segment in enumerate(manifest["segments"]):
        if not isinstance(segment, dict) or not isinstance(segment.get("id"), str):
            raise ValidationError(f"segment {index} must have a string id")
        segment_id = segment["id"]
        segment_dir = evidence / f"{index + 1:02d}-{segment_id}"
        segment_dir.mkdir(exist_ok=True)
        input_path = confined_file(route_root, segment.get("input"))
        input_cycle_shift = segment.get("input_cycle_shift")
        input_prefix_actions = segment.get("input_prefix_actions")
        input_overlay_relative = segment.get("input_overlay")
        input_overlay_path = (
            confined_file(route_root, input_overlay_relative)
            if input_overlay_relative is not None
            else None
        )
        input_script = cycle_input(
            input_path,
            input_cycle_shift,
            input_prefix_actions,
            input_overlay_path,
        )
        frame_limit = segment.get("frame_limit")
        if not isinstance(frame_limit, int) or isinstance(frame_limit, bool) or frame_limit <= 0:
            raise ValidationError(f"segment {segment_id} has invalid frame_limit")
        checkpoints = segment.get("checkpoints")
        if not isinstance(checkpoints, list) or not checkpoints:
            raise ValidationError(f"segment {segment_id} has no checkpoints")
        frames = [checkpoint.get("frame") for checkpoint in checkpoints if isinstance(checkpoint, dict)]
        if any(not isinstance(frame, int) or isinstance(frame, bool) or frame <= 0 for frame in frames):
            raise ValidationError(f"segment {segment_id} has an invalid checkpoint frame")
        if len(frames) != len(checkpoints):
            raise ValidationError(f"segment {segment_id} has an invalid checkpoint")

        prefix = segment_dir / "frame"
        state_path = segment_dir / "state.json"
        log_path = segment_dir / "runtime.log"
        command = [
            str(executable),
            "--headless",
            "--limit-frames",
            str(frame_limit),
            "--input",
            input_script,
            "--dump-frames",
            ",".join(str(frame) for frame in frames),
            "--screenshot-prefix",
            str(prefix),
            "--dump-state",
            str(state_path),
            "--save-dir",
            str(persistence),
            "--log-file",
            str(log_path),
        ]
        if args.pcm_seconds:
            command.extend(
                [
                    "--debug-audio",
                    "--debug-audio-seconds",
                    str(args.pcm_seconds),
                ]
            )
        else:
            command.append("--no-audio")
        if rtc_unix_time is not None:
            command.extend(["--rtc-unix-time", str(rtc_unix_time)])
        if ignore_rtc_persistence:
            command.append("--ignore-rtc-persistence")
        if host_configuration_path is not None:
            command.extend(
                ["--host-configuration", str(host_configuration_path)]
            )
        if fallback_policy is not None:
            command.extend(
                [
                    "--log-frame-fallbacks",
                    "--report-interpreter-hotspots",
                    "--interpreter-hotspot-limit",
                    "16",
                ]
            )
        command.extend(args.runtime_arg)
        port_state_path = segment_dir / "port-state.json"
        if args.capture_port_state:
            command.extend(["--port-state", str(port_state_path)])
        completed = subprocess.run(
            command,
            cwd=segment_dir,
            text=True,
            capture_output=True,
            check=False,
        )
        (segment_dir / "launcher.stdout").write_text(completed.stdout, encoding="utf-8")
        (segment_dir / "launcher.stderr").write_text(completed.stderr, encoding="utf-8")
        if completed.returncode != 0:
            raise ValidationError(
                f"segment {segment_id} exited with status {completed.returncode}"
            )
        if host_configuration_path is not None:
            diagnostics = completed.stdout + completed.stderr
            if log_path.is_file():
                diagnostics += log_path.read_text(
                    encoding="utf-8", errors="replace"
                )
            if str(host_configuration_path) in diagnostics:
                raise ValidationError(
                    f"segment {segment_id} leaked the host configuration path"
                )
        state = load_json(state_path)
        if replay_preflight is not None:
            host_identity = replay_preflight["host_configuration"]
            expected_host_state = {
                "present": True,
                "applied": True,
                "enabled": True,
                "policy_id": host_identity["policy_id"],
                "sha256": host_identity["sha256"],
            }
            if state.get("host_configuration") != expected_host_state:
                raise ValidationError(
                    f"segment {segment_id} host configuration state mismatch"
                )
        checkpoint_reports: list[dict[str, Any]] = []
        for checkpoint in checkpoints:
            checkpoint_id = checkpoint.get("id")
            expected_hash = checkpoint.get("frame_sha256")
            if not isinstance(checkpoint_id, str) or not checkpoint_id:
                raise ValidationError(f"segment {segment_id} has checkpoint without id")
            if not isinstance(expected_hash, str) or len(expected_hash) != 64:
                raise ValidationError(f"checkpoint {checkpoint_id} has invalid frame hash")
            frame_path = segment_dir / f"frame_{checkpoint['frame']:05d}.ppm"
            if not frame_path.is_file():
                raise ValidationError(f"checkpoint {checkpoint_id} did not capture its frame")
            actual_hash = sha256(frame_path)
            if actual_hash != expected_hash:
                raise ValidationError(
                    f"checkpoint {checkpoint_id} frame mismatch: "
                    f"expected {expected_hash}, got {actual_hash}"
                )
            checkpoint_reports.append(
                {
                    "id": checkpoint_id,
                    "frame": checkpoint["frame"],
                    "frame_sha256": actual_hash,
                    "passed": True,
                }
            )
        expected_state = segment.get("final_state")
        if not isinstance(expected_state, dict) or not expected_state:
            raise ValidationError(f"segment {segment_id} has no final_state assertions")
        state_assertions: list[dict[str, Any]] = []
        for dotted_path, expected in expected_state.items():
            if not isinstance(dotted_path, str):
                raise ValidationError(f"segment {segment_id} has invalid state path")
            actual = state_value(state, dotted_path)
            if actual != expected:
                raise ValidationError(
                    f"segment {segment_id} state mismatch at {dotted_path}: "
                    f"expected {expected!r}, got {actual!r}"
                )
            state_assertions.append(
                {"path": dotted_path, "expected": expected, "actual": actual, "passed": True}
            )
        pcm_report = None
        if args.pcm_seconds:
            pcm_path = segment_dir / "debug_audio.raw"
            if not pcm_path.is_file() or pcm_path.stat().st_size == 0:
                raise ValidationError(f"segment {segment_id} did not capture PCM")
            pcm_report = {
                "bytes": pcm_path.stat().st_size,
                "seconds_limit": args.pcm_seconds,
                "sha256": sha256(pcm_path),
            }
        fallback_report = (
            parse_fallback_log(log_path) if fallback_policy is not None else None
        )
        port_report = None
        if args.capture_port_state:
            port_state = load_json(port_state_path)
            if (
                not isinstance(port_state, dict)
                or port_state.get("schema") != "gbrecompiled.port-state"
                or port_state.get("active") is not True
                or port_state.get("headless") is not True
            ):
                raise ValidationError(
                    f"segment {segment_id} has invalid port state"
                )
            events = []
            for line in log_path.read_text(
                encoding="utf-8", errors="replace"
            ).splitlines():
                match = PORT_EVENT_RE.fullmatch(line)
                if match:
                    events.append(
                        {
                            "module": match.group("module"),
                            "state": match.group("state"),
                        }
                    )
                    continue
                match = NATIVE_POKEDEX_EVENT_RE.fullmatch(line)
                if match:
                    event = {
                        "module": match.group("module"),
                        "surface": "native-pokedex",
                    }
                    if match.group("state") is not None:
                        event["state"] = match.group("state")
                    else:
                        event["species"] = int(match.group("species"))
                    events.append(event)
                    continue
                match = NATIVE_PC_EVENT_RE.fullmatch(line)
                if match:
                    events.append(
                        {
                            "module": match.group("module"),
                            "surface": "native-pc",
                            "state": match.group("state"),
                        }
                    )
            port_report = {
                "state_sha256": sha256(port_state_path),
                "module_id": port_state.get("module_id"),
                "module_version": port_state.get("module_version"),
                "input_events": port_state.get("input_events"),
                "updates": port_state.get("updates"),
                "renders": port_state.get("renders"),
                "last_command_count": port_state.get("last_command_count"),
                "semantic_events": events,
            }
        segment_reports.append(
            {
                "id": segment_id,
                "passed": True,
                "input": str(input_path.relative_to(route_root)),
                "input_sha256": sha256(input_path),
                "input_cycle_shift": input_cycle_shift,
                "input_prefix_actions": input_prefix_actions,
                "input_overlay": input_overlay_relative,
                "input_overlay_sha256": (
                    sha256(input_overlay_path)
                    if input_overlay_path is not None
                    else None
                ),
                "command": command,
                "checkpoints": checkpoint_reports,
                "final_state": state_assertions,
                "state_sha256": sha256(state_path),
                "pcm": pcm_report,
                "fallbacks": fallback_report,
                "port": port_report,
            }
        )

    fallback_policy_report = None
    if fallback_policy is not None:
        observed_fallbacks = {
            (site["bank"], int(site["address"], 16), site["reason"])
            for segment in segment_reports
            for site in segment["fallbacks"]["sites"]
        }
        unknown = sorted(observed_fallbacks - allowed_fallbacks)
        stale = sorted(allowed_fallbacks - observed_fallbacks)
        if unknown:
            rendered = ", ".join(
                f"{bank:03X}:{addr:04X}/{reason}" for bank, addr, reason in unknown
            )
            raise ValidationError(f"unexplained fallback site(s): {rendered}")
        if stale:
            rendered = ", ".join(
                f"{bank:03X}:{addr:04X}/{reason}" for bank, addr, reason in stale
            )
            raise ValidationError(f"fallback policy site(s) not reached: {rendered}")
        fallback_policy_report = {
            "passed": True,
            "policy_sha256": sha256(fallback_policy_path),
            "observed_sites": len(observed_fallbacks),
            "total_entries": sum(
                site["entries"]
                for segment in segment_reports
                for site in segment["fallbacks"]["sites"]
            ),
            "unknown_sites": [],
            "stale_sites": [],
        }

    persistence_artifacts = {}
    for suffix in ("sav", "rtc"):
        matches = sorted(persistence.glob(f"*.{suffix}"))
        if len(matches) != 1:
            raise ValidationError(
                f"expected exactly one persistence .{suffix} artifact"
            )
        persistence_artifacts[suffix] = {
            "bytes": matches[0].stat().st_size,
            "sha256": sha256(matches[0]),
        }

    report = {
        "schema": "gbrecompiled.pokemon-crystal.route-result",
        "version": 1,
        "passed": True,
        "manifest_sha256": sha256(manifest_path),
        "executable_sha256": sha256(executable),
        "generation_receipt_sha256": sha256(receipt_path),
        "rtc_unix_time": rtc_unix_time,
        "ignore_rtc_persistence": ignore_rtc_persistence,
        "fallback_policy": fallback_policy_report,
        "runtime_args": args.runtime_arg,
        "capture_port_state": args.capture_port_state,
        "replay_preflight": replay_preflight,
        "persistence": persistence_artifacts,
        "segments": segment_reports,
    }
    (evidence / "result.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(
        f"PASS segments={len(segment_reports)} "
        f"checkpoints={sum(len(item['checkpoints']) for item in segment_reports)}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValidationError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
