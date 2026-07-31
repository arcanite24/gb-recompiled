# Crystal Recompiled execution backlog

This is the living execution checklist for Crystal Recompiled. The
[port plan](PLAN.md) explains the product direction and architecture; this
file records implementation truth.

The backlog is intentionally vertical. Every item must improve GB Recompiled
as a reusable tool and prove that improvement through Pokémon Crystal. Avoid
game-specific reach-through to emitted C names, and avoid generic engine work
that has no concrete Crystal consumer.

## How to use this backlog

The current frontier is **CR-M7-006**. Later work may be investigated, but an
item cannot be checked until all listed dependencies and its complete gate
pass.

Checkbox meaning:

- `[ ]` means unverified. Dependencies determine whether it is ready or
  blocked.
- `[x]` means the gate passed and durable evidence exists.
- Partial implementation remains unchecked.
- A historical log is a lead, not current evidence, until it is reproduced
  with the required identity and commands.

Work-selection protocol:

1. Choose the lowest-numbered unchecked item whose dependencies are checked.
2. Read its gate before changing code and create the raw evidence directory.
3. Keep the implementation limited to the named recompiler and Crystal
   deliverables.
4. If required work falls outside that contract, add or amend a backlog item
   rather than silently expanding the current one.
5. Run the full applicable gate, write the durable evidence summary, then
   check the item and advance the current-frontier declaration.

Each completed item must retain:

- raw logs, captures, state dumps, and generated-project measurements under
  `logs/pokemon-crystal/<item-id>/`;
- a tracked, ROM-free summary following the
  [evidence guide](evidence/README.md);
- ROM, recompiler, runtime, symbol, annotation, executable, input, and active
  mod hashes when applicable;
- exact commands, relevant flags, host/compiler identity, results, known
  limitations, and any manual verification boundary.

Do not put ROMs, saves, generated game sources, extracted assets, or upstream
reference files in tracked evidence.

## Milestone dependency map

```text
M0 Research foundation
 |
 v
M1 Reproducible integration
 |\
 | +------> M3 Analysis and metadata -----+
 v                                      |
M2 Vanilla truth -----------------------+----> M4 Semantic Workbench
                                               |\
                                               | +----> M6 Mod packages
                                               v
                                      M5 Transactional state
                                               \        /
                                                v      v
                                        M7 Presentation and release
```

M3 may proceed alongside M2 after M1 is reproducible. M4 requires both the
vanilla evidence and trustworthy semantic metadata. M5 and M6 can then proceed
independently. M7 requires both.

## M0 — Research foundation

Outcome: contributors can reproduce the exact-ROM and research inputs without
receiving copyrighted game content from another contributor.

- [x] **CR-M0-001 — Lock the supported ROM identity**
  - Depends on: none.
  - Recompiler: use the cartridge loader's size, mapper, CGB, logo, and header
    checksum validation as the first input gate.
  - Crystal: fail closed unless the user selects the US/Europe Rev 1 ROM with
    the documented size, SHA-1, and SHA-256.
  - Gate: `scripts/verify_rom.py` accepts the local supported ROM and rejects a
    mismatched file without reading or writing game data elsewhere.
  - Evidence: `README.md`, `scripts/verify_rom.py`.

- [x] **CR-M0-002 — Pin the technical reference corpus**
  - Depends on: CR-M0-001.
  - Recompiler: preserve Pan Docs and SameBoy as the ordered hardware
    references required by the parent project.
  - Crystal: pin the Rev 1 disassembly, symbols, wiki, applied mod references,
    map tooling, and independent save-format oracle.
  - Gate: `scripts/references.py verify` confirms every checkout, required
    file, commit, and locked file hash.
  - Evidence: `REFERENCES.md`, `references/sources.lock.json`,
    `scripts/references.py`.

- [x] **CR-M0-003 — Prepare a deterministic symbol input**
  - Depends on: CR-M0-002.
  - Recompiler: document the current limitation around RGBDS constant-only
    records and prove that banked address records import successfully.
  - Crystal: derive the same ignored `pokecrystal11.gbrecomp.sym` from the
    pinned Rev 1 symbol file on every machine.
  - Gate: `scripts/prepare_symbols.py` produces 58,456 address records with the
    locked hash, and `scripts/probe-symbols.sh` loads 53,784 unique symbols.
  - Evidence: `REFERENCES.md`, `scripts/prepare_symbols.py`,
    `scripts/probe-symbols.sh`.

