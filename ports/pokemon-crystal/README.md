# Pokémon Crystal native port

Status: vanilla route, independent CGB checkpoints, and deterministic Challenge
Mode package acceptance verified for the initial alpha scope.

This is the pilot exact-game project for GB Recompiled. The goal is not merely
to launch Pokémon Crystal in a native executable. The goal is to preserve the
original game while adding game-aware native surfaces, a durable mod contract,
and eventually presentation features that depend on knowing Crystal's
functions and data structures.

The working product name is **Crystal Recompiled**. It is not affiliated with
Nintendo, The Pokémon Company, Game Freak, or Creatures.

## Exact ROM contract

The local ROM already present in the parent repository is the supported
US/Europe Rev 1 release:

| Property | Value |
| --- | --- |
| Title | `PM_CRYSTAL` |
| Size | 2,097,152 bytes |
| SHA-1 | `f2f52230b536214ef7c9924f483392993e226cfb` |
| SHA-256 | `fdcc3c8c43813cf8731fc037d2a6d191bac75439c34b24ba1c27526e6acdc8a2` |
| Cartridge | MBC3 with timer, RAM, and battery |
| Hardware | CGB-only |

This identity matches the `pokecrystal11` build documented by the pret
disassembly. Any future release must ask the user for this ROM and reject every
other revision by default.

## Local setup

From this directory:

```bash
python3 scripts/verify_rom.py
python3 scripts/references.py verify
./scripts/probe-symbols.sh
```

`scripts/prepare_symbols.py` still reproduces the historical address-only
Phase 0 projection, but generation and the import probe now consume the pinned
raw RGBDS `.sym` file directly.

`references/vendor/`, prepared symbols, generated projects, ROMs, saves, and
captures are local-only. To reproduce the pinned reference checkout on a new
machine:

```bash
python3 scripts/references.py fetch
```

The reference downloader never fetches a commercial ROM.

Generated Crystal projects belong under the parent repository's
`output/pokemon-crystal-*` paths. Replays, state dumps, captures, and
diagnostic logs belong under `logs/pokemon-crystal/`. Both locations are
ignored local evidence, following the parent project's artifact conventions.

After creating the fresh parent recompiler build required by the
[execution backlog](BACKLOG.md), generate a provenance-locked project with:

```bash
python3 scripts/generate.py \
  --output ../../output/pokemon-crystal-baseline
```

The generator refuses an existing destination, a path outside the parent's
`output/pokemon-crystal-*` namespace, an unsupported ROM, or unverified
references. It accepts explicit recompiler, runtime, symbol, annotation,
native-patch, analysis, code-generation, and CMake profile inputs. The
port-owned `route/analysis-entry-points.json` is also consumed by default and
hashed into the receipt; it contains the exact analysis roots required to
close the checked route's generated-to-interpreter fallback sites. The
default symbol policy is `names-only`. Raw RGBDS records therefore provide
readable names and same-address aliases but contribute zero analyzer
boundaries. The reviewed `annotations/crystal-route.annotations` file is the
generator default and is recorded in metadata with `annotation` provenance;
use `--annotations none` only for an explicit unannotated control. It contains
25 route-proven global functions and 13 exact semantic data ranges.
Mid-function fallback recovery roots remain explicit analysis entry points
rather than being mislabeled as functions. All-bank analysis and aggressive
scan remain enabled because the established route currently requires their
coverage; `--analysis-scope reachable --scan off` is an explicit diagnostic
profile and is not release-safe yet. The generated tree contains:

- a path-independent `crystal-generation.json` receipt with input hashes;
- `crystal-build-profile.cmake`, which freezes the requested generated build
  profile;
- `crystal_widescreen_probe`, a headless renderer-neutral proof tool for the
  bounded New Bark / Route 29 presentation scene;
- `crystal_battle_probe`, a deterministic 1280-by-720 CPU reference for the
  typed native battle scene;
- the checked CC0 replacement-asset package under
  `port/assets/presentation`; and
- the current generated metadata and self-contained runtime snapshot.

Generated metadata classifies imported addresses as physical ROM, VRAM,
external RAM, WRAM, banked WRAM, HRAM, MMIO, or another explicit hardware
space. RGBDS constants are exported separately in the `constants` collection
and cannot become function IDs. The reviewed exact-ROM semantic anchors live
in `semantic/anchors.json`; validate their spaces, banks, ranges, widths,
aliases, RTC selectors, and generated-metadata matches with:

