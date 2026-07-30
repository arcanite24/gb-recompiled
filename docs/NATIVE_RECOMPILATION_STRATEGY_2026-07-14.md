# Native recompilation strategy reset

Date: 2026-07-14

Status: active strategy; supersedes the execution order in the original native recompilation backlog, while retaining that document as the technical inventory

## Decision

GB Recompiled should stop treating larger generated CPU regions as the primary route to another large performance gain.

The retained NR-2, NR-3, and NL-1 work proves that specialization is valuable when it removes measured runtime work: guarded WRAM/HRAM access improved the Tetris full-headless median by 4.9%, stable-span PPU execution improved three representative workloads by 7.7% to 18.5%, arithmetic DIV/TIMA advancement improved them by another 10.7% to 24.5%, and APU event batching removed 94.3% of APU advancement calls for a further 14.7% to 20.3% three-game gain. NL-4 additionally cut compiler peak RSS by 48.3% to 56.7% through smaller generated chunks and bounded parallelism without a meaningful runtime penalty. The rejected work is equally informative. Register-only cycle batching removed just 0.23% of tick commits, grew generated C by 17.7%, grew the executable by 5.9%, and ran about 1% slower. A decoded tile-row cache also lost about 2% despite a high hit count. NL-2 lazy PPU synchronization removed 61.8% of PPU calls, but yielded only 9.7%, 1.9%, and 1.2% on the representative workloads, so its added scheduler/state complexity was reverted. The post-scheduler NL-3 estimator cleared its commit-reduction gate on only one workload, keeping larger compiled regions deferred.

The revised strategy is:

1. Make hardware advancement event-driven so ordinary compiled instructions do not repeatedly perform device work that has no guest-visible result yet.
2. Form AOT execution regions from proven hardware visibility, not merely from straight-line control flow.
3. Shape generated output around actual hot code and keep cold or uncertain code compact and safe.
4. Build native replacement and enhanced-rendering support as an explicit per-game porting product, not as a shortcut in the accurate runtime.

Accuracy remains the default contract. Every fast path must retain an authoritative scalar or generic path for diagnostics, differential execution, and rejected boundary cases.

## What the completed experiments changed

### Confirmed wins

| Experiment | Full-runtime result | Why it worked |
| --- | ---: | --- |
| Guarded generated WRAM/HRAM access | 4.9% faster on Tetris | Removed generic address decoding from proven common memory traffic without changing device timing. |
| Stable-span PPU execution | 18.5% Tetris, 15.3% Link's Awakening, 7.7% Tetris DX | Removed repeated per-pixel work while leaving the dot state machine and observable boundaries authoritative. |

### Rejected assumptions

| Assumption | Evidence | Consequence |
| --- | --- | --- |
| Register-only batching would remove many runtime transitions | Only 52,815 of 23.15 million Tetris tick commits were removed, or 0.23%. | Do not retry the same region definition with more registers or more duplicated code. |
| Localized CPU state would pay for substantial code growth | The prototype was about 1% slower, with 17.7% more generated C and a 5.9% larger executable. | State localization is an implementation detail, not a milestone; require predicted dynamic coverage first. |
| A high-hit cache is necessarily a win | The decoded-row cache recorded 36.25 million hits and still regressed about 2%. | Count work removed and measure wall time; hit rate alone is not a keep criterion. |
| Dispatch is the main remaining compiled-execution cost | A fresh post-NR-3 Tetris sample placed `gb_dispatch` at only 33 of 4,241 top-of-stack samples, or 0.8%. | Cross-function inlining and dispatcher rewrites are not current priorities. |
| PPU optimization ended with stable spans | The same sample still placed `ppu_tick` at 1,368 samples, or 32.3%. | Reduce synchronization and state-machine invocations before adding another raster cache. |

The post-optimization sample is a directional single-workload profile, not a product-wide attribution result. Its artifacts are `logs/nr123_tetris_postopt_sample.txt` and `logs/nr123_tetris_postopt_sample_run.log`.

