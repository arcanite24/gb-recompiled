# NL-4 Generated Build Result — 2026-07-14

Status: **retained**

## Decision

Keep 1 MiB generated-code chunks and a Ninja compile pool of eight jobs for generated executable targets. The change cuts measured compiler process-tree peak RSS by **48.3% on mapper-heavy DMG** and **56.7% on CGB**, clearing the NL-4 keep gate with effectively unchanged runtime behavior.

This slice improves bounded-memory parallel compilation; it does not yet implement profile-guided hot/cold code selection. Generated source bytes are essentially unchanged, and the ROM byte-array translation unit remains the largest file in both projects.

## Retained implementation

- Function and dispatch source chunks target 1 MiB instead of 4 MiB.
- Generated Ninja projects cap executable-target compilation through `GBRECOMP_GENERATED_COMPILE_JOBS`, defaulting to `8`.
- Runtime and UI sources remain outside that pool, so their small translation units can still compile freely.
- Setting `-DGBRECOMP_GENERATED_COMPILE_JOBS=0` disables the pool.
- Single-ROM, multi-ROM, and Android-generated CMake surfaces use the same policy.
- `tools/profile_generated_build.py` records cold/warm wall time, compiler process-tree RSS, generated source footprint and hashes, executable/loadable size, configuration, and tool versions.

## Build evidence

The baselines and candidates used the same post-APU runtime, Release `-O3`, debug symbols, frame pointers, IPO off, stripping off, and performance counters off. Candidate measurements use 1 MiB chunks and an eight-job pool on a 12-core Apple Silicon host.

| Workload | Generated C files | Generated C bytes | Cold build | Peak compiler RSS | Executable | Loadable `__TEXT` |
|---|---:|---:|---:|---:|---:|---:|
| Zelda baseline, 4 MiB/unbounded | 31 | 108,347,977 | 143.24 s | 4.28 GiB | 29,443,728 B | 22,151,168 B |
| Zelda candidate, 1 MiB/pool 8 | 104 | 108,361,117 | 147.86 s | 2.22 GiB | 29,492,496 B | 22,167,552 B |
| Zelda delta | +235.5% | +0.012% | +3.23% | **-48.27%** | +0.166% | +0.074% |
| Tetris DX baseline, 4 MiB/unbounded | 24 | 83,265,301 | 43.29 s | 4.01 GiB | 18,314,984 B | 16,138,240 B |
| Tetris DX candidate, 1 MiB/pool 8 | 82 | 83,269,593 | 44.39 s | 1.74 GiB | 18,331,832 B | 16,138,240 B |
| Tetris DX delta | +241.7% | +0.005% | +2.55% | **-56.68%** | +0.092% | 0.000% |

The extra files are intentional compiler work units, not duplicated generated bodies. Warm no-op builds remained about 0.01–0.03 seconds.

### Pool selection

Zelda was also measured at lower pool depths:

| 1 MiB chunk pool | Cold build | Peak compiler RSS | Relative conclusion |
|---:|---:|---:|---|
| 4 | 176.54 s | 1.36 GiB | Lowest memory, but 23% slower than baseline |
| 6 | 156.11 s | 1.91 GiB | Middle point, still 9% slower |
| 8 | 147.86 s | 2.22 GiB | Retained balance: half the memory for 3.2% wall cost |

A four-job pool without smaller chunks took 181.44 seconds and 1.95 GiB. The combined result shows that chunk size and bounded concurrency both matter.

## Runtime and behavior gate

Fresh 9,000-frame, eight-trial, cycle-input runs compared separately built baseline and candidate binaries:

| Workload | Baseline median | Candidate median | Delta | Final state |
|---|---:|---:|---:|---|
| Zelda | 1.907314 s | 1.907414 s | +0.005% | Identical SHA-256 |
| Tetris DX | 2.974902 s | 2.976192 s | +0.043% | Identical SHA-256 |

Both are well inside the 3% runtime guardrail. Loadable data size was unchanged, runtime peak RSS did not increase, and the altered chunk boundaries do not change cold-code handling: any generated body still executes through the same deterministic dispatch and fallback behavior.

## Verification

- The complete repository suite passes: 34/34 CTest tests, including release relocation and explicit single- and multi-ROM compile-pool assertions.
- A freshly generated single-ROM project configured with Ninja, emitted a pool depth of 8, built successfully, and completed a 120-frame headless smoke.
- Android generation emits the same pool on its generated shared-library target.
- Zelda strict and Tetris DX directional differential runs each matched generated and interpreted execution for 500,000 steps.

Primary artifacts:

- `logs/nl4_baseline_zelda_20260714/artifact.json`
- `logs/nl4_chunk1m_pool8_zelda_20260714/artifact.json`
- `logs/nl4_baseline_tetrisdx_20260714/artifact.json`
- `logs/nl4_chunk1m_pool8_tetrisdx_20260714/artifact.json`
- `logs/nl4_runtime_zelda_20260714/artifact.json`
- `logs/nl4_runtime_tetrisdx_20260714/artifact.json`
- `logs/nl4_differential_20260714/zelda.log`
- `logs/nl4_differential_20260714/tetrisdx.log`

## Deferred work

Binary ROM embedding remains a promising portable source-size improvement because the emitted ROM array is now the largest translation unit. Streaming emitter bodies may reduce `gbrecomp` generation RSS. Profile-guided hot/native plus cold/compact representation remains separate work and must preserve visible, deterministic execution for every cold path; static discovery alone is not evidence that code is safely discardable.
