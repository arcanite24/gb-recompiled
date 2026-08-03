# Working on GB Recompiled

## Project and documentation map

GB Recompiled turns Game Boy ROMs into generated C and links that code with a shared runtime.

The main areas are:

- `recompiler/`: ROM loading, decoding, analysis, IR, code generation, and the `gbrecomp` CLI
- `runtime/`: CPU fallback, mappers, PPU, DMA, timers, audio, persistence, SDL, and ImGui
- `tests/`: repository-owned synthetic and unit regression tests
- `tools/`: accuracy, trace, differential-log, frame, audio, and benchmark helpers
- `roms/`: local test ROMs; do not commit copyrighted commercial ROMs
- `output/`: ad hoc generated projects and launcher builds
- `logs/`: durable repro captures, state dumps, frame captures, and benchmark results

Documentation roles:

- `README.md`: short public introduction and successful first build
- `RUNTIME.md`: generated-executable controls and diagnostics
- `ACCURACY.md`: current external-ROM test snapshot
- `GBC.md`: CGB implementation and validation status
- `ANDROID.md`: Android generation and APK workflow
- `GROUND_TRUTH_WORKFLOW.md`: trace-assisted coverage, including its limits
- `NATIVE_PATCHES.md`: exact-ROM native replacement manifest and callback ABI
- `PORT_MODULES.md`: exact-ROM read-only port/frontend module ABI
- `TODO.md`: prioritized live backlog
- `docs/CODE_IMPROVEMENT_AUDIT_2026-07-12.md`: audit evidence and completed P0 remediation
- `docs/RECOMPILER_CORRECTNESS_AUDIT_PLAN.md`: current correctness roadmap
- `docs/NL0_POST_WIN_PERFORMANCE_TRUTH_2026-07-14.md`: three-game attribution, estimator, build footprint, and NL-0 decision
- `docs/NL1_ARITHMETIC_TIMER_RESULT_2026-07-14.md`: retained timer optimization and scalar-oracle evidence
- `docs/NL2_LAZY_PPU_RESULT_2026-07-14.md`: rejected lazy-PPU experiment and measured limits
- `docs/APU_EVENT_BATCHING_RESULT_2026-07-14.md`: retained lazy APU scheduling and deterministic PCM evidence
- `docs/NL4_GENERATED_BUILD_RESULT_2026-07-14.md`: retained generated chunking and compiler-memory evidence
- `docs/NL3_POST_SCHEDULER_REPROFILE_2026-07-14.md`: post-scheduler compiled-region eligibility decision
- `docs/NL5_NATIVE_PATCH_SDK_RESULT_2026-07-14.md`: native replacement tracer-bullet design and gates
- `docs/NATIVE_RECOMPILATION_STRATEGY_2026-07-14.md`: active performance and AOT execution strategy

## Hardware references

For hardware behavior, consult `tech_docs/pan_docs.md` first. Use the local `SameBoy/` source as the second reference when Pan Docs is ambiguous or a proven implementation is needed.

This is mandatory for timing-sensitive work, especially:

- CGB registers and DMG-on-CGB behavior
- mapper address resolution
- PPU modes, STAT, palettes, and bus visibility
- OAM DMA and HDMA
- timers, interrupts, HALT/STOP, and speed switching

## Build and test standards

Always use CMake with Ninja. The root project requires CMake 3.20 or newer.

```bash
cmake -G Ninja -B build . -DBUILD_TESTS=ON
ninja -C build
ctest --test-dir build --output-on-failure
```

Do not trust a relocated CMake cache. If a build directory points to a different checkout, configure a fresh build directory instead of treating its failure as a source regression.

The repository-owned CTest suite is the default fast gate. It covers analyzer state, mapper resolution, 9-bit MBC5 banks, bus phases, HALT/OAM-bug CPU transitions, PPU/DMA timing primitives, state/test-runner protocols, multi-ROM isolation, and release relocation.

