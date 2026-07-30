# NL-0 post-win performance truth

Date: 2026-07-14

Status: complete

## Decision

Proceed with the NL-1 arithmetic DIV/TIMA prototype, then use its retained clock-advance API as the first input to NL-2 lazy PPU synchronization. Do not start NL-3 compiled regions yet.

The profiles identify recurring runtime work rather than generated dispatch as the next opportunity:

- `gb_timer_tick` accounts for 10.5% to 23.7% of named application leaf samples while processing 100% of committed CPU cycles. TIMA was disabled in all three captured scenes, so most of this measured work is DIV advancement and common-path bookkeeping.
- `ppu_tick` is the largest named leaf in all three workloads at 31.2% to 34.4%. Between 63.5% and 64.4% of PPU cycles occur outside draw mode, giving NL-2 a large exact-event scheduling target.
- `audio_advance` accounts for another 13.7% to 18.8%. APU event batching remains a credible follow-up, but it should not be mixed into the first timer or PPU experiment.
- DMA, serial, RTC, and accepted-interrupt activity are too sparse in these scenes to justify a first optimization slice.
- The visibility-aware estimator predicts only 37.5%, 17.0%, and 13.0% fewer `gb_tick`/`ppu_tick` calls. Safepoint removal is higher, but safepoints alone do not remove device work. NL-3 therefore misses the revised device-commit gate on two of three workloads and remains deferred until NL-2 can consume batched time.

NL-4 also gained stronger justification as an independent usability lane. The mapper-heavy control build emitted 103.21 MiB of C, took 154.73 seconds, and peaked at 3.88 GiB RSS. That is a real recompilation cost even though runtime RSS remains excellent.

## Scope and method

The three profiles use the same full-headless contract:

- Release build with generated `-O3`, debug symbols, frame pointers, no strip, and IPO off
- 9,000 guest frames, one warmup, four interleaved measured trials
- PPU timing, pixel rasterization, audio emulation, and final state enabled
- host presentation and pacing disabled
- one cycle-anchored recorded input per workload
- isolated home/config directories so local saves cannot alter the run
- a counters-off control, a basic-attribution build, and a separate opt-in visibility-estimator run

The profile runner records ROM, input, and executable hashes; git commit and dirty status; feature flags; generation/build/runtime wall time and process-tree RSS; generated-source and binary sizes; state hashes; raw counters; histograms; and symbol coverage. It refuses non-cycle-anchored input and fails if control, instrumented, estimator, or repeated final states differ.

The captured revision was `cc9f0939d13019fbd755095bf8ba57a08827af39` with a dirty working tree. The binary, ROM, input, and state hashes in each artifact are therefore the comparison identity; the commit alone is not sufficient provenance. The captured toolchain was Apple Clang 17.0.0, CMake 4.1.2, and Ninja 1.13.1. The runner now records those tool versions plus `gbrecomp` and aggregate generated-source hashes directly in future artifacts; the initial three artifacts retain the compiler identity in their configure logs.

Inputs:

- `tools/profiles/tetris.input`
- `tools/profiles/links_awakening.input`
- `tools/profiles/tetris_dx.input`

Raw profile artifacts:

- `logs/nl0_tetris_o3_20260714/artifact.json`
- `logs/nl0_zelda_o3_20260714/artifact.json`
- `logs/nl0_tetrisdx_o3_20260714/artifact.json`
- `logs/nl0_20260714/summary.json`
- `logs/nl0_profiling_off_ab_v2_20260714/artifact.json`

`logs/` is intentionally local artifact storage. The measurements needed for future decisions are reproduced below.
The three per-workload artifacts were written before the gate was tightened and retain the historical tick-or-safepoint boolean; `logs/nl0_20260714/summary.json` and the current summarizer report the revised device-commit gate used by this decision.

## Symbolized runtime attribution

All sampled application leaf addresses resolved to a named generated block or runtime function. System wait frames from inactive host threads were excluded from the application-CPU denominator.

| Named application leaf | Small DMG | Mapper-heavy DMG | CGB |
| --- | ---: | ---: | ---: |
| `ppu_tick` | 31.7% | 31.2% | 34.4% |
| `gb_tick` | 19.5% | 14.5% | 21.9% |
| `audio_advance` | 16.5% | 13.7% | 18.8% |
| `gb_timer_tick` | 15.0% | 23.7% | 10.5% |
| `ppu_render_background_span` | 7.5% | 7.4% | 5.7% |
| Symbolized application leaves | 100% | 100% | 100% |

The remaining named leaves are individually small. Generated dispatch was not a leading cost, so cross-function inlining and dispatcher work remain out of scope.

## Dynamic work and estimator

| Metric | Small DMG | Mapper-heavy DMG | CGB |
| --- | ---: | ---: | ---: |
| Tick commits | 116,064,246 | 78,761,895 | 153,210,169 |
| Candidate units / generated safepoints | 71.2% | 86.3% | 89.0% |
| Estimated removable tick commits | 37.5% | 17.0% | 13.0% |
| Conservative removable `ppu_tick` lower bound | 37.5% | 17.0% | 13.0% |
| Estimated removable safepoints | 42.6% | 59.4% | 53.2% |
| PPU cycles outside draw mode | 64.3% | 64.4% | 63.5% |
| Timer-processed committed cycles | 100% | 100% | 100% |
| TIMA enabled/reload cycles | 0% | 0% | 0% |
| DMA-active tick-call share | 0.0078% | 0.0114% | 0.0059% |
| Serial/RTC tick-call share | 0% | 0% | 0% |
| Interpreter fallbacks | 0 | 8,716 | 131 |

