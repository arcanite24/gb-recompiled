# Runtime usage

Each generated project links the recompiled ROM code with the GB Recompiled runtime. This guide covers the generated executable; `gbrecomp` itself has a separate `--help` command.

Examples below use a generated executable named `game`:

```bash
./output/game/build/game
```

## Controls and settings

The default desktop controls are:

| Action | Primary | Alternate |
| --- | --- | --- |
| D-pad | Arrow keys | W/A/S/D |
| Game Boy A | Z | J |
| Game Boy B | X | K |
| Start | Enter | — |
| Select | Backspace | Right Shift |
| Fast forward | Tab | — |
| Toggle maximum speed | Backtick | — |
| Quick save | F5 | — |
| Previous / next state slot | F6 / F7 | — |
| Quick load | F8 | — |
| Debug overlay | F1 | — |
| Native port UI | F2 | Controller R3 |
| Encounter Lens extension | F3 | — |
| Mute | M | — |
| Settings menu | F10 or Escape | — |

Controllers are enabled automatically. The default mapping uses the D-pad or left stick, physical south/east face buttons for Game Boy B/A, and Start/Back for Start/Select. Guide/Home or L3 opens the settings menu; R3 opens the native game panel when the exact-ROM port supplies one. Escape and Android Back also open the settings menu. While a native panel captures input, its D-pad/left-stick and A/B actions do not reach the guest game.

The settings menu can remap keyboard and controller gameplay actions and shortcuts. It also exposes audio output, speed, savestate slots, display smoothing, and diagnostic controls. Preferences are stored in SDL's per-application preference directory.

## Saves and savestates

Battery-backed RAM and MBC3 RTC data are loaded and saved by the SDL platform layer. Savestates provide ten slots per game and are checked against the ROM and state-format version before loading.

Treat savestates as local to a compatible runtime/game build. A runtime update can intentionally reject an older state when internal hardware state changes. Battery `.sav` files are the more durable persistence format.

## Execution options

| Option | Purpose |
| --- | --- |
| `--headless` | Run without normal window presentation; useful for tests and automation |
| `--limit <n>` | Stop after `n` guest instructions |
| `--limit-frames <n>` | Stop after `n` completed guest frames |
| `--model auto\|dmg\|cgb` | Select hardware mode; `auto` uses the cartridge header |
| `--input <script>` | Replay inline input events |
| `--record-input <file>` | Record live input as a cycle-anchored script |
| `--save-dir <directory>` | Place battery RAM, RTC, and savestate files in an existing directory |
| `--data-mod <artifact.gbdm>` | Activate one precompiled, exact-ROM data-overlay artifact without changing the generated executable or embedded ROM |
| `--host-configuration <file>` | Load a canonical applied host configuration for a native-patch package that declares an exact configuration contract |
| `--rtc-unix-time <seconds>` | Use a fixed Unix timestamp for deterministic MBC3 RTC load/save and replay |
| `--ignore-rtc-persistence` | Start RTC registers clean while still loading battery RAM; intended for explicitly isolated test routes |
| `--no-audio` | Disable APU/audio emulation for CPU/PPU-focused runs |
| `--smooth-lcd-transitions` | Re-present completed frames during long guest frames; enabled by default |
| `--no-smooth-lcd-transitions` | Disable the host-side long-frame smoother |
| `--native-presentation native\|original` | Select host or original presentation for native-patch packages; packages default to `native`, unpatched projects to `original` |
| `--port-ui-open` | Start a compiled native port module with its UI shown |
| `--disable-port-module` | Diagnostic failure injection: skip attachment of a compiled port module so exact-ROM native patches can prove they fail closed when their host surface is unavailable |
| `--port-toggle-frame <n>` | Deliver a native-UI toggle after guest frame `n`; repeat in strictly increasing order for multiple toggles |
| `--port-input-frame <n>:<action>` | Deliver `toggle`, `encounters`, `close`, `open-pc`, directional, `accept`, or `back` to the port UI after guest frame `n`; repeat in increasing order |
| `--port-state <file>` | Write port lifecycle/render counters, ordered source-built extensions, and the final renderer-independent command frame as JSON |

`--model cgb` runs a DMG cartridge in CGB compatibility mode. `--model dmg` rejects a CGB-only cartridge.

