# Code Improvement Audit — 2026-07-12

## Purpose

This report prioritizes code-side work that will improve recompilation accuracy, runtime accuracy, performance, portability, and developer/user experience. It audits the current checkout as a system: analyzer, IR/code generation, shared runtime, generated projects, test/benchmark tooling, CI, release packaging, and documentation.

The original audit was performed against a **dirty working tree**. Line numbers in the findings refer to that historical snapshot and may have moved. The remediation record below describes the implementation and fresh verification completed afterward.

The prioritized findings preserve the 2026-07-12 baseline. Read the remediation status first: P0 is complete, while the P1/P2 sections remain the active follow-up backlog and should not be interpreted as already implemented.

## P0 remediation status — completed 2026-07-13

All nine P0 code blockers identified by this audit have been addressed in the current working tree. The original findings remain below as the rationale and historical evidence; this section is the current status.

| ID | Status | Implemented outcome | Primary regression evidence |
| --- | --- | --- | --- |
| **P0.1** | Resolved | Generated ROM access now uses bounded, mapper-aware resolution rather than underflow-prone duplicated MBC1 arithmetic. Lower-window MBC1 mode 1 execution is represented explicitly. | `test_generated_mbc1_fast_read`, `test_runtime_rom_mapping`, `test_mbc1_lower_window_analysis`; focused ASan/UBSan pass. |
| **P0.2** | Resolved | The accuracy runner rejects empty/error/timeout/incomplete runs, fingerprints the ROM/recompiler/runtime/toolchain inputs, always reconfigures/rebuilds cached projects, captures deterministic state, and uses the opt-in Mooneye breakpoint protocol. | `test_accuracy_runner_policy.py`, `test_accuracy_runner_cache.py`; formerly timing-out OAM cases now terminate and report real pass/fail state. |
| **P0.3** | Resolved | A 16-bit `BankId` is used through ROM reads, analysis, IR, symbols, metadata, code generation, runtime dispatch, and fallback reporting. MBC5 banks `0x100-0x1FF` are no longer narrowed. | `test_mbc5_rom_bank_id`, `test_mbc5_analysis`, `test_mbc5_codegen`, `test_mbc5_runtime_bank`; synthetic 8 MiB fixture. |
| **P0.4** | Resolved | Aggressive-scan state is invocation-local, removing cross-ROM mutation and the parallel data race. Multi-ROM analysis isolates identical inputs under serial and parallel job counts. | `test_multi_rom_analysis_isolation.py` compares `--jobs 1` and `--jobs 2` metadata/function sets. |
| **P0.5** | Resolved | Analyzer inputs now join at control-flow merges, calls conservatively clobber constants, decoder-derived register effects invalidate stale values, and mapper state is modeled per mapper. | `test_analyzer_state_join`, `test_analyzer_call_clobber`, `test_analyzer_register_effects`, `test_analyzer_mapper_state`. |
| **P0.6** | Resolved | Runtime reads, analyzer mapping, generated fast reads, and generated dispatch share consistent MBC1/MBC1M/MBC2/MBC3/MBC5 window and bank rules, including the remapped MBC1 lower window. | `test_runtime_rom_mapping`, `test_generated_mbc1_fast_read`, `test_mbc1_lower_window_analysis`, mapper-state tests. |
| **P0.7** | Resolved | The PPU now advances through dot/event-aware internal and CPU-visible phases, models variable mode 3 penalties, LCD startup, STAT/LYC edges, VBlank entry, and phased VRAM/OAM bus visibility. | `test_ppu_dot_timing`; fresh targeted Mooneye PPU result: **13/13 pass** in `logs/p0_ppu_accuracy_final2.json`. |
| **P0.8** | Resolved | HDMA charges hardware time while non-CPU devices advance. OAM DMA now preserves sub-M-cycle phase, fresh/restart startup edges, prior-transfer ownership during restart, model/source-bus conflicts, HRAM execution, phased WRAM-stack returns, and DMG/CGB high-source behavior. | `test_hdma_timing`, `test_oam_dma_bus`, bus-phase tests; fresh targeted Mooneye OAM DMA result: **6/6 pass** in `logs/p0_oam_dma_accuracy_final2.json`. |
| **P0.9** | Resolved | Generated projects embed a clean runtime snapshot. Release archives include the runtime, root license, runtime provenance, and a relocated generate/configure/build/run smoke test on every release platform job. | `test_release_bundle.py`; `release_bundle_is_self_contained` passes locally and is wired into all release jobs. |

### Fresh verification summary