## Corrected performance model

On the sampled Tetris full-headless main execution thread, the named exclusive costs were:

| Leaf | Top-of-stack samples | Share |
| --- | ---: | ---: |
| `ppu_tick` | 1,368 | 32.3% |
| `gb_tick` excluding named callees | 668 | 15.8% |
| `gb_audio_step_timed` | 148 | 3.5% |
| `gb_dispatch` excluding generated work | 33 | 0.8% |

Generated execution remains on most stacks, but the optimized binary did not symbolize enough inlined/generated frames to divide the remaining time reliably. The next profiling slice must fix that before making claims about individual generated operations.

The code explains the named costs. A normal `gb_tick` currently:

- commits CPU and frame cycles
- advances the RTC, DMA, and serial engines when active
- advances DIV/TIMA with a per-cycle loop
- synchronizes the PPU at every instruction boundary
- checks frame and interrupt stop conditions
- advances timed audio

The runtime already stores `last_sync_cycles`, synchronizes before PPU-visible memory/register operations, and exposes a conservative next-event deadline. Those are the foundations of a lazy event engine. The current accurate path does not exploit them because it calls `gb_sync` for every non-zero instruction tick, and the PPU reports a one-cycle deadline throughout mode 3.

The new target is therefore not “fewer C function calls” in isolation. It is **fewer device advancements and boundary checks per unit of guest work**, without crossing a hardware-visible event.

## Product lanes

The broader product directions that can proceed alongside or after NL-5 are captured in [Beyond emulation: product directions for GB Recompiled](BEYOND_EMULATION_PRODUCT_DIRECTIONS_2026-07-14.md).

### Lane A — accurate event-driven runtime

This is the highest-priority performance lane. It benefits every generated game and the interpreter while preserving hardware output.

The intended architecture uses one monotonic CPU/system clock plus per-device synchronization state and deadlines. Compiled execution advances the clock. A device is materialized when:

- its next externally visible event is reached
- CPU memory or MMIO observes or mutates that device
- DMA or HDMA changes bus ownership
- an interrupt can become pending
- a frame, audio sample, trace, state capture, or differential boundary requires it

Internal PPU FIFO or raster work is not automatically a guest-visible event. It may be processed later in a proven span as long as mid-scanline writes and reads synchronize first and STAT, LY, interrupt, DMA, and frame transitions are published at the correct instruction boundary.

### Lane B — AOT-aware execution and output shaping

This lane supplies the benefits unique to recompilation.

The rejected NR-1 region included only register-only operations. A replacement experiment may include ROM, WRAM, and HRAM operations whose address class and bus visibility are proven by analysis or the retained NR-2 guards. It must stop before MMIO, VRAM, OAM, mapper control, DMA-sensitive access, EI/HALT/STOP, unknown control flow, a device deadline, or a diagnostic boundary.

Separately, profile-guided layout and a hot/native plus cold/compact output can reduce generated-source size, compiler memory, executable size, and instruction-cache pressure. Recorded coverage is a hint, never proof that cold code is unreachable; a deterministic safe cold path is mandatory.

### Lane C — native ports and enhancements

The Sonic Unleashed-style opportunity belongs here. An exact-ROM patch SDK can replace known game functions with native code, expose higher-level rendering or platform services, and optionally retain a timing-only or shadow hardware path when game logic observes PPU state.

This can produce much larger per-game gains and capabilities than generic emulation optimizations, but it changes the product contract. It must be opt-in, ROM-revision locked, replayable, and clearly identified as Native-port mode. A generic GPU PPU is not the next step: at 160 by 144 pixels it risks adding synchronization while preserving nearly all low-level emulation work.

## Revised execution order

### NL-0 — Post-win performance truth

**Status: complete.** See [the measured NL-0 result](NL0_POST_WIN_PERFORMANCE_TRUTH_2026-07-14.md).

Outcome: make the remaining runtime cost attributable and estimate the maximum coverage of each proposed fast path before implementing it.

