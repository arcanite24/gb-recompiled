#!/usr/bin/env python3
"""Exercise generated Crystal transactions against a user-provided save."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--save", type=Path, required=True)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--accessor-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[3]
    compiler = os.environ.get("CC") or shutil.which("cc")
    if compiler is None:
        raise RuntimeError("no C compiler found")
    if args.save.stat().st_size != 0x8000 or args.rom.stat().st_size != 0x200000:
        raise ValueError("unexpected Crystal save or ROM size")

    args.output_dir.mkdir(parents=True, exist_ok=True)
    probe = args.output_dir / "semantic-transaction-probe"
    output_save = args.output_dir / "committed-noop.sav"
    subprocess.run(
        [
            compiler,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(root / "runtime/include"),
            "-I",
            str(args.accessor_dir),
            str(root / "runtime/src/gbrt_semantic.c"),
            str(args.accessor_dir / "crystal_semantic.c"),
            str(
                root
                / "ports/pokemon-crystal/tools/semantic_transaction_probe.c"
            ),
            "-o",
            str(probe),
        ],
        check=True,
    )
    probe_result = json.loads(
        subprocess.check_output(
            [str(probe), str(args.save), str(args.rom), str(output_save)],
            text=True,
        )
    )
    source_hash = sha256(args.save)
    output_hash = sha256(output_save)
    if source_hash != output_hash:
        raise AssertionError("no-op transaction changed the source save")
    result = {
        "schema": "crystal-recompiled.semantic-transaction-verification",
        "version": 1,
        "passed": True,
        "rom_sha256": sha256(args.rom),
        "save_sha256": source_hash,
        "output_save_sha256": output_hash,
        "accessor_source_sha256": sha256(
            args.accessor_dir / "crystal_semantic.c"
        ),
        **probe_result,
    }
    result_path = args.output_dir / "result.json"
    result_path.write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
