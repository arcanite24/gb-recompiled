#!/usr/bin/env python3
"""Measure one generated project's cold/warm build and footprint reproducibly."""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
from dataclasses import asdict
from pathlib import Path

from run_nl0_profile import (
    command_version,
    generated_prefix,
    run_measured,
    sha256_file,
    sha256_paths,
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project-dir", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--json-out", type=Path, required=True)
    parser.add_argument("--label", required=True)
    parser.add_argument(
        "--cmake-arg",
        action="append",
        default=[],
        help="Additional CMake cache argument; repeat as needed",
    )
    return parser.parse_args()


def macho_size(path: Path) -> dict[str, int] | None:
    try:
        lines = subprocess.check_output(["size", str(path)], text=True).splitlines()
    except (OSError, subprocess.CalledProcessError):
        return None
    if len(lines) < 2:
        return None
    headings = lines[0].split()
    values = lines[1].split()
    if len(values) < len(headings):
        return None
    result: dict[str, int] = {}
    for heading, value in zip(headings, values):
        try:
            result[heading] = int(value, 0)
        except ValueError:
            continue
    return result or None


def main() -> int:
    args = parse_args()
    project_dir = args.project_dir.resolve()
    build_dir = args.build_dir.resolve()
    json_out = args.json_out.resolve()
    prefix = generated_prefix(project_dir)

    source_files = sorted(project_dir.glob("*.c"))
    if not source_files:
        raise RuntimeError(f"no generated C sources in {project_dir}")

    if build_dir.exists():
        shutil.rmtree(build_dir)
    log_dir = json_out.parent / f"{json_out.stem}-logs"
    if log_dir.exists():
        shutil.rmtree(log_dir)
    log_dir.mkdir(parents=True, exist_ok=True)

    common_flags = "-O3 -g -DNDEBUG -fno-omit-frame-pointer"
    configure_argv = [
        "cmake",
        "-G",
        "Ninja",
        "-S",
        str(project_dir),
        "-B",
        str(build_dir),
        "-DCMAKE_BUILD_TYPE=Release",
        "-DGBRECOMP_GENERATED_OPT_LEVEL=3",
        "-DGBRECOMP_ENABLE_IPO=OFF",
        "-DGBRECOMP_ENABLE_STRIP=OFF",
        "-DGBRECOMP_ENABLE_PERFORMANCE_COUNTERS=OFF",
        f"-DCMAKE_C_FLAGS_RELEASE={common_flags}",
        f"-DCMAKE_CXX_FLAGS_RELEASE={common_flags}",
        *args.cmake_arg,
    ]
    configure = run_measured(configure_argv, log_dir / "configure.log")
    cold = run_measured(
        ["ninja", "-C", str(build_dir)],
        log_dir / "build-cold.log",
    )
    warm = run_measured(
        ["ninja", "-C", str(build_dir)],
        log_dir / "build-warm.log",
    )

    binary = build_dir / prefix
    if not binary.is_file():
        raise RuntimeError(f"generated executable missing: {binary}")

    source_sizes = [path.stat().st_size for path in source_files]
    artifact = {
        "schema_version": 1,
        "label": args.label,
        "project_dir": str(project_dir),
        "build_dir": str(build_dir),
        "configuration": {
            "generator": "Ninja",
            "build_type": "Release",
            "generated_opt_level": 3,
            "ipo": False,
            "strip": False,
            "performance_counters": False,
            "extra_cmake_args": args.cmake_arg,
        },
        "generated_sources": {
            "files": len(source_files),
            "bytes": sum(source_sizes),
            "largest_file_bytes": max(source_sizes),
            "sha256": sha256_paths(source_files),
        },
        "measurements": {
            "configure": asdict(configure),
            "cold_build": asdict(cold),
            "warm_build": asdict(warm),
        },
        "executable": {
            "path": str(binary),
            "bytes": binary.stat().st_size,
            "sha256": sha256_file(binary),
            "size": macho_size(binary),
        },
        "tools": {
            "cmake": command_version("cmake", "--version"),
            "ninja": command_version("ninja", "--version"),
            "compiler": command_version("cc", "--version"),
        },
    }
    json_out.parent.mkdir(parents=True, exist_ok=True)
    json_out.write_text(json.dumps(artifact, indent=2) + "\n")
    print(
        f"{args.label}: {len(source_files)} C files, "
        f"{sum(source_sizes) / (1024 * 1024):.2f} MiB, "
        f"cold {cold.wall_seconds:.2f}s / {cold.peak_rss_bytes / (1024**3):.2f} GiB RSS"
    )
    print(f"Wrote {json_out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
