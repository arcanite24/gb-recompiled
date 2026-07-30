# Crystal source-built native extensions

Crystal Recompiled supports a bounded source-built extension set when a
feature needs live host behavior that a startup data overlay cannot express.
This is not a general plugin loader.

## Retained design

Each extension is a ROM-free package containing project-original C source and
`manifest.json`. The manifest locks:

- extension ID, semantic version, ABI version, and priority;
- exact Crystal Workbench host ID/version/ABI;
- exact supported ROM size and SHA-256;
- dependencies, conflicts, and the bounded capability set;
- source paths, hashes, entry symbol, authorship, and license.

`scripts/validate_port_extensions.py` rejects unknown fields, unsupported
capabilities, incompatible identity, duplicate IDs or entry symbols, missing
dependencies, conflicts, path escape, symlinks, and source hash mismatch. It
sorts valid packages by `(priority, extension ID)`.

`scripts/generate.py` accepts repeatable `--port-extension <manifest>`
arguments only alongside the exact Workbench port module. It copies the
validated sources into the private generated tree, emits a static registry,
and includes the ordered extension provenance in
`crystal-generation.json`. Changing the installed source-built set therefore
requires regeneration and rebuilding; toggling a compiled feature at runtime
does not.

At runtime, port extension ABI v1 checks the registry, descriptor, order,
exact ROM, and maximum count before base-module activation. Extensions receive
a reduced service view containing only metadata, semantic reads, host input,
logging, and bounded panel/text drawing. The runtime clears the base module's
semantic-edit service before any extension callback. Extensions cannot access
`GBContext`, writable guest memory, SDL, a graphics API, filesystem, network,
or native-patch call frames. This keeps the native-patch safepoint contract
unchanged.

## Concrete demand: Encounter Lens

`native-extensions/encounter-lens` adds a toggleable Route 29 encounter panel.
F3, or the deterministic `encounters` port action, displays the current map,
time period, encounter rate, and seven live slots. It reads the runtime's
semantic view, so an active Route 29 data overlay appears immediately without
duplicating or bypassing the overlay contract.

This cannot be a data overlay: an overlay can replace reviewed ROM bytes at
startup, but cannot receive host input or continuously render derived live
state. The lens is observational. Opening it changes no guest state, save,
RTC, or replay input.

Generate it with:

```bash
python3 scripts/generate.py \
  --port-module module/port-module.json \
  --port-extension \
    native-extensions/encounter-lens/manifest.json \
  --output ../../output/pokemon-crystal-encounter-lens
```

Then configure and build the generated project normally. F3 toggles the panel;
headless evidence can use:

```bash
./build/pokemon_crystal \
  --headless \
  --port-input-frame 1:encounters \
  --port-state port-state.json
```

The port-state v2 `extensions` array and generation receipt identify the
exact ordered source-built set. Replays that depend on this behavior must
retain the executable and receipt hashes in addition to ordinary input and
data-mod provenance.

## Options evaluated

| Model | Authoring capability | Determinism and provenance | Portability and trust | Decision |
| --- | --- | --- | --- | --- |
| Data overlay | Exact reviewed data replacement | Strong; installable without rebuild | Portable and non-executable | Insufficient for live input and host rendering |
| Source-built static registry | Native derived UI over bounded services | Strong; ordered source and manifest hashes are in the build receipt | Rebuilt per platform; reviewed code shares process trust | Retained for Encounter Lens |
| Dynamic native library | Same behavior without regenerating the game | Loader order, platform ABI, code-signing, and binary provenance add new failure surfaces | Platform-specific executable code with full process trust | Rejected until a real no-rebuild native use case justifies it |
| Sandboxed portable bytecode | Potential no-rebuild cross-platform behavior | Could be deterministic with signed modules, fixed host calls, memory limits, and fuel | Requires a VM, verifier, resource model, and stable portable ABI | Rejected; the lens gains no authoring capability that offsets this new subsystem |

The comparison does not claim dynamic or sandboxed modules are impossible.
It records that neither is justified by the first real native-extension use
case, so neither enters the supported attack surface or release contract.
