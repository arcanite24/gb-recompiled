#!/usr/bin/env python3
"""Map Crystal's historical fallback and analyzer sites to generated metadata."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

FALLBACK_RE = re.compile(
    r"\[INTERP\] Fallback site #\d+ "
    r"(?P<bank>[0-9A-Fa-f]+):(?P<address>[0-9A-Fa-f]+) "
    r"reason=(?P<reason>[a-z_]+)"
)
UNDEFINED_RE = re.compile(
    r"\[ERROR\] Undefined instruction at "
    r"(?P<bank>[0-9A-Fa-f]+):(?P<address>[0-9A-Fa-f]+)"
)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def parse_sites(path: Path, pattern: re.Pattern[str]) -> set[tuple[int, int]]:
    sites: set[tuple[int, int]] = set()
    for match in pattern.finditer(path.read_text(encoding="utf-8")):
        sites.add((int(match.group("bank"), 16), int(match.group("address"), 16)))
    return sites


def fail(message: str) -> None:
    raise ValueError(message)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--metadata", type=Path, required=True)
    parser.add_argument("--fallback-root", type=Path, required=True)
    parser.add_argument("--generation-log", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    try:
        metadata = json.loads(args.metadata.read_text(encoding="utf-8"))
        if metadata.get("schema") != "gbrecomp.metadata":
            fail("unsupported generated metadata")
        diagnostics = metadata.get("analysis_diagnostics")
        if not isinstance(diagnostics, list) or not diagnostics:
            fail("generated metadata has no analysis_diagnostics")

        by_kind_site: dict[tuple[str, int, int], list[dict]] = {}
        kinds: dict[str, int] = {}
        required = {
            "id",
            "kind",
            "bank",
            "address",
            "memory_space",
            "status",
            "evidence",
            "suggested_annotation",
            "relationship",
        }
        for record in diagnostics:
            missing = required - set(record)
            if missing:
                fail(f"diagnostic missing fields {sorted(missing)}")
            if any(not record[field] for field in (
                "id",
                "kind",
                "memory_space",
                "status",
                "evidence",
                "suggested_annotation",
                "relationship",
            )):
                fail(f"diagnostic is not actionable: {record.get('id')}")
            address = int(record["address"], 0)
            key = (record["kind"], int(record["bank"]), address)
            by_kind_site.setdefault(key, []).append(record)
            kinds[record["kind"]] = kinds.get(record["kind"], 0) + 1

        fallback_logs = sorted(args.fallback_root.glob("*/runtime.log"))
        if not fallback_logs:
            fail("fallback root contains no segment runtime logs")
        fallback_sites: set[tuple[int, int]] = set()
        for path in fallback_logs:
            fallback_sites |= parse_sites(path, FALLBACK_RE)
        if not fallback_sites:
            fail("historical fallback inventory is empty")

        missing_fallbacks = [
            f"{bank:03x}:{address:04x}"
            for bank, address in sorted(fallback_sites)
            if (
                "manual_entry_point",
                bank,
                address,
            ) not in by_kind_site
        ]
        if missing_fallbacks:
            fail(
                "fallback sites lack manual-entry diagnostics: "
                + ", ".join(missing_fallbacks)
            )
        for bank, address in fallback_sites:
            records = by_kind_site[("manual_entry_point", bank, address)]
            if not any(
                record["status"] == "configured"
                and record["relationship"] == "resolved_fallback_entry_point"
                for record in records
            ):
                fail(f"fallback mapping is not resolved at {bank:03x}:{address:04x}")

        undefined_sites = parse_sites(args.generation_log, UNDEFINED_RE)
        if not undefined_sites:
            fail("generation log contains no undefined-instruction controls")
        missing_undefined = [
            f"{bank:03x}:{address:04x}"
            for bank, address in sorted(undefined_sites)
            if (
                "undefined_instruction",
                bank,
                address,
            ) not in by_kind_site
        ]
        if missing_undefined:
            fail(
                "undefined sites lack metadata diagnostics: "
                + ", ".join(missing_undefined)
            )

        required_kinds = {
            "data_as_code_candidate",
            "manual_entry_point",
            "undefined_instruction",
            "unresolved_indirect_jump",
        }
        missing_kinds = required_kinds - kinds.keys()
        if missing_kinds:
            fail(f"missing Crystal diagnostic kinds: {sorted(missing_kinds)}")

        result = {
            "schema": "gbrecompiled.pokemon-crystal.analysis-diagnostics-result",
            "version": 1,
            "passed": True,
            "metadata_sha256": sha256(args.metadata),
            "generation_log_sha256": sha256(args.generation_log),
            "diagnostic_count": len(diagnostics),
            "diagnostic_kinds": dict(sorted(kinds.items())),
            "historical_fallback_sites": len(fallback_sites),
            "mapped_fallback_sites": len(fallback_sites),
            "undefined_instruction_sites": len(undefined_sites),
            "mapped_undefined_instruction_sites": len(undefined_sites),
        }
        rendered = json.dumps(result, indent=2, sort_keys=True) + "\n"
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(rendered, encoding="utf-8")
        print(rendered, end="")
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
