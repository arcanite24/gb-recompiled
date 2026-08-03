# Recompiler correctness roadmap

Updated: 2026-07-13

## Goal

Keep these layers semantically aligned:

1. ROM decoding
2. analyzer state and bank resolution
3. IR and generated C
4. interpreter fallback
5. mapper and device timing

Coverage is necessary but insufficient. A compiled address can still use the wrong operand, bank, cycle phase, flag behavior, or hardware side effect.

## Current foundation

The original correctness audit led to several completed safeguards:

- generated-vs-interpreter differential execution with CPU, memory, mapper, and device-state comparison
- fail-closed external-ROM testing with deterministic final-state dumps
- repository-owned mapper, analyzer, bus-phase, PPU, DMA, test-policy, and release fixtures
- 16-bit bank identities through analysis, metadata, generation, runtime mapping, and fallback
- mapper-specific analyzer state and conservative control-flow joins/call clobbering
- shared mapper-aware ROM resolution for runtime and generated reads
- final-M-cycle placement for generated and runtime memory operations covered by focused tests
- shared bus-phase primitives for stack and control-flow operations across generated, interpreted, and copied-RAM execution
- shared M1 immediate reads and idle M-cycles for `ADD SP,e` and `LD HL,SP+e` across generated, interpreted, and copied-RAM execution
- explicit final-read and read-modify-write bus phases for generated and interpreted `(HL)` operations, including generated ALU reads and start-of-final-M-cycle RMW stores
- ordered TIMA overflow/reload and DIV/TAC edge behavior
- phased interrupt entry with IE/IF write conflicts, source reselection, and CGB double-speed half-cycle preservation
- one shared generated/interpreted HALT transition contract, with canonical one-fetch PC suppression for the pending-interrupt HALT bug
- DMG-only OAM corruption with exact read/write patterns, PPU-row phase, unusable-range accesses, and shared 16-bit/stack/`HL+/-` bus primitives
- dot/event-aware PPU phases and model/source-aware OAM DMA

Fresh validation on 2026-07-13 produced:

- 31/31 repository-owned CTest cases passing
- 71/75 configured external ROM tests passing
- 13/13 configured PPU cases passing
- 6/6 configured OAM DMA cases passing
- 14/14 affected control/stack/SP-relative timing cases and 13/13 configured timer cases passing
- Blargg CPU/instruction/interrupt/memory/OAM behavior at 8/8

See `ACCURACY.md` for the external-ROM breakdown and `docs/CODE_IMPROVEMENT_AUDIT_2026-07-12.md` for P0 implementation evidence.

## Remaining correctness risks

### 1. CGB boot and I/O edges

Timer reload/write interactions, IE/IF masking, phased interrupt entry, and double-speed interrupt acceptance now have focused regressions and pass their configured external cases. The remaining nearby failures are boot-state and undocumented-I/O cases.

Next work:

- distinguish configured post-boot state from actual boot-ROM execution for `boot_div-cgb0` and `boot_div-cgbABCDE`
- complete model-specific masks/readback for `unused_hwio-GS` and `unused_hwio-C`
- validate KEY0/KEY1 and speed-switch phase behavior against Pan Docs and SameBoy

### 2. Bank-aware direct targets

Unknown targets currently force safe dispatch in several places. That is slower but safer than persisting an unsound bank.

Next work:

- fix physical-ROM reads used by banked indirect-jump table analysis
- persist direct CALL/JP target banks only when mapper state proves them
- add same-address/different-bank table fixtures
- require zero new external-test failures before enabling faster direct paths

### 3. Operand representation and duplicate semantics

Magic operand indices and obsolete emitter/lowering paths still make it easy to fix the wrong implementation or mishandle `(HL)`-style operands.

Next work:

- replace magic register values with typed operand variants
- add an IR verifier with exhaustive instruction/operand validation
- delete or isolate unused emitter, generator, and lowering paths
- remove declared analyzer/optimizer options that are not implemented

### 4. Validation independence and cost

Differential execution is valuable but compares two paths that share mapper and device code, and full mutable-memory comparison is expensive.

Next work:

- use region hashes or dirty ranges as a fast mismatch gate
- retain an explicit strict full-memory mode
- add injected-mismatch tests that prove first-divergence localization
- compare selected state and frame hashes against independent hardware tests or SameBoy
- retain the scheduler-chunk-invariant PCM hashes as a runtime regression oracle

### 5. Trace-assisted discovery quality

The current PyBoy “ground truth” tool samples one PC per frame, while the comparison tool checks only whether those addresses appear in generated comments.

