# APU event-batching result

Date: 2026-07-14

Status: retained

## Decision

Keep lazy APU event batching as the default runtime path and proceed to NL-4 generated-output/chunking work.

The runtime now accumulates CPU time across ordinary instruction ticks and publishes it at the next exact 44.1 kHz sample deadline or before guest-visible APU interaction. The existing chronological `gb_audio_step_timed` implementation remains the batch consumer, so DIV-driven frame-sequencer edges, channel evolution, CGB double-speed conversion, and sample emission keep their established ordering. Generated executables can select the former per-tick behavior with `--eager-audio` for a same-binary oracle.

This clears the independent runtime gate on every representative workload:

| Workload | Eager median | Batched median | Delta |
| --- | ---: | ---: | ---: |
| Tetris, DMG | 2.7017 s | 2.1532 s | **-20.30%** |
| Link's Awakening, mapper-heavy DMG | 2.2355 s | 1.9059 s | **-14.74%** |
| Tetris DX, CGB | 3.6508 s | 2.9616 s | **-18.88%** |

All three exceed the 5% keep threshold, and there is no regressing workload.

## Implementation boundary

The retained change is deliberately below the recompiler's CPU semantics:

- `GBContext` records the pending CPU-cycle span, its starting DIV value, speed mode, half-system-cycle remainder, and countdown to the next sample
- a sub-sample tick only updates this small scheduler state
- reaching a sample deadline flushes the whole span through `gb_audio_step_timed`
- APU register and wave-RAM reads/writes, CGB PCM12/PCM34 reads, DIV reset, speed switching, savestate capture, and counter reporting flush pending time first
- loading or resetting a context clears derived pending state; the savestate format does not change because saves synchronize before serializing the existing APU state
- `--eager-audio` flushes any pending time and restores one APU advancement per runtime tick without changing the executable

Channel phase does not need its own runtime flush deadline: it is not externally observable between samples unless an APU/PCM MMIO access occurs, and those accesses synchronize first. Frame-sequencer edges remain ordered inside `gb_audio_step_timed`; the sample deadline is much more frequent, while an intervening guest read or write is an explicit observer boundary.

## Measurement method

Each workload used a freshly generated counters-off Release project with generated `-O3`, frame pointers, IPO off, and the existing cycle-anchored input. One warmup and eight interleaved measured trials ran for 9,000 guest frames. Eager and batched trials used the same executable SHA-256; only `--eager-audio` differed. Every trial produced the same final-state hash within its workload.

Raw artifacts:

- `logs/apu_batch_tetris_9000_20260714/artifact.json`
- `logs/apu_batch_zelda_9000_20260714/artifact.json`
- `logs/apu_batch_tetrisdx_9000_20260714/artifact.json`
- `logs/apu_batch_counters_20260714/`
- `logs/apu_batch_equivalence_20260714/`

`logs/` and generated projects under `output/` are local evidence, so the decision-relevant values are preserved here.

## Actual work reduction

The counters-on 1,800-frame Tetris comparison reported:

| Counter | Eager | Batched | Change |
| --- | ---: | ---: | ---: |
| `audio_step_calls` | 23,193,690 | 1,331,711 | **-94.26%** |
| `audio_step_cycles` | 127,256,632 | 127,256,632 | unchanged |
| emitted stereo samples | 1,338,009 | 1,338,009 | unchanged |
| tick commits | 23,193,690 | 23,193,690 | unchanged |
| PPU calls/dots | unchanged | unchanged | unchanged |

This is a direct removal of runtime work rather than a proxy. About 21.9 million APU function/channel-update entries disappear while the same guest time and sample stream are processed.

## Correctness evidence

- The repository audio test now compares lazy and eager schedules in ordinary DMG and CGB double-speed execution, in addition to its existing 1-cycle, HALT-sized, and coarse-chunk profiles.
- It serializes and compares the final APU state, sample count, and deterministic PCM hash. It also verifies synchronization at CGB PCM reads, APU register writes, and DIV reset.
- The existing one-APU-clock-second test still emits exactly 44,100 stereo frames.
- A separate 180-frame Tetris capture produced byte-identical state, 139,080-frame PCM, and PPM frames 60, 120, and 180. PCM SHA-256 is `f05830c093a20a1f7194e76dcfc095d2e76acfc452a1cb416a5c38a28cef8a10`; every captured PPM has SHA-256 `16bfd036abe48da762179258272568cdfcf444501c43df9b2930af77912ece37` on both paths.
- Repository CTest passes 34/34 in both counters-off and fresh counters-on builds.
- Strict differential mode matches 500,000/500,000 steps for Tetris and Link's Awakening with fallback rejection enabled.
- The directional CGB differential run matches 500,000/500,000 steps for Tetris DX. As documented elsewhere, this workload is not general fallback-free evidence.
- The curated `misc` external subset remains at its documented baseline: `boot_regs-cgb` and `vblank_stat_intr-C` pass; the two CGB boot-DIV cases and `unused_hwio-C` remain the three known failures.

Differential mode remains a shared-runtime consistency check. The eager APU oracle, deterministic PCM/state comparison, observer-boundary regressions, and independent frame capture are the primary evidence for this slice.

## Footprint gate

Eager and batched trials use the same binary, so their executable size is identical. Against the matching post-NL-1 binaries, Mach-O loadable text/data size is unchanged for Tetris and Tetris DX; the mapper-heavy build is within 0.1%. Peak runtime RSS is equal between eager and batched runs. Relative to the post-NL-1 artifacts it changes by roughly -0.4%, +0.5%, and 0.0%, all comfortably within the 2% gate.

The scheduler adds only derived per-context fields and no heap allocation. It does not grow generated CPU bodies or alter the save-file version.

## Reproduction

Compare the two paths without rebuilding:

```bash
python3 tools/compare_nl0_controls.py \
  --before output/game/build/game \
  --after output/game/build/game \
  --before-arg=--eager-audio \
  --input-file tools/profiles/game.input \
  --frames 9000 \
  --repeat 8 \
  --warmup 1 \
  --json-out logs/apu-batch-game/artifact.json
```

Run the semantic gates with:

```bash
ctest --test-dir build --output-on-failure
./build/bin/test_audio_chunk_invariance
```

## Next step

Execute NL-4 as a build/usability experiment independent from runtime performance: measure generated source size, translation-unit count, cold/warm compiler wall time and process-tree RSS, executable/loadable size, runtime RSS, relocation, and hot-runtime neutrality on the mapper-heavy DMG and representative CGB projects. Do not mix the APU gain into the NL-4 baseline.
