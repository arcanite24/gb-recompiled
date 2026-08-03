# Data-mod packages

GB Recompiled data mods are ROM-free, declarative packages. Version 1 defines
identity, exact-game compatibility, deterministic composition, content
integrity, provenance, and an immutable runtime overlay artifact.

The generic package shape is documented by
`ports/pokemon-crystal/mods/package-schema.json`. Crystal's independently
versioned `target-policy.json` is the authority for the exact ROM, port,
semantic contract, allowed content targets, extensions, and size limits.

## Package contract

Every manifest declares:

- a reverse-domain-like package ID and semantic version;
- data-mod runtime ABI `gbrecomp.data-mod` v1;
- exact port, ROM, semantic-package, and semantic-schema identities;
- a numeric load order, exact-version dependencies, and package conflicts;
- SPDX-style license, authors, HTTPS source, and content origin; and
- one or more confined content files with stable IDs, known targets, and
  lowercase SHA-256 hashes.

The initial Crystal target policy permits bounded declarative content for
encounters, trainers, selected rules, accessibility settings, and
project-owned or compatibly licensed host assets. It does not permit
executable code, arbitrary guest addresses, ROM replacement data, or unknown
target types.

## Validation

Validate the complete installed set in one invocation:

```bash
python3 tools/validate_data_mods.py \
  --policy ports/pokemon-crystal/mods/target-policy.json \
  --package-schema ports/pokemon-crystal/mods/package-schema.json \
  --semantic-package ports/pokemon-crystal/semantic/package.json \
  --semantic-schema ports/pokemon-crystal/semantic/package-schema.json \
  --manifest /path/to/base/package.json \
  --manifest /path/to/addon/package.json \
  --output /path/to/resolved-mods.json
```

The validator reads and hashes every content file, rejects symlinks and path
escape, checks the installed semantic files against the policy, resolves exact
dependencies and conflicts, and emits a deterministic `(load order, package
ID)` sequence. Any error exits nonzero and produces no trusted resolution.

Package directories, user paths, and content are not logged by the generated
runtime in v1. Portable replay manifests record the ordered stable identities
and hashes without exposing local paths.

## Crystal encounter overlays

Crystal's first applied target is `crystal.encounters.v1`. Its JSON content is
keyed by reviewed identities rather than addresses:

```json
{
  "schema": "crystal.encounters",
  "version": 1,
  "changes": [
    {
      "map": "ROUTE_29",
      "time": "all",
      "slot": 0,
      "level": 5,
      "species": "HOPPIP"
    }
  ]
}
```

The current tracer bullet accepts the reviewed Route 29 map, its seven slots,
`morning`, `day`, `night`, or `all`, levels 1–100, and the species present in
the original Route 29 table. Unknown fields, maps, times, slots, species,
duplicate identities, or a changed exact-ROM table signature fail closed.

Crystal also applies `crystal.accessibility.v1` information-sign content. Its
initial reviewed surface is the west Route 29 sign, addressed by the stable
`ROUTE_29` / `WEST_SIGN` identity. A package supplies a title and two
1–18-character uppercase lines from a bounded Game Boy character set. The
compiler verifies the exact original sign bytes and fixed allocation before
emitting the replacement; authors never provide a ROM offset.

After package validation, the user compiles the resolved set against their
local ROM:

```bash
python3 tools/compile_crystal_data_mod.py \
  --resolution /path/to/resolved-mods.json \
  --rom /path/to/Pokemon-Crystal-Rev-1.gbc \
  --output /path/to/mods.gbdm \
  --report /path/to/mods-report.json

./output/pokemon-crystal/build/pokemon_crystal \
  --data-mod /path/to/mods.gbdm
```

The compact `.gbdm` file is a private derived artifact. It is bound to the
exact ROM hash and contains sorted expected/replacement byte pairs generated
from semantic identities. The runtime validates all entries before activation
and never writes them into `GBContext.rom`. Removing the option immediately
restores vanilla behavior; neither mode requires regenerating C.

## Shipped Crystal samples

Two independently useful, MIT-licensed, project-authored examples live under
`ports/pokemon-crystal/mods/samples/`:

- `route29-level-five` keeps the original Route 29 species and time-of-day
  roster while raising all 21 encounter slots to level 5;
- `route29-encounter-guide` changes one Route 29 sign into a compact day/night
  encounter hint without changing gameplay or save data.

See `ports/pokemon-crystal/mods/README.md` for exact commands. Each package
works alone. Together they resolve by `(load order, package ID)` and compile
to the exact union of their disjoint entries. Declared package conflicts fail
during validation. Overlapping semantic claims fail during compilation with
the semantic identity, byte offset, and both package/content IDs; stale output
is removed.

## Portable replay provenance

An input script alone is insufficient for a modded replay. Create a portable
replay envelope after compiling the resolved package set:

```bash
python3 tools/create_data_mod_replay.py \
  --seed ports/pokemon-crystal/replay/route29-seed.json \
  --rom /path/to/Pokemon-Crystal-Rev-1.gbc \
  --executable output/pokemon-crystal/build/pokemon_crystal \
  --generation-receipt output/pokemon-crystal/crystal-generation.json \
  --resolution /path/to/resolved-mods.json \
  --compile-report /path/to/mods-report.json \
  --artifact /path/to/mods.gbdm \
  --output /path/to/route29-replay.json
```

The single JSON envelope embeds the small private overlay artifact and exact
cycle-anchored input files. It records:

- exact ROM, executable, generation receipt, and generated-source inventory;
- ordered package IDs, versions, manifest hashes, content IDs/targets/hashes,
  and their canonical package-set hash;
- target-policy, package-schema, semantic-manifest, and semantic-schema hashes;
- a canonical runtime-configuration hash;
- per-segment input, frame, and selected-state expectations; and
- a canonical portable-seed hash over the game, mods, configuration, and
  ordered segments.

Crystal's replay driver validates the entire envelope, the user's ROM, the
local executable and receipt, the embedded artifact, package order, all
content/schema hashes, configuration, and segment order before it creates an
output directory or starts a guest process:

```bash
python3 ports/pokemon-crystal/scripts/verify_data_mod_replay.py \
  --manifest /path/to/route29-replay.json \
  --rom /path/to/Pokemon-Crystal-Rev-1.gbc \
  --executable output/pokemon-crystal/build/pokemon_crystal \
  --generation-receipt output/pokemon-crystal/crystal-generation.json \
  --output logs/pokemon-crystal/replay
```

The result is a diagnostic artifact with the complete provenance record,
selected deterministic outcomes, and one reproduction hash. Paths and
incidental runtime-log bytes are excluded from that hash, so independent
output directories produce the same value.