Next work:

- replace frame sampling with instruction-level trace capture where the reference API permits it
- record mapper bank and input provenance with every trace
- rename output and reporting so sampled coverage is not presented as semantic ground truth
- prefer trusted symbol/annotation boundaries over heuristic trace seeding when available

## Required validation ladder

Every correctness change should use the smallest relevant layers and finish with an independent signal:

| Layer | Required evidence |
| --- | --- |
| Unit/synthetic | Exact state, mapper, bus-phase, or metadata assertions |
| Generated smoke | Fresh generate, configure, build, and bounded headless run |
| Differential | First-divergence-free bounded run with unexpected fallback rejected |
| External ROM | Relevant Mooneye/Blargg case reaches its real pass protocol |
| Game-specific | Cycle-anchored replay plus state/frame/audio artifact where applicable |

Do not use a coverage percentage, successful compilation, or interpreter agreement alone as a hardware-accuracy claim.

## Completed slice: SP-relative timing

The SP-relative pair now samples its signed immediate late in M1 and retires the documented idle cycles through shared runtime primitives. Focused tests cover both sides of an OAM-DMA completion boundary, interpreter execution at `FDFF/FE00`, copied-HRAM execution, and generated calls without duplicate aggregate ticks.

Measured separately from correctness on the regenerated Tetris project:

| Metric | Before | After | Change |
| --- | ---: | ---: | ---: |
| Optimized benchmark FPS, 1,800 frames, 5 trials | 3,808.4 | 3,820.3 | +0.31%, within run variance |
| Default generated executable | 2,078,360 B | 2,078,376 B | +16 B |
| Optimized benchmark executable | 3,173,160 B | 3,173,176 B | +16 B |
| Generated Tetris C sources | 10,546,784 B | 10,545,772 B | -1,012 B |
| Peak benchmark RSS | 8.41 MiB | 8.45 MiB | +48 KiB, within process noise |

Raw benchmark artifacts are `logs/sp_relative_before.json` and `logs/sp_relative_after.json`; the fallback-rejecting 100,000-step comparison is `logs/sp_relative_differential.log`.

## Completed slice: memory bus timing

Blargg `mem_timing-1` exposed two distinct omissions: `BIT b,(HL)` sampled memory eagerly, and all `(HL)` read-modify-write operations collapsed their read and write into one host-side action. Generated code and interpreter fallback now place the read in its documented machine cycle and commit the RMW store at the start of the final M-cycle. Focused tests cover `BIT`, `INC`, and CB-prefixed RMW behavior across a live PPU access boundary, plus the exact LCD-enable retirement edge that previously regressed `intr_2_mode0_timing_sprites`.

A later mapper-heavy differential gate exposed the same eager-read class in generated `ADD/ADC/SUB/SBC/AND/OR/XOR/CP A,(HL)`. Link's Awakening diverged while reading `LY` at ROM `0:27F7`: generated code sampled before its timing commit, while the interpreter read on the final M-cycle. All eight generated ALU forms now emit `tick 7 / read / tick 1`, the focused code-generation test covers each form, and the corrected game matches strict differential execution for 500,000 steps with zero fallback.

`mem_timing-2` does not emit a serial verdict, so the fail-closed runner now verifies its stable completed-frame hash (`9E0E8400`) at frame 299. A complete rebuild produced 69/75 configured external passes: both memory-timing ROMs pass, the 14/14 control/stack/SP-relative, 13/13 PPU, 6/6 OAM DMA, and 13/13 timer preservation sets remain green, and the six unrelated known failures are unchanged.

Measured separately from correctness on a freshly regenerated Tetris project:

| Metric | Before | After | Observed change |
| --- | ---: | ---: | ---: |
| Reduced-workload benchmark FPS, 1,800 frames, 5 trials | 3,404.5 | 3,489.1 | +2.48%; an observed run result, not an interactive-runtime claim |
| Measured generated executable | 2,078,376 B | 2,078,376 B | 0 B |
| Generated Tetris C/H sources | 10,680,966 B | 10,716,208 B | +35,242 B (+0.33%) |
| Peak benchmark RSS | 8,650,752 B | 8,699,904 B | +49,152 B |

Raw benchmark artifacts are `logs/mem_timing_tetris_before.json` and `logs/mem_timing_tetris_after_final.json`. The fresh generated-project state is `logs/mem_timing_smoke_final.state.json`, the fallback-rejecting 100,000-step comparison is `logs/mem_timing_differential_final.log`, and the complete catalogue run is `logs/mem_timing_full_accuracy_final.stdout`.