Slices:

1. Produce symbolized Release profiles for one small DMG game, one mapper-heavy DMG game, and one CGB game using cycle-anchored recorded inputs.
2. Add low-overhead diagnostic attribution for time/calls or avoided work inside timer, PPU synchronization, audio, DMA/serial, interrupt checks, generated memory classes, and safepoints.
3. Record tick-size, PPU-mode, timer-state, and hardware-visibility-boundary histograms.
4. Add an offline estimator for how many `gb_tick`, `ppu_tick`, and safepoint commits a proposed visibility-aware region would remove without crossing the current deadline.
5. Record build wall time, compiler peak RSS, generated-source size, executable size, runtime RSS, enabled features, hashes, and dirty revision in one comparable artifact.

Keep gate:

- at least 90% of sampled stacks are attributable to a named generated block or runtime subsystem
- repeated profiles use identical ROM, input, feature, and executable hashes
- device attribution identifies enough removable work to justify NL-1 or NL-2, and the region estimator separately gates NL-3
- profiling-off builds show no measurable regression and no additional release dependency

NL-0 is deliberately short. It is a decision instrument, not a new telemetry subsystem.

Result: all three symbolized profiles reached 100% named application-leaf coverage. Timer advancement processed 100% of committed cycles and occupied 10.5% to 23.7% of named leaf samples, while 63.5% to 64.4% of PPU cycles occurred outside draw mode. The next runtime order is NL-1 timer arithmetic followed by NL-2 lazy PPU synchronization. The profiling-off context-layout regression found by the gate was removed; the corrected A/B measured -0.32% with identical state and loadable text/data size.

### NL-1 — Cheaper accurate clock advancement

**Priority: P0. Begin as soon as NL-0 identifies the common `gb_tick` cost.**

Outcome: reduce work within each committed tick before attempting to remove tick boundaries.

First candidate: replace the per-CPU-cycle DIV/TIMA loop with an O(1) or O(number-of-timer-events) advance. The common no-reload interval can count selected-divider falling edges arithmetically; overflow/reload windows and DIV/TAC write glitches retain an exact event or scalar path.

Then evaluate, independently:

- fast paths for inactive RTC, DMA, and serial combinations if attribution shows branch/call cost matters
- batching APU advancement to its next sample or frame-sequencer event, with MMIO synchronization and identical PCM
- constant-cycle tick helpers only if measured call overhead remains significant after device work is reduced

Keep gate for each independent change:

- exact state, frame, PCM, differential, timer, interrupt, DMA, PPU, and CGB double-speed preservation appropriate to the changed subsystem
- at least 5% median full-headless improvement on two representative games, or a smaller win whose implementation and footprint are correspondingly small
- no regression above 2% on the third game
- no executable or runtime-RSS growth above 2% without a separately accepted tradeoff

Result: the arithmetic DIV/TIMA slice is retained. Same-binary scalar-versus-arithmetic trials improved Tetris by 16.1%, Link's Awakening by 24.5%, and Tetris DX by 10.7%, with identical repeated state hashes, deterministic frame/PCM captures, 13/13 focused timer tests, and the required differential evidence. The implementation and evidence are recorded in [the NL-1 result](NL1_ARITHMETIC_TIMER_RESULT_2026-07-14.md). The subsequent NL-2 PPU slice was measured independently and rejected below.

Separate APU result: retained. Accumulating sub-sample APU time and synchronizing at exact sample or MMIO boundaries reduced APU advancement calls by 94.3% and improved the same three workloads by 20.3%, 14.7%, and 18.9%. Eager-oracle PCM/state tests, frame captures, double-speed coverage, and differential runs pass. The implementation and evidence are recorded in [the APU result](APU_EVENT_BATCHING_RESULT_2026-07-14.md).

### NL-2 — Lazy device synchronization and exact event scheduling

**Priority: P0 architectural milestone.**

Outcome: an ordinary instruction advances the global clock without invoking every device when no external event can occur.

