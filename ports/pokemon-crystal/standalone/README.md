# Crystal Recompiled

Crystal Recompiled is an exact-ROM Pokémon Crystal port project built with
GB Recompiled. This source repository contains original code, manifests,
schemas, tests, documentation, and CC0 presentation assets only. It contains
no ROM, save, extracted game asset, prebuilt symbol dump, generated game
source, or ROM-derived executable.

Pokémon and related names and marks belong to their respective owners. This
unofficial project is not affiliated with, sponsored by, or endorsed by
Nintendo, The Pokémon Company, Game Freak, or Creatures.

The user supplies an unmodified US/Europe Rev 1 ROM locally. Read
[`ports/pokemon-crystal/LEGAL.md`](ports/pokemon-crystal/LEGAL.md) before
distribution and
[`ports/pokemon-crystal/README.md`](ports/pokemon-crystal/README.md) for the
complete engineering workflow.

## Bootstrap a compatible GB Recompiled SDK

Obtain a GB Recompiled distribution that includes
`gbrecomp-release.json`, then run:

```bash
python3 ports/pokemon-crystal/scripts/bootstrap.py \
  --distribution /path/to/gb-recompiled-distribution \
  --fetch-references
```

The bootstrap validates every distribution file, the exact tool version,
required ABI versions/features, and the runtime source-tree hash before
installing ignored local dependencies. `--fetch-references` acquires only the
pinned symbol source required for generation. Developers can use
`--fetch-all-references` to acquire the broader optional oracle/tool set.
Every checkout is fetched from its owner at a pinned commit and remains
ignored.

## First run

```bash
python3 ports/pokemon-crystal/scripts/first_run.py
```

The native file picker accepts only the exact US/Europe Rev 1 ROM. For a
headless setup, pass `--rom /path/to/your-rom.gbc`. Validation happens before
the private cache is created. The script then generates and builds with all
ordinary tool output discarded and only versioned, path-free progress events
retained.

The default private cache is:

- macOS: `~/Library/Application Support/Crystal Recompiled`;
- Windows: `%LOCALAPPDATA%\Crystal Recompiled`; and
- Linux: `$XDG_CACHE_HOME/crystal-recompiled` or
  `~/.cache/crystal-recompiled`.

The cache is refused if it is inside the source checkout. It contains the
generated project, executable, receipts, and safe progress streams. Releases
and source control contain none of those files. The selected source-ROM path
is used only for generation and is not written to a receipt or progress
record.

Developers can still use `scripts/generate.py` with an ignored
`output/pokemon-crystal-*` destination for engineering workflows.

Platform release archives add an embedded, inventoried GB Recompiled SDK and
root `launch-crystal` wrappers. See
[`ports/pokemon-crystal/PACKAGING.md`](ports/pokemon-crystal/PACKAGING.md) for
clean-machine prerequisites, relocation, mod selection, save preservation,
and uninstall behavior.

Before publishing an archive, complete
[`ports/pokemon-crystal/RELEASE.md`](ports/pokemon-crystal/RELEASE.md) and
retain the notices in
[`ports/pokemon-crystal/THIRD_PARTY_NOTICES.md`](ports/pokemon-crystal/THIRD_PARTY_NOTICES.md).
