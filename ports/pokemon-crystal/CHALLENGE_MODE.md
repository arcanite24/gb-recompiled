# M8 milestone brief — Deterministic Challenge Mode

## Project Description

Challenge Mode is the first gameplay-changing feature built on Crystal
Recompiled's stable exact-ROM extension surfaces. It lets a player configure
deterministic wild- and trainer-battle difficulty at runtime without modifying
the supplied ROM or editing generated C. The milestone must improve GB
Recompiled with a reusable, bounded gameplay-mutation contract and prove that
contract through a complete Pokemon Crystal vertical slice.

## Why This Milestone

Crystal Recompiled already proves exact-ROM native hooks, typed semantic
reads, transactional writes, deterministic data overlays, native UI, replay
provenance, accurate fallbacks, and ROM-free packaging. The next useful test is
not another read-only panel. It is a player-visible rule change that combines
those systems and exposes the narrow capability still missing: safely changing
reviewed gameplay inputs at a known guest lifecycle boundary.

Challenge Mode is a strong recompilation feature because it can use stable
function identity, exact game semantics, typed state, deterministic native
configuration, and original-function fallback together. A generic emulator
does not naturally have that exact-revision semantic contract.

## User-Visible Outcome

A player can open a native Challenge Mode panel and enable or disable a
versioned ruleset. The initial ruleset can adjust wild and trainer levels from
reviewed inputs such as badge progress, party strength, and a configured
offset. The panel explains the active rule, effective level calculation, seed,
and provenance before the next battle begins.

With Challenge Mode disabled, the game behaves exactly like the supported
vanilla ROM. Enabling or removing the feature does not rewrite the ROM,
invalidate the player's save, or require editing generated source. A player can
return to the original behavior at any time.

## Completion Class

Integration. Completion requires a real generated Crystal executable, exact-ROM
battle hooks, native configuration, deterministic replays, save/restart proof,
and original-mode comparison. Unit tests or a synthetic battle model alone are
not sufficient.

## Final Integrated Acceptance

The final acceptance route starts from a clean private cache, enables Challenge
Mode through the native UI, enters one wild battle and one trainer battle, and
shows that both use the reviewed deterministic rule. The same ROM, save,
ruleset, configuration, and input replay must produce the same battle inputs,
guest state, and provenance across three runs.

The route then disables Challenge Mode and proves that the same vanilla route
matches the original-mode state, frame, persistence, and fallback evidence.
After restart and after uninstalling the feature, the unmodified game must load
the existing save successfully. Headless execution and the accurate renderer
must remain available throughout.

Automated panel and route evidence is authoritative for the initial alpha's
determinism and guest state. Physical play on a supported desktop host remains a
post-alpha usability check for controller comprehension and visible results.

## Architectural Decisions

### Never edit generated C

Generated C remains a private, reproducible build artifact rather than an
authoring API. Data changes use semantic overlays, function behavior uses
stable exact-ROM native hooks, guest writes use validated transactions, and UI
uses renderer-neutral port services.

Direct edits to emitted functions were rejected because they couple the feature
to analyzer naming, chunking, regeneration, and compiler output.

### Add a narrow battle-setup mutation contract

The reusable recompiler improvement is a versioned contract for reviewing and
mutating bounded battle-setup inputs at a proven lifecycle boundary. It must
name the exact ROM, hook identity, semantic fields, valid ranges, transaction
rules, and original behavior.

General writable memory access for port extensions was rejected. It would
weaken the semantic and safepoint boundaries far beyond the feature's needs.

### Keep configuration outside the guest save

Challenge Mode configuration is host-owned, versioned, and hashed. It must not
consume undocumented save bytes or change the original save schema. The active
configuration and ruleset identity enter replay and diagnostic provenance.

Embedding new state in the Crystal save was rejected for the first slice
because it creates compatibility, checksum, backup-save, and uninstall risks
without improving the battle rule itself.

### Preserve an exact vanilla path

Disabled mode must avoid the gameplay mutation and retain the generated
original-function path. Unsupported state, invalid configuration, unknown hook
identity, or validation failure must not apply a partial rule.

Silently approximating a level or continuing after a partial transaction was
rejected because it would make replays and saves ambiguous.

### Start with levels, not roster synthesis

The first integrated ruleset changes reviewed wild and trainer levels. It may
read badges and party strength, but it does not invent species, moves, items,
AI scripts, or trainer parties. Those require additional semantic schemas and
separate design decisions.

