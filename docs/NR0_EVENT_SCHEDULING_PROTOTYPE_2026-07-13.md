# NR-0/NR-1 event-scheduling prototype result

Date: 2026-07-13

Status: transform rejected at the first-game keep gate; instrumentation and the conservative event-deadline primitive retained

## Decision

Do not expand the first register-localization and cycle-batching transform to mapper-heavy or CGB games.

The prototype preserved Tetris state and passed strict differential execution, but it did not produce a meaningful full-runtime gain. The combined transform reduced runtime tick commits by only 0.23%, ran about 1% slower in longer full-headless trials, grew generated C by 17.7%, and grew the executable by 5.9% over the pre-transform baseline. This misses the NR-1 gate of at least 10% faster full-headless execution on representative games and exceeds the 5% executable-growth ceiling.

The generated localization and batching paths were therefore removed. The following pieces remain because they have no emulation-semantic cost when disabled or unused and give later work a sound measurement/scheduling foundation:

- compile-time-gated counters for tick commits/cycles, generated safepoints, direct body transitions, indirect dispatch, and interpreter fallback
- generated-runtime counter reporting through `--report-performance-counters`
- a conservative next-event query covering interrupt state, the active run budget, TIMA clock/reload edges, PPU boundaries, OAM DMA, serial completion, and outstanding HDMA stalls
- focused tests for both instrumentation modes and deadline boundary selection

## What was evaluated

The prototype was deliberately limited to the existing emitted-C backend:

1. Count generated/runtime transitions without changing the stop predicate.
2. Keep B, C, D, E, H, and L in local variables across small straight-line regions while retaining one timing commit per instruction.
3. Emit a guarded fast path for up to eight adjacent register-only instructions and one instruction-granular resume path.
4. Accept the batch only when all cycles fit before the earliest modeled runtime event.
5. Stop before broader game validation if the small-DMG keep gate fails.

Memory operations, control flow, DI/EI, HALT/STOP, 16-bit INC/DEC, CGB double speed, single-step execution, tracing, and instruction-limited diagnostics never entered the batch path. Pixel transfer used a one-dot deadline because its completion is FIFO-dependent. Timer reload state used a one-cycle deadline.

Pan Docs supplied the timing contract for PPU modes, TIMA falling-edge behavior, delayed EI, and interrupt acceptance. No ambiguity required a SameBoy implementation comparison for this slice.

## Measured result

All measurements used the local Tetris ROM, AppleClang 17, a Release build with thin LTO, 1,800 frames unless noted, one warmup, and five reduced-workload trials. The generated artifacts and logs remain untracked.

| Signal | Pre-transform baseline | Register localization | Localization plus batching | Result |
| --- | ---: | ---: | ---: | --- |
| Final-state SHA-256 | `62bafcba0198d13c98a6c7e73eb2e3bb06d32f4b94c66441e01fc80b7f4076d0` | identical | identical | pass |
| Strict differential | — | 500,000 steps | 500,000 steps | pass |
| Interpreter fallback | 0 | 0 | 0 | pass |
| Tick commits | 23,152,898 | 23,152,898 | 23,100,083 | 52,815 removed (0.23%) |
| Localized instructions | — | 21,246 | 20,170 | too little dynamic coverage |
| Accepted batches | — | — | 31,822 regions / 84,637 instructions | too little dynamic coverage |
| Rejected batch candidates | — | — | 7,149 | conservative deadline worked as intended |
| Reduced-workload mean | 0.4683 s | 0.4715 s | 0.4737 s | 1.15% slower than baseline |
| 9,000-frame full-headless median | 3.55 s | 3.57 s | 3.59 s | 1.13% slower than baseline |
| Peak RSS in reduced profile | 8,880,128 B | 8,863,744 B | 8,781,824 B | no memory regression |
| Generated C | 10,533,990 B | 11,640,407 B | 12,394,788 B | 17.7% growth |
| Executable | 3,053,912 B | 3,103,480 B | 3,235,400 B | 5.9% growth |