- [x] **CR-M0-004 — Establish the distribution and artifact boundary**
  - Depends on: CR-M0-001, CR-M0-002.
  - Recompiler: retain ROMs, generated projects, and runtime captures as local
    ignored artifacts.
  - Crystal: define a public-repository model containing original port code,
    manifests, schemas, and setup automation while excluding ROM-derived and
    unlicensed reference content.
  - Gate: the ROM, reference checkouts, prepared symbols, generated output, and
    logs are ignored; the policy names both prohibited and intended release
    contents.
  - Evidence: `LEGAL.md`, `README.md`, the relevant ignore rules.

Milestone exit: complete.

## M1 — Reproducible integration baseline

Outcome: one fresh toolchain state can build the recompiler, generate Crystal,
build the generated project, and run it with complete provenance.

- [x] **CR-M1-001 — Establish one fresh recompiler build**
  - Depends on: CR-M0-004.
  - Recompiler: configure a new CMake/Ninja build from the live source,
    including the native-patch implementation and repository-owned tests.
  - Crystal: record the exact source state that will generate the first port
    baseline; do not depend on the older `build` or `build-nl5` directories.
  - Gate: root compilation and CTest pass, including
    `native_patch_sdk_end_to_end`; the resulting CLI exposes symbols,
    annotations, and native-patch options.
  - Evidence: raw `logs/pokemon-crystal/CR-M1-001/`; summary
    `evidence/CR-M1-001.md`.

- [x] **CR-M1-002 — Add one port-owned generation entry point**
  - Depends on: CR-M1-001.
  - Recompiler: accept explicit ROM, runtime, symbol, annotation, native-patch,
    output, and build-profile inputs without hidden generated-tree state.
  - Crystal: provide one command or script that verifies the ROM and
    references, prepares symbols, and generates into an ignored
    `output/pokemon-crystal-*` destination.
  - Gate: two fresh destinations generated from identical inputs have matching
    metadata and source inventories, excluding documented nondeterministic
    build-system fields.
  - Evidence: raw `logs/pokemon-crystal/CR-M1-002/`; summary
    `evidence/CR-M1-002.md`.

- [x] **CR-M1-003 — Build and smoke the fresh generated project**
  - Depends on: CR-M1-002.
  - Recompiler: ensure the generated CMake/Ninja project consumes the current
    runtime snapshot and builds without reaching into the parent source tree.
  - Crystal: boot the exact Rev 1 project headlessly for at least 120 frames
    and retain a state dump and selected frames.
  - Gate: generation, configure, build, and headless execution all exit
    successfully from a fresh destination; a relocated generated directory
    behaves the same.
  - Evidence: raw `logs/pokemon-crystal/CR-M1-003/`; summary
    `evidence/CR-M1-003.md`.

- [x] **CR-M1-004 — Snapshot analysis and patchability metadata**
  - Depends on: CR-M1-003.
  - Recompiler: export discovered functions, imported names, provenance,
    patchability, and generated stable IDs in deterministic metadata.
  - Crystal: classify the initial `StartMenu`, `Pokedex`, `BillsPC`, save, and
    checksum anchors as patchable, unpatchable, or unresolved.
  - Gate: every candidate in `SEMANTIC_ANCHORS.md` has a metadata-backed status;
    no candidate is called a hook merely because it appears in a `.sym` file.
  - Evidence: raw `logs/pokemon-crystal/CR-M1-004/`; summary
    `evidence/CR-M1-004.md`.

Milestone exit: complete. A fresh, provenance-locked Crystal project builds
and runs, and its initial semantic anchors have evidence-backed statuses.

## M2 — Vanilla truth route

Outcome: a deterministic route proves meaningful Crystal gameplay, persistence,
and CGB behavior before native enhancements change presentation or state.