## Error Handling Strategy

Challenge Mode fails closed before mutating guest state. Invalid manifests,
ruleset versions, hashes, hook identities, semantic ranges, configuration
values, or transaction state produce a stable diagnostic and retain the
original game behavior when safe to do so. A violation discovered after a
transaction begins aborts the complete transaction; no partial battle setup is
committed.

Configuration loading reports whether the file is missing, unsupported,
malformed, or incompatible. A missing configuration means disabled vanilla
mode. An incompatible or malformed explicitly selected configuration is shown
to the player and is not silently replaced with defaults.

Replay startup rejects mismatched ruleset, configuration, executable, ROM, or
mod provenance before guest execution. Save failures continue to use the
existing transactional persistence and last-good-copy behavior.

## Risks and Unknowns

- The exact wild- and trainer-battle lifecycle hooks must be reconfirmed as
  returning, route-covered, and safepoint-correct before binding.
- Battle level data may move through temporary WRAM or copied structures whose
  authoritative window is narrower than the existing semantic schema.
- Badge-based and party-based scaling can create undesirable difficulty curves
  even when technically deterministic; the first ruleset needs conservative
  caps and transparent calculations.
- Trainer and wild initialization may require separate hook shapes even if
  they share one public mutation model.
- Configuration changes take effect only at the next battle boundary; the
  native panel exposes that timing before Apply.

## Existing Codebase / Prior Art

- The native-patch SDK provides exact-ROM stable function bindings,
  pre/original/post sequencing, deferred original execution, and fail-closed
  callback behavior.
- The semantic runtime provides typed reads plus staged, validated,
  runtime-owned transactions.
- The Crystal Workbench and native PC prove native controller-driven UI and a
  reviewed synchronous semantic edit.
- Data-mod packages prove deterministic exact-ROM overlays, dependency and
  conflict handling, removal, and replay provenance.
- The native Pokédex and Bill's PC replacements prove user-selectable native
  and original paths.
- The vanilla truth route, battle checkpoints, differential windows, save
  restart tests, and packaged verifier provide the acceptance foundation.
- Encounter Lens proves a reduced read-only source-built extension can display
  live semantic and overlaid encounter data without changing guest state.

## Relevant Requirements

- **M8-R1 — Exact identity:** every mutation applies only to the supported ROM,
  reviewed hooks, schema, and ruleset version.
- **M8-R2 — Determinism:** ROM, save, ruleset, configuration, seed, executable,
  mods, and replay input completely determine the result.
- **M8-R3 — Bounded mutation:** only reviewed battle-setup fields can change,
  through an atomic validated transaction.
- **M8-R4 — Vanilla equivalence:** disabled and removed modes reproduce the
  original route and retain compatible saves.
- **M8-R5 — Native usability:** a controller user can understand, configure,
  and inspect the active rule without using developer tools.
- **M8-R6 — Reusable recompiler value:** the battle mutation mechanism is a
  versioned game-agnostic contract with Crystal-specific schema and policy
  supplied by the port.
- **M8-R7 — Legal boundary:** the feature, configuration, tests, and release
  artifacts contain no ROM bytes, extracted assets, generated game source, or
  unlicensed upstream content.

## Scope

### In Scope

- One versioned Challenge Mode ruleset.
- Wild- and trainer-level adjustment at reviewed initialization boundaries.
- Badge progress, strongest-party level, bounded offsets, and conservative
  minimum/maximum levels as candidate rule inputs.
- Native controller-friendly configuration and rule explanation.
- Host-owned configuration persistence and complete provenance.
- A reusable bounded gameplay-mutation contract in GB Recompiled.
- Exact original-mode fallback, deterministic replay, restart, removal, and
  save-compatibility proof.

### Out of Scope

- Species, move, item, ability, AI, experience-curve, or trainer-party
  synthesis.
- Randomized rosters or world randomization.
- New quests, maps, scripts, NPCs, dialogue, or story progression.
- New save fields or migration of existing Crystal saves.
- Dynamic native libraries, arbitrary plugins, scripting VMs, or writable
  extension memory.
- Online services, telemetry, matchmaking, or cloud configuration.
- Support for additional Crystal revisions or languages.
- Completing the remaining public-release legal and host-controller gates.

### Non-Goals

