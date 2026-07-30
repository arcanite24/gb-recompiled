# NL-1 arithmetic DIV/TIMA result

Date: 2026-07-14

Status: retained

## Decision

Keep the arithmetic DIV/TIMA common path and proceed to NL-2 lazy PPU synchronization.

The optimized timer advances a disabled timer in O(1), counts selected-divider falling edges arithmetically while TIMA is enabled, and returns to the original one-T-cycle implementation for the short overflow/reload window. DIV and TAC writes still use the existing exact edge logic. A generated executable can select the untouched scalar implementation with `--scalar-timer`, which makes the A/B comparison use the same executable and generated code.

This is a material full-runtime win, not a timer microbenchmark:

| Workload | Scalar median | Arithmetic median | Delta |
| --- | ---: | ---: | ---: |
| Tetris, DMG | 3.0384 s | 2.5488 s | **-16.12%** |
| Link's Awakening, mapper-heavy DMG | 2.8966 s | 2.1859 s | **-24.54%** |
| Tetris DX, CGB | 3.9413 s | 3.5202 s | **-10.68%** |

All three workloads clear the 5% keep threshold. There is no regressing third workload.

## Implementation boundary

The retained change is deliberately narrow:

- the scalar implementation remains compiled as the diagnostic oracle
- timer-disabled spans update the 16-bit divider and published DIV register once
- timer-enabled spans derive the selected divider period and number of falling edges without visiting every T-cycle
- a span that reaches TIMA overflow stops at the exact edge, begins the existing four-T-cycle reload delay, and scalarizes the reload and reload-write window
- DIV, TIMA, TMA, and TAC MMIO handlers are unchanged
- CGB double-speed integration continues to supply CPU cycles to the timer while PPU and other system devices receive system cycles

The implementation adds no state to `GBContext`. The A/B selector is a process-wide diagnostic flag set once by generated `main`, so normal hot context layout and savestate format do not change.

## Measurement method

Each workload used a freshly generated counters-off Release project with generated `-O3`, frame pointers, IPO off, and the existing cycle-anchored input. The comparison ran one warmup and eight interleaved measured trials over 9,000 guest frames. Scalar and arithmetic trials used the same executable SHA-256; only `--scalar-timer` differed. Host presentation and pacing were disabled, while PPU timing, pixel rasterization, audio emulation, and final-state dumping remained enabled.

Raw artifacts:

- `logs/nl1_timer_tetris_9000_20260714/artifact.json`
- `logs/nl1_timer_zelda_9000_20260714/artifact.json`
- `logs/nl1_timer_tetrisdx_9000_20260714/artifact.json`
- `logs/nl1_timer_equivalence_20260714/`

`logs/` and generated projects under `output/` are intentionally local evidence, so the decision-relevant values are preserved in this report.

## Correctness evidence

- The repository now compares arithmetic and scalar timer states across deterministic randomized long spans and interleaved DIV/TIMA/TMA/TAC writes. It covers DMG and CGB double-speed execution, disabled and enabled TAC selections, divider wrap, repeated TIMA overflow, the pending reload interval, and the reload M-cycle.
- The existing exact overflow/reload, DIV-write glitch, TAC-write glitch, and rapid-toggle regression test passes unchanged.
- The focused external timer catalogue passes 13/13 after a forced rebuild.
- Repository CTest passes 34/34 in both counters-off and counters-on builds.
- Strict differential mode matches 500,000/500,000 steps for Tetris and Link's Awakening with fallback rejection enabled.
- The CGB directional differential run matches 500,000/500,000 steps for Tetris DX. As documented by NL-0, this game is not used as general fallback-free evidence.
- Every scalar and arithmetic 9,000-frame trial produced the same final-state hash within its workload.
- A separate 180-frame Tetris capture produced byte-identical state, 132,300-frame PCM output, and PPM captures at guest frames 60, 120, and 180. The PCM SHA-256 is `a7dd386052b0a3b0245d01a8654e27597601a7fd82556f31b1a5d8f455feff2b` for both paths.

Differential mode remains a shared-runtime consistency check. The independent scalar oracle, repository edge regressions, and external timer catalogue are the primary timer-specific evidence.

## Footprint gate

Because the timed A/B uses one binary, executable size is identical between scalar and arithmetic trials. Relative to the matching pre-NL-1 NL-0 binaries, the freshly generated executable deltas were +0.0061% for Tetris, +0.0008% for Link's Awakening, and -0.0888% for Tetris DX. Peak runtime RSS was equal or slightly lower for arithmetic in the interleaved measurements. These are comfortably inside the 2% footprint gate.

## Reproduction

After generating and building an optimized project, compare the two timer paths without rebuilding:

```bash
python3 tools/compare_nl0_controls.py \
  --before output/game/build/game \
  --after output/game/build/game \
  --before-arg=--scalar-timer \
  --input-file tools/profiles/game.input \
  --frames 9000 \
  --repeat 8 \
  --warmup 1 \
  --json-out logs/nl1-game/artifact.json
```

Run the focused semantic gates with:

```bash
ctest --test-dir build --output-on-failure
python3 tools/run_tests.py --filter timer --rebuild
```

## Next step

Begin NL-2 outside mode 3. Preserve the eager PPU path as the same-binary oracle, defer only through exact OAM/HBlank/VBlank visibility deadlines at first, and require identical state/frame/PCM evidence before applying the three-game runtime gate. Keep APU batching separate so its PCM and performance effects remain attributable.
