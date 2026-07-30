#!/usr/bin/env python3
"""Compile and run Crystal native-PC search/sort/movement matrices."""

from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory() as raw:
        generated = Path(raw) / "semantic"
        subprocess.run(
            [
                sys.executable,
                str(
                    root
                    / "ports/pokemon-crystal/scripts/"
                    "generate_semantic_accessors.py"
                ),
                "--manifest",
                str(root / "ports/pokemon-crystal/semantic/package.json"),
                "--output-dir",
                str(generated),
            ],
            check=True,
        )
        probe = Path(raw) / "native-pc-model-probe"
        subprocess.run(
            [
                "cc",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(root / "runtime/include"),
                "-I",
                str(root / "ports/pokemon-crystal/module"),
                "-I",
                str(generated),
                str(root / "runtime/src/gbrt_semantic.c"),
                str(root / "runtime/src/gbrt_data_mod.c"),
                str(root / "runtime/src/gbrt_hash.c"),
                str(generated / "crystal_semantic.c"),
                str(root / "ports/pokemon-crystal/module/crystal_pc.c"),
                str(
                    root
                    / "ports/pokemon-crystal/tools/"
                    "native_pc_model_probe.c"
                ),
                "-o",
                str(probe),
            ],
            check=True,
        )
        subprocess.run([str(probe)], check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
