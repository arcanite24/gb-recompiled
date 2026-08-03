# NL-3 Post-Scheduler Re-profile — 2026-07-14

Status: **eligibility gate failed; implementation remains deferred**

## Decision

Do not build the visibility-aware compiled-region prototype yet. After the retained arithmetic timer and event-batched APU scheduler, the conservative estimator predicts at least 20% fewer dynamic tick/device commits on only one of three representative 9,000-frame workloads.

The pre-implementation gate requires two workloads. This avoids another large generated-code transform whose apparent safepoint opportunity does not translate into enough removable device commits.

## Post-scheduler result

Fresh counter-enabled builds used the retained runtime, the current 1 MiB generated chunks, the exact cycle-anchored inputs, and `--report-performance-counters --estimate-visibility-regions` for 9,000 frames.

| Workload | Tick commits | Estimated removable commits | Removable | Safepoints | Estimated removable safepoints |
|---|---:|---:|---:|---:|---:|
| Tetris | 116,064,246 | 43,497,640 | **37.48%** | 67,974,633 | 42.57% |
| Zelda | 78,761,895 | 13,402,542 | **17.02%** | 12,088,137 | 59.43% |
| Tetris DX | 153,292,365 | 20,011,247 | **13.05%** | 19,926,308 | 53.34% |

Only Tetris clears the 20% device-commit threshold. Zelda and Tetris DX have substantial removable-safepoint estimates, but safepoints are explicitly supporting evidence rather than the keep gate.

## Why the scheduler wins did not unlock NL-3

The arithmetic timer replaces scalar work inside each `gb_tick`; it does not reduce `gb_tick` commit count. APU batching reduces `gb_audio_step_timed` calls by publishing accumulated time at exact audio events, but generated instructions still retire through the same tick/PPU/interrupt commit boundaries. The estimator therefore remains near its pre-APU 37.5%, 17.0%, and 13.0% prediction.

This is consistent with the retained APU result: audio work fell dramatically without changing CPU/device visibility boundaries. A larger compiled region would now remove mostly safepoint and call overhead while the expensive timer and audio inner work has already been reduced independently.

## Scene-length caution

A 1,800-frame exploratory capture predicted 20.71% for Zelda, provisionally clearing the threshold alongside Tetris. The full 9,000-frame profile fell to 17.02%. Eligibility decisions must therefore use the complete recorded workload, not an attractive startup segment.

## Evidence and next trigger

Raw records:

- `logs/nl3_reprofile_20260714/tetris-9000.log`
- `logs/nl3_reprofile_20260714/zelda-9000.log`
- `logs/nl3_reprofile_20260714/tetrisdx-9000.log`

Reconsider NL-3 only if a future scheduler change actually coalesces tick/PPU/interrupt commits, or if new representative profiles show two real workloads above the 20% gate. APU work reduction alone is not such a change.

Near-term effort should move to a recompilation-specific differentiator: binary ROM/resource embedding and streaming generation for tooling footprint, or the exact-ROM native replacement SDK for higher-level game-function replacement.
