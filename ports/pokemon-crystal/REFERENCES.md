# Pokémon Crystal reference inventory

All third-party checkouts live under `references/vendor/` and are ignored by
Git. `references/sources.lock.json` records the exact commits used for this
research snapshot. Run `python3 scripts/references.py verify` before relying on
them.

## Primary game references

### pret/pokecrystal

Role: authoritative byte-matching disassembly for the supported ROM revision.

High-value areas:

- `home/` and `engine/` for named routines and control flow;
- `ram/wram.asm`, `ram/sram.asm`, and `ram/hram.asm` for semantic state;
- `data/` and `maps/` for tables, events, scripts, encounters, and content;
- `docs/` for map scripts, command languages, bugs, and design flaws;
- the Makefile target `crystal11` for the Rev 1 build and symbol output.

The exact local ROM SHA-1 appears in the upstream README. That is identity
evidence, not permission to redistribute the ROM or the disassembly.

### pret/pokecrystal symbols branch

Role: prebuilt RGBDS `.sym` and `.map` files for the supported revisions.

The relevant input is `pokecrystal11.sym`. Its pinned SHA-256 is
`ca55588e83e4f4974e3872057eec12e8aac853bad1774e91486b5986cf6cb780`.

GB Recompiled now accepts the raw RGBDS file directly. It models constant-only
records such as `00 SCENE_AZALEATOWN_NOOP` separately from address symbols and
reports the complete import:

- 58,456 address records;
- 53,784 unique addresses;
- 4,672 duplicate-address alias records;
- 164 constant records and 164 unique constants.

Normal Crystal generation imports these records under the recompiler's
`names-only` policy. Same-address names are retained in metadata through
`source_symbols`, while only separately reviewed annotations may add analyzer
boundaries. For example, address `01:dcd7` retains `wCurMapDataEnd`,
`wPartyCount`, and `wPokemonData` rather than silently discarding two aliases.

`scripts/prepare_symbols.py` remains as the locked Phase 0 compatibility
projection. It retains only address records and verifies:

- 58,456 prepared address records;
- prepared SHA-256
  `16281bb303b0f61027a6e728d2517b463c12960e61aefb1f9d2d823dc49fe4cc`.

The projection is no longer required for parser compatibility or normal
generation. Neither raw import nor the projection turns every symbol into a
valid function entry or distinguishes every data range.

### pret/pokecrystal wiki

Role: practical modding notes and tutorials. Useful starting points include
assembly basics, ROM offsets, scripting commands, maps and landmarks, wild
encounter data, new items and moves, and save behavior.

Treat tutorials as implementation leads to verify against the pinned
disassembly and runtime evidence.

## Hardware truth references

### Pan Docs

Role: first reference for MBC3, CGB registers, speed switching, DMA/HDMA, PPU
timing, timers, interrupts, and memory visibility.

The pinned upstream repository is CC0-1.0. The parent GB Recompiled repository
also has a local `tech_docs/pan_docs.md`, but the port keeps its own reproducible
checkout so it can later be ejected.

### SameBoy

Role: second hardware reference when Pan Docs is ambiguous and an independent
implementation is useful. Prioritize its MBC3, RTC, memory, PPU, timer, and CPU
code. SameBoy is a higher-level comparison oracle; GB Recompiled differential
mode is not independent because generated and interpreted paths share devices.

## Applied mod and tooling references

| Source | Use | License handling |
| --- | --- | --- |
| Polished Crystal | Proven examples of a larger Pokédex, redesigned PC storage, 60 FPS overworld, weather, new maps, and extensive engine changes | No license file found; local research only |
| Tilemap Studio | Map and tilemap authoring compatible with pret projects | LGPL-3.0; keep as an external tool unless obligations are reviewed |
| PKHeX.Core | Independent Gen II save, Pokémon entity, inventory, and checksum cross-checks | GPL-3.0; external oracle only for the planned MIT port |
| pret/gb-asm-tools | Symbol, map, palette, and disassembly helper scripts | No license file found; local research only |

Polished Crystal is especially valuable as evidence that Crystal's engine can
support modern PC-box workflows, expanded data, richer weather, and faster
overworld presentation. It is not a code donor for this port without explicit
permission or a compatible license.

## Deferred references

- Crystal Clear documents useful open-world and scaling ideas, but a clear,
  license-compatible source repository was not identified in this pass.
- Crystal Legacy is useful as a design comparison for conservative rebalance,
  not as core recompilation infrastructure.
- `pokecrystal16` variants are relevant if the port later needs IDs beyond 255,
  but expansion is premature before the vanilla schema and save contract.

These can be added to the lock only when they answer a specific engineering
question.

## Research rules

1. Prefer the supported Rev 1 disassembly and symbol map for game semantics.
2. Prefer Pan Docs, then SameBoy, for hardware behavior.
3. Confirm every function hook against generated metadata and runtime traces.
4. Confirm every writable state field against save validation and replay.
5. Record the source commit whenever a schema or annotation is derived.
6. Do not copy unlicensed or GPL code into the port's distributable sources.
