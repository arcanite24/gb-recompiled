#!/usr/bin/env python3
"""Summarize NL-0 runtime counters and conservative region estimates."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


COUNTER_PREFIX = "[PERF-COUNTERS] "
HISTOGRAM_PREFIX = "[PERF-HISTOGRAM] "


def _parse_fields(text: str) -> dict[str, str]:
    fields: dict[str, str] = {}
    for token in text.strip().split():
        if "=" not in token:
            continue
        key, value = token.split("=", 1)
        fields[key] = value
    return fields


def parse_profile_log(path: Path) -> dict[str, Any]:
    counters: dict[str, int] | None = None
    histograms: dict[str, dict[str, int]] = {}

    for line in path.read_text(errors="replace").splitlines():
        if line.startswith(COUNTER_PREFIX):
            raw = _parse_fields(line[len(COUNTER_PREFIX) :])
            try:
                counters = {key: int(value, 0) for key, value in raw.items()}
            except ValueError as exc:
                raise ValueError(f"malformed counter record in {path}: {exc}") from exc
        elif line.startswith(HISTOGRAM_PREFIX):
            raw = _parse_fields(line[len(HISTOGRAM_PREFIX) :])
            name = raw.get("name")
            bucket_text = raw.get("buckets")
            count_text = raw.get("counts")
            if not name or bucket_text is None or count_text is None:
                raise ValueError(f"malformed histogram record in {path}: {line}")
            buckets = bucket_text.split(",")
            try:
                counts = [int(value, 0) for value in count_text.split(",")]
            except ValueError as exc:
                raise ValueError(f"malformed histogram counts in {path}: {exc}") from exc
            if len(buckets) != len(counts):
                raise ValueError(
                    f"histogram {name} in {path} has {len(buckets)} labels "
                    f"but {len(counts)} counts"
                )
            histograms[name] = dict(zip(buckets, counts, strict=True))

    if counters is None:
        raise ValueError(f"no {COUNTER_PREFIX.strip()} record found in {path}")
    if counters.get("available") != 1:
        raise ValueError(f"performance counters were not compiled into the run in {path}")
    return {"source": str(path), "counters": counters, "histograms": histograms}


def _ratio(numerator: int, denominator: int) -> float:
    return numerator / denominator if denominator else 0.0


def summarize_profile(profile: dict[str, Any]) -> dict[str, Any]:
    counters: dict[str, int] = profile["counters"]
    histograms: dict[str, dict[str, int]] = profile["histograms"]
    tick_commits = counters.get("tick_commits", 0)
    tick_cycles = counters.get("tick_cycles", 0)
    safepoints = counters.get("generated_safepoints", 0)
    removable_ticks = counters.get("region_estimated_removable_tick_commits", 0)
    removable_safepoints = counters.get("region_estimated_removable_safepoints", 0)
    candidate_units = counters.get("region_candidate_units", 0)
    ppu_modes = histograms.get("ppu_mode", {})
    ppu_mode_cycles = histograms.get("ppu_mode_cycles", {})
    timer_states = histograms.get("timer_state_cycles", {})

    tick_removal_share = _ratio(removable_ticks, tick_commits)
    ppu_tick_calls = counters.get("ppu_tick_calls", 0)
    non_ppu_tick_commits = max(0, tick_commits - ppu_tick_calls)
    # Existing logs do not attach an LCD-on bit to every region. Treat every
    # non-PPU commit as removable first to produce a conservative lower bound.
    removable_ppu_calls = max(0, removable_ticks - non_ppu_tick_commits)
    safepoint_removal_share = _ratio(removable_safepoints, safepoints)
    summary = {
        "source": profile["source"],
        "tick_commits": tick_commits,
        "tick_cycles": tick_cycles,
        "generated_safepoints": safepoints,
        "candidate_unit_share": _ratio(candidate_units, safepoints),
        "estimated_removable_tick_commits": removable_ticks,
        "estimated_tick_removal_share": tick_removal_share,
        "estimated_removable_ppu_tick_calls_lower_bound": removable_ppu_calls,
        "estimated_ppu_tick_removal_share_lower_bound": _ratio(
            removable_ppu_calls,
            ppu_tick_calls,
        ),
        "estimated_removable_safepoints": removable_safepoints,
        "estimated_safepoint_removal_share": safepoint_removal_share,
        "visibility_region_device_commit_gate_20_percent":
            tick_removal_share >= 0.20 or
            _ratio(removable_ppu_calls, ppu_tick_calls) >= 0.20,
        "visibility_region_safepoint_support_20_percent":
            safepoint_removal_share >= 0.20,
        "ppu_draw_tick_share": _ratio(ppu_modes.get("draw", 0), tick_commits),
        "ppu_non_draw_tick_share": _ratio(
            sum(value for key, value in ppu_modes.items() if key != "draw"),
            tick_commits,
        ),
        "ppu_draw_cycle_share": _ratio(ppu_mode_cycles.get("draw", 0), tick_cycles),
        "ppu_non_draw_cycle_share": _ratio(
            sum(value for key, value in ppu_mode_cycles.items() if key != "draw"),
            tick_cycles,
        ),
        "timer_processed_cycle_share": _ratio(
            counters.get("timer_tick_cycles", 0),
            tick_cycles,
        ),
        "timer_enabled_cycle_share": _ratio(
            timer_states.get("enabled", 0) + timer_states.get("reload", 0),
            tick_cycles,
        ),
        "device_activity": {
            "rtc_call_share": _ratio(counters.get("rtc_tick_calls", 0), tick_commits),
            "dma_call_share": _ratio(counters.get("dma_tick_calls", 0), tick_commits),
            "serial_call_share": _ratio(counters.get("serial_tick_calls", 0), tick_commits),
            "audio_call_share": _ratio(counters.get("audio_step_calls", 0), tick_commits),
            "interrupt_stop_share": _ratio(
                counters.get("interrupt_stops", 0),
                counters.get("interrupt_checks", 0),
            ),
        },
        "raw_counters": counters,
        "histograms": histograms,
    }
    return summary


def print_summary(summary: dict[str, Any]) -> None:
    print(summary["source"])
    print(
        f"  ticks: {summary['tick_commits']:,} commits / "
        f"{summary['tick_cycles']:,} CPU cycles"
    )
    print(
        f"  candidate units: {summary['candidate_unit_share'] * 100.0:.1f}% of "
        f"{summary['generated_safepoints']:,} generated safepoints"
    )
    print(
        f"  conservative estimate: {summary['estimated_removable_tick_commits']:,} "
        f"ticks ({summary['estimated_tick_removal_share'] * 100.0:.1f}%), "
        f"at least {summary['estimated_removable_ppu_tick_calls_lower_bound']:,} PPU calls "
        f"({summary['estimated_ppu_tick_removal_share_lower_bound'] * 100.0:.1f}%), "
        f"{summary['estimated_removable_safepoints']:,} safepoints "
        f"({summary['estimated_safepoint_removal_share'] * 100.0:.1f}%) removable"
    )
    print(
        f"  PPU call context: draw {summary['ppu_draw_tick_share'] * 100.0:.1f}%, "
        f"non-draw {summary['ppu_non_draw_tick_share'] * 100.0:.1f}%; "
        f"cycle context: draw {summary['ppu_draw_cycle_share'] * 100.0:.1f}%, "
        f"non-draw {summary['ppu_non_draw_cycle_share'] * 100.0:.1f}%"
    )
    print(
        f"  timer processed cycles: {summary['timer_processed_cycle_share'] * 100.0:.1f}%; "
        f"TIMA enabled/reload: {summary['timer_enabled_cycle_share'] * 100.0:.1f}%"
    )
    gate = "PASS" if summary["visibility_region_device_commit_gate_20_percent"] else "MISS"
    print(f"  NL-3 20% device-commit gate: {gate}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Summarize one or more --report-performance-counters logs."
    )
    parser.add_argument("logs", nargs="+", type=Path)
    parser.add_argument("--json-out", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    summaries: list[dict[str, Any]] = []
    try:
        for path in args.logs:
            summary = summarize_profile(parse_profile_log(path))
            summaries.append(summary)
            print_summary(summary)
    except (OSError, ValueError) as exc:
        raise SystemExit(str(exc)) from exc

    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps({"profiles": summaries}, indent=2) + "\n")
        print(f"Wrote {args.json_out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