Slices:

1. Split PPU deadlines into internal work and guest-visible events. Preserve the scalar dot path as the oracle.
2. Defer PPU synchronization through OAM, HBlank, and VBlank intervals until their exact external deadline or an observing memory/MMIO access.
3. Extend deferral into proven mode-3 spans only after the existing stable-span proof can expose a safe deadline longer than one dot.
4. Integrate timer, interrupt, DMA/HDMA, serial, and frame deadlines into one absolute next-event value.
5. Move APU work onto the same model only after deterministic PCM proves its scheduling invariance.
6. Retain an eager diagnostic mode and compile-time A/B control.

Important invariant: an exact event may be processed at an instruction boundary, but never after the first boundary at which it could affect interrupt acceptance or guest-visible state. The previous inaccurate 256-cycle coarse synchronization is not a valid implementation model.

Keep gate:

- zero new focused or catalogue failures
- identical state, frame, and PCM hashes under eager and lazy controls
- strict differential passes with fallback rejection on DMG workloads
- median full-headless improvement of at least 10% on two of three representative games, with no regression above 3%
- at least 50% fewer PPU synchronization calls outside diagnostic mode, or measured evidence explaining an equivalent reduction in device work

Result: rejected and reverted. The prototype cleared the call-reduction gate with 61.8% fewer Tetris PPU synchronizations and preserved identical repeated state hashes, but the three full-headless improvements were 9.7%, 1.9%, and 1.2%. It therefore missed the required 10% gain on two workloads despite adding cached scheduling state, observer synchronization, a savestate-version change, and a diagnostic control. The implementation was removed and the evidence is recorded in [the NL-2 result](NL2_LAZY_PPU_RESULT_2026-07-14.md). APU batching remains a separate experiment rather than an expansion of NL-2.

### NL-3 — Visibility-aware compiled regions

**Priority: P1; starts only if NL-0 predicts sufficient coverage and NL-2 can consume batched time.**

Outcome: use analysis and guarded address classes to advance multiple safe instructions with one device/event commit.

This is not a revival of the rejected register-only superblock. The region may include proven ROM, WRAM, banked-WRAM, and HRAM traffic and simple control flow. It uses one nonduplicated generated body, one local state frame only where profitable, and explicit metadata for entry guards, deadline budget, exits, flushes, and rejection reasons.

Keep gate:

- the estimator predicts removal of at least 20% of dynamic `gb_tick`, `ppu_tick`, or equivalent device commits on two workloads before code generation changes begin; safepoint removal alone is supporting evidence, not a keep gate
- the implemented path produces at least a 10% median full-headless improvement on two of three games
- generated C and executable size stay within 5%, with no regression above 3% on the third workload
- bus-phase, interrupt, DMA, copied-RAM, trace, savestate, and differential boundaries remain explicit

The required post-scheduler re-profile predicts 37.48%, 17.02%, and 13.05% fewer tick/device commits. APU batching removed audio advancement work but did not coalesce generated tick/PPU/interrupt commit boundaries, so only Tetris clears the 20% eligibility threshold. NL-3 remains deferred without an implementation prototype; see [the measured re-profile](NL3_POST_SCHEDULER_REPROFILE_2026-07-14.md).

### NL-4 — Hot/native and cold/compact output

**Priority: P1, parallel in concept but measured independently from runtime work.**

Outcome: reduce build time and footprint without assuming every discovered byte deserves a large native C body.

Order:

1. Embed ROM data as a binary object/resource and stream generated chunks.
2. Add function sections, hot/cold ordering, and metadata-backed profiles.
3. Evaluate a compact cold representation with deterministic fallback for uncertain or rarely executed code.
4. Consider direct object or IR emission only if emitted-C measurements identify the compiler frontend as the dominant build cost.

Keep gate:

- at least 25% lower generated-source size, compile wall time, or compiler peak RSS on a mapper-heavy/CGB project
- no hot-scene runtime regression above 3%
- unexpected cold execution remains visible and deterministic
- release relocation and generated-project portability continue to pass

