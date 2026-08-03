#!/usr/bin/env python3
"""Focused positive and fail-closed controls for Crystal diagnostic mapping."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path


def diagnostic(kind: str, bank: int, address: int, relationship: str) -> dict:
    return {
        "id": f"analysis:v1:{kind}:{bank:04x}:{address:04x}",
        "kind": kind,
        "bank": bank,
        "address": f"0x{address:04x}",
        "memory_space": "physical_rom",
        "status": "configured" if kind == "manual_entry_point" else "unresolved",
        "evidence": "focused fixture evidence",
        "suggested_annotation": f"function {bank:02x}:{address:04x}",
        "relationship": relationship,
    }


def run(
    validator: Path,
    metadata: Path,
    fallback_root: Path,
    generation_log: Path,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            sys.executable,
            str(validator),
            "--metadata",
            str(metadata),
            "--fallback-root",
            str(fallback_root),
            "--generation-log",
            str(generation_log),
        ],
        text=True,
        capture_output=True,
        check=False,
    )


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    validator = (
        root
        / "ports"
        / "pokemon-crystal"
        / "scripts"
        / "validate_analysis_diagnostics.py"
    )
    with tempfile.TemporaryDirectory() as raw:
        temporary = Path(raw)
        fallback_root = temporary / "fallback"
        segment = fallback_root / "segment"
        segment.mkdir(parents=True)
        (segment / "runtime.log").write_text(
            "[INTERP] Fallback site #1 001:4000 "
            "reason=bank_not_compiled entries=1\n",
            encoding="utf-8",
        )
        generation_log = temporary / "generation.log"
        generation_log.write_text(
            "[ERROR] Undefined instruction at 2:5000\n",
            encoding="utf-8",
        )
        records = [
            diagnostic(
                "manual_entry_point",
                1,
                0x4000,
                "resolved_fallback_entry_point",
            ),
            diagnostic(
                "undefined_instruction", 2, 0x5000, "potential_false_code"
            ),
            diagnostic(
                "unresolved_indirect_jump",
                3,
                0x6000,
                "potential_dispatch_fallback",
            ),
            diagnostic(
                "data_as_code_candidate", 4, 0x7000, "potential_false_code"
            ),
        ]
        metadata = temporary / "metadata.json"
        metadata.write_text(
            json.dumps(
                {
                    "schema": "gbrecomp.metadata",
                    "schema_version": 2,
                    "analysis_diagnostics": records,
                }
            ),
            encoding="utf-8",
        )

        positive = run(validator, metadata, fallback_root, generation_log)
        if positive.returncode != 0:
            raise AssertionError(
                f"valid mapping failed:\n{positive.stdout}\n{positive.stderr}"
            )

        records[0]["suggested_annotation"] = ""
        metadata.write_text(
            json.dumps(
                {
                    "schema": "gbrecomp.metadata",
                    "schema_version": 2,
                    "analysis_diagnostics": records,
                }
            ),
            encoding="utf-8",
        )
        empty_action = run(validator, metadata, fallback_root, generation_log)
        if empty_action.returncode == 0:
            raise AssertionError("validator accepted an empty next annotation")

        records[0]["suggested_annotation"] = "function 01:4000"
        records.pop(1)
        metadata.write_text(
            json.dumps(
                {
                    "schema": "gbrecomp.metadata",
                    "schema_version": 2,
                    "analysis_diagnostics": records,
                }
            ),
            encoding="utf-8",
        )
        missing_site = run(validator, metadata, fallback_root, generation_log)
        if missing_site.returncode == 0:
            raise AssertionError("validator accepted an unmapped undefined site")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