```bash
python3 scripts/validate_semantic_anchors.py \
  --manifest semantic/anchors.json \
  --schema semantic/anchor-schema.json \
  --metadata \
    ../../output/pokemon-crystal-baseline/pokemon_crystal_metadata.json
```

Raw `.sym` records do not carry object sizes, so their generic generated
metadata width is one byte. Multi-byte public candidates use the explicitly
reviewed widths in `semantic/anchors.json`.

To reconcile generated analysis decisions with the historical M2 fallback
inventory and the current generation's undefined-opcode sites:

```bash
python3 scripts/validate_analysis_diagnostics.py \
  --metadata \
    ../../output/pokemon-crystal-baseline/pokemon_crystal_metadata.json \
  --fallback-root \
    ../../logs/pokemon-crystal/CR-M2-004/inventory-20260729 \
  --generation-log ../../logs/pokemon-crystal/generation.stdout
```

The validator requires a stable bank/address, evidence, relationship, and next
annotation for every record. Historical fallback targets must map to explicit
configured entry-point diagnostics; undefined-opcode sites must map to
generated false-code diagnostics.

Validate the reviewed annotation set against the pinned symbols, semantic
manifest, recovery roots, and an unannotated metadata control with:

```bash
python3 scripts/validate_annotations.py \
  --annotations annotations/crystal-route.annotations \
  --symbols references/vendor/pokecrystal-symbols/pokecrystal11.sym \
  --semantic-manifest semantic/anchors.json \
  --entry-points route/analysis-entry-points.json \
  --metadata ../../output/pokemon-crystal-baseline/pokemon_crystal_metadata.json \
  --baseline-metadata \
    ../../output/pokemon-crystal-unannotated/pokemon_crystal_metadata.json
```

The versioned public-semantic input is `semantic/package.json`, governed by
`semantic/package-schema.json`. Package version 4 retains runtime ABI
`gbrecomp.semantic` version 1, the complete party and active-box transactional
views, and typed battle views for the live player structure, active party
slot, enemy, and context.
Generation validates the exact ROM, ABI and package versions, memory spaces,
banks, widths, ranges, overlaps, field types, provenance, and per-view access
policy before creating the destination. The validated package and schema are
copied into the generated tree and hashed in `crystal-generation.json`.

Generated `semantic/crystal_semantic.h` exposes bounded party and active-box
staging plus the validator used with the runtime transaction lifecycle:

```c
GBSemanticTransaction transaction = {0};
gbrt_semantic_transaction_begin(
    &transaction, context, actual_rom_sha256,
    CRYSTAL_SEMANTIC_ROM_SHA256);
crystal_semantic_stage_party(&transaction, party, party_size);
gbrt_semantic_transaction_validate(
    &transaction, crystal_semantic_validate_transaction, NULL);
gbrt_semantic_transaction_commit(&transaction);
```

Party staging updates live WRAM and both durable save copies, then recomputes
both Crystal checksums and check values. Active-box staging writes its exact
1,102-byte working mirror and the canonical selected box in ERAM bank 2 or 3.
Validation checks both records and requires them to agree. Save-backed readers
validate primary and backup check values and checksums, prefer the primary,
fall back to the backup, and fail closed when neither copy is valid.

Validation also rejects invalid counts, species, terminators, levels, HP
bounds, encoded names, and record sizes.
`crystal_semantic_encode_name` converts the supported ASCII subset to Crystal's
11-byte terminated encoding. Failed validation, explicit abort, or a failed
atomic persistence commit leaves both guest memory and durable state
unchanged.

To exercise those contracts against a local user-owned save without changing
it:

```bash
python3 scripts/verify_semantic_transactions.py \
  --save /path/to/pokemon_crystal.sav \
  --rom /path/to/Pokemon-Crystal-Rev-1.gbc \
  --accessor-dir ../../output/pokemon-crystal-baseline/semantic \
  --output-dir ../../logs/pokemon-crystal/semantic-transactions
```