The original NL-3 gate allowed either tick or safepoint removal. That is too permissive: removing a generated stop check without removing `gb_tick`, `ppu_tick`, or equivalent device work does not address the measured bottleneck. The roadmap now requires at least 20% estimated dynamic device-commit removal on two workloads. Only the small DMG workload passes.

The estimator is conservative in what it groups: register-only and already-proven ROM/WRAM/HRAM operations may participate; generic reads/writes, generated transitions, fallback, stopped state, and the next exact deadline terminate a group. It changes no emulated state and is enabled only by `--estimate-visibility-regions` on a counters-enabled build.

## Build and footprint truth

Control values are the instrumentation-off Release profile. Instrumented values are diagnostic builds and are not release footprint targets.

| Metric | Small DMG | Mapper-heavy DMG | CGB |
| --- | ---: | ---: | ---: |
| Generated C | 10.45 MiB | 103.21 MiB | 79.41 MiB |
| Control cold build | 5.26 s | 154.73 s | 40.90 s |
| Control compiler peak RSS | 1.18 GiB | 3.88 GiB | 3.95 GiB |
| Control executable | 3.26 MiB | 28.06 MiB | 17.48 MiB |
| Control runtime peak RSS | 8.73 MiB | 12.63 MiB | 11.17 MiB |
| Instrumented cold build | 7.24 s | 438.78 s | 174.84 s |
| Instrumented compiler peak RSS | 1.23 GiB | 4.41 GiB | 4.41 GiB |

The mapper-heavy instrumented build spent more than five minutes optimizing one 3.7 MiB generated translation unit. This does not affect normal releases, but the 154.73-second control build and 103.21 MiB source output independently pass the threshold for an NL-4 chunking/compact-output experiment.

## Profiling overhead and the release-off gate

The first profiling-off A/B found a 2.69% regression. The new NL-0 arrays and estimator state had been added unconditionally to `GBContext`; even though no counter operations executed, the enlarged context shifted hot runtime state. The gate caught the problem.

All new NL-0 state is now inside `GBRT_ENABLE_PERFORMANCE_COUNTERS`. A fresh eight-trial, interleaved small-DMG comparison against the retained pre-NL-0 generated/runtime snapshot produced:

| Gate | Before NL-0 | Corrected profiling-off | Delta |
| --- | ---: | ---: | ---: |
| Full-headless median | 2.9703 s | 2.9607 s | -0.32% |
| Peak runtime RSS | 8.72 MiB | 8.75 MiB | +0.03 MiB |
| Mach-O text/data sizes | identical | identical | 0 |
| Final state SHA-256 | identical | identical | pass |

The executable files differ by 288 bytes of debug/symbol metadata, not loadable text/data. There is no additional release dependency. `psutil` improves process-tree RSS sampling when available, but the runner has a standard `ps` fallback and imports without it.

Basic attribution added 8.2% to 10.9% over the initial instrumented-profile controls. Because those controls still contained the context-layout issue later removed, the corrected small-DMG directional overhead is approximately 14%. This is acceptable only for opt-in diagnostics; performance claims must continue to use counters-off builds. The more expensive region estimator runs separately so ordinary counter captures do not pay its cost.

## Verification

- counters-off repository CTest: 33/33 passed
- counters-on repository CTest: 33/33 passed
- focused external timer set: 13/13 passed
- small DMG strict differential: 500,000/500,000 matched, fallback rejection enabled
- mapper-heavy DMG strict differential: 500,000/500,000 matched, fallback rejection enabled
- CGB directional differential: 500,000/500,000 matched; this workload remains unsuitable as fallback-free evidence
- all 9,000-frame control, attribution, and estimator runs produced identical repeated final-state hashes per workload
- fresh corrected counters-off generated project built and completed the eight-trial release-off A/B

Differential mode is still a shared-runtime check, not an independent hardware oracle. The timer catalogue and existing repository tests provide the independent/focused evidence appropriate for a diagnostic-only change.

## Reproduction

One workload:

```bash
python3 tools/run_nl0_profile.py \
  --name tetris-dmg-small \
  --rom roms/tetris.gb \
  --gbrecomp build/bin/gbrecomp \
  --project-dir output/nl0-tetris \
  --build-root output/nl0-tetris-build \
  --input-file tools/profiles/tetris.input \
  --frames 9000 \
  --repeat 4 \
  --warmup 1 \
  --sample-seconds 5 \
  --json-out logs/nl0-tetris/artifact.json
```

Summarize existing counter logs:

```bash
python3 tools/summarize_nl0_profile.py \
  logs/nl0-tetris/estimator/runtime.log \
  --json-out logs/nl0-tetris/summary.json
```

The default symbolized diagnostic profile leaves IPO off. Use `--ipo` only for a separately labeled build; never compare artifacts whose ROM, input, feature, or executable hashes differ.

## Next execution order

1. **NL-1 timer arithmetic:** replace the scalar DIV/TIMA common path with O(1) edge counting behind an eager/scalar A/B control. Keep exact overflow/reload and DIV/TAC write behavior.
2. **NL-2 lazy PPU synchronization:** defer OAM, HBlank, and VBlank work to exact guest-visible deadlines, initially retaining scalar mode 3 and an eager oracle.
3. **NL-1 APU event batching:** evaluate only after timer and PPU results remain separable; preserve deterministic PCM exactly.
4. **NL-4 generated-output shape:** independently test smaller generated chunks/streaming or compact cold output against the mapper-heavy and CGB build baselines.
5. **NL-3 compiled regions:** re-profile after NL-1/NL-2. Begin only if at least two workloads predict 20% fewer device commits and the scheduler can consume the accumulated time.

If the timer prototype misses its 5% two-game keep gate, reject it cleanly and begin NL-2 rather than widening the timer patch. The measured PPU opportunity is larger and more consistent across the matrix.