| Verification | Result |
| --- | --- |
| Release configure/build with CMake + Ninja | Pass |
| Full repository CTest suite | **25/25 pass** (current suite, including timer and interrupt-edge regressions added during P1 follow-up) |
| Focused ASan/UBSan suite | **6/6 pass**: bounded MBC1 generation/runtime, 9-bit MBC5 analysis, analyzer joins, PPU timing, HDMA, and OAM DMA |
| Targeted Mooneye PPU suite | **13/13 pass** (`logs/p0_ppu_accuracy_final2.json`) |
| Targeted Mooneye OAM DMA suite | **6/6 pass** (`logs/p0_oam_dma_accuracy_final2.json`) |
| Previously regressed CPU cases | `reg_f` and `ei_timing` both pass from rebuilt output (`logs/p0_reg_f_final.json`, `logs/p0_ei_timing_final.json`) |
| Fresh representative generated project | `roms/tetris.gb` generated into `output/p0_tetris_smoke_final`, configured, and built successfully |
| Generated headless smoke | 120 frames, zero generated-to-interpreter fallbacks (`logs/p0_tetris_smoke_state.json`, `logs/p0_tetris_smoke_runtime.log`) |
| Generated/interpreter differential | **100,000/100,000 steps matched**, zero fallback (`logs/p0_tetris_differential_final.log`) |
| Canonical benchmark smoke | 600 frames, 1 warmup, 3 trials: recompiled 4,530.6 guest FPS / 8.5 MiB; PyBoy 2,288.1 guest FPS / 61.3 MiB (`logs/p0_tetris_benchmark_final.json`) |

The benchmark is a performance smoke, not an accuracy oracle. It uses the documented benchmark profile and should not be compared blindly with historical JSON produced under different PPU/runtime workloads.

### Remaining validation depth

The identified P0 implementations are closed, but the broader regression matrix should continue to grow. This local pass did not execute ThreadSanitizer, real GitHub-hosted cross-platform release jobs, acid2 image-oracle tests, a Magen HDMA corpus, or real-hardware/SameBoy trace captures. Those are confidence-building follow-ups for CI and the P1 accuracy program, not known reproductions of the fixed P0 defects.

## Executive conclusion

The project has a useful architecture and unusually good diagnostic foundations—generated/interpreter differential execution, input recording, frame dumping, fallback hotspots, annotations, and benchmark automation already exist. The next gains should nevertheless be correctness-led, not optimizer-led.

The most important conclusions are:

1. **Pause further fast-path expansion until the current accuracy baseline is trustworthy.** Fresh runs of `reg_f` and `ei_timing`, both recorded as passing in `ACCURACY.md`, fail in the current tree. The new generated ROM fast-read path also contains a confirmed out-of-bounds case for MBC1 mode 1.
2. **Fix the analyzer's bank and state model before enabling more direct dispatch.** MBC5's ninth bank bit is truncated, 256+ bank loops can fail to terminate, multi-ROM analysis has shared mutable state, and constant/bank propagation is path-order-dependent and can retain stale values.
3. **Make tests capable of failing the build.** The current accuracy runner can return success with zero passing tests, compilation errors, timeouts, or incomplete results. `BUILD_TESTS=ON` refers to a missing directory, and release CI does not execute a generated ROM.
4. **Treat PPU/DMA timing as the largest runtime-accuracy program.** Fixed scanline phases, immediate HDMA, and simplified OAM DMA bus blocking cannot reproduce several documented DMG/CGB behaviors.
5. **Separate safe optimization from reduced-workload benchmarking.** The current benchmark path skips raw background and sprite rendering, not only audio/output/RGB conversion. Its headline FPS is therefore useful as a core-only metric but not as an emulator comparison or rasterizer regression gate.

## Priority definitions

| Priority | Meaning |
| --- | --- |
| **P0** | Can produce wrong code, memory-unsafe generated code, hangs/data races, false-green validation, or unusable releases. Address before broad optimization or release. |
| **P1** | High-impact accuracy, performance-validity, durability, portability, or maintainability work. Start after P0 guardrails exist. |
| **P2** | Important efficiency and usability improvements whose value should be measured after correctness is protected. |
| **P3** | Cleanup/documentation that reduces future mistakes but is not itself a blocker. |

## Evidence collected

### Build and validation snapshot

| Check | Result | Interpretation |
| --- | --- | --- |
| Fresh configure: `cmake -G Ninja -B build_audit .` | Pass | The main project configures successfully from a clean build directory. The existing `build/` cache points to an older checkout path and was intentionally not reused. |
| Fresh build: `ninja -C build_audit` | Pass with warnings | Main code builds. Warnings include GNU-only extensions, unused analyzer counters, unused IR parameters, and an unused SDL variable. |
| Test configure: `cmake -G Ninja -B build_audit_tests -DBUILD_TESTS=ON .` | Fail | Top-level CMake calls `add_subdirectory(tests)`, but `tests/` does not exist (`CMakeLists.txt:56-58`). |
| `python3 tools/run_tests.py --dry-run` | 75 errors, process exit 0 | The runner lists the intended external corpus but treats every skipped/dry-run case as an error and still succeeds because only literal `fail` increments the exit code. |
| Fresh `reg_f --rebuild` | Fail | `ACCURACY.md:43` records this test as passing. Current-tree regression or baseline drift must be resolved. |
| Fresh `ei_timing --rebuild` | Fail | `ACCURACY.md:105` records this test as passing. Current-tree regression or baseline drift must be resolved. |
| Fresh `unused_hwio-C --rebuild` | Fail | Confirms an unresolved hardware-I/O accuracy case. |
| Fresh `ei_timing --differential 200000 --differential-fail-on-fallback` | Pass: 200,000 steps / 19 frames matched | Generated and interpreter execution agree, but both share the runtime. This does not prove hardware correctness and explains why an external test can fail while differential mode passes. |
| Fresh Tetris generation | Pass with many undefined-instruction diagnostics | Aggressive scanning can classify data as code and emits noisy error-level diagnostics without a strict failure policy. Summary counts were left in hexadecimal due stream formatting state. |

