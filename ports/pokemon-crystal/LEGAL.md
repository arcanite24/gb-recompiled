# Distribution boundary

This document is an engineering policy, not legal advice.

Owning or supplying a ROM is an important part of a clean release design, but
it does not by itself settle every copyright, trademark, or anti-circumvention
question in every jurisdiction. A public release needs a final legal review.
The engineering release checklist is in [RELEASE.md](RELEASE.md); it cannot
substitute for qualified legal advice.

## Never track or release

- Pokémon Crystal ROMs, patches containing substantial original ROM data, or
  save files;
- generated C, object files, executables, metadata, or caches that embed or
  reproduce ROM bytes;
- extracted sprites, maps, music, text, or other game assets;
- local upstream reference checkouts without a compatible redistribution
  license;
- prebuilt pret symbol or map files while their redistribution rights remain
  unclear;
- Nintendo, Pokémon, Game Freak, or Creatures branding presented as project
  ownership or endorsement.

## Intended public contents

- original Crystal Recompiled launcher and native feature code;
- exact-ROM hashes and a local ROM picker;
- GB Recompiled compatibility/version metadata;
- original manifests, tests, schema tooling, and reviewed annotations;
- reproducible scripts that fetch third-party public references directly from
  their owners at pinned commits;
- original replacement assets with explicit licenses and provenance;
- documentation, acknowledgements, license notices, and disclaimers.

## Build and release model

The user selects their own exact US/Europe Rev 1 ROM. The launcher verifies it
locally, generates any ROM-derived sources or cache locally, and stores saves
locally. Project releases do not contain the ROM or the generated game.

If startup latency makes local compilation impractical, solve that with a
legally reviewed cache or relocation design. Do not solve it by publishing a
precompiled ROM-derived executable without review.

## Third-party reference policy

Repositories without an explicit license are for local inspection only. A
public repository may contain their URL, commit ID, hashes, and original setup
automation, but not their files.

GPL tools may be executed as separate development or validation programs.
Their code must not be copied or linked into an MIT-licensed port unless the
project explicitly adopts a compatible licensing strategy. LGPL components
need a specific integration and notice review before distribution.

## Names and trademarks

Pokémon, Pokémon Crystal, Nintendo, Game Freak, Creatures, and related names
and marks belong to their respective owners. Crystal Recompiled is an
unofficial, independently developed compatibility and modification project. It
is not affiliated with, sponsored by, or endorsed by Nintendo, The Pokémon
Company, Game Freak, or Creatures.