`--host-configuration` is emitted only when an exact-ROM native-patch manifest
declares a host-configuration contract. The runtime requires canonical JSON,
the declared schema/version and policy identity, and values within the declared
bounds before it creates a guest context. Missing configuration means the
feature is disabled. Malformed, non-canonical, incompatible, or out-of-range
files exit before guest execution. Diagnostics and `--dump-state` expose only
the policy and SHA-256 identity, never the configuration path. Configuration
is host-owned and does not alter battery RAM or the cartridge save schema.
When a compiled native panel applies a new configuration, the runtime validates
the same exact contract, writes canonical JSON through an atomic replacement,
and only then publishes the new live value. Challenge Mode changes take effect
at the next battle boundary, never in the middle of an active battle.

Data overlays are opt-in. Without `--data-mod`, every ROM read uses the
untouched user-provided ROM. With it, the runtime validates the artifact ABI,
exact ROM size and SHA-256, ordered non-overlapping ranges, bounds, and every
expected source byte before guest execution. Any failure exits without
activating a partial or previously loaded overlay. CPU, OAM DMA, and HDMA
cartridge reads then resolve through the immutable overlay; cartridge
identity, persistence compatibility, and the owned ROM allocation continue
to use the original bytes. Live semantic ROM readers see the active overlay.
Use `gbrt_semantic_reader_init_live_original` when a tool explicitly needs the
exact original ROM view.

For modded replay, do not treat `--input` and `--data-mod` as sufficient
provenance. Use the portable replay envelope documented in
[Data-mod packages](DATA_MODS.md). Its driver validates ROM, executable,
generated source inventory, ordered packages and content, schema contract,
artifact, configuration, and embedded input hashes before launching this
executable.

Input entries can be frame anchored, such as `120:S:1`, or cycle anchored,
such as `c4412912:S:8192`. Periodic cycle pulses use
`p<start>-<last-start>/<period>:<buttons>:<duration>`; for example,
`p1000000-2000000/50000:A:10000` presses A for 10,000 cycles every 50,000
cycles. The duration must be shorter than the period. Periodic entries retain
the complete duration of the final pulse and avoid expanding long automation
routes into thousands of parser entries. New recordings use individual cycle
anchors because they preserve the exact observed press duration.

`--save-dir` is intended for isolated replay and test sessions. It fails if the
directory does not already exist, and keeps `.sav`, `.rtc`, and savestate files
out of the user's normal preference directory.

Battery and RTC loads require the exact expected file size and stage data
before changing guest state. A malformed file is rejected visibly, and
automatic persistence writes are suppressed for that process so teardown
cannot overwrite the rejected input. Writes use a `.tmp-v1` transaction,
flush and synchronize its complete bytes, then atomically replace the primary
file where the host supports it. Short writes, failed synchronization, and
failed replacement retain the previous `.sav` or `.rtc`; a stale stage is
ignored by loading and safely truncated by the next successful transaction.
Battery RAM remains a raw cartridge-compatible image. RTC serialization v2
uses a fixed 40-byte, little-endian layout and accepts v1 files for migration.
`--rtc-unix-time` controls the wall-clock elapsed-time component;
guest-executed RTC cycles still advance normally.
`--ignore-rtc-persistence` is not normal play behavior and must be disclosed by
any evidence that uses it.

Exact-ROM source integrations can layer validated state edits on that atomic
persistence boundary with `gbrt_semantic_transaction_begin`, bounded
`gbrt_semantic_transaction_write` calls, a staged-reader validator, and
`commit` or `abort`. Mutable ranges are limited to external RAM and fixed or
banked WRAM. A commit persists the complete staged battery snapshot before
publishing staged bytes to live guest memory; persistence failure therefore
rolls back without exposing a partial edit. The latest transaction sequence,
outcome, and merged dirty ranges appear under `semantic_transaction` in
`--dump-state` JSON for deterministic replay evidence. The lifecycle is a
synchronous safepoint operation: guest execution must remain paused from
`begin` through `commit` or `abort`.

Port ABI v3 exposes that lifecycle to reviewed source-built modules as the
one-shot `run_semantic_edit` service. The runtime begins the transaction,
invokes a callback that may stage and validate bounded semantic records, then
commits only if the callback returns success with a still-active validated
transaction. The callback-scoped transaction must not be retained, committed,
or aborted by the module. Failure is aborted before guest execution resumes.
The same ABI exposes path-free host-configuration identity, a runtime-owned
atomic Apply callback, and explicit modal input capture. It does not grant a
module direct filesystem access.