### Performance snapshot

The canonical helper was used on the freshly generated Tetris project:

```text
600 frames, 1 warmup, 3 trials, generated -O3 + IPO
Recompiled: 12,775.2 guest FPS, 8.4 MiB peak RSS
PyBoy:       2,269.2 guest FPS, 61.5 MiB peak RSS
```

Artifact: `logs/audit_tetris_benchmark_20260712.json`.

This is **not a full-runtime performance claim**. In the current tree, benchmark mode sets `gbrt_rgb_framebuffer_enabled = false`; `ppu_render_scanline()` then returns before both background and sprite rendering (`runtime/src/ppu.c:783-831`). The result should be labeled **core-only benchmark** until the profiles are split. Older JSON files should not be compared directly without checking their feature flags and source revision.

## Prioritized findings

### P0 — Correctness, safety, and release blockers

#### P0.1 — Generated MBC1 fast reads can index before the ROM buffer

**Evidence:** The active generated helper in `recompiler/src/codegen/c_emitter.cpp:2290-2304` excludes the lower ROM window from the bank-0 branch when `mbc_mode == 1`, then accepts the same address in `addr < 0x8000` and computes `addr - 0x4000u`. For `addr < 0x4000`, that subtraction underflows. Direct ROM reads also omit the `rom_size` validation used by the shared runtime.

**Impact:** Generated code can read outside the ROM allocation. MBC1 mode-1 software can return corrupt data or crash; invalid/out-of-range bank selections can also become host memory-safety bugs. This helper is emitted into normal generated projects, not only benchmark builds.

**Recommendation:** Remove or gate the shortcut immediately. Replace duplicated mapper arithmetic with one tested mapper-aware physical-ROM resolver used by both `gb_read8()` and generated fast paths. The resolver must cover lower/upper windows, mapper masks, zero-bank rules, MBC1M, and ROM-size bounds.

**Acceptance:** ASan/UBSan synthetic tests for MBC1 mode 1 lower-window reads and out-of-range MBC1/MBC3/MBC5 selections; generated-vs-interpreter checks; external mapper tests.

#### P0.2 — The accuracy pipeline can report false success

**Evidence:** `tools/run_tests.py:473-488` returns nonzero only when `status == "fail"`. Compile errors, configure/build errors, timeouts, missing executables, incomplete results, zero selected tests, and dry-run skips therefore do not fail the process. Cached generated projects are reused based on file existence (`tools/run_tests.py:175-201`), not a source/runtime/config fingerprint. `compare_ground_truth.py` likewise reports missing coverage without enforcing a threshold, and frame-sampled PC capture is too sparse to constitute an execution oracle. `mass_recompile.py` can report success without finding and running an executable.

**Impact:** Accuracy documents, CI, batch compatibility claims, and optimization decisions can all be based on stale binaries or no valid execution at all. Fresh failures of tests previously recorded as passing make this a current blocker, not a theoretical concern.

**Recommendation:** Define explicit policies:

- fail on any error/timeout, zero executed tests, or unexpected incomplete result;
- record a recompiler hash, runtime hash, ROM SHA-256, generator arguments, build profile, and executable hash;
- invalidate generated/build caches when any fingerprint changes;
- add `--allow-incomplete` only as an explicit local override;
- make ground-truth tools enforce coverage and comparison thresholds;
- require batch tools to prove that the expected executable ran and reached the requested stop condition.

**Acceptance:** Unit tests for every runner status and exit code, deliberate compile/runtime failures, stale-cache mutation test, and a CI job proving a known-bad generated ROM fails the job.

#### P0.3 — MBC5's 9-bit ROM bank is truncated across the recompiler

**Evidence:** Pan Docs permits MBC5 banks `$000-$1FF` (`tech_docs/pan_docs.md:10472-10495`), and `ROM::bank_count()` supports 512 banks. However, `ROM::read_banked()` takes `uint8_t` (`recompiler/include/recompiler/rom.h:180`), analyzer bank packing/extraction uses eight bits (`recompiler/src/analyzer.cpp:23-29`), symbol parsing rejects banks above `0xFF` (`recompiler/src/symbol_table.cpp:139`), and generated dispatch narrows `ctx->rom_bank` to `uint8_t` (`recompiler/src/codegen/c_emitter.cpp:2996`). The analyzer also uses `uint8_t` loop counters against a wider `bank_count()` (`analyzer.cpp:629,654`), which can wrap indefinitely at 256+ banks.

**Impact:** An 8 MiB MBC5 ROM may hang analysis. Banks 256-511 cannot be decoded, named, compiled, dispatched, or distinguished in hotspot reports.

**Recommendation:** Introduce one `uint16_t BankId` type end-to-end: ROM API, analyzer addresses/state, symbols, annotations, IR, generated dispatch, runtime fallback instrumentation, metadata, and diagnostics. Audit every narrowing conversion rather than patching individual loops.

**Acceptance:** Synthetic 8 MiB MBC5 ROM executing distinct signatures in banks `0x000`, `0x100`, and `0x1FF`; generation must terminate, metadata must preserve each bank, generated execution must avoid fallback, and differential/external checks must pass.

#### P0.4 — Multi-ROM analysis has a data race and leaks state between ROMs