The CR-M5-003 writable-save verifier exports deterministic fixtures, reloads
each one through the original game, compares generated accessors with a
separately implemented decoder, and invokes a pinned PKHeX.Core checkout as an
external process:

```bash
python3 scripts/verify_writable_saves.py \
  --save /path/to/pokemon_crystal.sav \
  --rom /path/to/Pokemon-Crystal-Rev-1.gbc \
  --accessor-dir ../../output/pokemon-crystal-baseline/semantic \
  --executable ../../output/pokemon-crystal-baseline/build/pokemon_crystal \
  --dotnet /path/to/dotnet \
  --pkhex-dir references/vendor/pkhex \
  --output-dir ../../logs/pokemon-crystal/writable-save-validation
```

The ROM, source save, emitted fixtures, PKHeX checkout, and generated probes
are evidence inputs and remain outside tracked source.

## Data overlays

Validated ROM-free packages can change the reviewed Route 29 encounter table
at process startup without patching the ROM or regenerating this project.
`tools/compile_crystal_data_mod.py` converts stable map/time/slot/species
identities into a private exact-ROM `.gbdm` artifact. Run the same generated
executable with `--data-mod <artifact>` to enable it, or omit the option for
the unmodified accurate path. The runtime retains both the overlaid semantic
ROM view and an explicit exact-original view. See the root
[data-mod contract](../../DATA_MODS.md).

The portable seed in `replay/route29-seed.json` records the two-segment
cycle-anchored Route 29 proof. `tools/create_data_mod_replay.py` turns it and a
locally resolved package set into one self-contained replay manifest.
`scripts/verify_data_mod_replay.py` verifies the complete provenance envelope
before starting the guest, reproduces the selected frame/state oracles, and
emits a path-independent reproduction hash.

Two ROM-free samples are ready to install under [`mods/samples`](mods/README.md):
`route29-level-five` is a difficulty package that preserves the original
encounter roster, and `route29-encounter-guide` is an information package that
adds a compact time-of-day hint to the west Route 29 sign. Both work alone or
as one deterministically ordered set. Removing `--data-mod` restores the
vanilla frames/state and accepts the same save bytes.

## Encounter Lens source-built extension

The first feature that requires native extension composition is the Route 29
Encounter Lens. It uses F3 (or deterministic
`--port-input-frame <frame>:encounters`) to show the current time period,
encounter rate, and seven live slots. Because it reads the active semantic ROM
view, the level-five sample mod is reflected immediately. Opening the lens is
observational and leaves guest state and saves unchanged.

Build it into a fresh generated project with:

```bash
python3 scripts/generate.py \
  --port-module module/port-module.json \
  --port-extension \
    native-extensions/encounter-lens/manifest.json \
  --output ../../output/pokemon-crystal-encounter-lens
```

The exact ordered extension set and hashes are recorded in the generation
receipt. Source-built static composition is the only retained native extension
model; dynamic native libraries and sandboxed bytecode were evaluated but are
not supported. See [NATIVE_EXTENSIONS.md](NATIVE_EXTENSIONS.md) for the
capability boundary and decision record.

The optional widescreen/high-resolution path is governed separately by
[PRESENTATION.md](PRESENTATION.md). It combines reviewed Crystal semantic
scene state with a renderer-independent copy of accurate PPU tiles, maps,
sprites, palettes, timing, UI commands, and fallback pixels. The accurate PPU
continues to run and remains guest-visible authority in every mode.

## Standalone source contract

`scripts/eject.py` creates a fresh standalone source repository from a strict
allowlist. It includes the original port sources, manifests, schemas, tests,
documentation, fetch automation, CC0 assets, the project license, and the
three generic data-mod tools used by the checked workflows. It excludes local
ROMs, saves, generated projects, executables, object files, symbol dumps,
reference checkouts, caches, and Python bytecode. `SOURCE-MANIFEST.json`
records every emitted file, size, and SHA-256.

GB Recompiled 0.1.0 exposes `--version-json`. Its release identity includes the
semantic, native-patch, port-module, port-extension, data-mod, and presentation
ABI versions plus the generation features Crystal requires.
`tools/create_gbrecomp_distribution.py` creates a self-contained SDK
distribution with a complete `gbrecomp-release.json` inventory and clean
runtime-tree hash.