Native replacement changes must additionally run `native_patch_sdk_end_to_end`.
Keep `gb_native_call_original()` as a scheduling disposition: generated bodies
can return at safepoints before the guest function returns, so post hooks must
remain tied to the captured guest call frame rather than the native C stack.

## Required validation for generated behavior

Changes to analyzer logic, code generation, generated CMake, runtime behavior, or runtime headers must validate both the root build and a freshly generated project.

```bash
ninja -C build

./build/bin/gbrecomp <test-rom> -o output/<test-project>
cmake -G Ninja -S output/<test-project> -B output/<test-project>/build
ninja -C output/<test-project>/build
./output/<test-project>/build/<test-project> --headless --limit-frames 120
```

Use a legal local ROM for the smoke run. The synthetic CTest fixtures remain the portable CI gate. When a generated build appears to ignore a runtime change, regenerate and explicitly rebuild it; stale generated trees are a recurring source of false conclusions.

Apply focused external tests according to the subsystem:

```bash
python3 tools/run_tests.py --filter ppu
python3 tools/run_tests.py --filter oam_dma
python3 tools/run_tests.py --filter timer
python3 tools/run_tests.py --filter misc
```

The accuracy runner is fail-closed: an empty selection, timeout, build error, missing state dump, or incomplete result is a failure. Use `--rebuild` when verifying changes that could invalidate generated output.

Blargg `oam_bug` uses its documented external-RAM protocol rather than serial output: `$A001-$A003` must contain `DE B0 61`, `$A000=00` passes, `$A000=80` is incomplete, and any other signed status fails. A missing or malformed signature never passes.

## Accuracy workflow

Use three distinct evidence layers and label them correctly:

1. Repository-owned unit and synthetic tests validate isolated invariants.
2. Differential mode validates generated execution against this project's interpreter.
3. Mooneye, Blargg, SameBoy comparisons, and frame/audio hashes provide independent or higher-level evidence.

Differential mode is not an independent hardware oracle because both paths share runtime devices.

```bash
./output/game/build/game \
  --differential 500000 \
  --differential-log 100000 \
  --differential-fail-on-fallback
```

For stable game-specific repros, record cycle-anchored input and keep the artifacts:

```bash
./output/game/build/game \
  --log-file logs/session.log \
  --record-input logs/session.input

./output/game/build/game \
  --log-file logs/session-replay.log \
  --input "$(cat logs/session.input)"
```

Use `--dump-frames`, `--dump-present-frames`, and `--dump-state` when visual or machine-state evidence matters.

## Interpreter fallback workflow

Measure fallback before optimizing around it:

```bash
./output/game/build/game \
  --log-file logs/interpreter.log \
  --limit 500000 \
  --log-frame-fallbacks \
  --report-interpreter-hotspots \
  --interpreter-hotspot-limit 12

python3 tools/summarize_interpreter_log.py logs/interpreter.log
```

If the summary reports `No interpreter fallback recorded`, the remaining slowdown is in compiled execution or runtime/device work.

Prefer generated `*_metadata.json` sidecars over scraping generated C for names and address mappings.

## Recompiler debugging

When analysis hangs, misses code, or emits implausible output:

```bash
./build/bin/gbrecomp roms/game.gb --trace
./build/bin/gbrecomp roms/game.gb --limit 10000
```

Then:

- inspect `[ERROR] Undefined instruction` diagnostics
- use `--add-entry-point bank:address` for a known missed target
- use `--no-scan` to isolate aggressive data-as-code discovery
- use `--symbols` for naming, not as blanket proof that every ROM label is callable
- use `.sym` directives or `--annotations` for trusted function, label, and data boundaries

The Nintendo logo and cartridge-header ranges are built-in ROM data. Keep analyzer state conservative at joins and calls; an unknown target that dispatches safely is preferable to a confidently wrong bank.

## Performance workflow

Do not benchmark a normal interactive window with wall-clock timing. Use:

```bash
python3 tools/benchmark_emulators.py roms/game.gb \
  --recompiled-binary output/game/build/game \
  --frames 1800 \
  --repeat 5 \
  --warmup 1 \
  --json-out logs/game-benchmark.json
```

