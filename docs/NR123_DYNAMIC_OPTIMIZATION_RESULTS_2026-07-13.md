# NR-1 through NR-3 dynamic optimization results

Date: 2026-07-13

Status: NR-1 deferred after its measured rejection; NR-2 generated-memory fast path retained; NR-3 stable-span PPU retained

## Decision summary

This pass found two worthwhile native-recompilation wins without broadening the IR or weakening hardware timing:

- **Retain NR-2 guarded generated-memory access.** Generated WRAM, banked-WRAM, and HRAM reads and writes bypass the generic decoder when OAM DMA does not own the bus. On Tetris this reduced the four-run full-headless median from 3.06 seconds to 2.91 seconds, a 4.9% improvement. The state hash and strict differential result were identical. The executable grew 3.3%, within the 5% gate.
- **Retain NR-3 stable-span PPU execution.** Sprite-free background/window pixels that stay within one tile row now render as conservative multi-pixel spans. Median full-headless time improved by 18.5% on Tetris, 15.3% on Link's Awakening, and 7.7% on Tetris DX. State, framebuffer, PCM, differential, PPU, and OAM-DMA gates passed.
- **Do not restart NR-1 state localization yet.** The first implementation removed only 0.23% of tick commits, ran about 1% slower, grew generated C by 17.7%, and grew the executable by 5.9%. Fresh sampling also identifies `ppu_tick` as the largest named hotspot. The retained counter and event-deadline infrastructure is sufficient until a materially different superblock shape meets the existing re-entry criteria.
- **Reject the decoded tile-row cache prototype.** It achieved 36.25 million hits against 5.18 million misses in the Tetris run but made the full runtime about 2% slower. It was removed before the retained stable-span implementation.

The pass also fixed an accuracy defect exposed by the mapper-heavy differential gate: generated `ADD/ADC/SUB/SBC/AND/OR/XOR/CP A,(HL)` sampled memory before the final M-cycle. All eight forms now perform the read in the same final-M-cycle phase as the interpreter.

## Measurement provenance

The checkout was dirty because this pass continued the preceding correctness and runtime goals. The hashes below identify the exact local inputs and generated binaries; they are not release claims.

| Field | Value |
| --- | --- |
| Base commit | `cc9f0939d13019fbd755095bf8ba57a08827af39` |
| Working tree | dirty, 51 paths at the measurement snapshot |
| Host | Apple arm64, macOS 26.3 |
| Compiler | AppleClang 17.0.0 (`clang-1700.6.4.2`) |
| Generated build | CMake Release, `-O3 -DNDEBUG`, ThinLTO enabled |
| Runtime profile | full headless, PPU rasterization and APU enabled, host presentation and pacing disabled |
| Trial shape | four interleaved 9,000-frame runs; no scripted input; battery saves removed before each mapper/CGB trial |
| Tetris ROM SHA-256 | `0d6535aef23969c7e5af2b077acaddb4a445b3d0df7bf34c8acef07b51b015c3` |
| Link's Awakening ROM SHA-256 | `ed42628d5cd8c73a6f8c6a8965f19674876a4fa04e616834e26b81b35963ad87` |
| Tetris DX ROM SHA-256 | `d349dc93423c6abcd775d3b6a8797df715a44a42ec837afb21bf17ae43b40a9e` |

This profile is deliberately not `--benchmark`: it retains raster and audio work so the results measure the runtime paths changed here. Local timing and counter artifacts are under `logs/nr123_*_20260713/` and `logs/nr123_*_current.log`.

## Dynamic attribution

The compile-time-gated counters were refreshed from 1,800-frame runs after the ALU bus-phase fix. Percentages use generated memory operations or rendered pixels as their denominator.

| Workload | Tick commits | Safepoints | Specialized reads | Specialized writes | Stable-span pixels | Interpreter fallback |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Tetris | 23,152,898 | 13,558,012 | 99.1% | 49.7% | 86.7% | 0 |
| Link's Awakening | 22,236,284 | 10,534,327 | 96.2% | 59.6% | 59.2% | 0 |
| Tetris DX | 31,188,258 | 3,907,263 | 84.6% | 77.0% | 74.1% | 545 |

Tetris and Link's Awakening show that generated reads already spend very little time in the generic decoder. Tetris DX retains more ambiguous CGB/mapper traffic and known analyzer fallback, so it remains a useful compatibility smoke but not a fallback-free performance oracle.

The PPU path covers enough pixels to explain its gain: 35,966,307 Tetris pixels, 24,565,039 Link's Awakening pixels, and 30,738,874 Tetris DX pixels were rendered inside stable spans. Sprite-bearing lines, window-start edges, startup/stall dots, tracing, tile boundaries, and the final pixel remain scalar.

## NR-1 — deferred, not broadened

The first NR-1 shape is documented in [the event-scheduling prototype result](NR0_EVENT_SCHEDULING_PROTOTYPE_2026-07-13.md). It was removed after the small-DMG gate because its dynamic coverage could not make a 10% full-runtime gain plausible.

Fresh evidence reinforces that decision:

- the rejected batch covered only 84,637 instructions and removed 52,815 of 23.15 million tick commits
- the complete local-state version would need A, flags, SP, PC, and cycle debt, but those additions would not change the low observed register-only coverage
- a live full-headless sample in `logs/nr123_tetris_sample.txt` identifies `ppu_tick` as the largest named hotspot, while the retained NR-3 work delivers the expected double-digit gain

The conservative next-event query and all transition counters remain. Re-enter NR-1 only when profiles identify hot regions capable of a 10% full-runtime improvement, a single local-state frame can avoid duplicated fast/slow bodies, and generated metadata can describe boundaries, flush reasons, and expected coverage.

## NR-2 — guarded memory specialization retained

