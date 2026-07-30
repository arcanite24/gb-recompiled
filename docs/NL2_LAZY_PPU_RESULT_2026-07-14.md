# NL-2 lazy PPU synchronization result

Date: 2026-07-14

Status: rejected and reverted

## Decision

Do not retain lazy PPU synchronization outside mode 3 in its tested form. Proceed to the separate APU event-batching experiment.

The prototype did what it was intended to do mechanically: it reduced `ppu_tick` calls by 61.84% on the instrumented Tetris run while preserving the same PPU dots and every other reported transition/device count. However, that reduction did not translate into the required broad full-runtime gain:

| Workload | Eager median | Lazy median | Delta |
| --- | ---: | ---: | ---: |
| Tetris, DMG | 2.9065 s | 2.6246 s | **-9.70%** |
| Link's Awakening, mapper-heavy DMG | 2.3297 s | 2.2845 s | **-1.94%** |
| Tetris DX, CGB | 3.9226 s | 3.8747 s | **-1.22%** |

The NL-2 gate required at least 10% on two of three workloads. None cleared 10%, and two were near 2%. The implementation added scheduling state, observer synchronization, a savestate-version change, and a diagnostic control, so the narrow gains do not justify retaining that complexity.

## Prototype boundary

The measured implementation:

- retained eager PPU publication behind a same-binary `--eager-ppu` oracle
- kept mode 3 eager and deferred only OAM, HBlank, and VBlank spans
- cached the next exact PPU event deadline after reset, PPU advancement, and PPU-register writes
- published deferred state at the first instruction boundary reaching that deadline
- synchronized before PPU MMIO mutation and before observers such as LY reads, frame/state capture, framebuffer access, HALT fast-forward, and DMG OAM-corruption row selection
- adjusted the combined event deadline for PPU work already pending since the last synchronization

An initial version recomputed the PPU deadline on every instruction boundary and slightly regressed the Tetris smoke. Caching the deadline removed that overhead and produced the results above, but it did not make the improvement portable across the representative workloads.

All NL-2 runtime, generated-CLI, context-layout, and savestate-format changes were reverted after the gate failed. The retained tree therefore has no `--eager-ppu` option and continues to publish PPU state at every ordinary instruction boundary.

## Measurement method

Each workload used a freshly generated counters-off Release project with generated `-O3`, frame pointers, and IPO off. One warmup and eight interleaved trials ran for 9,000 guest frames using the existing cycle-anchored input. Eager and lazy trials used the same executable SHA-256; only `--eager-ppu` differed. All repeated eager/lazy trials produced the same final-state hash within each workload.

Raw artifacts:

- `logs/nl2_lazy_tetris_9000_20260714/artifact.json`
- `logs/nl2_lazy_zelda_9000_20260714/artifact.json`
- `logs/nl2_lazy_tetrisdx_9000_20260714/artifact.json`
- `logs/nl2_lazy_tetris_smoke_20260714/eager-counters.log`
- `logs/nl2_lazy_tetris_smoke_20260714/lazy-counters.log`

`logs/` and generated projects under `output/` are local evidence, so the decision-relevant results are preserved here.

## Synchronization evidence

The 1,800-frame Tetris counters-on comparison reported:

| Counter | Eager | Lazy | Change |
| --- | ---: | ---: | ---: |
| `ppu_tick_calls` | 23,193,690 | 8,850,916 | **-61.84%** |
| `ppu_dots` | 126,391,988 | 126,391,988 | unchanged |
| `ppu_draw_dots` | 44,714,928 | 44,714,928 | unchanged |
| `ppu_rendered_pixels` | 41,472,000 | 41,472,000 | unchanged |
| tick commits | 23,193,690 | 23,193,690 | unchanged |

This clears the device-call reduction half of the NL-2 gate. It also explains why the whole-runtime result is workload-dependent: the prototype removed calls into already batched OAM/HBlank/VBlank work, but it did not reduce CPU tick commits, mode-3 work, rendering, audio advancement, or other per-instruction runtime work.

## Correctness and rollback evidence

Before the performance decision:

- the lazy/eager visibility-deadline regression passed
- repository CTest passed 35/35
- the focused external PPU catalogue passed 13/13 after a forced rebuild
- the focused external OAM-DMA catalogue passed 6/6 after a forced rebuild
- all three 9,000-frame same-binary comparisons produced identical repeated final-state hashes

The extended frame, PCM, and differential gates were intentionally not used to promote the slice after it had already failed the mandatory performance gate. After the targeted rollback, the runtime files match the retained post-NL-1 source snapshot byte for byte, and repository CTest passes 34/34.

## Interpretation

The NL-0 observation that roughly 64% of PPU cycles occur outside draw mode was real, but cycle coverage overstated the removable cost. Those modes already advance in large spans inside `ppu_tick`; eliminating many calls mainly saves call/branch overhead. Tetris benefits materially from that overhead reduction, while the mapper-heavy and CGB workloads spend enough time elsewhere that the same structural change barely moves total runtime.

This result argues against expanding the prototype into lazy mode 3 or a more elaborate global scheduler now. A better next experiment is APU batching because current counters show `audio_step` still runs once per tick commit, and deterministic PCM invariance is already covered by repository tests. NL-3 should remain deferred until a retained scheduler change makes device-commit removal broad enough on at least two workloads.

## Next step

Prototype APU advancement to the next channel, frame-sequencer, or exact sample event behind a same-binary scalar/eager control. Require byte-identical deterministic PCM and state, then apply the independent NL-1-style runtime and footprint gate. If that experiment also produces only workload-local gains, move to NL-4 generated-output/chunking rather than broadening scheduler complexity.