## Completed slice: HALT bug verification and fetch contract

Blargg `halt_bug` was a test-protocol false negative: a baseline frame capture already rendered all nine result rows followed by `Passed`, but the ROM publishes no serial verdict. The fail-closed runner now verifies its stable completed frame 299 (`28BBA01F`), and its policy test pins that name/frame/hash mapping so an empty serial stream cannot silently become either a false pass or a false failure.

The implementation pass still found a real uncovered edge. A pending HALT bug could enter the copied-WRAM/HRAM fast path before the interpreter's special opcode fetch, allowing a supported RAM instruction to skip PC suppression and leave `halt_bug` armed. HALT-bug execution now bypasses copied-code fast paths for exactly one opcode fetch. Generated HALT and interpreter HALT also call the same `gbrt_execute_halt` transition, which owns post-fetch PC, `IME`/`IE`/`IF` evaluation, halted/bug state, and retirement cycles.

The focused regression covers opcode-as-immediate duplication, repeated HALT re-arming, IME-clear wake without interrupt service, RST pushing the RST address, IME-set interrupt entry from the post-HALT PC, and generated use of the shared contract. A full rebuild produced 70/75 configured external passes: Blargg improves from 6/8 to 7/8, all four Mooneye HALT cases remain green, and the 14/14 control/stack/SP-relative, 13/13 PPU, 6/6 OAM DMA, 13/13 timer, and both memory-timing preservation sets remain green.

Measured separately from correctness on a freshly regenerated Tetris project:

| Metric | Before | After | Observed change |
| --- | ---: | ---: | ---: |
| Reduced-workload benchmark FPS, 1,800 frames, 5 trials | 3,879.2 | 3,864.7 | -0.37%, within run variance |
| Default generated executable | 2,078,376 B | 2,078,392 B | +16 B |
| Optimized benchmark executable | 3,173,176 B | 3,173,240 B | +64 B |
| Generated Tetris C/H sources | 10,716,208 B | 10,708,741 B | -7,467 B (-0.07%) |
| Peak benchmark RSS | 8,880,128 B | 8,798,208 B | -81,920 B, within process noise |

Raw benchmark artifacts are `logs/halt_bug_tetris_before.json` and `logs/halt_bug_tetris_after.json`. The fresh generated-project state is `logs/halt_bug_tetris_smoke_final.state.json`, the fallback-rejecting 500,000-step comparison is `logs/halt_bug_tetris_differential_final.log`, the focused external run is `logs/halt_family_after_shared_contract.json`, and the complete catalogue run is `logs/halt_bug_full_accuracy_final.stdout`.

## Completed slice: DMG OAM corruption

The runtime now models the DMG-family OAM corruption bus behavior described by Pan Docs, using SameBoy for the revision-specific combined-read formulas and phase details. Direct reads and writes across `$FE00-$FEFF`, 16-bit `INC`/`DEC`, stack reads/writes, and `LD A,(HL+/-)` / `LD (HL+/-),A` trigger the correct read, write, or combined pattern for the PPU's active eight-byte row. The first row, LCD startup scan, end-of-scan boundary, and CGB no-corruption behavior are explicitly gated.

Generated code, interpreter fallback, and copied-RAM execution share timed primitives for the operations whose bus sequencing is genuinely identical. Dedicated auto-index IR operations preserve the `HL` address/update phase instead of lowering it into an eager memory access plus an unrelated zero-cycle increment. Direct accesses to the unusable `$FEA0-$FEFF` range now retain their documented corruption side effect while still returning `$FF` or discarding the write.

The accuracy runner forces the CGB-compatible `oam_bug` ROM to DMG hardware and decodes Blargg's signed `$A000-$A003` external-RAM verdict from the state dump. The aggregate ROM now reports `00 DE B0 61` and `Passed`. Seven finite individual ROMs also report their signed pass verdict; the verbose timing sweep's first 2,646 protocol bytes match SameBoy exactly, providing an independent bounded oracle before its individual text buffer wraps.

Fresh preservation evidence:

- 27/27 repository-owned CTest cases
- 8/8 Blargg, including both memory-timing ROMs and `oam_bug`
- 5/5 HALT, 14/14 control/stack/SP-relative, 13/13 PPU, 6/6 OAM DMA, and 13/13 timer cases
- 71/75 complete configured external catalogue; only the excluded CGB boot-DIV and DMG/CGB unused-I/O cases fail
- fresh 120-frame generated Tetris smoke with zero dispatch fallbacks
- 500,000 generated-vs-interpreter steps matched across 57 frames with fallback rejection enabled