- [x] **CR-M2-001 — Curate the cycle-anchored gameplay route**
  - Depends on: CR-M1-003.
  - Recompiler: retain cycle-anchored replay as a stable generated-runtime
    contract and reject malformed input.
  - Crystal: create a ROM-free route covering title, new game, overworld,
    transition, wild battle, trainer battle, Start menu, Pokédex, PC, save,
    restart, and Continue.
  - Gate: a route manifest names machine-verifiable checkpoints and the replay
    reaches each one without live input.
  - Evidence: raw `logs/pokemon-crystal/CR-M2-001/`; summary
    `evidence/CR-M2-001.md`.

- [x] **CR-M2-002 — Prove deterministic generated execution**
  - Depends on: CR-M2-001.
  - Recompiler: capture final state, selected frame, deterministic PCM, input,
    executable, metadata, and feature-flag identities.
  - Crystal: replay the complete route at least three times from equivalent
    clean state.
  - Gate: all comparable state, frame, and PCM hashes match; any real-time RTC
    input is controlled or explicitly isolated.
  - Evidence: raw `logs/pokemon-crystal/CR-M2-002/`; summary
    `evidence/CR-M2-002.md`.

- [x] **CR-M2-003 — Prove generated-versus-interpreter consistency**
  - Depends on: CR-M2-001.
  - Recompiler: run strict differential comparison with explicit fallback
    rejection and retain an injected-mismatch self-test for the oracle.
  - Crystal: cover representative boot, overworld, menu, and battle windows
    from the recorded route.
  - Gate: every bounded comparison completes without divergence or unexpected
    fallback; shared-runtime scope is stated and not presented as hardware
    proof.
  - Evidence: raw `logs/pokemon-crystal/CR-M2-003/`; summary
    `evidence/CR-M2-003.md`.

- [x] **CR-M2-004 — Classify and close interpreter fallback**
  - Depends on: CR-M2-002.
  - Recompiler: report fallback entry, reason, bank/address, instruction count,
    cycles, frame, and relevant unresolved dispatch metadata.
  - Crystal: reproduce and classify every fallback reached by the full route,
    using the existing historical hotspots only as leads.
  - Gate: the release route has zero unexplained fallback; any deliberately
    retained fallback has a written correctness rationale and explicit port
    policy.
  - Evidence: raw `logs/pokemon-crystal/CR-M2-004/`; summary
    `evidence/CR-M2-004.md`.

- [x] **CR-M2-005 — Prove battery-save and MBC3 RTC restart behavior**
  - Depends on: CR-M2-001.
  - Recompiler: preserve stable save identity, battery RAM, RTC registers,
    elapsed-time policy, and failure diagnostics across process restart.
  - Crystal: save in-game, exit, reload, Continue, and verify player state plus
    one controlled RTC-dependent behavior.
  - Gate: primary data and RTC survive two clean restarts; truncation or
    mismatched save data fails visibly without destroying the last good copy.
  - Evidence: raw `logs/pokemon-crystal/CR-M2-005/`; summary
    `evidence/CR-M2-005.md`.

- [x] **CR-M2-006 — Add independent CGB checkpoints**
  - Depends on: CR-M2-002.
  - Recompiler: run the relevant CGB, timer, HDMA, LCD/STAT, and MBC3
    repository/external tests using Pan Docs then SameBoy for interpretation.
  - Crystal: compare selected deterministic route checkpoints against SameBoy
    or another explicitly independent oracle.
  - Gate: checkpoint frames and inspected state agree, or every difference is
    understood and resolved before claiming compatibility.
  - Evidence: raw `logs/pokemon-crystal/CR-M2-006/`; summary
    `evidence/CR-M2-006.md`.

Milestone exit: new-game-to-save-to-continue is deterministic, independently
checked, and free of unexplained fallback or persistence damage.

## M3 — Crystal-aware analysis and metadata

Outcome: imported reverse-engineering knowledge improves names and boundaries
without turning constants or game data into confidently wrong code.

