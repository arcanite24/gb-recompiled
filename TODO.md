# Project backlog

This is the live backlog. Completed P0 audit remediation and its verification evidence are recorded in [the July 2026 code improvement audit](docs/CODE_IMPROVEMENT_AUDIT_2026-07-12.md).

## P1 — Accuracy and semantic correctness

- [x] Model final-M-cycle bus timing for `CALL`, `JP`, `RET`, `RETI`, `RST`, `PUSH`, and `POP`; the 12 affected Mooneye control/stack timing cases now pass.
- [x] Fix timer reload/write edges, including DIV/TAC glitches and the four-cycle TIMA overflow/reload window; the configured timer set is 13/13.
- [x] Fix IE/IF masking, interrupt-entry stack phases/reselection, and CGB double-speed interrupt acceptance; `ie_push`, `if_ie_registers`, and Blargg `interrupt_time` now pass.
- [x] Model `ADD SP,e` and `LD HL,SP+e` immediate-read and idle M-cycles across generated, interpreted, and copied-RAM execution; both configured Mooneye timing ROMs now pass.
- [x] Split `(HL)` reads and read-modify-write stores into their real bus phases across generated and interpreted execution; Blargg `mem_timing-1` and `mem_timing-2` now pass.
- [x] Verify Blargg `halt_bug` through its stable rendered verdict and unify generated/interpreted HALT entry; focused tests cover pending-IRQ fetch suppression, copied-RAM execution, repeated HALT, wake behavior, RST, and interrupt entry.
- [x] Model DMG OAM corruption for direct reads/writes, 16-bit IDU operations, stack phases, and `HL+/-` accesses across generated, interpreted, and copied-RAM execution; Blargg is now 8/8.
- [ ] Resolve `unused_hwio-GS` and `unused_hwio-C` with model-specific readback/masking tests.
- [ ] Fix bank-aware direct-target persistence and banked `JP HL` table reads without turning conservative unknowns into wrong compiled calls.
- [ ] Speed up differential comparison with hashes or dirty ranges while retaining an explicit strict/full-memory mode.
- [ ] Add independent state/frame or trace oracles so shared runtime bugs cannot pass generated-vs-interpreter differential checks.
- [x] Make APU output invariant to scheduler batch size, use an exact 44.1 kHz clock, and cover direct, HALT-heavy, and CGB double-speed execution with deterministic PCM hashes.
- [x] Remove SDL audio callback data races, validate the callback/UI boundary under TSAN, and batch producer publication and callback copies.
- [ ] Make battery, RTC, and savestate writes transactional; move savestates toward an explicit portable serialized format.

## P1 — Product and build durability

- [ ] Generate projects through a staging directory and atomically replace the destination only after every write succeeds.
- [ ] Produce precise errors for short writes, invalid paths, full disks, malformed numeric CLI values, and missing option arguments.
- [ ] Replace unconditional GNU/Clang extensions in emitted/runtime code with portable compiler abstractions.
- [ ] Run generation, generated-project build, execution, differential smoke, and release relocation on ordinary CI pushes and pull requests.
- [ ] Unify single-ROM, multi-ROM, and Android runtime/CMake templates and make build-profile defaults consistent.
- [ ] Add a tested Python dependency manifest for PyBoy, psutil, and Pillow.
- [ ] Add git state, ROM/binary hashes, compiler/profile, runtime flags, and input hashes to benchmark and accuracy artifacts.

## P1 — Game Boy Color

- [ ] Fix CGB DIV initialization behavior (`boot_div-cgb0` and `boot_div-cgbABCDE`).
- [ ] Fix `unused_hwio-C` I/O readback and masking.
- [ ] Complete KEY0/PGB edge behavior and the remaining undocumented CGB I/O masks.
- [ ] Replace the FF56 infrared stub with defined behavior or an explicit unsupported path.
- [ ] Validate double-speed, HDMA, LCD/STAT, and DMG-on-CGB edge cases against Pan Docs and SameBoy.
- [ ] Expand the curated CGB hardware-test and real-game smoke matrix.

## P2 — Recompiler performance and maintainability