The three `--port-*` options are present only when the generated project
compiled an exact-ROM port module. They work in `--headless` mode without a
graphics device. `--port-state` reports host/module lifecycle state plus the
ordered active extension identities and final panel/text command frame. Port
state schema v3 adds `input_captured` to the v2 extension inventory; it remains separate from
`--dump-state`, which is guest-only. See
[Port and frontend modules](PORT_MODULES.md) for the ABI and activation rules.
The separate [native presentation contract](NATIVE_PRESENTATION.md) defines
completed-frame map, tile, sprite, UI, timing, and accurate-PPU shadow data
for future exact-game renderers.

## Capture and logging options

| Option | Purpose |
| --- | --- |
| `--log-file <file>` | Redirect runtime output to a log |
| `--trace` | Enable verbose runtime tracing |
| `--trace-entries <file>` | Record executed bank/address entry points |
| `--dump-state <file>` | Write the final machine state as JSON |
| `--save-state-file <file>` | Write a complete binary savestate when execution stops |
| `--load-state-file <file>` | Resume normal execution from a compatible complete savestate; intended for local diagnostics and replay calibration |
| `--dump-frames <list>` | Dump selected guest frames |
| `--dump-present-frames <list>` | Dump every host present associated with selected guest frames |
| `--screenshot-prefix <path>` | Set the output prefix for frame captures |

Keep reproducible artifacts under `logs/`:

```bash
./output/game/build/game \
  --log-file logs/game.log \
  --record-input logs/game.input

./output/game/build/game \
  --log-file logs/game-replay.log \
  --input "$(cat logs/game.input)"
```

## Interpreter fallback diagnostics

Generated execution falls back to the interpreter when the analyzer did not emit a compiled target. Instrument the fallback before assuming it is responsible for a slowdown:

```bash
./output/game/build/game \
  --log-file logs/interpreter.log \
  --limit 500000 \
  --log-frame-fallbacks \
  --report-interpreter-hotspots \
  --interpreter-hotspot-limit 12

python3 tools/summarize_interpreter_log.py logs/interpreter.log
```

| Option | Purpose |
| --- | --- |
| `--log-frame-fallbacks` | Report the first and last fallback site in affected frames |
| `--report-interpreter-hotspots` | Print the complete cause-aware fallback-site inventory plus aggregate interpreter hotspots at shutdown |
| `--interpreter-hotspot-limit <n>` | Limit the aggregate hotspot table |

The fallback inventory is independent of the hotspot limit. Each
`[INTERP] Fallback site` row reports the runtime bank/address, handoff count,
interpreted instruction and cycle totals, first/last frame, reason, and the
number of compiled bank variants at that address. Reasons are:

- `bank_not_compiled`: the address is dispatchable in other banks, but not the
  active bank;
- `address_not_compiled`: the generated page exists, but the address does not;
- `page_not_compiled`: no generated dispatch page exists for the address;
- `writable_hram`: immutable compiled dispatch is unsafe for writable HRAM.

The leading inventory row reports `complete=yes` only when every distinct site
fit in the runtime's diagnostic inventory. Treat `complete=no` or a nonzero
`dropped` count as a failed measurement. Interpreter control-flow exits
(including jumps, calls, returns, restarts, HALT, and STOP) are included in the
per-site instruction and cycle totals.

If the summary says `No interpreter fallback recorded`, investigate runtime/device work instead of analyzer coverage.

## Differential execution

Differential mode advances generated execution and the interpreter from matching initial states and stops at the first detected divergence:

```bash
./output/game/build/game \
  --differential 500000 \
  --differential-log 100000 \
  --differential-fail-on-fallback
```

| Option | Purpose |
| --- | --- |
| `--differential [steps]` | Compare for a bounded number of steps; default is 10,000 |
| `--differential-frames <n>` | Stop after `n` completed frames |
| `--differential-state <file>` | Load the same complete savestate into both comparison contexts |
| `--differential-log <n>` | Print progress every `n` matched steps |
| `--differential-no-memory` | Skip full mutable-memory comparisons for a faster, weaker check |
| `--differential-log-fallbacks` | Log generated-to-interpreter fallback during comparison |
| `--differential-fail-on-fallback` | Treat any fallback as a failure |
| `--differential-inject-mismatch <step>` | Perturb interpreter state at one step to self-test mismatch detection |