- [x] **CR-M3-001 — Support complete RGBDS symbol syntax**
  - Depends on: CR-M1-004.
  - Recompiler: parse address symbols and constant-only records deliberately,
    either modeling constants or ignoring them with explicit counts.
  - Crystal: load the raw pinned `pokecrystal11.sym` without the preparation
    adapter being required for parser compatibility.
  - Gate: focused parser tests cover valid constants, malformed records,
    duplicates, comments, RAM symbols, and address bounds; Crystal reports the
    expected imported counts.
  - Evidence: raw `logs/pokemon-crystal/CR-M3-001/`; summary
    `evidence/CR-M3-001.md`.

- [x] **CR-M3-002 — Separate naming from analyzer trust**
  - Depends on: CR-M3-001.
  - Recompiler: add an explicit names-only symbol policy; only trusted
    annotations may create function, label, or data boundaries.
  - Crystal: import rich names without seeding tens of thousands of speculative
    functions from ROM data.
  - Gate: names-only and annotated runs preserve imported names while the
    annotated run changes boundaries only where its provenance says it should.
  - Evidence: raw `logs/pokemon-crystal/CR-M3-002/`; summary
    `evidence/CR-M3-002.md`.

- [x] **CR-M3-003 — Model semantic memory spaces in metadata**
  - Depends on: CR-M3-002.
  - Recompiler: distinguish physical ROM, VRAM, external RAM, WRAM, banked
    WRAM, HRAM, MMIO, and constants in imported and generated metadata.
  - Crystal: represent map, party, Pokédex, box, checksum, and RTC anchors with
    the correct memory space, bank, address, width, and provenance.
  - Gate: no WRAM or SRAM anchor can be mistaken for a ROM function ID; schema
    validation rejects overlapping or impossible ranges.
  - Evidence: raw `logs/pokemon-crystal/CR-M3-003/`; summary
    `evidence/CR-M3-003.md`.

- [x] **CR-M3-004 — Export unresolved analysis decisions**
  - Depends on: CR-M3-002.
  - Recompiler: include unresolved indirect jumps, RAM overlays, uncertain
    entry points, data-as-code diagnostics, and fallback relationships in
    metadata.
  - Crystal: map every M2 fallback and undefined-instruction site to an
    actionable metadata record.
  - Gate: a contributor can identify the responsible bank/address, evidence,
    and next annotation without scraping generated C or console text.
  - Evidence: raw `logs/pokemon-crystal/CR-M3-004/`; summary
    `evidence/CR-M3-004.md`.

- [x] **CR-M3-005 — Generate narrow trusted Crystal annotations**
  - Depends on: CR-M2-004, CR-M3-004.
  - Recompiler: keep annotations source-aware, deterministic, validated, and
    independent of emitted names or chunking.
  - Crystal: annotate only confirmed functions, dispatch tables, ROM data, and
    copied-RAM boundaries needed by the route and semantic anchors.
  - Gate: all previously observed false-code and fallback sites are eliminated
    or deliberately classified, with identical route hashes.
  - Evidence: raw `logs/pokemon-crystal/CR-M3-005/`; summary
    `evidence/CR-M3-005.md`.

- [x] **CR-M3-006 — Reconfirm stable hook candidates**
  - Depends on: CR-M3-003, CR-M3-005.
  - Recompiler: prove stable function IDs and patchability survive identical
    regeneration, chunk changes, and symbol-name changes.
  - Crystal: confirm `StartMenu` and `Pokedex` first; keep `BillsPC` and save
    functions observational until persistence work is complete.
  - Gate: hook candidates have returning ROM-backed bodies, known callers,
    replay coverage, and stable IDs; uncertain candidates remain unbound.
  - Evidence: raw `logs/pokemon-crystal/CR-M3-006/`; summary
    `evidence/CR-M3-006.md`.

Milestone exit: Crystal's route and first native feature can rely on metadata
and annotations without reaching into unstable generated implementation names.

## M4 — Semantic bridge and native Workbench

Outcome: a read-only native Crystal surface consumes stable semantic APIs while
the vanilla guest remains behaviorally unchanged.

