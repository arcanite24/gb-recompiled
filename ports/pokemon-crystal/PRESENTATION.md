# Crystal native presentation mapping

Crystal native presentation is an optional host image derived from two
separate authorities:

- reviewed exact-ROM semantic accessors describe Crystal's game meaning; and
- the accurate PPU snapshot preserves hardware tiles, maps, sprites, palettes,
  timing, and fallback pixels.

The generic ABI and shadow-hardware rules are in the root
[`NATIVE_PRESENTATION.md`](../../NATIVE_PRESENTATION.md).

## Overworld

At a completed-frame safepoint, the port populates
`GBPresentationScene.map` from versioned semantic identities:

- `player.location` supplies current map group/number and player coordinates;
- `world.map_connections` is the reviewed root for resolving adjacent maps;
- `crystal_overworld_build_new_bark_scene` resolves the reviewed New Bark
  west-connection record, map attributes, and both ROM block streams into two
  bounded regions;
- the same builder maps Crystal's 13 live object structs into world-space
  semantic sprites after applying the original four-tile coordinate padding.

The current map is the origin region. Connected maps use signed block origins
relative to it. Regions may not overlap the shared block array or exceed its
bounds. The native camera is host presentation state; it does not write
Crystal's player, map, scroll, or camera variables.

The M7 prototype is intentionally bounded to New Bark Town and the connected
Route 29 edge. It renders a 256-by-144 host surface with an original
project-owned procedural block style, using block identities and object state
from the user's ROM rather than stored derived art. Unknown maps, uncovered
camera edges, scripted transitions, LCD-off spans, unclassified raster
effects, and objects requiring grass/under-tile occlusion stay on the accurate
160-by-144 path. This is a composition proof, not the final art direction.

Crystal object flags map low/normal/high priority into the generic contract.
`IN_GRASS` and `UNDER_TILES` request semantic background occlusion; because
the bounded prototype does not yet publish reviewed per-block foreground
masks, the renderer falls back instead of drawing those objects incorrectly.

## Battles and UI

`GBPresentationScene.battle` carries reviewed player/enemy species, levels, and
a versioned phase value. Semantic package v4 publishes exact typed views for
the active player, active party slot, enemy, and battle context. The accessor
prefers Crystal's live `wBattleMon`; during the battle-intro boundary, before
that union is populated, it resolves `wCurBattleMon` into the reviewed party
record. Invalid slot, species, level, mode, ROM, or memory range fails closed.
The port renderer receives typed values and never reaches through to emitted C
symbols or raw WRAM.

Menus and overlays use the existing bounded `GBPortFrame` command stream.
The native Pokédex, PC, Workbench, and Encounter Lens remain renderer-neutral
panel/text producers. The M7-003 battle prototype uses the same panel
composition at 1280 by 720, two original CC0 procedural assets, and a fixed
effect seed. Its deterministic CPU reference draws panel treatment, an arena
effect, and species-derived silhouettes from live player/enemy species and
levels. It is a composition and provenance proof, not final replacement art.

`assets/presentation/manifest.json` is the source authority for output
dimensions, effect seed, asset IDs, content SHA-256, SPDX license, and source.
Generation copies that package into the generated project and records it in
the receipt. The headless probe hashes every asset before rendering and runs
twice to prove an identical 1280-by-720 output.

## Mode selection and fallback

- `--native-presentation original` presents the accurate PPU result and does
  not activate a semantic replacement.
- `--native-presentation native` may request a native scene only after
  presentation ABI v1 and Crystal Rev 1 ROM identity pass.
- Replacement configuration or asset provenance disagreement retains the
  original presentation; replacement bytes are never trusted by path alone.
- A missing or invalid semantic scene, unknown map/battle phase, invalid UI
  frame, non-completed-frame capture, or unsupported renderer capability
  retains the original PPU presentation.
- Headless runs may capture and validate the full contract without creating a
  window, renderer, texture, or GPU device.

Native presentation never changes generated CPU execution, PPU ticking,
VRAM/OAM arbitration, DMA, interrupts, semantic state, persistence, or replay
input. Replay evidence must retain the executable, generation receipt, ROM,
presentation ABI/configuration, semantic package, and any replacement-asset
hashes.