**Evidence:** `recompiler/src/analyzer.cpp:1342-1393` declares `aggressive_regions` as a function-local `static std::set` and mutates it. Multi-ROM generation calls `analyze()` concurrently (`recompiler/src/main.cpp:1719-1743`). Even in serial execution, later ROMs inherit scanned addresses from earlier ROMs.

**Impact:** Parallel analysis performs unsynchronized container mutation (undefined behavior). Serial and parallel results can omit different code regions, making launcher output nondeterministic and order-dependent.

**Recommendation:** Make aggressive-scan state local to one `analyze()` invocation and keep all analyzer mutation instance-owned. Add a determinism contract for metadata and generated source.

**Acceptance:** Generate two fixtures with different unreferenced code at the same bank/address; compare byte-identical metadata/source under `-j1`, repeated `-jN`, reversed ROM order, and ThreadSanitizer.

#### P0.5 — Analyzer abstract state is unsound at control-flow joins and calls

**Evidence:** `AnalysisState` carries known registers and the current bank, but visited-state deduplication is only by bank/address (`recompiler/src/analyzer.cpp:716-717,833-848`). The first path processed wins; divergent constants are neither joined nor reprocessed. The handwritten transfer chain (`analyzer.cpp:933-1061`) has no conservative default invalidation, so many loads, arithmetic/rotate/CB operations, and calls leave stale constants. CALL fallthrough preserves every known register (`analyzer.cpp:1168-1170`) even though the SM83 has no callee-saved convention.

The mapper model compounds this: it recognizes only one immediate-address store shape, treats bank switching as a generic eight-bit value, forces zero to one for every mapper, misses MBC1 high/mode and MBC5's ninth bit, and can lose the selected switchable bank when entering a fixed-bank helper (`analyzer.cpp:1066-1076,1160`). Constant ROM loads through HL use the code bank instead of the mapped data bank (`analyzer.cpp:954,966`).

**Impact:** Stale/path-dependent constants drive mapper writes and `JP HL` recovery. The analyzer can omit valid targets or confidently generate wrong-bank targets. Queue ordering can change output.

**Recommendation:** Replace “first path wins” with a fixed-point abstract interpreter:

- per-block input state with a documented join lattice;
- constants become unknown (or a bounded value set) when paths disagree;
- decoder-derived register read/write effects with conservative invalidation by default;
- calls clobber constants unless a verified summary says otherwise;
- mapper-specific state: MBC1 low/high/mode, MBC3 bank, MBC5 low/high;
- mapper writes recognized from any statically known effective address.

**Acceptance:** Table-driven effects for every valid opcode, diamond-CFG fixtures with different A/HL/bank values, call-clobber fixtures, bank-0 trampolines under multiple selected banks, and stable output under randomized worklist order.

#### P0.6 — MBC1 runtime mapping and generated code dispatch disagree

**Evidence:** In MBC1 mode 1, writes to the high register update `ram_bank` but can leave the upper ROM-bank state stale (`runtime/src/gbrt.c:1707-1716`). `gb_read8()` maps the lower ROM window to banks `$20/$40/$60` in advanced mode (`gbrt.c:1535-1545`), while generated dispatch forces every PC below `$4000` to physical bank 0 (`recompiler/src/codegen/c_emitter.cpp:2996-3001`). Pan Docs describes the secondary register and advanced-mode lower-window mapping at `tech_docs/pan_docs.md:10048-10105`.

**Impact:** Reads and compiled instruction dispatch can observe different physical ROMs. Recompiled execution cannot represent code running from a remapped lower window.

**Recommendation:** Store mapper registers independently and derive effective lower/upper physical banks through the same resolver used for reads and dispatch. Do not overload `rom_bank` with partially normalized mapper state.

**Acceptance:** MBC1 and MBC1M fixtures that change the secondary register while already in mode 1 and execute different code from both windows.

#### P0.7 — PPU timing is structurally unable to represent important hardware behavior

**Evidence:** The runtime uses fixed 80/172/204-cycle mode lengths (`runtime/include/ppu.h:24-28`), latches key registers once per line (`runtime/src/ppu.c:242-250`), and renders a complete line after fixed mode 3 ends (`ppu.c:984-1008`). Pan Docs documents variable mode 3, fetch stalls, mid-line effects, FIFO mixing, palette blocking, and STAT timing (`tech_docs/pan_docs.md:2423-2486`). SameBoy uses a guarded batch path and falls back to a FIFO state machine (`SameBoy/Core/display.c:1854-1872,1942-2042`).

**Impact:** Raster effects, window/sprite timing, STAT behavior, CGB palette access, and game-specific artifacts cannot be fixed reliably with more scanline-end special cases.

**Recommendation:** Build a dot/event-aware FIFO path, retaining scanline batching only behind a proof that no timing-sensitive event can occur. Define the slow path as the oracle and make the fast path continuously cross-checkable.

**Acceptance:** Relevant Mooneye/acid2 STAT/mode-3/window/OAM cases, SameBoy trace comparisons, and recorded frame-hash repros for known game-specific scenes.

#### P0.8 — HDMA and OAM DMA do not charge/model the hardware they block