- [x] **CR-M4-001 — Define the versioned semantic schema**
  - Depends on: CR-M2-006, CR-M3-003.
  - Recompiler: define schema, runtime ABI, exact-ROM, memory-space, type,
    provenance, and access-policy fields with fail-closed validation.
  - Crystal: describe player identity/location, badges, Pokédex flags, party,
    active box, map connections, RTC, and selected species tables.
  - Gate: malformed memory spaces, widths, ranges, versions, or ROM identities
    fail before generated output is written.
  - Evidence: raw `logs/pokemon-crystal/CR-M4-001/`; summary
    `evidence/CR-M4-001.md`.

- [x] **CR-M4-002 — Generate read-only typed accessors**
  - Depends on: CR-M4-001.
  - Recompiler: generate bounded, context-aware accessors without exposing
    emitted function names or raw runtime internals as the public contract.
  - Crystal: read current map, coordinates, party, badges, and Pokédex progress
    from both live state and a local save fixture.
  - Gate: accessors reject wrong mode/bank/size and match independently decoded
    values for every exposed field.
  - Evidence: raw `logs/pokemon-crystal/CR-M4-002/`; summary
    `evidence/CR-M4-002.md`.

- [x] **CR-M4-003 — Expose a narrow port/frontend extension**
  - Depends on: CR-M4-002.
  - Recompiler: provide versioned input, update, read-only semantic-state,
    rendering, logging, and metadata services without coupling headless core
    execution to SDL/ImGui.
  - Crystal: register one port module that can show or hide native UI while
    the original game continues to run.
  - Gate: accurate and headless builds remain available with no graphics
    device, and an unknown ROM or ABI cannot activate the module.
  - Evidence: raw `logs/pokemon-crystal/CR-M4-003/`; summary
    `evidence/CR-M4-003.md`.

- [x] **CR-M4-004 — Ship the read-only Pokégear Workbench slice**
  - Depends on: CR-M4-002, CR-M4-003.
  - Recompiler: support deterministic host presentation sourced from semantic
    accessors.
  - Crystal: display current map/position, party summary, badge and Pokédex
    progress, and one species page with encounter/evolution/move information.
  - Gate: live and save-backed views agree; missing or locked game knowledge is
    represented honestly rather than fabricated.
  - Evidence: raw `logs/pokemon-crystal/CR-M4-004/`; summary
    `evidence/CR-M4-004.md`.

- [x] **CR-M4-005 — Prove the Workbench is observational**
  - Depends on: CR-M4-004.
  - Recompiler: capture semantic events and guest hashes with the module
    disabled, enabled/closed, and enabled/open.
  - Crystal: exercise the complete vanilla route while repeatedly opening and
    closing the Workbench.
  - Gate: guest state, frame, PCM, save, RTC, and fallback results remain
    identical across all three modes except explicitly excluded host UI state.
  - Evidence: raw `logs/pokemon-crystal/CR-M4-005/`; summary
    `evidence/CR-M4-005.md`.

- [x] **CR-M4-006 — Add the native Pokédex replacement**
  - Depends on: CR-M3-006, CR-M4-005.
  - Recompiler: let a verified native hook request host presentation and retain
    the safepoint-correct generated original path.
  - Crystal: bind the confirmed `Pokedex` function to the native screen with a
    user-visible option to use the original screen.
  - Gate: original mode matches the unpatched route; native mode preserves the
    guest callee contract and save state across entry, browsing, and exit.
  - Evidence: raw `logs/pokemon-crystal/CR-M4-006/`; summary
    `evidence/CR-M4-006.md`.

Milestone exit: complete. Crystal has a materially better native
Pokédex/Workbench whose
public contracts survive regeneration and whose read-only operation is proven
non-invasive.

## M5 — Transactional state and native PC

Outcome: native features may modify Crystal state only through validated,
recoverable transactions that the original game accepts.

- [x] **CR-M5-001 — Make battery and RTC persistence transactional**
  - Depends on: CR-M2-005, CR-M4-002.
  - Recompiler: use staged writes, atomic replacement where supported, explicit
    serialization versions, and recoverable failure diagnostics for battery
    RAM and RTC state.
  - Crystal: preserve primary save, backup save, active box, RTC, and stable
    title/hash-based identity.
  - Gate: injected short-write, full-disk, interruption, truncation, and stale
    temporary-file cases retain the last known-good save.
  - Evidence: raw `logs/pokemon-crystal/CR-M5-001/`; summary
    `evidence/CR-M5-001.md`.