Generated code now routes `gb_read8` and `gb_write8` through an inline classifier. With no active OAM DMA it directly accesses:

- fixed WRAM at `$C000-$CFFF`
- selected banked WRAM at `$D000-$DFFF`
- HRAM at `$FF80-$FFFE`

ROM, VRAM, OAM, external RAM/RTC, mapper control, MMIO, DMA-restricted access, unusable memory, and all unresolved cases retain the generic helpers. The reduced-workload benchmark-only I/O shortcuts remain separately gated by the existing benchmark flag.

`GBRT_DISABLE_GENERATED_FAST_MEMORY` is an internal compile-time A/B control. On Tetris, with stable-span PPU enabled in both binaries:

| Signal | Guarded fast path | Generic-memory control | Result |
| --- | ---: | ---: | --- |
| 9,000-frame median | 2.91 s | 3.06 s | 4.9% faster |
| Final-state SHA-256 | `62bafcba0198d13c98a6c7e73eb2e3bb06d32f4b94c66441e01fc80b7f4076d0` | identical | pass |
| Strict differential | 500,000 steps, zero fallback | state control | pass |
| Executable | 3,072,296 B | 2,973,384 B | +3.3% |
| Peak observed RSS | 9,240,576 B | 9,175,040 B | +0.7% |

The guarded classifier is retained. More ROM, external-RAM, MMIO, or dispatch specialization should wait for typed address-space proof and a dynamic residual-address profile; the current aggregate read coverage does not justify speculative helpers.

## NR-3 — stable-span PPU retained

The retained implementation keeps the existing dot state machine authoritative. When a run is proven stable, it fetches one background/window tile row and resolves several pixels before returning to the timing loop. It never crosses a tile boundary, window trigger, sprite-bearing scanline, startup/stall region, trace event, or final-pixel mode transition. The scalar helper remains the oracle and can be selected with `GBRT_DISABLE_PPU_STABLE_SPANS` for internal A/B validation.

| Workload | Stable-span median | Scalar median | Improvement | Executable delta | Peak-RSS delta |
| --- | ---: | ---: | ---: | ---: | ---: |
| Tetris | 2.93 s | 3.59 s | 18.5% | 0 B | below scalar by 0.5% |
| Link's Awakening | 3.16 s | 3.73 s | 15.3% | +16,528 B (+0.07%) | +0.1% |
| Tetris DX | 3.87 s | 4.20 s | 7.7% | 0 B | +0.4% |

All three workloads preserved their 1,800-frame state hashes:

- Tetris: `62bafcba0198d13c98a6c7e73eb2e3bb06d32f4b94c66441e01fc80b7f4076d0`
- Link's Awakening: `dd53ccecf460e068fdd8a257d16800c49306efb861d3d528fc9606458ec12b0c`
- Tetris DX: `4bf4c98fec004470ea680ce89e27aa6971fccd232d6b7ca0fa84076c58431bdf`

For Link's Awakening, stable and scalar paths also produced:

- identical frame-120 internal hash `4386C83F`
- identical PPM SHA-256 `c22637bbabd5304424f476401a470db25fe2726a2d870490e00c234319606130`
- identical one-second PCM SHA-256 `42783b00f10c5f80ae717c584c941acbb3514982549346a406ca3bceb11afc52`

The decoded-row cache attempted before stable spans was rejected. Although it had high hit volume, invalidation checks and cache traffic cost more than the repeated decode it replaced. Stable spans win because they remove repeated per-pixel function work and tile fetches while preserving the same hardware-state ownership.

## Accuracy issue found by the gate

Link's Awakening originally diverged at differential step 375,823 at ROM `0:27F7`, opcode `ADD A,(HL)`, with `HL=$FF44` (`LY`). Generated code read `LY` before its eight-cycle timing commit, while the interpreter advanced seven cycles, read on the final M-cycle, then advanced the last cycle. The scalar PPU control failed at the identical step, proving the stable-span code was not the cause.

Generated `ADD`, `ADC`, `SUB`, `SBC`, `AND`, `OR`, `XOR`, and `CP` with `(HL)` now use the final-M-cycle read contract. The code-generation regression asserts the exact `tick 7 / read / tick 1` sequence for all eight operations. The corrected Link's Awakening build matches the interpreter for 500,000 strict steps with zero fallback.

## Verification

- root CMake/Ninja build succeeded
- repository CTest: 31/31 passed
- focused external PPU tests: 13/13 passed with `--rebuild`
- focused external OAM-DMA tests: 6/6 passed with `--rebuild`
- Tetris strict differential: 500,000 steps, zero fallback
- Link's Awakening strict differential: 500,000 steps, zero fallback
- Tetris DX differential: 500,000 steps matched; not run as fallback-free because the workload records 545 existing fallbacks over 1,800 frames
- freshly regenerated Release projects built and ran for small DMG, mapper-heavy DMG, and CGB workloads

The complete 75-ROM catalogue was not rerun for this slice. No claim is made beyond the focused external suites and representative game evidence above; the public accuracy snapshot remains governed by `ACCURACY.md`.

## Next work

1. Add typed address-space facts and residual generic-address profiling before expanding NR-2 into ROM, external-RAM, MMIO, or native indirect-dispatch paths.
2. Extend stable spans only where a new proof and workload justify it: sprite-free CGB attributes are already covered, while sprite-interacting spans and SIMD remain deferred.
3. Re-profile after these two retained wins. NR-1 should resume only if CPU state serialization becomes a leading full-runtime cost and the proposed regions meet the existing coverage re-entry gate.
4. Keep GPU work, cross-function inlining, and a new IR backend behind later evidence. The CPU stable-span path already captures the largest measured raster benefit without adding synchronization or a platform-specific renderer.