**Evidence:** HDMA copies blocks immediately (`runtime/src/gbrt.c:1429-1521`) and uses `stopped` without advancing the other devices through the documented transfer time. Pan Docs specifies 8 normal-speed or 16 double-speed M-cycles per block (`tech_docs/pan_docs.md:4374-4400`). OAM DMA currently permits all `$FF00+` CPU access and directly writes OAM (`gbrt.c:1528-1533,1686-1690,3234-3279`), while DMG and CGB have different source-bus ownership/conflict rules (`pan_docs.md:1747-1765`; `SameBoy/Core/memory.c:253-269`).

**Impact:** Timer, PPU, APU, interrupt, and CPU phases diverge around HDMA; DMA-from-HRAM code, CGB opposite-bus access, and PPU/OAM conflicts behave incorrectly.

**Recommendation:** Represent DMA as an event/cycle budget that advances non-CPU hardware while the CPU is blocked. Add model- and source-bus-aware ownership/conflict state shared with memory access and the PPU.

**Acceptance:** Mooneye/Magen HDMA and OAM-DMA suites on DMG/CGB and both CGB speeds, plus generated/interpreter/external trace comparison around transfers.

#### P0.9 — Release archives do not contain a usable recompiler distribution

**Evidence:** Generated CMake refers to the repository's external `runtime/` tree (`recompiler/src/codegen/c_emitter.cpp:4010-4072`), while release packaging archives the recompiler binary and README without the runtime sources/libraries required to configure a generated project (`.github/workflows/release.yml:37-43` and peer platform jobs).

**Impact:** A downloaded release can generate source that cannot be built outside the repository checkout.

**Recommendation:** Choose and test one contract: either install a relocatable `gbrt` package discoverable by generated CMake, or emit self-contained generated projects with a versioned runtime snapshot. Include license/provenance and test the archive in an empty environment.

**Acceptance:** CI downloads each produced archive into a clean directory, generates a legal synthetic ROM, configures/builds the generated project with CMake+Ninja, and runs a deterministic headless smoke test.

### P1 — High-impact accuracy, validity, and durability

#### P1.1 — Direct target-bank resolution is discarded; `JP HL` tables can read the wrong bank

The analyzer stores an instruction before setting the local copy's `resolved_target_bank` (`recompiler/src/analyzer.cpp:1104-1106,1146,1175`). IR lowering reads the already-stored value (`recompiler/src/ir/ir_builder.cpp:540,597`), so absolute CALL/JP targets remain unknown and take slower runtime dispatch. Separately, the `JP HL` table heuristic uses flat `rom.read()` for banked addresses (`analyzer.cpp:1276-1305`), which reads the wrong physical table above bank 1.

**Recommendation:** Fix the banked table read immediately, rejecting ambiguous mapper state. Persist resolved direct targets only **after P0.5 makes the state/mapper model sound**; the current unknown target forces a safe dispatch and may be masking bad analysis.

**Acceptance:** Same-address tables with different contents in banks 1/2, concrete IR target assertions, and zero avoidable dispatch/fallback without any external-test regression.

#### P1.2 — Differential mode is valuable but too expensive and not an independent oracle

`runtime/src/differential.c:303-424,551-590,712-716` byte-compares WRAM, VRAM, OAM, HRAM, IO, palettes, ERAM, and three framebuffers after every instruction. Fixed regions alone total about 211 KiB per step—roughly 98 GiB scanned during a 500,000-step run. Meanwhile both sides share mapper, PPU, DMA, timer, and APU implementations.

**Recommendation:** Use `memcmp` or region hashes as the first gate and localize only after a mismatch; then add dirty pages/ranges while retaining `--differential-full` as a strict/debug option. Maintain a three-layer accuracy ladder:

1. synthetic unit/mapper/opcode tests;
2. generated-vs-interpreter differential execution;
3. independent hardware-oracle tests/traces (Mooneye, Blargg, SameBoy, frame hashes).

**Acceptance:** Materially faster 500k-step runs with identical injected-mismatch step/address reporting, plus CI coverage in all three layers.

#### P1.3 — Benchmark mode changes the emulated workload

The helper always supplies `--benchmark` (`tools/benchmark_emulators.py:203-227`). That mode disables `gbrt_rgb_framebuffer_enabled`; `ppu_render_scanline()` exits before raw background and sprite rendering (`runtime/src/ppu.c:783-831`). Current fast-tick logic also batches/simplifies device work. This conflicts with documentation that describes skipping output-only work.

**Recommendation:** Define two explicit profiles:

- `--benchmark`: full emulation and raw framebuffer generation; skip pacing, SDL upload, audio output, and optionally final RGB conversion only;
- `--benchmark-core-only` (or `--benchmark-unsafe-fast`): reduced-workload experiments, never the default comparison.

Record all feature flags, git SHA/dirty state, ROM/executable hashes, compiler, optimization/IPO settings, and input hash in benchmark JSON.

**Acceptance:** A/B state hashes and raw framebuffer hashes match between interactive-headless and default benchmark profiles for identical recorded input. Report both profiles separately.

#### P1.4 — APU output depends on scheduler batch size

**Status (2026-07-13): completed.** APU channel advancement, sample deadlines, and DIV-driven frame-sequencer edges are now processed chronologically. An exact rational accumulator emits 44,100 samples per 4,194,304 system cycles without the drift from the former rounded fixed-point period. Repository regressions prove identical PCM hashes and serialized APU state for coarse and fine direct stepping, HALT-heavy runtime stepping, and CGB double-speed stepping; DIV reset is also constrained to its single documented falling edge. A freshly generated Tetris capture produced the same non-silent 10-second PCM hash twice (`b684c7a8a5c18196e05514fc5bb28506dd44879a9ecdc1cf7aa6b5ffba013754`).

