# Native recompilation backlog

Updated: 2026-07-14

Status: technical inventory and experiment history; execution priority moved to the [2026-07-14 strategy reset](NATIVE_RECOMPILATION_STRATEGY_2026-07-14.md)

## 2026-07-14 execution reset

The original backlog correctly identified event scheduling, memory specialization, PPU spans, native replacements, and compact output as useful architectural directions. Its initial execution order did not survive measurement:

- the NR-1 register-only localization/batching shape removed only 0.23% of tick commits and regressed runtime and code size
- NR-2 guarded memory access produced a retained 4.9% Tetris win, but measured read specialization is already 84.6% to 99.1%, limiting blind expansion
- NR-3 stable spans produced the largest retained gains, but a post-win sample still places PPU synchronization and `gb_tick` device work far ahead of dispatch overhead

NL-0 is complete; see [the post-win performance result](NL0_POST_WIN_PERFORMANCE_TRUTH_2026-07-14.md). The measured order is now:

1. cheaper DIV/TIMA advancement
2. lazy PPU synchronization at exact guest-visible events
3. APU event batching as a separate measured slice
4. hot/native and cold/compact output shaping as an independent build/usability lane
5. visibility-aware compiled regions only after a post-scheduler estimator clears the device-commit gate
6. native replacement SDK, followed by per-game native rendering

Do not use the historical “Recommended first goal” below as the next task. It remains here to explain how the rejected NR-1 conclusion was reached.

## Purpose

This backlog turns GB Recompiled from a runtime that executes statically generated CPU functions into an ahead-of-time system that can exploit whole-program knowledge, per-game profiles, and optional native replacements.

The intended reader is a contributor selecting the next optimization milestone. After reading, they should be able to choose a contained slice, understand its dependencies and safety boundaries, and decide from measured evidence whether the implementation should be retained.

The current strategy is **event-driven, observability-proven specialization**:

- make each clock commit cheap, then advance devices only at real hardware-visible event boundaries
- form generated regions from proven memory and device visibility rather than register-only straight-line shape
- specialize memory operations only when their address space and bus behavior are proven
- batch PPU raster work only across spans that cannot be affected by guest-visible events
- keep hot code native while giving cold or uncertain code a compact deterministic path
- expose explicit game-specific replacement points without weakening the generic accurate path

## Current baseline

Refresh this section before starting a milestone. These numbers describe the 2026-07-13 checkout and are not permanent product claims.

| Signal | Current evidence | Interpretation |
| --- | ---: | --- |
| External accuracy catalogue | 71/75 | The four known failures are the two CGB boot-DIV cases and the DMG/CGB unused-I/O cases. An optimization must not add failures. |
| Repository tests | 33/33 counters-off and counters-on | Includes analyzer, mapper, bus-phase, timer, interrupt, HALT, OAM-corruption, PPU, audio, event-deadline, generated-project, release, and NL-0 profiling-tool coverage. |
| Recompiled Tetris core profile | 3,864 FPS, 8.38 MiB peak RSS | Reduced-workload profile; excludes audio and pixel rasterization. |
| PyBoy in the same benchmark report | 4,752 FPS, 61.72 MiB peak RSS | Memory is already a clear advantage; CPU performance is not consistently ahead. |
| Full-headless Tetris, four 9,000-frame runs | 2.93-second median | Retained stable spans improve the matching scalar control's 3.59-second median by 18.5%. |
| Post-span full-headless Tetris sampling | `ppu_tick` 32.3%, `gb_tick` exclusive 15.8%, `gb_dispatch` exclusive 0.8% of main-thread top-of-stack samples | Device advancement and synchronization now outrank dispatcher work; refresh across recorded three-game inputs before implementation. |
| Generated Tetris runtime transitions | 23,152,898 tick commits and 13,558,012 safepoints over 1,800 frames | Generated code still crosses the runtime boundary at nearly every instruction. |
| Generated Tetris memory operations | 4,008,264 specialized and 36,404 generic reads; 23,729 specialized and 24,008 generic writes | Guarded NR-2 paths cover 99.1% of reads but only 49.7% of writes; expand only from typed residual evidence. |
| Generated Tetris source and executable | 10,961,097 bytes of current C; 3,072,296-byte executable | A 32 KiB ROM expands substantially at build time even though resident memory remains low. |