This is a recompiler-vs-interpreter consistency check, not an independent hardware oracle: both paths share mapper, PPU, DMA, timer, and audio implementations.

## Performance diagnostics

`--debug-performance` enables the main slowdown signals. For a stable scene, combine it with input recording:

```bash
./output/game/build/game \
  --log-file logs/performance.log \
  --record-input logs/performance.input \
  --debug-performance
```

Individual options include:

- `--log-slow-frames <ms>`
- `--log-slow-vsync <ms>`
- `--log-lcd-transitions`
- `--debug-audio`
- `--debug-audio-seconds <n>`
- `--debug-audio-trace`
- `--audio-stats`

### Generated execution counters

Generated projects can compile diagnostic transition and device-attribution counters for optimization work. They are off by default and do not participate in savestates, state dumps, or differential comparisons.

```bash
cmake -G Ninja -S output/game -B output/game/build-counters \
  -DCMAKE_BUILD_TYPE=Release \
  -DGBRECOMP_ENABLE_PERFORMANCE_COUNTERS=ON
ninja -C output/game/build-counters

./output/game/build-counters/game \
  --headless \
  --limit-frames 1800 \
  --report-performance-counters
```

The final `[PERF-COUNTERS]` record reports whether instrumentation was compiled in; tick commits/cycles; generated safepoints and transitions; generic and specialized generated reads/writes; interpreter fallback; PPU calls/dots/draw work/rendered pixels/stable spans; audio samples; timer, audio, RTC, DMA, and serial activity; interrupt/frame stops; and visibility-region estimates. `[PERF-HISTOGRAM]` records report tick size, PPU mode by calls and cycles, timer state, next-event deadline, visibility boundaries, and estimated region sizes. A normal build accepts the reporting option but prints `available=0` and zero base counters.

Memory counters describe generated memory operations, not every runtime or device access. A specialized operation used a generated WRAM/banked-WRAM/HRAM path or an explicitly enabled benchmark-only I/O path; a generic operation retained `gb_read8`/`gb_write8` and all of its mapper, bus, DMA, MMIO, and model behavior. Stable-span dots are a subset of rendered PPU pixels, while draw dots also include scalar startup, stall, sprite, window-edge, and final-transition work.

The conservative visibility-region estimator is deliberately separate from basic attribution because it is more expensive:

```bash
./output/game/build-counters/game \
  --headless \
  --limit-frames 1800 \
  --report-performance-counters \
  --estimate-visibility-regions
```

It groups only register-only and already-proven safe-memory work, stops at generic memory, transitions, fallback, stopped state, or the current exact deadline, and estimates removable tick/PPU/safepoint commits without changing execution. Use it for one diagnostic capture, not timing trials.

### Timer A/B control

Generated projects use arithmetic DIV/TIMA advancement by default. `--scalar-timer` selects the retained one-T-cycle timer oracle for correctness checks and same-binary performance comparisons; it is diagnostic, not a recommended player setting. The control changes no savestate or generated-code format.

```bash
python3 tools/compare_nl0_controls.py \
  --before output/game/build/game \
  --after output/game/build/game \
  --before-arg=--scalar-timer \
  --input-file tools/profiles/game.input \
  --frames 9000 \
  --repeat 8 \
  --warmup 1 \
  --json-out logs/timer-ab/artifact.json
```

Compare counters only across matching ROMs, inputs, frame limits, models, feature flags, and executable hashes. Their purpose is to explain a timing change, not replace state hashes or independent accuracy tests. Keep counters off for release/runtime measurements.

### Reproducible full-headless attribution

`tools/run_nl0_profile.py` generates one fresh project, builds symbolized counters-off and counters-on Release variants, measures cold/warm build and runtime wall/RSS, runs the estimator separately, checks identical repeated state hashes, samples named leaf coverage on macOS, and writes one provenance-rich JSON artifact. The input must contain only cycle-anchored `c<cycle>:<buttons>:<duration>` entries.

