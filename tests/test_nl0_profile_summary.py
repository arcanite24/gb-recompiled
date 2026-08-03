#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "tools" / "summarize_nl0_profile.py"
SPEC = importlib.util.spec_from_file_location("summarize_nl0_profile", MODULE_PATH)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def main() -> int:
    counter_line = (
        "[PERF-COUNTERS] available=1 tick_commits=100 tick_cycles=400 "
        "generated_safepoints=80 region_candidate_units=60 "
        "region_estimated_removable_tick_commits=25 "
        "region_estimated_removable_safepoints=20 timer_tick_calls=100 "
        "timer_tick_cycles=400 ppu_tick_calls=95 audio_step_calls=90 interrupt_checks=100 "
        "interrupt_stops=5\n"
    )
    histograms = (
        "[PERF-HISTOGRAM] name=ppu_mode "
        "buckets=lcd_off,oam,draw,hblank,vblank counts=0,10,60,20,10\n"
        "[PERF-HISTOGRAM] name=ppu_mode_cycles "
        "buckets=lcd_off,oam,draw,hblank,vblank counts=0,40,200,120,40\n"
        "[PERF-HISTOGRAM] name=timer_state_cycles "
        "buckets=disabled,enabled,reload counts=100,280,20\n"
    )

    with tempfile.TemporaryDirectory(prefix="gbrecomp_nl0_summary_") as tmp:
        path = Path(tmp) / "profile.log"
        path.write_text(counter_line + histograms)
        summary = MODULE.summarize_profile(MODULE.parse_profile_log(path))

    assert summary["estimated_tick_removal_share"] == 0.25
    assert summary["estimated_removable_ppu_tick_calls_lower_bound"] == 20
    assert summary["estimated_ppu_tick_removal_share_lower_bound"] == 20 / 95
    assert summary["estimated_safepoint_removal_share"] == 0.25
    assert summary["candidate_unit_share"] == 0.75
    assert summary["ppu_draw_tick_share"] == 0.60
    assert summary["ppu_non_draw_tick_share"] == 0.40
    assert summary["ppu_draw_cycle_share"] == 0.50
    assert summary["ppu_non_draw_cycle_share"] == 0.50
    assert summary["timer_processed_cycle_share"] == 1.0
    assert summary["timer_enabled_cycle_share"] == 0.75
    assert summary["visibility_region_device_commit_gate_20_percent"] is True
    assert summary["visibility_region_safepoint_support_20_percent"] is True
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