The batched run accumulated 455,796 guest cycles, but the baseline performed 127,256,636 guest cycles. Most execution therefore remained outside the proposed regions. The deadline guard was not the primary rejection source: 31,822 of 38,971 dynamic candidates were accepted. The limiting factor was the code shape and workload coverage, not an excessively strict timer/PPU boundary.

After removing the transform, the final retained instrumentation-off build measured 0.4660 seconds in the reduced profile and a 3.55-second median over three 9,000-frame full-headless trials. Relative to the pre-transform baseline, that is 0.49% faster in the short reduced profile and unchanged in the longer full-headless sample—both within run variance. Peak RSS fell slightly, the executable was 240 bytes smaller, and generated C grew 3.95% from the no-op counter hooks and reporting path. The retained foundation therefore has no measured runtime or executable-size regression outside the gate.

## Why this shape missed

The fast path duplicated every eligible region so mid-region dispatch, single-step, and diagnostic execution could retain the exact slow path. That increased instruction-cache and build footprint before runtime savings were known.

More importantly, the Tetris workload did not spend enough time in adjacent register-only instructions. It frequently crossed memory, branch, call, dispatch, and device boundaries. Even perfect execution of the observed batches could remove only a small fraction of the 23.15 million timing commits.

Localizing six registers without localizing A, flags, SP, PC, or accumulated cycles also left helper traffic and per-instruction timing intact. That slice proved resume correctness, but it could not by itself amortize the generated code growth.

## Retained deadline contract

The retained query returns CPU T-cycles until the earliest batching-relevant event. It returns zero when execution must remain instruction-granular, including pending interrupt acceptance, delayed IME, tracing, single-step, active stop/halt state, CGB double speed, coarse benchmark scheduling, unsynchronized devices, an exhausted run budget, or an outstanding HDMA stall.

The PPU component returns the exact next state-machine edge in OAM, HBlank, and VBlank. It returns one dot in pixel transfer and no deadline while the LCD is disabled. Timer, DMA, and serial deadlines use their next internal edge or completion. This API is not currently used by generated execution, so it does not affect release runtime cost.

## Re-entry criteria

Do not retry the same per-region duplicated fast/slow transform on more games. Revisit event-scheduled superblocks only when a design can satisfy all of these conditions:

- dynamic profiles identify hot regions that account for enough tick commits to make a 10% full-runtime gain plausible
- one local-state frame covers A, flags, SP, PC, and accumulated cycles as well as the general registers
- diagnostic resume is represented without duplicating every cold region
- generated metadata records the region boundary, flush reason, and estimated dynamic coverage
- the small-DMG gate shows a clear win before mapper-heavy and CGB validation begins

The most promising near-term alternative is to complete NR-0 observability and then measure NR-2 memory/dispatch specialization or NR-3 stable-span PPU work. Both target costs that the current Tetris evidence shows are materially more frequent than register-only batches.

## Reproduction

Build and run repository tests:

```bash
cmake -G Ninja -B build . -DBUILD_TESTS=ON
ninja -C build
ctest --test-dir build --output-on-failure
```

Generate an instrumented project and print counters:

```bash
./build/bin/gbrecomp roms/tetris.gb -o output/nr0-tetris
cmake -G Ninja -S output/nr0-tetris -B output/nr0-tetris/build \
  -DCMAKE_BUILD_TYPE=Release \
  -DGBRECOMP_ENABLE_IPO=ON \
  -DGBRECOMP_ENABLE_PERFORMANCE_COUNTERS=ON
ninja -C output/nr0-tetris/build
./output/nr0-tetris/build/tetris \
  --headless \
  --limit-frames 1800 \
  --dump-state logs/nr0-tetris-state.json \
  --report-performance-counters
```

Verify generated/interpreter consistency:

```bash
./output/nr0-tetris/build/tetris \
  --differential 500000 \
  --differential-log 100000 \
  --differential-fail-on-fallback
```

Use a legal local ROM. The mapper-heavy expansion was intentionally not run because the first-game performance gate failed.