`gb_audio_step()` advances channels over an entire cycle batch before emitting sample boundaries (`runtime/src/audio.c:757-984`). HALT fast-forward can submit 80-456 cycles at once (`runtime/src/gbrt.c:3388-3419,3619-3625,3715-3717`), spanning multiple 44.1 kHz sample points.

**Impact:** HALT-heavy scenes can repeat an end-of-batch channel state, alias, and generate different PCM for the same guest execution under different scheduler chunking.

**Recommendation:** Process channel edges and sample deadlines chronologically, or split device ticks at the next APU event.

**Acceptance:** `gb_audio_step(N)` must yield the same PCM hash as an equivalent sequence of smaller steps; compare recordings/spectra on HALT-heavy scenes.

#### P1.5 — SDL audio callback state contains C++ data races

**Status (2026-07-13): completed.** Mute and volume controls are atomic, callback underruns are accumulated atomically and published to the main-thread stats snapshot, and the callback consumes contiguous ring spans with block copies. The producer now publishes one write position per 32-frame batch instead of per sample. A two-million-frame producer/callback stress completed under ThreadSanitizer without a diagnostic while toggling mute and volume and checking PCM block integrity and counter behavior. A separate 20-million-frame synthetic pipeline profile reduced producer publications by about 32x and warm-run user CPU from 0.22-0.24 seconds to 0.10-0.11 seconds; this is callback-pipeline evidence, not full interactive-runtime performance. Device reset and reopen remain serialized with SDL's audio-device lock/close lifecycle.

The callback reads non-atomic mute/volume controls and updates non-atomic stats (`runtime/src/platform_sdl.cpp:2686-2723`), while the main/UI thread writes/reads them (`platform_sdl.cpp:1223-1225,2145-2155,2381-2386,2601-2603`). Inline audio stats are also unsynchronized, and the producer performs atomic ring operations/stat updates per sample (`platform_sdl.cpp:2726-2748`).

**Recommendation:** Use atomics or SDL device locking for callback controls/counters, aggregate stats locally, and publish audio in blocks with one ring update per block.

**Acceptance:** TSAN with dummy audio while toggling/reopening audio, monotonic counter assertions, PCM integrity checks, and audio-on/off profiling.

#### P1.6 — Save, RTC, and savestate writes are not transactional or portable

Savestates overwrite the final slot directly and serialize native structs/padding (`runtime/src/gbrt.c:1173-1233`; `runtime/include/ppu.h:101-152`). Battery and RTC files are also overwritten directly (`runtime/src/platform_sdl.cpp:3463-3502`).

**Impact:** A crash, short write, or full disk can destroy the last good save. Savestates are compiler/architecture/layout dependent.

**Recommendation:** Use an explicit versioned little-endian section format with lengths/checksum. Write a same-directory temporary file, flush/fsync, atomically rename, and preserve/recover a last-known-good backup.

**Acceptance:** Fault injection after every write, forced termination/full-disk tests, malformed-state fuzzing, and cross-compiler/platform round trips.

#### P1.7 — Generated-project writes are non-atomic and report weak errors

`recompiler/src/codegen/c_emitter.cpp:4139+` checks file open but not final write/flush/close state, writes the target directory in place, and collapses exceptions to a generic failure. A failed generation can leave a mixed old/new tree.

**Recommendation:** Generate into a sibling staging directory, verify each stream, emit the exact failed path/operation, and atomically replace the destination after all artifacts and metadata are complete.

**Acceptance:** Inject short writes/full disk/read-only paths and prove a nonzero result, precise diagnostic, and untouched previous output.

#### P1.8 — Compiler portability is contradicted by emitted GNU/Clang syntax

The active emitter writes `__builtin_expect` unconditionally (`recompiler/src/codegen/c_emitter.cpp:2292,2315,2331,2351`), while the project advertises MSVC support. Runtime code also contains unguarded GNU weak attributes (`runtime/src/gbrt.c:3029,3035`), and generated/runtime CMake emits Unix-style optimization flags in multiple places.

**Recommendation:** Centralize `GB_LIKELY`, `GB_UNLIKELY`, weak-symbol/export, visibility, and optimization configuration behind compiler feature macros and CMake target properties. Add at least compile-only generated-project jobs for MSVC and AppleClang/GCC.

#### P1.9 — CI does not exercise the product's actual output

The only workflow is release-oriented and builds the repository, but it does not generate, build, execute, or differentially validate a ROM on ordinary pushes/PRs. The documented checked-in smoke ROM/project does not exist as a tracked legal fixture, and `roms/`/generated outputs are ignored.

**Recommendation:** Add small synthetic ROM generators/fixtures whose bytes and expected traces are owned by this repository. CI should build `gbrecomp`, generate single- and multi-ROM projects, configure/build them, run deterministic headless tests, exercise `BUILD_TESTS=ON`, and validate a release archive.

#### P1.10 — Build profiles and runtime integration are duplicated and drifting

