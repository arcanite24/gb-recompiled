# Crystal Recompiled port plan

## Mission

Ship a native Pokémon Crystal port that starts from an exact user-provided ROM,
reproduces the vanilla game faithfully, and then exposes stable semantic
interfaces for native features and mods.

The differentiator is game knowledge. A feature belongs in this project when
it depends on named Crystal functions, maps, scripts, party or box structures,
event flags, or data tables. Generic emulator conveniences are useful but are
not the product thesis.

## North star

The first signature feature should be a **native Crystal Workbench** integrated
with the running game:

- a searchable modern Pokédex with encounter, evolution, and move information;
- a world map and quest/event journal derived from the live save state;
- party and PC-box browsing with instant search and sorting;
- a mod inspector showing active packages, hooks, and semantic data overrides;
- a native UI that can coexist with the original 160-by-144 view and later
  replace selected in-game screens.

A generic emulator could display an overlay, but it cannot provide this
reliably for one exact game without the same symbols, schemas, hooks, and save
semantics. This makes the Workbench a practical proof of recompilation rather
than an emulator skin.

The longer-term visual target is an optional native presentation mode:
widescreen map composition, higher-resolution UI, and native battle effects
driven by original game state. It is deliberately not the first milestone
because GB Recompiled does not yet expose a stable renderer extension API.

## Architecture

The public project should contain only original port code and reproducible
setup metadata:

```text
user ROM
   |
   v
exact hash gate ---> GB Recompiled + prepared symbols ---> local generated game
                          |                                      |
                          v                                      v
                  semantic schema/accessors              native port package
                          |                                      |
                          +------------> native shell <----------+
                                             |
                                      versioned mod packages
```

The local generated game is a build artifact. It embeds or derives from the
user's ROM and must never be a release asset.

## Phase 0 — freeze the inputs

Deliverables:

- exact ROM identity and a fail-closed verifier;
- pinned local references with provenance and license notes;
- a prepared Rev 1 symbol file accepted by GB Recompiled;
- a reproducible symbol-import probe;
- documented `output/pokemon-crystal-*` and `logs/pokemon-crystal/` evidence
  naming within the parent repository's artifact conventions.

Current evidence:

- the local ROM passes its internal header checksum and matches the documented
  `pokecrystal11` SHA-1;
- the upstream RGBDS symbol file contains 58,621 records;
- GB Recompiled accepts the raw RGBDS file, models its 164 constant-only
  records separately, and reports 58,456 address records, 4,672
  duplicate-address aliases, and 53,784 unique addresses;
- Crystal imports those records under an explicit names-only policy, preserves
  all same-address aliases in metadata, and contributes zero inferred analyzer
  boundaries unless a reviewed annotation file is supplied;
- the historical address-only projection still reproduces 58,456 lines but is
  no longer required for parser compatibility;
- an analysis probe then completes, but reports unresolved indirect jumps and
  a still very large analyzer graph. Names-only symbols improve naming; they
  do not prove code boundaries or by themselves solve analyzer overreach.

Exit gate: every contributor can reproduce these facts without possessing any
file from another contributor's ROM or save.

## Phase 1 — establish vanilla truth

Generate a fresh Crystal project with the raw pinned symbols and capture a
repeatable baseline:

1. boot to the title screen;
2. start a new game and reach the overworld;
3. save, restart, and continue;
4. exercise an overworld transition, wild battle, trainer battle, menu,
   Pokédex, PC box, and RTC-dependent event;
5. record cycle-anchored input and retain state, frame, audio, fallback, and
   performance evidence.

Validation must use three distinct layers:

- repository-owned synthetic tests for isolated runtime invariants;
- differential mode for generated execution versus this runtime's interpreter;
- SameBoy or hardware-oriented tests plus frame and audio comparisons as an
  independent reference.

Special attention belongs on MBC3 RTC persistence, CGB double-speed behavior,
HDMA and LCD interaction, save checksums and backup saves, and serial behavior.
The port must not claim full Crystal compatibility from a title-screen boot.

Exit gate: a deterministic new-game-to-save-to-continue route passes without
unexpected interpreter fallback, state divergence, or save corruption.

## Phase 2 — build the semantic bridge

Create a port-owned schema generator that combines:

- banked ROM symbols and generated function metadata;
- trusted, reviewed annotations for code and data boundaries;
- WRAM, SRAM, and HRAM layouts from the disassembly;
- independently validated save parsing and checksums;
- explicit schema and GB Recompiled ABI versions.

