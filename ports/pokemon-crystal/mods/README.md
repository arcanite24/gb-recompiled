# Crystal sample data mods

These are ROM-free, declarative examples for the exact supported Pokémon
Crystal Rev 1 ROM. They contain only project-authored JSON and package
metadata. Users must provide their own matching ROM to compile a private
`.gbdm` overlay.

## Samples

- `samples/route29-level-five` raises every Route 29 wild encounter slot to
  level 5 while preserving the original species roster and time-of-day
  differences.
- `samples/route29-encounter-guide` replaces the west Route 29 sign with a
  compact project-authored hint naming the leading day and night encounters.
  It does not change gameplay.

The packages are independently versioned and have no dependency on each
other. Their declared load orders are 100 and 200. Loading both produces the
deterministic ordered set:

1. `org.gbrecompiled.crystal.route29-level-five`
2. `org.gbrecompiled.crystal.route29-encounter-guide`

## Build and run

From the GB Recompiled root:

```bash
python3 tools/validate_data_mods.py \
  --policy ports/pokemon-crystal/mods/target-policy.json \
  --package-schema ports/pokemon-crystal/mods/package-schema.json \
  --semantic-package ports/pokemon-crystal/semantic/package.json \
  --semantic-schema ports/pokemon-crystal/semantic/package-schema.json \
  --manifest ports/pokemon-crystal/mods/samples/route29-level-five/package.json \
  --manifest ports/pokemon-crystal/mods/samples/route29-encounter-guide/package.json \
  --output /private/path/crystal-samples.json

python3 tools/compile_crystal_data_mod.py \
  --resolution /private/path/crystal-samples.json \
  --rom /private/path/Pokemon-Crystal-Rev-1.gbc \
  --output /private/path/crystal-samples.gbdm \
  --report /private/path/crystal-samples-report.json

./output/pokemon-crystal/build/pokemon_crystal \
  --data-mod /private/path/crystal-samples.gbdm
```

Omit either `--manifest` to compile one sample by itself. Omit `--data-mod`
when launching to return to vanilla behavior immediately. The compiler never
writes the ROM, and neither sample changes the save format.

## Conflict behavior

Manifest-declared package conflicts fail during validation. If two otherwise
compatible packages claim the same reviewed semantic field, compilation fails
with the semantic identity, physical offset, and both package/content IDs.
No partial artifact is retained.