Single-ROM generation defaults to the smaller iteration profile, while multi-ROM generation emits a distinct Release/O3/IPO-on policy. Runtime source/library logic is duplicated among `runtime/CMakeLists.txt`, single-ROM output, multi-ROM output, and Android generation. This has already produced different behavior and compiler assumptions across output modes.

**Recommendation:** One generated-CMake module/template should own runtime discovery/embedding and build profiles. Make profile selection explicit and identical for single/multi-ROM unless the user requests otherwise.

### P2 — Performance and maintainability opportunities

These are high-confidence static risks, but measured benefit should be established with representative 32 KiB, multi-bank, and maximum-size ROMs before and after each change.

#### P2.1 — Analyzer scans scale poorly

- `AnnotationIndex::contains_data()` linearly scans ranges (`recompiler/src/analyzer.cpp:41-55`) inside byte-level scans. Use sorted, merged per-bank intervals plus binary search.
- Pointer and aggressive discovery walk almost every ROM byte (`analyzer.cpp:629-661,1329-1404`) and repeatedly run entropy/pattern/decoder checks. Cache plausibility by physical ROM offset and make expensive heuristic passes targeted or explicit.
- Pointer discovery can probe every switchable bank for each fixed-bank candidate, approaching quadratic behavior as bank count grows.
- Dense bank/address state is stored in allocation-heavy `std::map`/`std::set` containers (`recompiler/include/recompiler/analyzer.h:107+`). Prefer vectors/bitsets indexed by physical ROM offset and sparse maps only for metadata.
- Function construction performs a new CFG BFS per call target (`analyzer.cpp:1519-1593`), repeatedly traversing shared tails. Compute block ownership/components once.
- Symbol loading decodes every byte/bank before normal analysis (`recompiler/src/symbol_table.cpp:172+`). Reuse a shared decode index or defer heuristic inference.

#### P2.2 — Code generation creates avoidable memory and compiler load

The emitter retains complete function-body strings before copying them into chunks (`recompiler/src/codegen/c_emitter.cpp:3371-3422`) and serializes every ROM byte as textual C (`c_emitter.cpp:3462-3489`). Stream/move completed chunks, and embed/link ROM data as a binary object or generated binary resource. Measure peak generator RSS, generated bytes, C compiler peak RSS, and clean/incremental build time.

#### P2.3 — Dead duplicate pipelines make correctness work harder

The obsolete `CEmitter` class, a file-local `Generator`, unused IR lowering helpers, and completely stubbed IR optimizer passes coexist with the active `generate_output()` path. `ir::optimize()` is not invoked. Analyzer options such as `detect_computed_jumps`, `track_bank_switches`, `mark_unreachable`, and `max_functions` are declared but not enforced; `analyze_bank()` ignores its bank argument.

**Recommendation:** Delete or quarantine dead paths before refactoring active semantics. Either implement and test a pass or remove it from the public surface; avoid “planned” no-op knobs.

#### P2.4 — Operand representation is fragile

`(HL)` is represented through a magic register index and handled through repeated emitter special cases and `reg8_names[]` lookups. Replace magic values with typed memory operands and exhaustive variant handling. Add an IR verifier that rejects impossible operand/instruction combinations before emission.

#### P2.5 — CLI parsing can abort or silently accept malformed input

Numeric parsing uses uncaught `std::stoi`/`std::stoul`, and some missing flag values fall through without a focused error (`recompiler/src/main.cpp:1542-1610`). A direct `--limit nope` check aborted the process rather than producing normal CLI help. Generated runtime parsing has similar ad hoc behavior.

**Recommendation:** Use one typed parser with range validation, consistent `--help`, unknown-option errors, nonzero usage exits, and tests for every flag/value edge.

#### P2.6 — Differential logging and test builds can distort or waste work

Debug logging flushes frequently and can perturb the timing being investigated. The accuracy runner also rebuilds the shared runtime separately for each test and runs sequentially. Add buffered/binary trace modes, shared content-addressed runtime builds, and bounded parallel test execution while keeping per-test logs deterministic.

#### P2.7 — Core runtime/platform coupling limits portability and profiling

`gbrt` currently bundles core emulation with SDL/ImGui. Split a platform-neutral `gbrt_core` from SDL/UI adapters. This makes headless tests, fuzzing, WebAssembly/low-end ports, and core-only profiling smaller and less ambiguous.

### P3 — Documentation and project hygiene

1. Refresh `ACCURACY.md`; it is a March snapshot and still describes a DMG-oriented status that conflicts with current CGB work. Generate it only from a provenance-rich successful run.
2. Update or replace `docs/RECOMPILER_CORRECTNESS_AUDIT_PLAN.md`; several proposed items now exist (notably differential mode), while other warnings remain valid.
3. Reconcile README build prerequisites and setup filenames, Android/GBC roadmap claims, duplicate setup sections, and benchmark semantics.
4. Add a root license file matching the README claim and include it in release artifacts.
5. Summarize aggressive-scan diagnostics by confidence/category, restore stream formatting after hexadecimal output, and add `--strict-analysis` / diagnostic thresholds. Heuristic data-as-code findings should not flood error-level output while generation still returns success.
6. Add a Python dependency manifest for PyBoy, psutil, and Pillow, with a tested setup command.
7. Put git SHA/dirty state, commands, ROM hash, generated metadata hash, compiler/profile, and binary hash in benchmark, test, trace, and compatibility artifacts.

