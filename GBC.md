# Game Boy Color support

GB Recompiled runs CGB-only cartridges and CGB-enhanced cartridges through the same generated-project flow used for DMG games. CGB support is functional but not complete; compatibility claims should remain tied to fresh tests and reproducible game-specific evidence.

## Implemented

- automatic hardware selection from cartridge header byte `0x143`
- explicit `--model auto|dmg|cgb` runtime selection
- CGB mode and DMG-on-CGB compatibility mode
- KEY1 speed switching and double-speed CPU operation, including half-system-cycle carry across split CPU bus phases
- VBK and SVBK bank selection
- CGB palettes and VRAM-bank-1 tile attributes
- CGB background, window, and sprite rendering
- timed general-purpose HDMA and HBlank HDMA
- model- and source-aware OAM DMA behavior
- MBC3 RTC persistence and stable battery-save identities
- 9-bit MBC5 ROM banking through analysis, code generation, runtime mapping, and interpreter fallback

## Current hardware-test snapshot

The relevant CGB, LCD/STAT, and timer selections were rerun on 2026-07-29:

| Test | Result |
| --- | --- |
| `boot_regs-cgb` | Pass |
| `vblank_stat_intr-C` | Pass |
| `boot_div-cgb0` | Fail |
| `boot_div-cgbABCDE` | Fail |

The complete timer selection passed 13/13 and the LCD/STAT selection passed
4/4. The CGB selection passed 1/3. The two failures are the already disclosed
boot-revision DIV gap: generated executables initialize a configured post-boot
state and do not execute a CGB boot ROM.

Blargg `interrupt_time` also passes both its normal-speed and CGB double-speed 13-M-cycle interrupt measurements. It is tracked in the complete catalogue rather than the five-case curated CGB subset above.

The passing boot-register test validates the runtime's configured post-boot state; it does not mean the CGB boot ROM is emulated. See [Accuracy](ACCURACY.md) for the complete configured suite.

Crystal Recompiled now has a pinned SameBoy comparison using SameBoy's
MIT-licensed CGB boot ROM implementation. Four selected title, new-game,
overworld, and Continue checkpoints have zero unexplained frame/state
differences after comparing in the native 5-bit CGB color domain. The Continue
checkpoint also proves that SameBoy independently accepts the generated
battery file. This is game-route evidence, not a claim that all CGB hardware
tests or all Crystal scenes are accurate.

Tetris DX received a fresh generated-project optimization smoke on 2026-07-13. Generated and interpreted execution matched for 500,000 steps, and the stable-span and scalar PPU paths produced the same 1,800-frame state SHA-256, `4bf4c98fec004470ea680ce89e27aa6971fccd232d6b7ca0fa84076c58431bdf`. The generated workload still records 545 interpreter fallbacks over 1,800 frames, so this is CGB behavior/performance evidence rather than a fallback-free analyzer-coverage claim. See [the NR-1 through NR-3 result](docs/NR123_DYNAMIC_OPTIMIZATION_RESULTS_2026-07-13.md).

## Known gaps

- the CGB boot ROM is not emulated
- DIV initialization does not match all CGB boot revisions
- some undocumented CGB I/O readback and masking behavior remains incomplete
- KEY0/PGB edge behavior is incomplete
- FF56 infrared support is stub-level
- serial behavior is sufficient for current single-system use but is not a complete link-cable implementation
- double-speed, HDMA, LCD/STAT, and DMG-on-CGB interactions outside the
  selected Crystal route still need broader independent validation
- known-good games can still encounter analyzer coverage gaps and interpreter fallback

## Reproduce the curated subset

Build the current recompiler, then run the five configured `misc` cases:

```bash
cmake -G Ninja -B build .
ninja -C build
python3 tools/run_tests.py --filter misc --rebuild
```

The test runner treats build errors, timeouts, missing state dumps, incomplete runs, and an empty selection as failures.

## Reference order

For CGB implementation work:

1. consult `tech_docs/pan_docs.md`
2. compare with the local `SameBoy/` implementation when Pan Docs is ambiguous
3. add or update a repository-owned regression test
4. run the relevant Mooneye CGB case and a recorded real-game smoke