Inside an ejected checkout, `scripts/bootstrap.py` verifies every release
file, the CLI identity, exact compatible ABI set, required features, and
runtime tree before installing ignored local dependencies. The generation
reference scope downloads only the commit-addressed
`pokecrystal11.sym` raw file and verifies the SHA-256 from
`references/sources.lock.json`; full Git reference/oracle checkouts remain an
explicit developer option.

The standalone tree deliberately keeps the same
`ports/pokemon-crystal` layout. `scripts/first_run.py` presents a native ROM
file picker (or accepts `--rom` for headless use), validates the exact Rev 1
identity before creating a cache, and generates/builds below the platform's
private user cache. It refuses a cache inside the checkout. Ordinary child
process output is discarded; the retained launcher and recompiler JSON Lines
streams contain only versioned stage and error codes, never selected paths.
Generation pins the stable `pokemon_crystal` output prefix, so a user's ROM
filename cannot enter generated filenames or metadata. Receipts retain the
supported ROM hash and artifact hashes but not the source path, ROM bytes, or
save data.

The existing generator remains available for engineering work and writes
below the checkout's ignored `output/pokemon-crystal-*` namespace. In either
flow it consumes the installed `build/bin/gbrecomp`, `runtime`, and reference
cache without a parent-source fallback.

`scripts/create_release.py` combines an inventoried ejected source tree with a
matching platform SDK, adds thin launch wrappers, and emits a deterministic
tar.gz or zip archive. The wrapper bootstraps the embedded SDK, reuses a
receipt-verified private build after relocation, always directs saves to the
external private cache, and passes an optional `--data-mod` artifact through
to the fail-closed runtime. See [PACKAGING.md](PACKAGING.md).

## Native PC

Port ABI v3 lets the Workbench submit one synchronous, runtime-owned semantic
edit without receiving `GBContext` or retained guest-memory pointers. The
native PC browses all 14 boxes, searches by species, sorts the selected box by
species or level, and moves Pokémon between party and box through an explicit
stage/confirm interaction. Back cancels without starting a transaction.

Generated box accessors normalize Crystal's legacy never-opened empty-box
encodings on read. Authored boxes remain strict, and an active-box edit updates
both its canonical record and working mirror. Ordinary held items, names,
moves, and complete boxed records are preserved. Mail-bearing moves, a
last-party-member deposit, and capacity overflow fail before staging.

Run the complete UI, original-load, checksum, and PKHeX matrix with:

```bash
python3 scripts/verify_native_pc.py \
  --save /path/to/pokemon_crystal.sav \
  --rom /path/to/Pokemon-Crystal-Rev-1.gbc \
  --accessor-dir ../../output/pokemon-crystal-baseline/semantic \
  --executable ../../output/pokemon-crystal-baseline/build/pokemon_crystal \
  --dotnet /path/to/dotnet \
  --pkhex-dir references/vendor/pkhex \
  --output-dir ../../logs/pokemon-crystal/native-pc-validation
```

The exact-ROM patch at `native-patches/bills-pc/manifest.json` binds
`gbfn:v1:0005:5668` (`BillsPC`). `--native-presentation native` opens the
native PC while retaining the original body for guest timing and save-related
safepoints; `original` bypasses the host surface. If the native surface is
unavailable, the replacement fails closed before a semantic transaction or
save write.

To compare two fresh generations before either has build state:

```bash
python3 scripts/compare_generated.py \
  ../../output/pokemon-crystal-first \
  ../../output/pokemon-crystal-second
```

Only a generated `build/` tree and host UI state are excluded from that
comparison. Source, metadata, receipts, ROM-bearing generated files, and the
runtime snapshot must match byte for byte.

To prove the naming/trust boundary against a separately annotated control:

```bash
python3 scripts/compare_symbol_policies.py \
  --names-only ../../output/pokemon-crystal-names \
  --annotated ../../output/pokemon-crystal-annotated \
  --expected-function-id gbfn:v1:0001:5b04 \
  --expected-source-symbol IntroMenu_DummyFunction \
  --output ../../logs/pokemon-crystal/symbol-policy-comparison.json
```

Configure and build the generated project with its frozen profile:

```bash
cmake -G Ninja \
  -C ../../output/pokemon-crystal-baseline/crystal-build-profile.cmake \
  -S ../../output/pokemon-crystal-baseline \
  -B ../../output/pokemon-crystal-baseline/build
ninja -C ../../output/pokemon-crystal-baseline/build
```

