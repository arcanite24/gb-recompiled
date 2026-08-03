#!/usr/bin/env python3
"""Interleave two profiling-off full-headless runtime configurations."""

from __future__ import annotations

import argparse
import json
import statistics
from pathlib import Path

from run_nl0_profile import (
    run_runtime_trial,
    sha256_file,
    sha256_text,
    validate_cycle_input,
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", required=True, type=Path)
    parser.add_argument("--after", required=True, type=Path)
    parser.add_argument(
        "--before-arg",
        action="append",
        default=[],
        help="Argument passed only to the before configuration (repeatable)",
    )
    parser.add_argument(
        "--after-arg",
        action="append",
        default=[],
        help="Argument passed only to the after configuration (repeatable)",
    )
    parser.add_argument("--input-file", required=True, type=Path)
    parser.add_argument("--frames", type=int, default=1800)
    parser.add_argument("--repeat", type=int, default=6)
    parser.add_argument("--warmup", type=int, default=1)
    parser.add_argument("--json-out", required=True, type=Path)
    return parser.parse_args()


def summarize(values: list[dict[str, object]]) -> dict[str, object]:
    walls = [float(item["wall_seconds"]) for item in values]
    return {
        "runs": len(values),
        "median_seconds": statistics.median(walls),
        "mean_seconds": statistics.mean(walls),
        "min_seconds": min(walls),
        "max_seconds": max(walls),
        "max_peak_rss_bytes": max(int(item["peak_rss_bytes"]) for item in values),
        "trials": values,
    }


def main() -> int:
    args = parse_args()
    if args.frames <= 0 or args.repeat <= 0 or args.warmup < 0:
        raise SystemExit("frames/repeat must be positive and warmup must be non-negative")
    binaries = {"before": args.before.resolve(), "after": args.after.resolve()}
    runtime_args = {
        "before": list(args.before_arg),
        "after": list(args.after_arg),
    }
    for label, binary in binaries.items():
        if not binary.is_file():
            raise SystemExit(f"{label} binary does not exist: {binary}")
    input_script = args.input_file.read_text().strip()
    try:
        validate_cycle_input(input_script)
    except ValueError as exc:
        raise SystemExit(str(exc)) from exc

    artifact_dir = args.json_out.resolve().parent
    measurements: dict[str, list[dict[str, object]]] = {label: [] for label in binaries}
    state_hashes: dict[str, list[str]] = {label: [] for label in binaries}
    for phase, count in (("warmup", args.warmup), ("trial", args.repeat)):
        for index in range(count):
            # Alternate order so thermal drift does not consistently favor one binary.
            labels = list(binaries)
            if index % 2:
                labels.reverse()
            for label in labels:
                run_dir = artifact_dir / "runs" / f"{phase}-{index + 1}" / label
                state_path = run_dir / "state.json"
                measurement = run_runtime_trial(
                    binaries[label],
                    input_script,
                    args.frames,
                    run_dir / "runtime.log",
                    state_path,
                    run_dir / "home",
                    False,
                    extra_args=runtime_args[label],
                )
                if phase == "trial":
                    measurements[label].append(
                        {
                            "wall_seconds": measurement.wall_seconds,
                            "peak_rss_bytes": measurement.peak_rss_bytes,
                            "exit_code": measurement.exit_code,
                            "log": measurement.log,
                        }
                    )
                    state_hashes[label].append(sha256_file(state_path))

    all_state_hashes = state_hashes["before"] + state_hashes["after"]
    if len(set(all_state_hashes)) != 1:
        raise RuntimeError("before/after state hashes differ across profiling-off trials")
    summaries = {label: summarize(values) for label, values in measurements.items()}
    before_median = float(summaries["before"]["median_seconds"])
    after_median = float(summaries["after"]["median_seconds"])
    payload = {
        "schema_version": 1,
        "profile": {
            "kind": "full-headless",
            "frames": args.frames,
            "repeat": args.repeat,
            "warmup": args.warmup,
            "performance_counters": False,
            "visibility_estimator": False,
            "host_presentation": False,
            "host_pacing": False,
        },
        "input": {
            "file": str(args.input_file.resolve()),
            "sha256": sha256_text(input_script),
            "anchor": "cycle",
        },
        "binaries": {
            label: {
                "path": str(binary),
                "sha256": sha256_file(binary),
                "bytes": binary.stat().st_size,
                "runtime_args": runtime_args[label],
            }
            for label, binary in binaries.items()
        },
        "runtime": summaries,
        "comparison": {
            "after_minus_before_seconds": after_median - before_median,
            "after_vs_before_fraction": after_median / before_median - 1.0,
            "state_sha256": all_state_hashes[0],
        },
    }
    args.json_out.parent.mkdir(parents=True, exist_ok=True)
    args.json_out.write_text(json.dumps(payload, indent=2) + "\n")
    print(
        f"profiling-off A/B: before {before_median:.4f}s, after {after_median:.4f}s "
        f"({payload['comparison']['after_vs_before_fraction'] * 100.0:+.2f}%)"
    )
    print(f"Wrote {args.json_out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