The core-only and full-headless profiles answer different questions. Never compare their FPS directly or present either as interactive performance.

The first event-scheduled localization/batching prototype preserved state but missed its performance and size gates. See [the July 2026 prototype result](NR0_EVENT_SCHEDULING_PROTOTYPE_2026-07-13.md) before proposing another superblock shape. The subsequent NR-2/NR-3 execution pass retained guarded generated-memory access and stable-span PPU rendering; its measurements and limits are in [the NR-1 through NR-3 result](NR123_DYNAMIC_OPTIMIZATION_RESULTS_2026-07-13.md). The follow-up [NL-0 matrix](NL0_POST_WIN_PERFORMANCE_TRUTH_2026-07-14.md) makes timer arithmetic and lazy PPU synchronization the next runtime work and defers compiled regions because only one workload predicts at least 20% fewer device commits.

## Reference model: Unleashed Recompiled

[Unleashed Recompiled](https://github.com/hedge-dev/UnleashedRecomp) is useful as an architectural reference, not as a graphics recipe to copy literally. Its toolchain recompiles Xbox 360 PowerPC code, translates Xenos shaders to host shader code, and supplies native kernel, audio, and modern graphics backends. Its renderer can consume relatively high-level game draw work and avoid console-GPU behavior that a native port does not need.

[XenonRecomp](https://github.com/hedge-dev/XenonRecomp) also demonstrates two techniques that transfer directly:

- guest registers can become native locals so redundant context saves and restores disappear
- generated functions and instruction addresses can expose explicit replacement or hook points

The graphics boundary does not transfer directly. A Game Boy program does not submit shaders or draw calls; it writes VRAM, OAM, palettes, and PPU registers. Moving those low-level operations to a GPU would still emulate the PPU and could add synchronization overhead around a 160 by 144 workload.

The applicable lesson is therefore:

- use whole-program recompilation knowledge to specialize CPU, memory, dispatch, and hardware-event work
- add a native replacement layer so a known game's higher-level engine routines can optionally submit native rendering or use native services
- keep generic hardware rendering as the compatibility and accuracy oracle

## Product modes and contracts

Optimizations and enhancements must have explicit contracts:

| Mode | Contract | Allowed techniques |
| --- | --- | --- |
| Accurate | Preserve hardware-visible state, timing, frame, and PCM behavior | Proven superblocks, event scheduling, guarded memory specialization, stable-span raster batching |
| Optimized | Same observable result as Accurate mode, using more aggressive but verified fast paths | Profile-guided layout, cached decoding, SIMD, hot/cold partitioning |
| Native port | Explicit game-specific enhancements that may intentionally differ from original presentation | Function replacements, native renderer, high-resolution assets, widescreen UI, new platform services |

Accurate mode remains the default compatibility path. Native-port behavior must be opt-in and tied to an exact ROM identity or other equally strong compatibility contract.

## Global engineering rules

- Pan Docs is the primary hardware reference; SameBoy is the implementation reference when behavior is ambiguous.
- Optimize hardware events, not merely elapsed cycle totals. Bus phases, interrupt boundaries, DIV edges, PPU visibility, and DMA restrictions remain observable.
- Retain a slow diagnostic path for single-step, trace, and differential execution.
- Treat generated-vs-interpreter differential mode as a shared-runtime consistency check, not an independent hardware oracle.
- Compare only matching benchmark profiles with identical recorded input.
- Record source revision, dirty state, compiler, build type, runtime flags, ROM and executable hashes, input hash, profile features, trials, and peak RSS.
- Keep generated projects and measurements out of tracked source. Never commit copyrighted ROMs.
- Reject an optimization that wins only by removing modeled work or weakening a test oracle.

## Original milestone inventory

### NR-0 — Establish performance truth and dynamic counters

**Outcome:** Every later optimization has reproducible baselines and can explain why it helped.

Tasks:

- [ ] Define three named profiles: reduced-workload core, full-headless runtime, and interactive presentation.
- [x] Make the NL-0 full-headless artifacts record enabled and disabled subsystems; extend the same schema to the core and interactive runners when those profiles are refreshed.
- [x] Add dynamic counters for runtime tick commits, generated safepoints, generic and specialized memory operations, direct and indirect dispatch, interpreter fallback, PPU dots, PPU stable spans, and audio samples.
  - [x] Retain compile-time-gated tick, safepoint, direct/indirect dispatch, and fallback counters with generated-runtime reporting.
  - [x] Add memory-path, PPU-dot/stable-span, and audio-sample counters.
- [x] Create cycle-anchored recorded inputs for at least one small DMG game, one mapper-heavy DMG game, and one CGB game.
- [ ] Capture cold and warm build time, peak compiler memory, generated source size, executable size, runtime RSS, frame time, and input latency separately.
- [ ] Add a benchmark comparison check that rejects mismatched profiles.

Acceptance:

- A fresh contributor can reproduce the three profiles from one documented entry point.
- Repeated runs report stable hashes and feature flags.
- An optimization report can attribute a change to fewer events or cheaper events instead of reporting FPS alone.

### NR-1 — Event-scheduled superblocks and localized CPU state

**Outcome:** Straight-line generated code behaves like compiled native code rather than repeatedly serializing an emulated CPU after every instruction.

Depends on: NR-0.

Tasks:

- [ ] Introduce a generated local-state frame for CPU registers, flags, PC, SP, and accumulated cycles.
- [ ] Define explicit flush and restore operations at runtime-observable boundaries.
- [x] Expose a conservative next device or interrupt event deadline; generated batching does not consume it after the first transform missed its gate.
- [ ] Accumulate cycles across register-only instructions and commit them before timed memory, control-flow, interrupt, debugging, or device events.
- [ ] Generate native superblocks for proven straight-line regions.
- [ ] Remove redundant stopped and single-step checks from the release fast path while retaining the diagnostic path.
- [ ] Flush state before unknown dispatch, copied-RAM execution, native hooks, savestate capture, and differential comparison.
- [ ] Add verifier rules that reject a superblock when an observable boundary is not represented.

Accuracy hazards:

- STAT and interrupt changes between guest instructions
- timer reload and DIV edge behavior
- OAM DMA and HDMA bus ownership
- CGB double-speed half-cycle carry
- HALT, STOP, EI delay, and interrupt acceptance
- memory operations whose read or write occurs on a specific M-cycle

Initial keep gate:

- Identical final state, frame hashes, PCM hashes, and cycle-anchored replay results.
- Strict differential execution passes with fallback rejection.
- The complete external catalogue remains at least 71/75 with no new failure.
- Median full-headless time improves by at least 10% on two of three representative games, with no regression above 3% on the third.
- Runtime RSS and executable size do not grow by more than 5% without a separately justified tradeoff.
- If the gate is missed, keep the instrumentation and reject or narrow the transform.

Prototype result: the first localization and register-only batching shape was rejected. It was correct on the small DMG gate but about 1% slower, removed only 0.23% of tick commits, grew generated C by 17.7%, and grew the executable by 5.9%. Per the ordered gate, mapper-heavy and CGB expansion did not begin. The retained evidence and re-entry criteria are in [the prototype result](NR0_EVENT_SCHEDULING_PROTOTYPE_2026-07-13.md).

Current disposition: **deferred**. Fresh sampling still places `ppu_tick` ahead of generated CPU state traffic, while the measured NR-2 and NR-3 slices produce real full-runtime gains. Do not retry the duplicated per-region fast/slow shape. Re-enter only through the prototype's coverage, complete-local-frame, metadata, and small-DMG keep criteria.

### NR-2 — Recompiler-informed memory and dispatch specialization

**Outcome:** Proven native memory and control-flow operations bypass generic address decoding while unsafe cases retain the current helpers.

Depends on: sound bank-state analysis and the corresponding correctness roadmap items.

Tasks:

- [ ] Give IR memory operations a typed address-space classification.
- [ ] Specialize immutable ROM reads, fixed-bank reads, WRAM, banked WRAM, and HRAM separately.
  - [x] Retain guarded generated fast paths for fixed WRAM, selected banked WRAM, and HRAM when OAM DMA is inactive.
  - [ ] Add immutable and fixed-bank ROM paths only after typed mapper/address proof and residual-address profiling.
- [ ] Add range analysis so an indirect address can remain on a direct path across a loop when its region is proven.
- [x] Emit guarded fast paths when the region is likely but not statically certain.
- [ ] Keep VRAM, OAM, mapper-control, MMIO, DMA-restricted, and model-dependent operations on timed helpers unless equivalent behavior is proven.
- [ ] Resolve bank-aware direct targets before emitting native calls or tail calls.
- [ ] Convert proven indirect jump tables into native switches with a safe default dispatch.
- [ ] Emit metadata explaining why each operation was direct, guarded, or generic.

Acceptance:

- Synthetic tests cover every specialized region, bank transition, and rejected ambiguous case.
- Direct and generic paths produce identical state under injected boundary conditions.
- Dynamic generic memory and dispatch counts fall on the recorded workloads.
- Matching-profile speed improves without an accuracy, binary-size, or compile-memory regression outside the declared gate.

First result: **retained**. Generated reads are specialized on 99.1% of Tetris, 96.2% of Link's Awakening, and 84.6% of Tetris DX dynamic operations. The guarded Tetris path improves the full-headless median by 4.9% with identical state and strict differential evidence; executable size grows 3.3% and peak RSS 0.7%. `GBRT_DISABLE_GENERATED_FAST_MEMORY` remains as an internal A/B control. See [the execution report](NR123_DYNAMIC_OPTIMIZATION_RESULTS_2026-07-13.md).

### NR-3 — Stable-span PPU execution

**Outcome:** Preserve dot-accurate timing while rendering groups of pixels when guest-visible state cannot change inside the group.

Depends on: NR-0. The event-deadline work from NR-1 should be reused when available.

Tasks:

- [x] Separate the PPU timing state machine from its raster work without duplicating hardware state.
- [ ] Cache decoded 2bpp tile rows and invalidate them on relevant VRAM writes; the first cache prototype was measured and rejected as about 2% slower full-headless.
- [x] Classify the first safe spans by background/window source, palette, tile bank, tile boundary, window edge, and absence of sprite interaction.
- [x] Render stable multi-pixel spans with one tile-row fetch while retaining per-pixel palette resolution.
- [x] Fall back to the dot path around window restarts, object fetches, mid-scanline writes, startup edges, tracing, final-pixel transitions, and model-specific behavior.
- [ ] Start with DMG, then add CGB attributes, palettes, priority, and compatibility mode.
  - [x] Validate the CPU span path on small DMG, mapper-heavy DMG, and a CGB-enhanced game.
  - [ ] Add an explicit DMG-on-CGB compatibility-mode span gate before claiming that mode separately.
- [x] Count stable and fallback dots so performance reports explain coverage.

Acceptance:

- Existing PPU, STAT, OAM, DMA, and model-specific tests remain green.
- Curated mid-scanline and window/sprite cases force the fallback path and retain their hashes.
- Stable scenes produce identical frame, state, and timing evidence against the dot path.
- The full-headless profile improves materially; no claim is based on the core-only profile because that profile disables rasterization.

The first implementation should be CPU based. GPU execution is justified only after stable spans exist and measurement shows that dispatch and synchronization will not dominate the 160 by 144 workload.

First result: **retained**. Stable spans cover 86.7% of rendered Tetris pixels, 59.2% of Link's Awakening pixels, and 74.1% of Tetris DX pixels. Four-run full-headless medians improve by 18.5%, 15.3%, and 7.7% respectively, with matching state and focused PPU/OAM evidence. `GBRT_DISABLE_PPU_STABLE_SPANS` remains as the scalar A/B control. See [the execution report](NR123_DYNAMIC_OPTIMIZATION_RESULTS_2026-07-13.md).

### NR-4 — Native replacement and patch SDK

**Outcome:** Generated projects can replace selected game functions or insert hooks without editing regenerated source.

Tasks:

- [x] Define stable overrideable function entry points with access to the original generated implementation.
- [x] Support pre-call, replacement, and post-call hooks; arbitrary instruction-address hooks remain out of scope for v1.
- [x] Describe hooks through exact-ROM address manifests and generated metadata.
- [x] Require an exact ROM hash and recompiler/runtime contract version before enabling a patch set.
- [x] Keep callback entry/exit on materialized `GBContext` state and preserve guest-call lifetime across safepoints.
- [ ] Expose narrow platform-neutral APIs for input, rendering, audio, persistence, logging, and generated metadata.
- [x] Copy validated user sources and a normalized manifest into each generated project.
- [x] Add a legal synthetic example that replaces a function, calls the original, and survives regeneration and relocation.
- [x] Compile native dispatch completely out of unpatched wrappers and contexts; retain repeated runtime measurement as a release gate.

Acceptance:

- A synthetic generated project can override a function, call its original implementation, and insert a bounded hook.
- Mismatched ROM hashes or contract versions fail closed with a precise diagnostic.
- Accurate projects with no patches retain byte-identical generated behavior and matching performance.
- Hooked execution participates in logging, state capture, and deterministic replay.

First result: **retained tracer bullet**. Exact-ROM manifests, stable function
IDs, C/C++ source packages, pre/replacement/post callbacks, deferred
call-original, runtime ROM validation, and a legal safepoint-spanning fixture are
implemented. Instruction-address hooks and broad platform services remain
deferred until a concrete port needs them. See [the NL-5 result](NL5_NATIVE_PATCH_SDK_RESULT_2026-07-14.md).

### NR-5 — Native rendering and enhancement layer

**Outcome:** Game-specific ports can replace recognized engine rendering work with native presentation without coupling the generic hardware runtime to a GPU API.

Depends on: NR-4. Generic presentation improvements may proceed independently if they remain platform-layer work.

Tasks:

- [ ] Keep scaling, palette correction, LCD simulation, and post-processing in a replaceable presentation backend.
- [ ] Define a native-port rendering contract for extracted tiles, sprites, maps, UI, and higher-resolution assets.
- [ ] Allow a patched game routine to submit native rendering work while retaining a timing-only or shadow hardware path when game logic observes PPU state.
- [ ] Make enhanced rendering, widescreen layouts, and asset packs explicit per-game features.
- [ ] Keep headless testing and Accurate mode independent of GPU availability.
- [ ] Measure transfer, synchronization, pipeline creation, and presentation costs before choosing a graphics backend.

Acceptance:

- Accurate mode remains hardware-rendered and unchanged.
- Native-port mode is clearly identified in logs and artifacts.
- A native renderer cannot silently activate for an unknown ROM revision.
- Headless tests require no graphics device.
- Performance claims include GPU/driver/platform provenance and do not substitute enhanced output for an accuracy oracle.

### NR-6 — Hot/native and cold/compact code generation

**Outcome:** Large ROMs retain native hot-path performance without expanding every conservative or rarely executed target into large C and machine-code bodies.

Depends on: NR-0 and measured hot-path coverage. Re-evaluate after NR-1 and NR-2 because those changes alter code shape.

Tasks:

- [ ] Embed ROM bytes as a binary object or resource instead of a large C initializer.
- [ ] Partition generated functions and blocks into hot, warm, cold, and uncertain groups.
- [ ] Keep proven hot code native and evaluate compact micro-ops for cold or speculative blocks.
- [ ] Distinguish deliberate cold execution from unexpected interpreter fallback in diagnostics.
- [ ] Use function sections and profile-guided layout to reduce instruction-cache pressure.
- [ ] Evaluate direct object or LLVM IR emission as a build-time experiment, not as an assumed runtime win.
- [ ] Measure generated source size, compile wall time, peak compiler memory, link time, executable size, installed size, and runtime RSS independently.

Acceptance:

- Recorded hot scenes do not enter the cold path unexpectedly.
- Cold paths remain deterministic, differential-testable, and safe for banked or copied-RAM execution.
- Build time or footprint improves materially without a hot-scene regression above 3%.
- Release relocation and generated-project portability remain intact.

## Historical first goal

Start with a contained **NR-0 plus NR-1 prototype**, not a repository-wide rewrite:

> Prototype an accuracy-preserving event-scheduled superblock path that keeps CPU state in native locals and batches cycle commits until real hardware-observable boundaries.

Suggested slices:

1. Add dynamic transition and safepoint counters with no semantic change.
2. Localize CPU state inside one generated straight-line region while preserving per-instruction cycle commits.
3. Add cycle accumulation only across register-only operations.
4. Introduce one next-event deadline covering interrupts, timer, and PPU boundaries.
5. Expand to a small DMG game and one mapper-heavy game only if each slice passes the keep gate.

Do not begin with cross-function inlining, GPU work, or a new IR backend. The prototype should determine whether state localization and event scheduling produce a meaningful full-runtime gain before broadening the architecture.

2026-07-13 outcome: completed and rejected at step 4. Instrumentation and the conservative deadline remain; localization and duplicated register-only fast/slow paths do not. Do not proceed to step 5 without a materially different code shape and a new small-DMG keep-gate result.

## Verification matrix for every retained milestone

| Layer | Required evidence |
| --- | --- |
| Repository-owned | Full CTest plus focused synthetic tests for every new invariant and rejected fast path |
| Generated project | Fresh generation, configure, build, 120-frame smoke, deterministic state dump, and zero unexpected fallback |
| Differential | Bounded generated-vs-interpreter run with fallback rejection and injected mismatch coverage where relevant |
| Independent accuracy | Focused hardware ROMs plus a complete provenance-aware catalogue before updating the published snapshot |
| Media | Recorded-input frame hashes and PCM hashes on representative scenes |
| Concurrency | Sanitizer or equivalent evidence for any new worker, callback, or shared cache |
| Performance | Matching profiles, warmups, repeated trials, variance, peak RSS, code/build footprint, and dynamic event counters |
| Portability | Root build, freshly generated build, release relocation when templates or runtime packaging change |
| Documentation | Live backlog, runtime behavior docs, and the relevant correctness or platform status document updated from measured results |

## Shortcuts to reject

- Replacing the accurate PPU with a GPU renderer and calling the result equivalent.
- Batching across instruction boundaries without a hardware-event or bus-phase proof.
- Treating trace coverage as proof that unobserved code is unnecessary.
- Compiling only recorded paths without a safe cold path.
- Directly accessing VRAM, OAM, MMIO, or mapper registers because the address looks constant.
- Comparing a reduced-workload recompiled profile against a full emulator or interactive runtime.
- Adding renderer dependencies to the platform-neutral core or headless tests.
- Starting a wholesale LLVM/backend rewrite before smaller emitted-C prototypes establish the benefit.
- Reporting FPS without state, frame, PCM, feature, input, and provenance evidence.

## Long-term destination

The intended architecture is a layered system:

1. A hardware-faithful generic runtime for compatibility and verification.
2. An event-scheduled AOT executor that specializes CPU, memory, and dispatch behavior using proven whole-program knowledge.
3. A stable native replacement SDK for per-game ports and mods.
4. Optional enhanced rendering and assets that are explicit, versioned, and separable from accuracy claims.
5. A hot/native and cold/compact output strategy that keeps build and memory costs proportional to useful code.

That combination—not GPU acceleration alone—is the durable advantage static recompilation can offer over a conventional emulator.