The helper builds a dedicated optimized generated binary by default. `--benchmark` is a reduced-workload core profile: it disables pacing, host presentation, audio emulation, final RGB conversion, and pixel rasterization while retaining CPU/device timing work. Never present it as full interactive-runtime performance, and compare only matching profiles.

When a change is intended to reduce generated/runtime transitions, configure the generated project with `-DGBRECOMP_ENABLE_PERFORMANCE_COUNTERS=ON` and run it with `--report-performance-counters`. Add `--estimate-visibility-regions` only for the separate conservative region-estimator capture; it is intentionally more expensive than basic attribution. These counters are diagnostic-only and do not replace state, frame, PCM, or independent hardware evidence. Keep normal release measurements instrumentation-off.

For a reproducible full-headless attribution/build-footprint artifact, use `tools/run_nl0_profile.py` with a cycle-anchored file under `tools/profiles/`. It creates fresh counters-off and counters-on Release builds, checks repeated state hashes, records ROM/input/executable hashes and feature flags, measures build/runtime wall and process-tree RSS, and samples symbol coverage where supported. Summarize raw logs with `tools/summarize_nl0_profile.py`; use `tools/compare_nl0_controls.py` for the profiling-off regression gate. The symbolized diagnostic profile leaves IPO off by default and must not be compared to the ordinary reduced-workload benchmark profile.

Use `--no-recompiled-autobuild` only when measuring the exact binary already on disk. A result near 60 FPS usually indicates a stale or incorrectly configured run.

For NL-1 timer regression checks, compare the default arithmetic timer against `--scalar-timer` in the same generated executable. `tools/compare_nl0_controls.py` accepts repeatable `--before-arg` and `--after-arg` options and rejects divergent final-state hashes. The retained result and exact gate are in `docs/NL1_ARITHMETIC_TIMER_RESULT_2026-07-14.md`.

For APU scheduling regression checks, compare the default batched path against `--eager-audio` in the same generated executable. A retained result requires identical deterministic PCM and APU state across fine/coarse, HALT-heavy, CGB double-speed, MMIO-observer, and DIV-reset boundaries before applying the three-game performance gate.

For generated-build footprint work, use `tools/profile_generated_build.py` rather than a shell `time` around Ninja. Generated Ninja projects limit executable-target compilation to eight jobs by default; override with `-DGBRECOMP_GENERATED_COMPILE_JOBS=<n>`, or use `0` to disable the pool. Compare source bytes/files, cold wall time, compiler process-tree RSS, executable/loadable size, and runtime separately.

For scene-specific slowdown investigation, combine `--debug-performance`, `--log-slow-frames`, `--log-slow-vsync`, and `--log-lcd-transitions` with recorded input.

## Multi-ROM, Android, and releases

Directory input generates a shared SDL + ImGui launcher. Validate both graphical default behavior and `--list-games` / `--game <id>` scripting when launcher code changes.

Android output is single-ROM, landscape, controller-first, and `arm64-v8a`. It requires an external SDL2 source checkout through `SDL2_SOURCE_DIR`; follow `ANDROID.md` rather than inventing a separate build path.

Release archives must contain `gbrecomp`, the embedded runtime source snapshot, the root license, and runtime provenance. A release change is incomplete until the extracted archive can generate, configure, build, and run a synthetic project from a relocated directory.

## Documentation and artifact hygiene

- Put ad hoc generated projects in `output/`.
- Put logs, JSON, traces, state dumps, and captures in `logs/`.
- Keep the public README short; put specialized workflows in the focused documents linked from it.
- Update `RUNTIME.md` when runtime flags, controls, settings, or persistence behavior changes.
- Regenerate `ACCURACY.md` only from a complete, provenance-aware run.
- Update `GBC.md` after CGB behavior or its curated test subset changes.
- Update this file when the required build or verification workflow changes.

Preserve unrelated working-tree changes. Prefer small, independently verified commits, and do not claim compatibility, accuracy, or performance beyond the evidence actually collected.