## Recommended execution sequence

### Phase 0 — Restore a trustworthy baseline (P0, immediate)

1. Disable/fix the generated fast ROM path (P0.1).
2. Make the test runner fail on errors, zero execution, and incomplete runs; fingerprint all caches (P0.2).
3. Reproduce and bisect the current `reg_f` and `ei_timing` regressions against the last known passing revision/worktree, using fresh generated builds and external serial-test outcomes.
4. Add the first legal synthetic mapper/opcode fixtures and a generated-project CI smoke.
5. Split and truthfully label full-runtime vs core-only benchmarks before using FPS to justify more changes.

**Exit gate:** fresh main build; generated smoke build; selected prior-pass hardware tests restored; test tool deliberately fails on injected errors; ASan MBC1 fast-read case clean; full-runtime benchmark state/frame hashes match the normal headless path.

### Phase 1 — Make banked analysis sound (P0, next)

1. Introduce `BankId` and remove all eight-bit narrowing (P0.3).
2. Remove analyzer shared/static mutation and prove deterministic parallel output (P0.4).
3. Add the abstract-state join/worklist and conservative opcode effects (P0.5).
4. Add mapper-specific state and one physical-ROM resolver shared across analysis, runtime, generated reads, and dispatch (P0.5/P0.6).
5. Fix banked jump-table reads; only then persist resolved direct targets (P1.1).

**Exit gate:** 512-bank fixture, mapper matrix, randomized-worklist determinism, `-j1`/`-jN` byte-identical output, TSAN clean, and no loss in the external accuracy suite.

### Phase 2 — Close the largest runtime hardware gaps (P0/P1)

1. Introduce event-driven device scheduling primitives shared by PPU and DMA.
2. Implement and validate timed HDMA and model/source-aware OAM DMA.
3. Add a correct dot/FIFO PPU reference path, then recover safe batching behind explicit guards.
4. Make APU output chunk-invariant and remove callback races.
5. Make persistence transactional/versioned.

**Exit gate:** targeted Mooneye/Magen/acid2 cases, SameBoy trace agreement on selected repros, stable recorded frame/PCM hashes, TSAN, and persistence fault-injection tests.

### Phase 3 — Optimize measured bottlenecks safely (P1/P2)

1. Speed up differential comparison with hashes/dirty ranges.
2. Profile analysis on small/medium/maximum-bank fixtures; then replace interval/container/scan hot spots.
3. Reduce generated-source and compiler load by binary ROM embedding and streamed chunks.
4. Re-enable direct compiled targets and introduce real IR passes only behind correctness gates.
5. Batch audio-ring publication and split `gbrt_core` after behavior is locked.

**Exit gate:** before/after artifacts include full provenance; accuracy hashes/tests are unchanged; reported speedups distinguish generator time, generated build time, core execution, full-runtime execution, and interactive presentation.

### Phase 4 — Productize the toolchain (P1/P3)

1. Make generated output atomic and errors actionable.
2. Unify generated CMake/runtime integration and build profiles.
3. Validate MSVC/GCC/AppleClang.
4. Ship a self-contained or relocatable release and test it from the archive.
5. Refresh README, accuracy/status documents, license, and Python setup.

## Proposed regression matrix

| Layer | Minimum cases | Required assertions |
| --- | --- | --- |
| Recompiler unit | Every opcode's register effects; address packing; annotations; mapper registers; direct/indirect targets | Deterministic IR/metadata, conservative unknowns, no invalid operand forms |
| Synthetic generated ROMs | ROM-only, MBC1/MBC1M, MBC3, 512-bank MBC5; bank-0 trampolines; mode-1 lower window; multi-ROM parallel | Generation terminates, expected signatures, zero unexpected fallback, `-j1 == -jN` |
| Generated vs interpreter | Fixed instruction budgets and cycle-anchored recorded inputs | CPU/memory/device state equality; injected mismatch localization |
| Independent hardware tests | Curated Blargg/Mooneye/Magen/acid2 DMG+CGB set | Serial pass markers and expected stop condition; no errors/incomplete accepted |
| Runtime media | Known scenes from representative DMG/CGB games | Frame hashes, present-frame hashes, PCM hashes, save round trips |
| Build/release | Single-ROM, multi-ROM, clean archive, supported compilers | Configure/build/run from clean paths; no repository-relative dependency |
| Performance | Generator, generated compile, differential, core-only, full-runtime, interactive | Provenance-rich JSON; accuracy hashes unchanged; no stale binary |

## Highest-value first milestone

A contained first milestone should combine P0.1, P0.2, and the smallest part of P0.3/P0.4:

1. eliminate the unsafe generated ROM read or route it through the runtime resolver;
2. make `run_tests.py` fail correctly and fingerprint generated outputs;
3. add repository-owned MBC1 mode-1 and 256/512-bank MBC5 fixtures;
4. make analyzer scan state invocation-local;
5. add one PR job that builds, generates, rebuilds, and runs those fixtures;
6. rerun `reg_f` and `ei_timing` from fresh output and update `ACCURACY.md` only after they are genuinely passing.

This milestone is intentionally a guardrail milestone. It reduces the risk that subsequent analyzer, PPU, and performance work makes the project faster at producing or executing incorrect output.