Measured separately from correctness on freshly regenerated Tetris output:

| Metric | Previous slice | OAM slice | Observed change |
| --- | ---: | ---: | ---: |
| Reduced-workload benchmark FPS, 1,800 frames, 5 trials | 3,864.7 | 3,804.2 | -1.57%; the unchanged previous binary rechecked at 3,752.7 FPS, so this is within observed cross-run variance |
| Default generated executable | 2,078,392 B | 2,029,096 B | -49,296 B (-2.37%) |
| Optimized benchmark executable | 3,173,240 B | 3,053,960 B | -119,280 B (-3.76%) |
| Generated Tetris C/H sources | 10,708,741 B | 10,669,184 B | -39,557 B (-0.37%) |
| Peak benchmark RSS | 8,798,208 B | 8,896,512 B | +98,304 B; within process noise |

Raw benchmark artifacts are `logs/halt_bug_tetris_after.json`, `logs/oam_bug_tetris_before_recheck.json`, and `logs/oam_bug_tetris_after.json`. Generated smoke and differential evidence are `logs/oam_bug_tetris_smoke.state.json` and `logs/oam_bug_tetris_differential.log`. The independent timing prefix proof is `logs/oam_bug_timing_effect_prefix_sha256.txt`; the complete catalogue is `logs/oam_bug_full_accuracy_final.stdout` and `ACCURACY.md`.

## Completed slice: runtime audio scheduling and callback boundary

The APU now processes channel advancement, sample deadlines, and DIV-driven frame-sequencer edges in chronological order. CPU cycles are converted to CGB system cycles while preserving the speed-switch remainder, and an exact rational sample accumulator emits 44,100 samples per 4,194,304 system cycles. DIV reset clocks at most the one falling edge documented by Pan Docs. Repository-owned regression coverage compares coarse and fine direct stepping, HALT-sized runtime batches, and CGB double-speed batches with exact PCM hashes and serialized final APU state.

The SDL producer/callback boundary now uses atomic mute and volume controls, callback-local underrun aggregation, and main-thread stats publication. The callback copies contiguous ring spans, while the producer publishes its write position once per 32 audio frames. Device reset and reopen continue to use SDL's audio lock/close lifecycle.

Fresh preservation and runtime evidence:

- repository-owned CTest covers deterministic PCM hashes, exact one-second sample count, DIV reset, HALT-heavy stepping, CGB double speed, PCM block integrity, and producer/callback concurrency
- a two-million-frame callback stress completed under ThreadSanitizer without a diagnostic
- a freshly generated Tetris project completed 120 headless frames with zero dispatch fallbacks and matched 500,000 generated-vs-interpreter steps across 57 frames with fallback rejection enabled
- two non-silent 10-second generated-runtime captures were byte-identical with SHA-256 `b684c7a8a5c18196e05514fc5bb28506dd44879a9ecdc1cf7aa6b5ffba013754`
- the complete external catalogue remains 71/75 with the same `unused_hwio-GS`, `boot_div-cgb0`, `boot_div-cgbABCDE`, and `unused_hwio-C` failures

Measured separately from correctness:

| Metric | Before | After | Observed change |
| --- | ---: | ---: | ---: |
| Producer write-position publications, 20 million-frame stress | about 19.9 million | about 0.62 million | about 32x fewer |
| Synthetic pipeline warm-run user CPU | 0.22-0.24 s | 0.10-0.11 s | about 55% lower |
| Synthetic pipeline warm-run wall time | 0.12-0.13 s | about 0.06 s | about 54% lower |
| Reduced-workload core benchmark FPS, 1,800 frames, 5 trials | 3,804.2 | 3,922.3 | no observed regression; this profile disables audio |

The batching profile isolates the in-process producer/callback pipeline and must not be presented as full interactive-runtime performance. Generated smoke and differential artifacts are under `logs/audio_runtime_20260713/`; the core benchmark is `logs/audio-runtime-core-benchmark-20260713.json`.

## Next contained slice

Resolve `unused_hwio-GS` and `unused_hwio-C` as one model-specific I/O readback goal. Inventory each failing register/bit against Pan Docs first and SameBoy second, add DMG/CGB intermediate read/write regressions, and preserve the 8/8 Blargg and existing timing/device sets. Keep CGB boot DIV initialization and bank-analysis work outside that goal.