- [x] Complete NL-0 symbolized three-game profiles, dynamic device attribution, build/footprint capture, cycle-input provenance, and the conservative region estimator; see [the measured result](docs/NL0_POST_WIN_PERFORMANCE_TRUTH_2026-07-14.md).
- [x] Retain the NL-1 arithmetic DIV/TIMA common path after 10.7% to 24.5% three-game full-headless wins with scalar-oracle, timer, state, frame, PCM, and differential evidence; see [the measured result](docs/NL1_ARITHMETIC_TIMER_RESULT_2026-07-14.md).
- [x] Evaluate lazy PPU synchronization outside mode 3; reject and revert it after 61.8% fewer PPU calls produced only 9.7%, 1.9%, and 1.2% three-game gains, below the keep gate; see [the measured result](docs/NL2_LAZY_PPU_RESULT_2026-07-14.md).
- [x] Retain APU event batching after 94.3% fewer APU advancements and 14.7% to 20.3% three-game full-headless wins with eager-oracle, PCM, state, frame, double-speed, observer, and differential evidence; see [the measured result](docs/APU_EVENT_BATCHING_RESULT_2026-07-14.md).
- [x] Re-profile visibility-aware compiled regions after the retained scheduler wins; defer implementation because 37.5%, 17.0%, and 13.1% predicted commit removal clears the 20% gate on only one workload; see [the NL-3 result](docs/NL3_POST_SCHEDULER_REPROFILE_2026-07-14.md).
- [x] Retain 1 MiB generated chunks and an eight-job Ninja compile pool after reducing mapper-heavy/CGB compiler peak RSS by 48.3%/56.7% with effectively neutral runtime; see [the NL-4 result](docs/NL4_GENERATED_BUILD_RESULT_2026-07-14.md).
- [x] Retain the NL-5 exact-ROM native replacement SDK with stable IDs, fail-closed manifests, C/C++ source packages, safepoint-correct original/post composition, runtime identity checking, relocation, and a legal synthetic example; see [the NL-5 result](docs/NL5_NATIVE_PATCH_SDK_RESULT_2026-07-14.md).
- [x] Execute the first event-scheduled/localized-state prototype and apply its keep gate; [the July 2026 result](docs/NR0_EVENT_SCHEDULING_PROTOTYPE_2026-07-13.md) retains counters and the event deadline but rejects the low-coverage transform.
- [x] Complete the NR-0 generated memory/PPU/audio counters and use them to execute measured NR-2/NR-3 slices; [the July 2026 result](docs/NR123_DYNAMIC_OPTIMIZATION_RESULTS_2026-07-13.md) retains guarded WRAM/HRAM access and stable-span PPU rendering.
- [x] Retain guarded generated fast paths for WRAM, banked WRAM, and HRAM after a 4.9% Tetris full-headless win with identical state and strict differential evidence.
- [x] Retain conservative sprite-free background/window PPU spans after 18.5%, 15.3%, and 7.7% full-headless wins on small DMG, mapper-heavy DMG, and CGB workloads.
- [ ] Extend NL-0 artifact identity and mismatched-profile rejection to refreshed core and interactive profiles, including separate frame-time and input-latency evidence.
- [ ] Revisit CPU state localization only as an implementation detail of a visibility-aware region that passes the new coverage gate; do not retry the rejected register-only shape.
- [ ] Replace linear annotation-range lookup with sorted/merged per-bank intervals.
- [ ] Cache byte plausibility/decode results used by aggressive scans and avoid repeated whole-ROM passes.
- [ ] Replace dense `map`/`set` analyzer state with indexed vectors/bitsets where measurements justify it.
- [ ] Avoid repeated CFG traversals when building functions and ownership information.
- [ ] Stream generated C chunks instead of retaining complete function bodies in memory.
- [ ] Evaluate binary ROM embedding to reduce generated-source size and compiler memory.
- [ ] Remove or quarantine obsolete emitter/generator/lowering paths and unimplemented public options.
- [ ] Replace magic operand indices such as `(HL)` with typed, exhaustively validated operands.
- [ ] Split platform-neutral emulation from SDL/ImGui integration for headless tests, fuzzing, WebAssembly, and profiling.

## Product and platform work

- [ ] Add per-game configuration to the multi-ROM launcher.
- [ ] Fix double-click launch in the graphical multi-ROM picker.
- [ ] Add a custom Android app icon.
- [ ] Add an optional touch gameplay overlay for Android.
- [ ] Add multi-ROM Android output only after the single-ROM lifecycle is stable.
- [ ] Add WebAssembly support after the runtime/platform split.
- [ ] Add shader support without coupling core emulation to a renderer.
- [ ] Benchmark representative low-end hardware with clearly labeled profiles.

## Generated output for modding and ports

Already available:

- [x] `.sym` function, internal-label, RAM/HRAM, and ROM-data naming
- [x] trusted `function`, `label`, and `data` annotations
- [x] deterministic sanitized names with provenance
- [x] `*_metadata.json` sidecars for emitted symbols and addresses
- [x] exact-ROM stable function IDs, patchability metadata, and generated native ID headers
- [x] pre/replacement/post native bindings with deferred call-original support

Next:

- [ ] Export unresolved indirect jumps and detected RAM overlays in metadata.
- [ ] Detect and name safe ROM pointer/byte tables without treating arbitrary data as code.
- [ ] Add optional schemas for game-specific tables and enums.
- [ ] Distinguish callable functions from local control-flow entry points.
- [ ] Recover simple structured control flow only behind semantic-equivalence tests.
- [ ] Extend the selected-function SDK only from a concrete port: narrow input, rendering, audio, or persistence services remain future versioned interfaces.
- [ ] Make output ordering, chunking, and formatting stable enough for reviewable regeneration diffs.

## Documentation and usability

- [ ] Add generated-runtime `--help` with strict unknown-option and missing-value errors.
- [ ] Replace frame-sampled “ground truth” capture with instruction-level trace capture, or rename the tool and its output format to match its actual fidelity.
- [ ] Keep compatibility claims tied to fresh hashes, commands, and artifacts instead of an unversioned game list.
- [ ] Add a logo or new screenshots only when they represent the current runtime.