- [x] **CR-M5-002 — Implement validated semantic write transactions**
  - Depends on: CR-M4-001, CR-M5-001.
  - Recompiler: expose begin/validate/commit/abort semantics with bounded
    writes, dirty ranges, and replay-visible transaction metadata.
  - Crystal: implement party and box invariants, string encoding, Pokémon
    structure validation, primary/backup checksums, and rollback.
  - Gate: invalid species, counts, sizes, checksums, or interrupted commits
    abort without changing durable state.
  - Evidence: raw `logs/pokemon-crystal/CR-M5-002/`; summary
    `evidence/CR-M5-002.md`.

- [x] **CR-M5-003 — Cross-check writable saves independently**
  - Depends on: CR-M5-002.
  - Recompiler: provide export/import test fixtures without linking GPL
    reference code into the runtime or port.
  - Crystal: compare party, boxes, player data, Pokédex, checksums, and backup
    selection against the original load path and an external Gen II checker.
  - Gate: every port-authored fixture is accepted and decoded identically by
    all three paths; discrepancies block writable UI.
  - Evidence: raw `logs/pokemon-crystal/CR-M5-003/`; summary
    `evidence/CR-M5-003.md`.

- [x] **CR-M5-004 — Ship native PC browsing and organization**
  - Depends on: CR-M5-003.
  - Recompiler: support transactional semantic edits from a native frontend
    without retaining pointers into movable or banked guest state.
  - Crystal: provide box search, sorting, party/box movement, and explicit
    confirmation while preserving mail, held items, names, and box capacity.
  - Gate: scripted edit matrices round-trip through original Crystal and leave
    valid primary/backup saves; cancel performs no write.
  - Evidence: raw `logs/pokemon-crystal/CR-M5-004/`; summary
    `evidence/CR-M5-004.md`.

- [x] **CR-M5-005 — Bind the verified `BillsPC` replacement**
  - Depends on: CR-M3-006, CR-M5-004.
  - Recompiler: preserve pre/original/post and native replacement semantics
    across save-related safepoints and process restart.
  - Crystal: offer native and original PC workflows through the confirmed
    `BillsPC` function ID.
  - Gate: both modes pass the route, persistence, checksum, and failure
    injection suites; native mode never writes outside reviewed transactions.
  - Evidence: raw `logs/pokemon-crystal/CR-M5-005/`; summary
    `evidence/CR-M5-005.md`.

Milestone exit: native party/box changes are recoverable, independently valid,
and accepted by unmodified Crystal logic.

## M6 — Versioned mod packages

Outcome: users can install deterministic exact-ROM extensions without patching
or redistributing the ROM.

- [x] **CR-M6-001 — Define the data-mod manifest**
  - Depends on: CR-M4-001, CR-M4-005.
  - Recompiler: validate package ID/version, exact ROM, semantic schema, runtime
    ABI, dependencies, conflicts, load order, content hashes, and provenance.
  - Crystal: define safe initial overlay targets for encounters, trainer
    parameters, selected rules, accessibility settings, and original assets.
  - Gate: malformed, incompatible, conflicting, escaping, or unknown content
    fails closed before guest execution.
  - Evidence: raw `logs/pokemon-crystal/CR-M6-001/`; summary
    `evidence/CR-M6-001.md`.

- [x] **CR-M6-002 — Apply data overlays without rewriting the ROM**
  - Depends on: CR-M6-001.
  - Recompiler: resolve semantic reads through deterministic overlays while
    retaining the exact original value and an unmodified accurate path.
  - Crystal: load modified encounter or difficulty data from a package at
    startup.
  - Gate: enabling and disabling the package requires no ROM modification or
    regenerated C; inactive mode matches the vanilla route.
  - Evidence: raw `logs/pokemon-crystal/CR-M6-002/`; summary
    `evidence/CR-M6-002.md`.