```bash
python3 tools/run_nl0_profile.py \
  --name game-profile \
  --rom path/to/game.gb \
  --gbrecomp build/bin/gbrecomp \
  --project-dir output/game-profile \
  --build-root output/game-profile-build \
  --input-file tools/profiles/game.input \
  --frames 9000 \
  --repeat 4 \
  --warmup 1 \
  --sample-seconds 5 \
  --json-out logs/game-profile/artifact.json
```

The default diagnostic build uses generated `-O3`, debug symbols, frame pointers, and IPO off so stacks remain attributable. `--ipo` creates a separately labeled LTO profile. Summarize raw counter logs with `tools/summarize_nl0_profile.py`; use `tools/compare_nl0_controls.py` for an interleaved profiling-off regression gate against a retained binary snapshot.

Generated Ninja projects use 1 MiB code chunks and cap executable-target compilation at eight concurrent jobs to bound compiler memory without serializing runtime/UI compilation. Override the limit at configure time with `-DGBRECOMP_GENERATED_COMPILE_JOBS=<n>`, or set it to `0` to disable the pool. `tools/profile_generated_build.py` produces comparable cold/warm build and footprint artifacts; the retained measurements are in `docs/NL4_GENERATED_BUILD_RESULT_2026-07-14.md`.

### Audio capture and diagnostics

The APU advances channel events, DIV-driven frame-sequencer edges, and sample deadlines chronologically. Its exact rational sample clock emits 44,100 stereo frames per 4,194,304 Game Boy system cycles, independent of scheduler batch size. This includes HALT fast-forward and CGB double-speed execution.

Normal execution accumulates sub-sample APU time and publishes it at the next exact sample deadline or before an APU/PCM MMIO observer, DIV reset, speed switch, savestate, or diagnostic report. Use `--eager-audio` only for same-binary regression measurements against the former per-tick schedule. The retained gate and artifacts are documented in `docs/APU_EVENT_BATCHING_RESULT_2026-07-14.md`.

`--debug-audio` writes `debug_audio.raw` in the current directory. The file contains interleaved stereo signed 16-bit samples at 44.1 kHz in host byte order. Limit the capture with `--debug-audio-seconds <n>`. The analyzer currently expects that filename in its own working directory, so keep the capture in an artifact directory and run:

```bash
mkdir -p logs/audio-capture
(cd logs/audio-capture && ../../output/game/build/game --headless \
  --limit-frames 720 --debug-audio --debug-audio-seconds 10)
(cd logs/audio-capture && python3 ../../tools/analyze_audio.py)
```

Use `--debug-audio-trace` to write APU register and activity diagnostics to `debug_audio_trace.log`. `--audio-stats` reports queue depth, underruns, and related output metrics. These captures describe the emulated APU stream before host-device resampling or mixing, so they are suitable for deterministic PCM hashes.

## Benchmarking

Use the benchmark helper instead of timing a normal interactive window:

```bash
python3 tools/benchmark_emulators.py path/to/game.gb \
  --recompiled-binary output/game/build/game \
  --frames 1800 \
  --repeat 5 \
  --warmup 1 \
  --json-out logs/game-benchmark.json
```

The helper creates a dedicated optimized generated build by default and runs the executable with `--benchmark`.

`--benchmark` is a reduced-workload, core-oriented profile. It disables pacing and host presentation, audio emulation, final RGB conversion, and pixel rasterization while retaining CPU/device timing work. Results are useful for comparing the same profile across revisions; they are not full interactive-runtime measurements. Use `--no-recompiled-autobuild` only when you intentionally want to measure the exact binary already on disk.

## Test-only options

The accuracy runner uses these options for deterministic test-ROM execution:

- `--stop-on-test-breakpoint` stops when a Mooneye-style `LD B,B` sentinel executes.
- `--dump-state <file>` captures registers, machine state, full HRAM, fixed
  WRAM bank 0 (`C000-CFFF`), switchable WRAM bank 1 (`D000-DFFF`), the first
  256 bytes of available external RAM (`eram_a000_a0ff`), and dispatch
  fallback count for pass/fail inspection. `total_cycles` is the monotonic
  64-bit replay clock; unlike the hardware-facing 32-bit `cycles` field it
  remains ordered after a 32-bit wrap. The explicit WRAM banks make
  symbol-backed game-state checkpoints possible without depending on the
  active `SVBK` value at process exit.

Normal games should not use `--stop-on-test-breakpoint`.