- Rebalancing every battle in the game in the first milestone.
- Claiming an objectively ideal difficulty curve.
- Replacing the accurate PPU or battle renderer.
- Creating a generic cheat engine or arbitrary memory editor.

## Technical Constraints

- CMake and Ninja remain the supported build path.
- The exact-ROM and generated-project fail-closed contracts remain mandatory.
- Native hooks must preserve the original-body scheduling and safepoint model.
- Guest writes must be staged, validated, atomic, range-bounded, and abortable.
- Headless builds cannot require SDL, ImGui, a GPU, or an interactive window.
- Runtime configuration must use a documented writable user-data location and
  must not enter the public package or generated source inventory.
- Normal release performance measurements remain instrumentation-off.
- No copyrighted ROM, save, generated source, extracted asset, or private path
  may enter tracked evidence.

## Integration Points

- Native-patch dispatch and generated stable function metadata.
- Crystal semantic schema and generated accessors.
- Runtime-owned semantic transactions and validation callbacks.
- Port module input, bounded presentation commands, and controller mappings.
- Data-mod and source-built extension manifests where ruleset composition
  creates real demand.
- Replay, state, diagnostic, and generation provenance.
- Private-cache launcher configuration and persistence.
- Crystal route validation, differential execution, save restart, and packaged
  release verification.

## Testing Requirements

Unit tests cover rule arithmetic, bounds, caps, schema validation, malformed
configuration, hash mismatch, wrong ROM, wrong hook identity, transaction
abort, and provenance serialization. Synthetic native-patch tests prove
original execution occurs at most once and that post behavior remains attached
to the guest call frame.

Generated integration tests cover one wild and one trainer battle in disabled
and enabled modes. Three equivalent enabled runs must produce identical
battle-input, state, replay, and provenance hashes. Strict differential checks
cover disabled/original mode and any enabled windows where both paths are
expected to agree before the deliberate mutation.

Persistence tests cover restart, save and Continue, malformed configuration,
feature removal, and reinstallation without save changes. Headless tests prove
the feature does not require a graphics device. Physical controller acceptance
remains post-alpha usability evidence and does not replace deterministic route
proof.

The final gate includes the repository-owned test suite plus fresh generation,
configure, build, complete route execution, package reconstruction, and
path-free evidence from the exact implementation commit.

## Acceptance Criteria

- The player can enable and disable one named, versioned Challenge Mode
  ruleset from native controller-driven UI.
- The UI displays the inputs and effective calculation for the next applicable
  battle without exposing private paths or raw guest memory.
- One wild battle and one trainer battle receive the expected bounded level
  adjustment.
- Three equivalent enabled runs produce identical guest and provenance
  evidence.
- Disabled mode matches the retained vanilla route for state, selected frames,
  persistence, and fallback behavior.
- Invalid or incompatible configuration cannot partially mutate a battle.
- Replay startup rejects any ruleset, configuration, ROM, executable, mod, or
  input mismatch before execution.
- Save, restart, Continue, uninstall, and reinstall preserve the player's
  original compatible save.
- The implementation edits no generated C and requires no ROM rewrite.
- Headless and accurate presentation modes remain available.
- The reusable mutation contract is versioned, documented, independently
  tested with a legal synthetic fixture, and consumed by Crystal.
- Tracked evidence is ROM-free, path-free, provenance-complete, and names all
  manual verification boundaries.

## Resolved Decisions

- The supported exact ROM uses `LoadEnemyMon` (`gbfn:v1:000f:68eb`) for the
  reviewed wild boundary and `TryAddMonToParty` (`gbfn:v1:0003:588c`) for the
  guarded opposing-trainer-party boundary.
- Strongest-party level means the highest conscious party member; empty and
  fully fainted parties fail closed to the original level.
- Ruleset v1 combines the strongest conscious party level, badge progress, a
  bounded configurable offset, and hard minimum/maximum levels. The locked
  acceptance configuration uses offset `-1`.
- Configuration edits remain a draft until the user selects Apply. The panel
  shows every effective input and the calculated next result before commit.
- The shared gameplay-mutation ABI exposes one atomic transaction mechanism
  with typed wild and trainer event inputs; Crystal supplies the guards,
  semantic fields, and policy.
- The clean Crystal package selects the ROM-free Challenge native patch during
  local generation, but the persisted host configuration is absent/disabled by
  default. No runtime package loader or ROM rewrite is required.