- [x] **CR-M6-003 — Record mods in deterministic replay provenance**
  - Depends on: CR-M6-002.
  - Recompiler: include ordered package, content, schema, executable, and
    configuration hashes in replay and diagnostic artifacts.
  - Crystal: reproduce a modded route from one portable seed/manifest plus the
    user's exact ROM.
  - Gate: matching inputs reproduce matching results; any package or order
    mismatch is detected before replay.
  - Evidence: raw `logs/pokemon-crystal/CR-M6-003/`; summary
    `evidence/CR-M6-003.md`.

- [x] **CR-M6-004 — Ship two independent sample mods**
  - Depends on: CR-M6-003.
  - Recompiler: demonstrate deterministic composition and useful conflict
    diagnostics through two separately versioned packages.
  - Crystal: ship one encounter/difficulty package and one accessibility or
    information package using original project-owned content only.
  - Gate: each mod works alone and together, conflicts are covered, and removal
    restores vanilla hashes and save compatibility.
  - Evidence: raw `logs/pokemon-crystal/CR-M6-004/`; summary
    `evidence/CR-M6-004.md`.

- [x] **CR-M6-005 — Extend native module composition only from real demand**
  - Depends on: CR-M6-004.
  - Recompiler: evaluate multiple native packages, priority, dynamic loading,
    or a sandboxed portable module format without weakening the v1 exact-ROM
    and safepoint contracts.
  - Crystal: implement one feature that cannot be expressed as a data overlay
    and compare source-built, dynamic, and sandboxed options.
  - Gate: retain only a design that adds a real authoring capability, composes
    deterministically, fails closed, and preserves replay provenance.
  - Evidence: raw `logs/pokemon-crystal/CR-M6-005/`; summary
    `evidence/CR-M6-005.md`.

Milestone exit: complete. Two distributable, ROM-free Crystal mods compose
deterministically and can be removed without regeneration or save damage. The
first source-built native extension adds a separately proven live host
capability without widening the exact-ROM or safepoint boundary.

## M7 — Native presentation and public release

Outcome: the exact-game port has a clearly separated native presentation mode
and can be ejected into a reproducible public repository.

- [x] **CR-M7-001 — Define the native presentation contract**
  - Depends on: CR-M4-006, CR-M6-002.
  - Recompiler: expose renderer-independent map, tile, sprite, UI, timing, and
    shadow-hardware contracts while keeping accurate/headless modes available.
  - Crystal: describe how native presentation consumes semantic map and battle
    state and when the original PPU path remains authoritative.
  - Gate: native presentation cannot activate for an unknown ROM or ABI, and
    headless tests require no GPU.
  - Evidence: raw `logs/pokemon-crystal/CR-M7-001/`; summary
    `evidence/CR-M7-001.md`.

- [x] **CR-M7-002 — Prototype widescreen overworld composition**
  - Depends on: CR-M7-001.
  - Recompiler: submit adjacent map blocks and sprite state through the native
    contract without coupling the core to one graphics API.
  - Crystal: render a bounded overworld scene beyond 160-by-144 using original
    logic and project-owned presentation assets.
  - Gate: camera edges, map connections, priority, occlusion, transitions, and
    guest-visible PPU behavior have explicit tests or conservative fallback.
  - Evidence: raw `logs/pokemon-crystal/CR-M7-002/`; summary
    `evidence/CR-M7-002.md`.

- [x] **CR-M7-003 — Add high-resolution native UI and battle effects**
  - Depends on: CR-M7-001.
  - Recompiler: support original replacement assets with provenance and
    deterministic presentation configuration.
  - Crystal: improve selected menus and battle presentation while retaining
    original rendering as a runtime option.
  - Gate: accurate mode remains unchanged; replacement assets are
    project-owned or compatibly licensed; performance evidence names the host
    graphics stack.
  - Evidence: raw `logs/pokemon-crystal/CR-M7-003/`; summary
    `evidence/CR-M7-003.md`.

- [x] **CR-M7-004 — Create the standalone-repository build contract**
  - Depends on: CR-M4-006, CR-M5-005, CR-M6-004.
  - Recompiler: publish or locate a compatible versioned recompiler/runtime
    release without relying on this dirty parent checkout.
  - Crystal: move only original port sources, manifests, schema tools, tests,
    docs, and fetch automation into the ejectable source tree.
  - Gate: a clean standalone checkout can acquire permitted dependencies,
    verify a user ROM, generate locally, build, and run the vanilla route.
  - Evidence: raw `logs/pokemon-crystal/CR-M7-004/`; summary
    `evidence/CR-M7-004.md`.

