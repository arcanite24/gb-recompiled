#!/usr/bin/env python3
"""Build and measure one reproducible NL-0 full-headless workload."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import statistics
import subprocess
import sys
import time
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any

try:
    import psutil
except ImportError:  # The profiler has a standard-library/ps fallback.
    psutil = None

from summarize_nl0_profile import parse_profile_log, summarize_profile


@dataclass
class Measurement:
    wall_seconds: float
    peak_rss_bytes: int
    exit_code: int
    log: str


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sha256_text(text: str) -> str:
    return hashlib.sha256(text.encode()).hexdigest()


def sha256_paths(paths: list[Path]) -> str:
    digest = hashlib.sha256()
    for path in sorted(paths):
        digest.update(path.name.encode())
        digest.update(b"\0")
        with path.open("rb") as handle:
            for chunk in iter(lambda: handle.read(1024 * 1024), b""):
                digest.update(chunk)
    return digest.hexdigest()


def command_version(*argv: str) -> str:
    try:
        output = subprocess.check_output(argv, text=True, stderr=subprocess.STDOUT)
    except (OSError, subprocess.CalledProcessError):
        return "unavailable"
    return output.splitlines()[0].strip() if output else "unknown"


def total_rss_bytes(root_pid: int) -> int:
    if psutil is not None:
        try:
            process = psutil.Process(root_pid)
            processes = [process, *process.children(recursive=True)]
        except (psutil.Error, ProcessLookupError):
            return 0
        total = 0
        for item in processes:
            try:
                total += item.memory_info().rss
            except (psutil.Error, ProcessLookupError):
                pass
        return total

    # `psutil` is convenient but deliberately optional. Both macOS and Linux
    # expose PID, parent PID, and RSS (KiB) through this POSIX-style `ps` form.
    try:
        output = subprocess.check_output(
            ["ps", "-axo", "pid=,ppid=,rss="],
            text=True,
            stderr=subprocess.DEVNULL,
        )
    except (OSError, subprocess.CalledProcessError):
        return 0
    rows: dict[int, tuple[int, int]] = {}
    for line in output.splitlines():
        fields = line.split()
        if len(fields) != 3:
            continue
        pid, parent_pid, rss_kib = map(int, fields)
        rows[pid] = (parent_pid, rss_kib)
    descendants = {root_pid}
    changed = True
    while changed:
        changed = False
        for pid, (parent_pid, _) in rows.items():
            if parent_pid in descendants and pid not in descendants:
                descendants.add(pid)
                changed = True
    return sum(rows[pid][1] * 1024 for pid in descendants if pid in rows)


def run_measured(
    argv: list[str],
    log_path: Path,
    *,
    cwd: Path | None = None,
    env: dict[str, str] | None = None,
    sample_interval: float = 0.005,
) -> Measurement:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    merged_env = os.environ.copy()
    if env:
        merged_env.update(env)
    with log_path.open("wb") as log:
        start = time.perf_counter()
        child = subprocess.Popen(argv, cwd=cwd, env=merged_env, stdout=log, stderr=subprocess.STDOUT)
        peak_rss = 0
        poll_interval = sample_interval if psutil is not None else max(sample_interval, 0.05)
        while child.poll() is None:
            peak_rss = max(peak_rss, total_rss_bytes(child.pid))
            time.sleep(poll_interval)
        peak_rss = max(peak_rss, total_rss_bytes(child.pid))
        exit_code = child.wait()
        elapsed = time.perf_counter() - start
    measurement = Measurement(elapsed, peak_rss, exit_code, str(log_path))
    if exit_code != 0:
        tail = "\n".join(log_path.read_text(errors="replace").splitlines()[-30:])
        raise RuntimeError(
            f"command failed with exit code {exit_code}: {' '.join(argv)}\n{tail}"
        )
    return measurement


def git_provenance(root: Path) -> dict[str, Any]:
    def output(*argv: str) -> str:
        return subprocess.check_output(argv, cwd=root, text=True).strip()

    commit = output("git", "rev-parse", "HEAD")
    status = output("git", "status", "--short")
    return {
        "commit": commit,
        "dirty": bool(status),
        "changed_path_count": len(status.splitlines()) if status else 0,
    }


def validate_cycle_input(text: str) -> None:
    tokens = [token.strip() for token in text.split(",") if token.strip()]
    if not tokens:
        raise ValueError("cycle-anchored input file is empty")
    pattern = re.compile(r"c[0-9]+:[A-Za-z+]*:[1-9][0-9]*")
    invalid = [token for token in tokens if not pattern.fullmatch(token)]
    if invalid:
        raise ValueError(
            "NL-0 inputs must use only c<cycle>:<buttons>:<duration> entries; "
            f"invalid: {invalid[0]}"
        )


def generated_prefix(project_dir: Path) -> str:
    metadata = sorted(project_dir.glob("*_metadata.json"))
    if len(metadata) != 1:
        raise RuntimeError(
            f"expected one generated metadata sidecar in {project_dir}, found {len(metadata)}"
        )
    suffix = "_metadata.json"
    return metadata[0].name[: -len(suffix)]


def configure_build(
    project_dir: Path,
    build_dir: Path,
    counters: bool,
    ipo: bool,
    log_dir: Path,
) -> dict[str, Any]:
    build_dir.mkdir(parents=True, exist_ok=True)
    common_flags = "-O3 -g -DNDEBUG -fno-omit-frame-pointer"
    configure = run_measured(
        [
            "cmake",
            "-G",
            "Ninja",
            "-S",
            str(project_dir),
            "-B",
            str(build_dir),
            "-DCMAKE_BUILD_TYPE=Release",
            "-DGBRECOMP_GENERATED_OPT_LEVEL=3",
            f"-DGBRECOMP_ENABLE_IPO={'ON' if ipo else 'OFF'}",
            "-DGBRECOMP_ENABLE_STRIP=OFF",
            f"-DGBRECOMP_ENABLE_PERFORMANCE_COUNTERS={'ON' if counters else 'OFF'}",
            f"-DCMAKE_C_FLAGS_RELEASE={common_flags}",
            f"-DCMAKE_CXX_FLAGS_RELEASE={common_flags}",
        ],
        log_dir / "configure.log",
    )
    cold = run_measured(
        ["ninja", "-C", str(build_dir)],
        log_dir / "build-cold.log",
    )
    warm = run_measured(
        ["ninja", "-C", str(build_dir)],
        log_dir / "build-warm.log",
    )
    return {
        "configure": asdict(configure),
        "cold": asdict(cold),
        "warm": asdict(warm),
    }


def isolated_environment(home: Path) -> dict[str, str]:
    home.mkdir(parents=True, exist_ok=True)
    return {
        "HOME": str(home),
        "XDG_DATA_HOME": str(home / "data"),
        "XDG_CONFIG_HOME": str(home / "config"),
        "SDL_VIDEODRIVER": "dummy",
        "SDL_AUDIODRIVER": "dummy",
    }


def run_runtime_trial(
    binary: Path,
    input_script: str,
    frames: int,
    log_path: Path,
    state_path: Path,
    home: Path,
    report_counters: bool,
    estimate_regions: bool = False,
    extra_args: list[str] | None = None,
) -> Measurement:
    argv = [
        str(binary),
        "--headless",
        "--limit-frames",
        str(frames),
        "--input",
        input_script,
        "--dump-state",
        str(state_path),
    ]
    if report_counters:
        argv.append("--report-performance-counters")
    if estimate_regions:
        argv.append("--estimate-visibility-regions")
    if extra_args:
        argv.extend(extra_args)
    return run_measured(argv, log_path, env=isolated_environment(home))


def summarize_trials(measurements: list[Measurement]) -> dict[str, Any]:
    walls = [measurement.wall_seconds for measurement in measurements]
    return {
        "runs": len(measurements),
        "median_seconds": statistics.median(walls),
        "mean_seconds": statistics.mean(walls),
        "min_seconds": min(walls),
        "max_seconds": max(walls),
        "max_peak_rss_bytes": max(measurement.peak_rss_bytes for measurement in measurements),
        "trials": [asdict(measurement) for measurement in measurements],
    }


def sample_symbol_coverage(
    binary: Path,
    input_script: str,
    log_dir: Path,
    seconds: int,
) -> dict[str, Any] | None:
    if seconds <= 0 or platform.system() != "Darwin" or not shutil.which("sample"):
        return None
    run_log = log_dir / "sample-run.log"
    sample_path = log_dir / "sample.txt"
    env = os.environ.copy()
    env.update(isolated_environment(log_dir / "sample-home"))
    with run_log.open("wb") as log:
        child = subprocess.Popen(
            [
                str(binary),
                "--headless",
                "--limit-frames",
                "100000000",
                "--input",
                input_script,
            ],
            env=env,
            stdout=log,
            stderr=subprocess.STDOUT,
        )
        try:
            time.sleep(0.25)
            subprocess.run(
                ["sample", str(child.pid), str(seconds), "-file", str(sample_path)],
                check=True,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
        finally:
            child.terminate()
            try:
                child.wait(timeout=3)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait()

    named = 0
    unknown = 0
    in_table = False
    binary_marker = f"(in {binary.name})"
    for line in sample_path.read_text(errors="replace").splitlines():
        if line.startswith("Sort by top of stack"):
            in_table = True
            continue
        if in_table and line.startswith("Binary Images:"):
            break
        if not in_table or binary_marker not in line:
            continue
        match = re.search(r"\s([0-9]+)$", line)
        if not match:
            continue
        count = int(match.group(1))
        if line.lstrip().startswith("???"):
            unknown += count
        else:
            named += count
    total = named + unknown
    return {
        "sample": str(sample_path),
        "seconds": seconds,
        "named_application_leaf_samples": named,
        "unknown_application_leaf_samples": unknown,
        "symbolized_share": named / total if total else 0.0,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--gbrecomp", required=True, type=Path)
    parser.add_argument("--project-dir", required=True, type=Path)
    parser.add_argument("--build-root", required=True, type=Path)
    parser.add_argument("--input-file", required=True, type=Path)
    parser.add_argument("--frames", type=int, default=1800)
    parser.add_argument("--repeat", type=int, default=4)
    parser.add_argument("--warmup", type=int, default=1)
    parser.add_argument("--sample-seconds", type=int, default=5)
    parser.add_argument(
        "--ipo",
        action="store_true",
        help="Enable IPO/LTO. The symbolized NL-0 diagnostic profile leaves it off by default.",
    )
    parser.add_argument("--json-out", required=True, type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.frames <= 0 or args.repeat <= 0 or args.warmup < 0:
        raise SystemExit("frames/repeat must be positive and warmup must be non-negative")
    root = Path(__file__).resolve().parents[1]
    rom = args.rom.resolve()
    gbrecomp = args.gbrecomp.resolve()
    project_dir = args.project_dir.resolve()
    build_root = args.build_root.resolve()
    artifact_dir = args.json_out.resolve().parent
    input_script = args.input_file.read_text().strip()
    try:
        validate_cycle_input(input_script)
    except ValueError as exc:
        raise SystemExit(str(exc)) from exc
    if project_dir.exists() and any(project_dir.iterdir()):
        raise SystemExit(f"refusing to profile into non-empty generated project: {project_dir}")
    if build_root.exists() and any(build_root.iterdir()):
        raise SystemExit(f"refusing to profile into non-empty build root: {build_root}")
    if args.json_out.exists():
        raise SystemExit(f"refusing to overwrite existing profile artifact: {args.json_out}")
    project_dir.parent.mkdir(parents=True, exist_ok=True)
    build_root.mkdir(parents=True, exist_ok=True)
    artifact_dir.mkdir(parents=True, exist_ok=True)

    generation = run_measured(
        [str(gbrecomp), str(rom), "-o", str(project_dir)],
        artifact_dir / "generation.log",
        cwd=root,
    )
    prefix = generated_prefix(project_dir)
    variants = {
        "control": False,
        "instrumented": True,
    }
    build_results: dict[str, Any] = {}
    binaries: dict[str, Path] = {}
    for variant, counters in variants.items():
        build_dir = build_root / variant
        build_results[variant] = configure_build(
            project_dir,
            build_dir,
            counters,
            args.ipo,
            artifact_dir / variant,
        )
        binary = build_dir / prefix
        if not binary.exists():
            raise RuntimeError(f"generated binary not found: {binary}")
        binaries[variant] = binary

    runtime_measurements: dict[str, list[Measurement]] = {name: [] for name in variants}
    state_hashes: dict[str, list[str]] = {name: [] for name in variants}
    for phase, count in (("warmup", args.warmup), ("trial", args.repeat)):
        for index in range(count):
            for variant in variants:
                run_dir = artifact_dir / "runs" / f"{phase}-{index + 1}" / variant
                state_path = run_dir / "state.json"
                measurement = run_runtime_trial(
                    binaries[variant],
                    input_script,
                    args.frames,
                    run_dir / "runtime.log",
                    state_path,
                    run_dir / "home",
                    variants[variant],
                )
                if phase == "trial":
                    runtime_measurements[variant].append(measurement)
                    state_hashes[variant].append(sha256_file(state_path))

    if len(set(state_hashes["control"] + state_hashes["instrumented"])) != 1:
        raise RuntimeError("control/instrumented state hashes are not identical across trials")

    estimator_dir = artifact_dir / "estimator"
    estimator_state = estimator_dir / "state.json"
    estimator_measurement = run_runtime_trial(
        binaries["instrumented"],
        input_script,
        args.frames,
        estimator_dir / "runtime.log",
        estimator_state,
        estimator_dir / "home",
        True,
        True,
    )
    if sha256_file(estimator_state) != state_hashes["control"][0]:
        raise RuntimeError("visibility estimator changed final emulated state")
    counter_summary = summarize_profile(
        parse_profile_log(Path(estimator_measurement.log))
    )
    sample_summary = sample_symbol_coverage(
        binaries["instrumented"],
        input_script,
        artifact_dir,
        args.sample_seconds,
    )
    source_files = list(project_dir.glob("*.c"))
    runtime_summary = {
        variant: summarize_trials(measurements)
        for variant, measurements in runtime_measurements.items()
    }
    control_median = runtime_summary["control"]["median_seconds"]
    instrumented_median = runtime_summary["instrumented"]["median_seconds"]

    payload = {
        "schema_version": 1,
        "name": args.name,
        "profile": {
            "kind": "full-headless",
            "frames": args.frames,
            "repeat": args.repeat,
            "warmup": args.warmup,
            "benchmark_reduced_workload": False,
            "ppu_timing": True,
            "pixel_rasterization": True,
            "audio_emulation": True,
            "host_presentation": False,
            "host_pacing": False,
            "ipo": args.ipo,
        },
        "provenance": {
            "git": git_provenance(root),
            "host": platform.platform(),
            "machine": platform.machine(),
            "python": sys.version.split()[0],
            "toolchain": {
                "cmake": command_version("cmake", "--version"),
                "ninja": command_version("ninja", "--version"),
                "cc": command_version("cc", "--version"),
            },
            "gbrecomp_sha256": sha256_file(gbrecomp),
            "rom": str(rom),
            "rom_sha256": sha256_file(rom),
            "input_file": str(args.input_file.resolve()),
            "input_sha256": sha256_text(input_script),
            "input_anchor": "cycle",
        },
        "generation": asdict(generation),
        "generated": {
            "project": str(project_dir),
            "metadata": str(next(project_dir.glob("*_metadata.json"))),
            "source_file_count": len(source_files),
            "source_bytes": sum(path.stat().st_size for path in source_files),
            "source_sha256": sha256_paths(source_files),
        },
        "builds": {
            variant: {
                **build_results[variant],
                "binary": str(binaries[variant]),
                "binary_sha256": sha256_file(binaries[variant]),
                "binary_bytes": binaries[variant].stat().st_size,
            }
            for variant in variants
        },
        "runtime": runtime_summary,
        "estimator_run": asdict(estimator_measurement),
        "profiling_overhead": {
            "median_seconds_delta": instrumented_median - control_median,
            "median_fraction":
                (instrumented_median / control_median - 1.0) if control_median else 0.0,
            "state_sha256": state_hashes["control"][0],
        },
        "counters": counter_summary,
        "symbol_sample": sample_summary,
    }
    args.json_out.parent.mkdir(parents=True, exist_ok=True)
    args.json_out.write_text(json.dumps(payload, indent=2) + "\n")
    print(
        f"{args.name}: control median {control_median:.4f}s, "
        f"instrumented {instrumented_median:.4f}s "
        f"({payload['profiling_overhead']['median_fraction'] * 100.0:+.2f}%)"
    )
    print(
        f"Estimated removable ticks: "
        f"{counter_summary['estimated_tick_removal_share'] * 100.0:.1f}%"
    )
    if sample_summary:
        print(f"Symbolized application leaf samples: {sample_summary['symbolized_share'] * 100.0:.1f}%")
    print(f"Wrote {args.json_out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
