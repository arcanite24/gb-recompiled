#!/usr/bin/env python3
"""Verify that one ROM's aggressive scan cannot suppress another ROM's code."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--gbrecomp", type=Path, required=True)
    parser.add_argument("--fixture-generator", type=Path, required=True)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        rom_dir = root / "roms"
        rom_dir.mkdir()

        for name in ("alpha", "beta"):
            subprocess.run(
                [
                    sys.executable,
                    str(args.fixture_generator),
                    "--mapper",
                    "mbc1",
                    "--output",
                    str(rom_dir / f"{name}.gb"),
                ],
                check=True,
                timeout=30,
            )

        expected = (1, "0x6000")
        results: dict[int, list[set[tuple[int, str]]]] = {}
        for jobs in (1, 2):
            output_dir = root / f"generated-j{jobs}"
            completed = subprocess.run(
                [
                    str(args.gbrecomp),
                    str(rom_dir),
                    "--jobs",
                    str(jobs),
                    "--output",
                    str(output_dir),
                ],
                capture_output=True,
                text=True,
                timeout=60,
            )
            if completed.returncode != 0:
                print(completed.stdout)
                print(completed.stderr, file=sys.stderr)
                return completed.returncode

            cmake = (output_dir / "CMakeLists.txt").read_text()
            if (
                "GBRECOMP_GENERATED_COMPILE_JOBS" not in cmake
                or "JOB_POOL_COMPILE" not in cmake
            ):
                print(
                    f"multi-ROM output omitted the generated compile pool with --jobs {jobs}",
                    file=sys.stderr,
                )
                return 1

            runtime_sources = {
                "gbrt.c",
                "gbrt_data_mod.c",
                "gbrt_hash.c",
                "gbrt_host_configuration.c",
                "gbrt_port.c",
                "gbrt_presentation.c",
                "gbrt_semantic.c",
                "differential.c",
                "interpreter.c",
                "ppu.c",
                "audio.c",
                "audio_stats.c",
                "platform_sdl.cpp",
            }
            missing_runtime_sources = sorted(
                source
                for source in runtime_sources
                if f"${{GBRT_DIR}}/src/{source}" not in cmake
            )
            if missing_runtime_sources:
                print(
                    "multi-ROM output omitted runtime sources: "
                    + ", ".join(missing_runtime_sources),
                    file=sys.stderr,
                )
                return 1

            function_sets: list[set[tuple[int, str]]] = []
            for name in ("alpha", "beta"):
                metadata = json.loads(
                    (output_dir / f"{name}_metadata.json").read_text()
                )
                functions = {
                    (function["bank"], function["address"])
                    for function in metadata["functions"]
                }
                function_sets.append(functions)
                if expected not in functions:
                    print(
                        f"{name} lost unreferenced function "
                        f"{expected[0]}:{expected[1]} with --jobs {jobs}",
                        file=sys.stderr,
                    )
                    return 1

            if function_sets[0] != function_sets[1]:
                print(
                    f"identical ROMs differed with --jobs {jobs}",
                    file=sys.stderr,
                )
                return 1
            results[jobs] = function_sets

        if results[1] != results[2]:
            print("serial and parallel analysis produced different functions", file=sys.stderr)
            return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