Generate typed accessors for a small first surface:

- player identity and location;
- badges, Pokédex seen/caught flags, and event flags;
- party and current PC box;
- active map and map connections;
- RTC state and save-generation metadata.

Access is read-only first. Writes require transactions, invariant checks,
backup-save handling, and deterministic replay tests. PKHeX.Core is a useful
GPL-licensed cross-check, not code to copy or link into an MIT port.

Exit gate: the same fixture saves decode identically through the port schema,
the original game's own load path, and an independent checker for every field
the Workbench exposes.

## Phase 3 — ship the first beyond-emulation slice

Add a native, read-only Workbench panel while the vanilla game continues to
run unchanged. The first vertical slice shows:

- current map and player position;
- party names, levels, HP, status, held items, and moves;
- badge and Pokédex progress;
- a selected species page backed by named ROM tables.

It must work from live state and from a loaded save, survive regeneration, and
produce identical guest state whether the panel is open or closed.

The second slice hooks the original Pokédex entry point and offers a native
replacement while retaining an explicit “use original” path. This will require
an SDK addition for stable UI/render access; the existing native-patch ABI is
enough for exact-ROM function hooks but intentionally lacks renderer and
dynamic-module access.

Exit gate: the native screen is materially better than the original UI,
preserves deterministic guest behavior, and has no dependency on emitted C
symbol names.

## Phase 4 — make mods a product surface

Evolve the exact-ROM patch package into a port-level mod contract:

- package and semantic-schema versions;
- exact ROM and required port versions;
- stable named functions and data fields;
- original assets only, with explicit provenance;
- deterministic load order and conflicts;
- data-table overlays without rewriting the user's ROM;
- development hot reload only where it cannot corrupt guest state;
- replay metadata that records all active mods and hashes.

Candidate sample mods:

- configurable encounter and trainer difficulty rules;
- a randomizer that emits a complete reproducibility seed and manifest;
- new quests implemented through reviewed event-script extensions;
- accessibility rules such as text speed, palettes, input assists, and battle
  information;
- original UI themes and native battle effects.

Exit gate: two independent mods can be installed, validated, replayed, and
removed without regenerating or redistributing ROM-derived content.

## Phase 5 — native presentation and platform features

Only after the semantic and mod contracts are stable:

- compose adjacent map blocks for optional widescreen exploration;
- add higher-resolution native menus and battle presentation;
- support original replacement art and audio packs;
- explore native matchmaking for trades and battles with protocol validation;
- retain the vanilla renderer and local link behavior as compatibility paths.

Each replacement needs an explicit timing and state contract. Rendering work
must not silently bypass guest-visible PPU behavior when game logic can observe
it.

## Phase 6 — eject and release

Move this directory into a standalone repository when all of these are true:

- the parent GB Recompiled APIs used by the port are versioned and documented;
- a clean checkout can locate GB Recompiled or fetch a compatible release;
- the ROM picker validates Rev 1 and builds all ROM-derived output locally;
- Windows, macOS, and Linux packaging is reproducible;
- no ROM, save, symbol dump, disassembly, extracted asset, or generated game
  source is tracked or included in a release;
- third-party licenses and trademark disclaimers have been reviewed;
- the vanilla route and flagship native feature have durable evidence.

The likely release contents are a small launcher, original native-port sources,
manifests, schemas authored by this project, setup scripts, and documentation.
The user supplies the exact ROM on first run.

## Main risks and early decisions

| Risk | Current response |
| --- | --- |
| Disassembly and symbol repositories have no explicit license | Keep local and ignored; fetch by pinned commit; do not redistribute their files |
| Symbols are not code/data boundary proof | Add narrow trusted annotations and verify behavior |
| Crystal stresses CGB, RTC, saves, and indirect dispatch | Make the deterministic vanilla route the first engineering milestone |
| Generated output can contain ROM-derived material | Treat all generated projects as private local artifacts |
| Native-patch ABI lacks semantic accessors and renderer hooks | Build the schema bridge first; version SDK extensions rather than reaching into emitted names |
| GPL reference code could contaminate an MIT release | Use it only as an external oracle unless the licensing strategy changes explicitly |
| Widescreen/HD scope can swallow the port | Gate it behind a proven Workbench and mod contract |

## Execution backlog

The living checklist, dependency order, verification gates, and evidence
locations are maintained in [the execution backlog](BACKLOG.md). Update this
plan only when the product direction, architecture, or milestone sequence
changes.