Runtime RSS is already a strength and should not be traded away for small build-time gains.

Result: the first NL-4 slice is retained. Reducing generated code chunks from 4 MiB to 1 MiB and bounding generated-target Ninja compilation to eight jobs cut compiler process-tree peak RSS by 48.3% on mapper-heavy DMG and 56.7% on CGB. Cold build wall time increased by 3.2% and 2.6%, generated bytes were effectively unchanged, executable/loadable size moved by at most 0.17%, and 9,000-frame runtime medians moved by only 0.005% and 0.043% with identical final state. The evidence and remaining binary-ROM/hot-cold work are recorded in [the NL-4 result](NL4_GENERATED_BUILD_RESULT_2026-07-14.md).

### NL-5 — Native replacement SDK

**Status: complete tracer bullet.** See [the NL-5 result](NL5_NATIVE_PATCH_SDK_RESULT_2026-07-14.md).

Outcome: exact-ROM function replacement, pre/post hooks, original-call access, deterministic replay, and a legal synthetic example that survives regeneration.

This milestone is worthwhile even if it does not accelerate unmodified games. It converts GB Recompiled from a fast compatibility runtime into a platform for durable native ports, mods, accessibility work, higher-level rendering, and platform integration.

Result: retained. Generated projects accept a strict exact-ROM manifest, expose
regeneration-stable function IDs and metadata, compile C/C++ patch sources, and
support pre/replacement/post plus a safepoint-resumable original disposition.
Unpatched wrappers retain their direct-body shape. A legal 32 KiB fixture proves
deferred post across a frame boundary, C/C++ builds, relocation, precise
generation failures, and runtime rejection of modified embedded ROM bytes.

### NL-6 — Per-game native rendering

**Priority: P3; depends on NL-5 and a chosen game.**

Outcome: a patched game routine submits higher-level native rendering while accurate hardware rendering remains available as a shadow or compatibility path.

Do not start with a generic GPU rasterizer. Start with one legal synthetic program or explicitly selected game, quantify CPU/GPU synchronization, and keep enhanced output separate from accuracy evidence.

## Work explicitly stopped or deferred

- No second register-only or duplicated fast/slow superblock prototype.
- No cross-function inlining campaign until symbolized profiles identify call overhead as material.
- No decoded tile-row cache retry without a different invalidation/data-layout model and a predicted wall-time benefit.
- No broad new memory helpers while reads are already specialized at 84.6% to 99.1% on the measured games; profile residual addresses first.
- No new IR/backend rewrite as a presumed runtime optimization.
- No generic GPU PPU as the next performance milestone.
- No performance gate using Tetris DX as fallback-free evidence until its existing interpreter fallbacks are resolved.

## Next recommended goal

Choose the next goal from two evidence-backed lanes:

1. **Tooling footprint:** binary ROM/resource embedding plus streaming generation. This directly targets the remaining large ROM-array translation unit and recompiler-side peak memory without perturbing accurate runtime scheduling.
2. **Native-port capability:** choose one legal or explicitly selected game and use the retained NL-5 SDK to identify the first higher-level service or rendering replacement. Define its shadow/timing contract before adding GPU or platform APIs.

Do not start NL-3 code generation from the current estimator result. Reconsider it only after a scheduler change that coalesces actual tick/device commits or after two representative full profiles clear the 20% gate.

## Success definition

The project reaches the next level when recompilation changes the amount of runtime work, not merely the language in which an emulator loop is expressed:

- accurate mode advances hardware at exact externally visible events
- generated code crosses those boundaries only when its proven memory and control-flow behavior requires it
- hot code remains native while cold or uncertain code does not dominate build cost
- exact-ROM native ports can replace higher-level game work without weakening the generic compatibility path

That combination can improve runtime speed, build footprint, and modding capability while preserving the memory advantage already demonstrated by the current runtime.
