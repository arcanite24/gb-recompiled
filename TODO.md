# Project backlog

This file tracks open work only. Completed accuracy remediation is summarized in
the [accuracy report](ACCURACY.md) and
[code improvement audit](docs/CODE_IMPROVEMENT_AUDIT_2026-07-12.md). Measured
performance experiments, including rejected approaches, remain in the
[native recompilation strategy](docs/NATIVE_RECOMPILATION_STRATEGY_2026-07-14.md)
and its linked result reports.

## P1 — Accuracy and semantic correctness

- [ ] Resolve `unused_hwio-GS` and `unused_hwio-C` with model-specific readback
  and masking tests.
- [ ] Fix bank-aware direct-target persistence and banked `JP HL` table reads
  without turning conservative unknowns into wrong compiled calls.
- [ ] Speed up differential comparison with hashes or dirty ranges while
  retaining an explicit strict/full-memory mode.
- [ ] Add independent state, frame, or trace oracles so shared runtime bugs
  cannot pass generated-vs-interpreter differential checks.
- [ ] Make battery, RTC, and savestate writes transactional; move savestates
  toward an explicit portable serialized format.

## P1 — Product and build durability

- [ ] Generate projects through a staging directory and atomically replace the
  destination only after every write succeeds.
- [ ] Produce precise errors for short writes, invalid paths, full disks,
  malformed numeric CLI values, and missing option arguments.
- [ ] Replace unconditional GNU/Clang extensions in emitted/runtime code with
  portable compiler abstractions.
- [ ] Run generation, generated-project build, execution, differential smoke,
  and release relocation on ordinary CI pushes and pull requests.
- [ ] Unify single-ROM, multi-ROM, and Android runtime/CMake templates and make
  build-profile defaults consistent.
- [ ] Add a tested Python dependency manifest for PyBoy, psutil, and Pillow.
- [ ] Add Git state, ROM/binary hashes, compiler profile, runtime flags, and
  input hashes to benchmark and accuracy artifacts.

## P1 — Game Boy Color

- [ ] Fix CGB DIV initialization behavior (`boot_div-cgb0` and
  `boot_div-cgbABCDE`).
- [ ] Fix `unused_hwio-C` I/O readback and masking.
- [ ] Complete KEY0/PGB edge behavior and the remaining undocumented CGB I/O
  masks.
- [ ] Replace the FF56 infrared stub with defined behavior or an explicit
  unsupported path.
- [ ] Validate double-speed, HDMA, LCD/STAT, and DMG-on-CGB edge cases against
  Pan Docs and SameBoy.
- [ ] Expand the curated CGB hardware-test and real-game smoke matrix.

## P2 — Performance and maintainability

- [ ] Extend artifact identity and mismatched-profile rejection to refreshed
  core and interactive profiles, including frame-time and input-latency
  evidence.
- [ ] Revisit CPU-state localization only within a visibility-aware region that
  clears the measured coverage gate.
- [ ] Replace linear annotation-range lookup with sorted, merged per-bank
  intervals.
- [ ] Cache byte plausibility and decode results used by aggressive scans.
- [ ] Replace dense analyzer maps and sets with indexed vectors or bitsets where
  measurements justify it.
- [ ] Avoid repeated CFG traversals when building functions and ownership.
- [ ] Stream generated C chunks instead of retaining complete function bodies
  in memory.
- [ ] Evaluate binary ROM embedding to reduce generated-source size and compiler
  memory.
- [ ] Remove or quarantine obsolete emitter, generator, lowering paths, and
  unimplemented public options.
- [ ] Replace magic operand indices such as `(HL)` with typed, exhaustively
  validated operands.
- [ ] Split platform-neutral emulation from SDL/ImGui integration for headless
  tests, fuzzing, WebAssembly, and profiling.

## Product and platform

- [ ] Add per-game configuration to the multi-ROM launcher.
- [ ] Fix double-click launch in the graphical multi-ROM picker.
- [ ] Add a custom Android app icon.
- [ ] Add an optional touch gameplay overlay for Android.
- [ ] Add multi-ROM Android output after the single-ROM lifecycle is stable.
- [ ] Add WebAssembly support after the runtime/platform split.
- [ ] Add shader support without coupling core emulation to a renderer.
- [ ] Benchmark representative low-end hardware with clearly labeled profiles.

## Generated output for modding and ports

- [ ] Export unresolved indirect jumps and detected RAM overlays in metadata.
- [ ] Detect and name safe ROM pointer and byte tables without treating
  arbitrary data as code.
- [ ] Add optional schemas for game-specific tables and enums.
- [ ] Distinguish callable functions from local control-flow entry points.
- [ ] Recover simple structured control flow only behind semantic-equivalence
  tests.
- [ ] Extend the selected-function SDK only from a concrete port and a narrow,
  versioned host-service contract.
- [ ] Make output ordering, chunking, and formatting stable enough for
  reviewable regeneration diffs.

## Documentation and usability

- [ ] Add generated-runtime `--help` with strict unknown-option and
  missing-value errors.
- [ ] Replace frame-sampled trace capture with instruction-level capture, or
  rename the tool and output format to match their actual fidelity.
- [ ] Keep compatibility claims tied to fresh hashes, commands, and artifacts
  instead of an unversioned game list.
