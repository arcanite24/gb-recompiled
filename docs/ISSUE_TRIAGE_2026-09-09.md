# Issue triage — 2026-09-09

Reviewed all five open reports against main at
`d88d9eba8fa2bed22d5910b7dd9701bdaab27c44`, after PRs #21 and #22.
Tests ran on macOS arm64 with CMake and Ninja. Local ROMs were used for
reproduction; ROMs, generated game code, saves, and game screenshots are not
included in this change.

| Issue | Evidence and disposition |
| --- | --- |
| [#20: Castlevania and palettes](https://github.com/arcanite24/gb-recompiled/issues/20) | Fixed the palette selector: completed PPU frames use RGB555-expanded colors, but the SDL recoloring code matched only older RGB constants. Black-and-white and amber now map all four DMG shades. Current main also reaches Crystal Castle gameplay in Belmont's Revenge and the first playable area in Legends, with movement/jump input, through 6,000 frames each. Keep the report open for the original CachyOS startup/freeze/glitch retest; this is not whole-game or Linux acceptance. |
| [#12: Blinking Lines](https://github.com/arcanite24/gb-recompiled/issues/12) | The reporter explicitly says in #20 that Belmont's flickering lines and Gaiden's text flicker stopped. Current Gaiden dialogue pixels at x=0..159, y=96..143 are identical over all 60 captured frames 2880..2939. Resolved on the reporter's confirmation, with this additional bounded check. |
| [#7: RAM code](https://github.com/arcanite24/gb-recompiled/issues/7) | Answered and covered by `generated_dynamic_code_end_to_end`. Both paths of the exact overlapping `jr z` / `db $3E` / `xor a` example execute correctly without fallback. WRAM and HRAM calls read changed operands and opcodes on repeated visits, including an operand modified by the guest within the same dispatch. No new decoder/runtime fix was needed. This does not certify all Prehistorik Man effects or arbitrary demoscene software. |
| [#11: German Zelda](https://github.com/arcanite24/gb-recompiled/issues/11) | Fresh generation/build and name-entry smoke pass. The attached screenshot shows unused opcode `$FD` execution around `$7F18` and `$7F28`; the text attachment is a build log. The post-first-dungeon enemy encounter was not reproduced, and no nearby save or route was available. Keep open; the next useful artifact is a save/state before that encounter plus ROM identity and input steps. |
| [#6: Thanks / distorted audio](https://github.com/arcanite24/gb-recompiled/issues/6) | The report does not identify a ROM, scene, or audio backend. Existing PCM scheduler-invariance and audio callback-concurrency tests pass, but they do not establish Linux listening quality. Keep open pending a concrete audio reproduction. |

## Verification

- Root CMake/Ninja build and all **84 CTests pass**. This includes native-patch
  end-to-end execution, generated bank guards, multi-ROM isolation, and release
  relocation/packaging checks.
- `display_palette_maps_completed_ppu_frames` failed before the fix with
  `FFE6F6CD != FFFFFFFF`. Afterward it verifies all four shades, all three
  palettes, padded texture rows, startup colors, unrelated host pixels, and
  preservation of colors on CGB hardware (including DMG compatibility mode).
- A fresh repository-owned synthetic ROM generated, configured, built, and
  ran headlessly for 120 frames with the changed runtime.
- Belmont was regenerated and rebuilt after the fix. Five gameplay PPM files
  at frames 4799/5099/5399/5699/5999 and the final 6,000-frame state dump are
  byte-identical to the pre-fix run. Palette selection is a host presentation
  change; diagnostic frame output is preserved.
- Belmont's first 500,000 differential steps match the project's interpreter.
  This is only 70 frames and shares runtime devices; it is not independent
  hardware evidence or a castle-selection test.
- PyBoy captures were used to distinguish title transitions from corruption.
  Boot timing and frame-sampled inputs differ, so same-numbered captures are
  not treated as exact state or timing matches.

The Crystal standalone contract's runtime source hash was refreshed for the
changed runtime. Passing its packaging tests does not add game-route acceptance.

## Reproduction inputs and ROM identities

Retained cycle-anchored inputs are under
[`tools/profiles/issue-repros-20260909`](../tools/profiles/issue-repros-20260909).
`belmont.input` and `legends.input` reach their first levels; `startup.input`
drives the 3,000-frame smoke for Gaiden, German Zelda, and Prehistorik Man.
Prehistorik Man was only inspected through its animated title sequence.

| Local short name | ROM | SHA-256 |
| --- | --- | --- |
| `belmont.gb` | Castlevania II - Belmont's Revenge (USA, Europe) | `17570ceec1b22153604622c4412d048dd8f7ccb4626daf9ddea96de8a062dbf2` |
| `legends.gb` | Castlevania Legends (USA, Europe) (SGB Enhanced) | `56d3dee063b8801704a284bd1bc229b94f15a3a448f485d347f04283d9bd16d7` |
| `zelda_de.gb` | The Legend of Zelda - Link's Awakening (Germany) | `4783815341590cde0b0d33482d644d4e8b6fcd0140535320e9668a83df869a49` |
| `gaiden.gbc` | Resident Evil Gaiden (USA) | `9a97678cbd8da02c8763e977674e17f460c06ea8b73bad35c52fe6817f506d44` |
| `prehistorik.gb` | Prehistorik Man (USA, Europe) | `b81ff42ea4168aed1f8da3b36a61d1c6d6d6a646268aebee0a7c75dc1c26814e` |

For the matching locally supplied Belmont ROM:

```bash
mkdir -p logs/issues-20260909/belmont
./build/bin/gbrecomp roms/issue-repros-20260909/belmont.gb \
  -o output/issues-20260909/belmont
cmake -G Ninja -S output/issues-20260909/belmont \
  -B output/issues-20260909/belmont/build
ninja -C output/issues-20260909/belmont/build
./output/issues-20260909/belmont/build/belmont \
  --headless --limit-frames 6000 \
  --input "$(cat tools/profiles/issue-repros-20260909/belmont.input)" \
  --dump-frames 4799,5099,5399,5699,5999 \
  --screenshot-prefix logs/issues-20260909/belmont/replay \
  --dump-state logs/issues-20260909/belmont/replay-state.json
```

Local logs and captures are retained under `logs/issues-20260909/`: `ctest.log`,
`palette-before.log`, `palette-after.log`, `palette-final.log`, `smoke-*.log`,
`dynamic-code.log`, each game's input/state/frame captures,
`belmont/before-after.json`, and `gaiden/text-stability.json`.