To run the complete route with fail-closed fallback accounting:

```bash
python3 scripts/validate_route.py \
  --manifest route/manifest.json \
  --executable ../../output/pokemon-crystal-baseline/build/pokemon_crystal \
  --generation-receipt ../../output/pokemon-crystal-baseline/crystal-generation.json \
  --evidence-dir ../../logs/pokemon-crystal/route-fallback-gate \
  --fallback-policy route/fallback-policy.json
```

The checked fallback policy is intentionally empty. A new fallback, a stale
policy entry, an incomplete runtime inventory, or a dropped diagnostic site
fails the route gate.

To build the exact-ROM native Pokédex integration, pass its checked patch
package during generation:

```bash
python3 scripts/generate.py \
  --native-patch native-patches/pokedex/manifest.json \
  --output ../../output/pokemon-crystal-native
```

Generated executables with that package default to the native presentation.
Use `--native-presentation original` for Crystal's unmodified screen or
`--native-presentation native` for the host overlay. The native binding opens
the port-owned Pokédex surface and deliberately retains the generated original
callee underneath it. This preserves Crystal's farcall return frame, timing,
mapper state, and save-side effects while native controls browse and close the
overlay. Headless route evidence can schedule those controls with repeatable
`--port-input-frame <frame>:<action>` arguments.

The route manifest also fixes the RTC wall clock and explicitly ignores the
persisted `.rtc` file at each segment boundary. This preserves the established
long, cycle-anchored gameplay route from time-of-day NPC drift; it is disclosed
test isolation, not normal play behavior. CR-M2-005 separately exercises real
RTC loading and elapsed-time behavior across clean process restarts without
that switch.

To reproduce the independent CGB checkpoints, build the pinned SameBoy Core
and its MIT-licensed boot ROM first. Building the boot ROM requires RGBDS:

```bash
make -C references/vendor/sameboy tester bootroms
```

Then replay the checked new-game and restart inputs through the port-owned
oracle:

```bash
python3 scripts/run_sameboy_oracle.py \
  --rom /path/to/Pokemon-Crystal-Rev-1.gbc \
  --input route/inputs/new-game.json \
  --frames 770,2509,8250 \
  --frame-limit 8250 \
  --rtc-unix-time 1700000000 \
  --output-dir ../../logs/pokemon-crystal/sameboy-new-game

python3 scripts/run_sameboy_oracle.py \
  --rom /path/to/Pokemon-Crystal-Rev-1.gbc \
  --battery ../../logs/pokemon-crystal/route/persistence/pokemon_crystal.sav \
  --input route/inputs/restart-continue.json \
  --frames 1500 \
  --frame-limit 1500 \
  --rtc-unix-time 1700000000 \
  --output-dir ../../logs/pokemon-crystal/sameboy-restart
```

The driver rejects any ROM or SameBoy checkout that differs from the pinned
identities. `compare_sameboy_checkpoints.py` compares the captures in the
original 5-bit CGB color domain and checks selected PC, SP, and semantic WRAM
anchors. It does not distribute the ROM, battery data, generated executable,
or upstream reference checkout.

For a fast boot smoke that leaves an unambiguous 120 completed frames in the
state schema:

```bash
../../output/pokemon-crystal-baseline/build/pokemon_crystal \
  --headless \
  --no-audio \
  --limit-frames 121 \
  --dump-state ../../logs/pokemon-crystal/headless-state.json
```

Battery and RTC files are written beside the executable on desktop unless
`--save-dir` selects an existing isolated directory. Move or isolate them
before a clean-state reproduction.

## Read next

- [Port plan](PLAN.md)
- [Challenge Mode](CHALLENGE_MODE.md)
- [Execution backlog](BACKLOG.md)
- [Research inventory](REFERENCES.md)
- [Initial semantic anchors](SEMANTIC_ANCHORS.md)
- [Distribution boundary](LEGAL.md)
- [Public release review](RELEASE.md)
- [Third-party notices](THIRD_PARTY_NOTICES.md)
- [Data-mod package policy](mods/target-policy.json)
- [Pinned source manifest](references/sources.lock.json)