- [x] **CR-M7-005 — Add first-run ROM selection and local generation**
  - Depends on: CR-M7-004.
  - Recompiler: expose stable machine-readable generation progress and
    fail-closed diagnostics suitable for a launcher.
  - Crystal: let the user select the exact Rev 1 ROM, validate it locally, and
    create all ROM-derived output in a private local cache.
  - Gate: unsupported ROMs fail before generation; no ROM bytes, paths, saves,
    or derived content enter logs, telemetry, releases, or source control.
  - Evidence: raw `logs/pokemon-crystal/CR-M7-005/`; summary
    `evidence/CR-M7-005.md`.

- [ ] **CR-M7-006 — Prove cross-platform packaging and relocation**
  - Depends on: CR-M7-005.
  - Recompiler: provide reproducible supported releases and generated-project
    relocation across Windows, macOS, and Linux.
  - Crystal: package the launcher and original port assets without the ROM or
    generated game and validate controller-first operation.
  - Gate: clean machines on all three platforms complete first run, route
    smoke, save/restart, mod loading, and uninstall without losing user saves.
  - Evidence: raw `logs/pokemon-crystal/CR-M7-006/`; summary
    `evidence/CR-M7-006.md`.
  - Progress: four-platform exact-ROM package verification and macOS arm64
    physical-controller acceptance are retained; remaining host-family
    controller attestations keep this item open.

- [ ] **CR-M7-007 — Complete public-release review**
  - Depends on: CR-M7-002, CR-M7-003, CR-M7-006.
  - Recompiler: document the supported ABI, compatibility evidence, known
    limitations, and release provenance.
  - Crystal: complete legal review, third-party notices, trademark disclaimer,
    accessibility pass, release smoke, and public documentation.
  - Gate: release archives contain only intended original/permitted files; the
    extracted release reproduces the documented workflow; every claim links to
    current evidence.
  - Evidence: raw `logs/pokemon-crystal/CR-M7-007/`; summary
    `evidence/CR-M7-007.md`.

Milestone exit: a clean public checkout creates the port from a user-selected
exact ROM, supports deterministic mods and native presentation, preserves
accurate mode, and distributes no prohibited content.

## Standard verification matrix

Apply the rows relevant to each item. Passing a narrower row cannot support a
broader claim.

| Change class | Minimum verification |
| --- | --- |
| Documentation or manifest | Schema/link validation, fresh-reader walk-through, ignore-boundary check |
| Recompiler/analyzer/codegen | Root build and CTest, fresh Crystal generation/build/run, metadata comparison |
| Generated execution | Deterministic state, selected frames, PCM where relevant, strict differential window |
| CGB/runtime timing | Focused hardware tests, Pan Docs/SameBoy review, independent Crystal checkpoint |
| Native hook or frontend | Unpatched/original/native comparisons, safepoint route, headless availability |
| Semantic read | Bounds/bank/type failures plus live/save/independent value comparison |
| Semantic write or persistence | Transaction, checksum, restart, corruption, short-write, rollback, independent decoder |
| Mod package | Exact identity, dependency/conflict/load-order failures, replay hashes, uninstall/vanilla recovery |
| Renderer/presentation | Accurate-path invariance, shadow-hardware contract, headless test, GPU/platform provenance |
| Release | Clean checkout, relocated dependency, user-ROM first run, package inventory, three-platform smoke |

## Deferred until a dependency creates real demand

- arbitrary ROM revisions or cross-game semantic identity;
- Japanese, Australian, Virtual Console, or mobile variants;
- copied-RAM and mid-block native hooks;
- online matchmaking before local serial/link behavior is independently
  validated;
- 16-bit species/move expansion before vanilla schemas and saves are stable;
- runtime-native module hot loading before data mods prove package semantics;
- widescreen or GPU work that bypasses the M7 presentation contract;
- claims of full compatibility, fallback-free AOT, or improved performance
  without the corresponding evidence gate.
