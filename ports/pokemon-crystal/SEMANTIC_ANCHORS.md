# Initial semantic anchors

These are research leads from the pinned `pokecrystal11.sym`, not yet public
port contracts. Their machine-readable, exact-ROM representation is
[`semantic/anchors.json`](semantic/anchors.json), validated against
[`semantic/anchor-schema.json`](semantic/anchor-schema.json). A function must
appear as a discovered, returning, ROM-backed, patchable entry in fresh GB
Recompiled metadata before a native manifest can bind it.

The M1 snapshot uses metadata SHA-256
`d0c51c5f0dfc190885af10e8ae3a9790c1368d3bdb82e1dabc9c45e636a6a7f0`.
The same snapshot was produced from the original and relocated generated
projects. A `patchable` result means the current SDK can bind the generated
function body; it does not promote the symbol to a stable public semantic
contract. M3 must still narrow the over-broad analysis and prove each hook on
the gameplay route.

## Function-hook candidates

M3-006 revalidated these candidates in
[`semantic/hook-candidates.json`](semantic/hook-candidates.json). `StartMenu`
and `Pokedex` retain stable address-derived IDs across annotation-name and
single-function layout perturbations, but current generated metadata still
marks both unpatchable; the recorded route also does not enter `Pokedex`.
They therefore remain deliberately unbound. CR-M5-005 promotes `BillsPC` to
the exact-ROM replacement manifest at
`native-patches/bills-pc/manifest.json` after the transactional PC and
persistence gates completed. Save and checksum functions remain
observational.

| Symbol | Bank:address | Candidate function ID | M1 metadata status | Intended use |
| --- | --- | --- | --- | --- |
| `StartMenu` | `04:65cd` | `gbfn:v1:0004:65cd` | unpatchable | Native menu or Workbench entry |
| `BillsPC` | `05:5668` | `gbfn:v1:0005:5668` | patchable, bound in CR-M5-005 | Native PC-box workflow |
| `Pokedex` | `10:4000` | `gbfn:v1:0010:4000` | unpatchable | Native Pokédex replacement |
| `_SaveGameData` | `05:4c10` | `gbfn:v1:0005:4c10` | unpatchable | Observe save transactions |
| `SaveChecksum` | `05:4e13` | `gbfn:v1:0005:4e13` | unpatchable | Validate save-write integration |
| `TryLoadSaveFile` | `05:4ea5` | `gbfn:v1:0005:4ea5` | unpatchable | Observe primary/backup save selection |

Start with pre/post hooks that always request the generated original. Do not
replace save or checksum routines as an early experiment.

## Read-only state candidates

| Symbol | Bank:address | M1 metadata status | Meaning |
| --- | --- | --- | --- |
| `wMapGroup` | `01:dcb5` | resolved-read-only | Current map group |
| `wMapNumber` | `01:dcb6` | resolved-read-only | Current map number |
| `wYCoord` | `01:dcb7` | resolved-read-only | Player map Y coordinate |
| `wXCoord` | `01:dcb8` | resolved-read-only | Player map X coordinate |
| `wPartyCount` | `01:dcd7` | banked WRAM, width 1, preserved alias | Party size |
| `wPartySpecies` | `01:dcd8` | resolved-read-only | Party species list |
| `wPartyMons` | `01:dcdf` | resolved-read-only | Party Pokémon structures |
| `wPokedexCaught` | `01:de99` | resolved-read-only | Caught bit field |
| `wPokedexSeen` | `01:deb9` | resolved-read-only | Seen bit field |
| `sBackupChecksum` | `00:bf0d` | resolved-read-only | Backup save checksum |
| `sChecksum` | `01:ad0d` | resolved-read-only | Primary save checksum |
| `sBoxCount` | `01:ad10` | resolved-read-only | Active box count |
| `sBoxSpecies` | `01:ad11` | resolved-read-only | Active box species list |

`wPartyCount` shares `01:dcd7` with `wCurMapDataEnd` and `wPokemonData`.
CR-M3-002 preserves all three source names in generated metadata while keeping
`wPokemonData` as the deterministic canonical emitted name. This resolves the
name-loss problem, but it does not decide which semantic view is active at a
given time. The public schema must still model that ambiguity deliberately.

WRAM and SRAM banks are not interchangeable with physical ROM banks. The M3
anchor manifest now encodes memory space, bank, address, width, access policy,
and provenance, and models the five MBC3 RTC registers by selector. Its
validator rejects impossible or overlapping ranges and verifies that no
non-ROM anchor appears as a generated function ID. The native function-ID
format applies only to ROM-backed functions.

Imported `.sym` records have no size information, so generated symbol metadata
conservatively gives them width 1. The reviewed manifest is authoritative for
multi-byte candidates such as the 288-byte party structures and 32-byte
Pokédex bit fields.

## First proof

The smallest useful player-facing proof is:

1. read the current map and party through reviewed accessors;
2. render them in a native read-only Pokégear panel;
3. hook `StartMenu` or `Pokedex` only after generated metadata confirms the
   exact entry;
4. retain the original path and prove identical final guest state;
5. add save writes only after primary/backup checksum fixtures pass.
