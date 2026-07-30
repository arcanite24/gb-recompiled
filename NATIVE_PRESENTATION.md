# Native presentation contract

GB Recompiled's native presentation ABI is a renderer-independent,
completed-frame snapshot. It lets an exact-game port prepare widescreen or
higher-resolution host rendering without making a graphics API part of the
runtime and without turning native rendering into guest-visible hardware.

The v1 ABI is declared in `runtime/include/gbrt_presentation.h`.

## Activation

`gbrt_presentation_source_init` requires:

- native presentation ABI version 1;
- an expected ROM size and SHA-256;
- matching live embedded ROM bytes;
- initialized accurate PPU, VRAM, OAM, and I/O state; and
- an explicit headless/interactive host profile.

Unknown ABI or ROM identity fails before a source becomes active. A source
captures only while `GBContext.frame_done` is set. This is the completed-frame
safepoint; it does not permit a native consumer to sample partially changing
hardware state from another thread.

The API has no SDL, ImGui, window, GPU, texture, shader, or command-buffer
type. A headless caller needs no graphics device.

## Frame contents

`gbrt_presentation_capture` validates and copies five contracts into one owned
`GBPresentationFrame`:

1. **Map/scene** — a game-specific scene kind and stable ID, up to nine
   positioned semantic map regions and 4,096 block identities, camera/player
   coordinates, up to 32 world-space semantic sprites with explicit priority
   and occlusion requirements, or bounded battle participants and phase.
2. **Tiles/maps** — both CGB banks of `$8000-$97FF` tile data, both 32-by-32
   tile-ID maps from VRAM bank 0, and corresponding CGB attribute maps from
   bank 1.
3. **Sprites** — all 40 OAM entries with raw bytes plus decoded screen
   coordinates, 8/16-pixel height, tile bank, palette, flips, and background
   priority.
4. **UI** — the existing bounded `GBPortFrame` panel/text command stream.
5. **Timing and shadow hardware** — exact ROM identity, completed-frame and
   cycle counters, PPU mode/register endpoint, CGB palettes, and the accurate
   160-by-144 15-bit pixel result.

Scene meaning is supplied by reviewed port semantic accessors. The generic
runtime does not infer maps, battles, or UI from WRAM addresses or generated
symbol names. Malformed region ranges, missing scene data, invalid UI bounds,
or mismatched scene/UI ABI fail closed.

## Accurate PPU authority

The snapshot declares
`GB_PRESENTATION_SHADOW_ACCURATE_PPU` and
`guest_writeback_allowed = false`.

The ordinary PPU still executes every dot and remains authoritative for:

- LCDC/STAT/LY behavior and interrupts;
- mode 2/3/0/1 timing and variable mode 3 length;
- CPU visibility of VRAM, OAM, and CGB palettes;
- OAM DMA and HDMA interaction;
- raster effects and mid-scanline register changes;
- sprite selection, priority, and the ten-object-per-line limit; and
- the accurate 160-by-144 fallback pixels.

Native rendering consumes a copy. Mutating or discarding that copy cannot
change guest registers, memory, interrupts, timing, saves, or the accurate
framebuffer. A native renderer may present a different host image, but it
cannot skip PPU ticks or write a derived result back into hardware state.

The captured VRAM/OAM endpoint alone cannot reconstruct every mid-frame raster
effect. A port must use the included accurate pixels or remain in original
presentation whenever its semantic renderer cannot prove a scene-safe
replacement.

## Renderer-neutral widescreen reference

`gbrt_presentation_compose_widescreen` is a deterministic CPU reference
compositor over an owned `uint32_t` surface. It proves that adjacent semantic
regions and world sprites can produce a host image wider than 160 pixels
without SDL, a GPU, or a graphics-API type in the ABI. Its block treatment is
an original procedural style keyed by the submitted block IDs; it does not
extract or distribute commercial tile art.

Composition is all-or-fallback. It returns the accurate-path disposition
before writing output when:

- the semantic scene, ABI, sprite state, or output surface is invalid;
- a transition or unclassified raster effect is active;
- the accurate LCD is disabled;
- any camera-covered block is absent from the submitted current/adjacent map
  regions; or
- a visible sprite needs background occlusion for which no reviewed mask was
  submitted.

Low, normal, and high semantic sprites otherwise compose in stable input order
within each priority. The function reads only the captured frame. The accurate
PPU framebuffer and all guest-visible state remain unchanged.

## Replacement assets and configuration

`GBPresentationReplacementConfig` is the renderer-neutral activation contract
for high-resolution replacements. It records the presentation mode, output
dimensions, deterministic effect seed, exact ROM identity, and a bounded list
of replacement assets. Each `GBPresentationReplacementAsset` carries a stable
ID, content SHA-256, SPDX license expression, and source description.

`gbrt_presentation_validate_replacements` fails closed for an unknown ABI or
ROM, unsupported mode or dimensions, missing native assets, malformed hashes,
missing provenance, or duplicate IDs. The validator reads metadata only: it
does not load images, create a graphics device, or choose a renderer. Asset
bytes remain owned by the exact-game port, which must verify each content hash
before use.

`original` mode requires no replacement assets and retains the accurate PPU
image. `native` mode is an optional host presentation; it cannot change guest
execution, shadow hardware, persistence, or replay input. Replays and release
evidence must record the complete configuration and ordered asset hashes.

## Hardware basis

The layout follows the repository's pinned Pan Docs:

- two CGB VRAM banks, with 384 16-byte tiles per bank;
- two 32-by-32 background/window maps and bank-1 attributes;
- 40 four-byte OAM entries, 8-by-8 or 8-by-16 sprites, and ten selected per
  scanline;
- 154 scanlines of 456 dots, with visible mode 3 lasting 172–289 dots; and
- blocked CPU access during the PPU modes in which video hardware owns the
  corresponding resource.

The contract snapshots the runtime's already-modeled result; it does not
replace those rules with a simplified renderer model.
