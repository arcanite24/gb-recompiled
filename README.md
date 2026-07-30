# GB Recompiled

GB Recompiled is an experimental static recompiler for Game Boy and Game Boy Color ROMs. It analyzes LR35902 code, emits native C, and links the generated project with a shared SDL2 runtime.

<p align="center">
  <img src="dino.png" alt="A game running through GB Recompiled" width="400">
</p>

The project does not include ROMs. Use only ROM images that you are legally allowed to use.

## Project status

The recompiler currently supports:

- DMG and CGB execution, including DMG games running in CGB compatibility mode
- ROM-only cartridges and the MBC1, MBC2, MBC3, and MBC5 mapper families
- generated C projects with an embedded, self-contained runtime snapshot
- SDL video, audio, keyboard/controller remapping, battery saves, RTC data, and savestates
- generated-to-interpreter fallback for code that static analysis did not compile
- single-ROM desktop builds, a graphical multi-ROM launcher, and single-ROM Android output
- symbol maps, trusted annotations, trace-assisted discovery, differential execution, and deterministic repro tools
- exact-ROM native function hooks/replacements for opt-in ports and mods

Hardware accuracy and game compatibility are still active work. Passing recompilation is not a guarantee that a game will run perfectly. The external suite includes cycle-boundary coverage for stack, control-flow, and SP-relative instructions; see [Accuracy](ACCURACY.md) and [Game Boy Color status](GBC.md) for current evidence and known gaps.

## Downloads

Prebuilt `gbrecomp` archives are published on the [GitHub Releases page](https://github.com/arcanite24/gb-recompiled/releases):

| Platform | Archive |
| --- | --- |
| Linux x64 | `gb-recompiled-linux-x64.tar.gz` |
| macOS Intel | `gb-recompiled-macos-x64.tar.gz` |
| macOS Apple silicon | `gb-recompiled-macos-arm64.tar.gz` |
| Windows x64 | `gb-recompiled-windows-x64.zip` |

The same release workflow also publishes ROM-free Crystal Recompiled source
packages for Linux x64, macOS x64/arm64, and Windows x64. Each contains an
inventoried platform SDK and builds the game locally only after the user
selects the supported exact ROM. See the
[Crystal packaging guide](ports/pokemon-crystal/PACKAGING.md).

The archives include the runtime sources needed to generate a relocatable
project plus `gbrecomp-release.json`, a complete file inventory and
machine-readable tool/runtime ABI identity. Building a generated project still
requires CMake, Ninja, SDL2 development files, and a C/C++ compiler.

Inspect an installed tool without a ROM:

```bash
gbrecomp --version
gbrecomp --version-json
```

Launchers can request a stable, path-free JSON Lines progress stream with
`--progress-json <file>` and prevent a user-selected filename from entering
generated artifacts with `--output-prefix <id>`. Progress records use the
versioned `gbrecomp.progress` schema and enumerated stage/error codes; they do
not contain input or output paths.

## Build from source

Requirements:

- CMake 3.20 or newer
- Ninja
- a C11 compiler and a C++20 compiler
- SDL2 development files for the runtime and generated projects

```bash
git clone https://github.com/arcanite24/gb-recompiled.git
cd gb-recompiled
cmake -G Ninja -B build .
ninja -C build
```

The recompiler is written to `build/bin/gbrecomp` (`gbrecomp.exe` on Windows).

## Recompile and run a ROM

```bash
./build/bin/gbrecomp path/to/game.gb -o output/game

cmake -G Ninja -S output/game -B output/game/build
ninja -C output/game/build

./output/game/build/game
```

The same flow accepts `.gbc` ROMs. Hardware mode is selected from the cartridge header by default; use `--model dmg` or `--model cgb` on the generated executable only when you need an explicit override.

Generated single-ROM projects default to a size- and iteration-oriented build profile. For a deliberately optimized build:

```bash
cmake -G Ninja -S output/game -B output/game/build-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DGBRECOMP_GENERATED_OPT_LEVEL=3 \
  -DGBRECOMP_ENABLE_IPO=ON \
  -DGBRECOMP_ENABLE_STRIP=OFF
ninja -C output/game/build-release
```

Run `./build/bin/gbrecomp --help` for the current generation options. Runtime controls and diagnostic flags are documented in [Runtime usage](RUNTIME.md).

## Multi-ROM launcher

Passing a directory recursively recompiles its `.gb`, `.gbc`, and `.sgb` files into one shared desktop launcher:

```bash
./build/bin/gbrecomp path/to/roms -o output/collection
cmake -G Ninja -S output/collection -B output/collection/build
ninja -C output/collection/build

./output/collection/build/collection
./output/collection/build/collection --list-games
./output/collection/build/collection --game tetris
```

Launching without `--game` opens the SDL + ImGui graphical picker. Use `--jobs <n>` to cap parallel generation, or `--jobs 1` when debugging analyzer output.

## Improve analysis with project data

Community symbol files make generated names easier to read:

```bash
./build/bin/gbrecomp path/to/game.gbc \
  -o output/game \
  --symbols path/to/game.sym \
  --symbol-policy names-only
```

`names-only` preserves imported names and aliases without treating symbol
records as analyzer boundaries. The legacy `infer-boundaries` policy remains
the CLI default for compatibility, but it should be used only when the symbol
source is intended to guide analysis.

For trusted code and data boundaries, combine names-only symbols with a
separately reviewed annotations file:

```text
function 00:0150 BootEntry
label 00:0153 BootEntry.loop
data 1f:4000 0x120 MapScriptTable
```

```bash
./build/bin/gbrecomp path/to/game.gbc \
  -o output/game \
  --symbols path/to/game.sym \
  --symbol-policy names-only \
  --annotations path/to/game.annotations
```

Generated projects can include a `*_metadata.json` sidecar with emitted names,
all same-address source aliases, provenance, memory spaces, constants, and
actionable analyzer diagnostics. The `analysis_diagnostics` records identify
unresolved indirect or direct targets, undefined opcodes, heuristic
data-as-code candidates, configured RAM overlays, and explicit or inferred
entry points without requiring generated-C or console-log scraping. Use
`--reachable-only --no-scan` when a port supplies reviewed entry points and
should not seed heuristic roots in every bank. Trace-guided discovery is also
available, but it measures observed code coverage rather than semantic correctness; read
[Trace-guided coverage](GROUND_TRUTH_WORKFLOW.md) before relying on it.

## Documentation

- [Runtime usage](RUNTIME.md): controls, settings, saves, diagnostics, differential mode, and benchmarking
- [Accuracy](ACCURACY.md): current test results, limitations, and reproduction commands
- [Game Boy Color status](GBC.md): implemented CGB behavior and remaining gaps
- [Android](ANDROID.md): generate, build, install, and troubleshoot an APK
- [Trace-guided coverage](GROUND_TRUTH_WORKFLOW.md): use observed execution points to improve code discovery
- [Native replacement SDK](NATIVE_PATCHES.md): exact-ROM function hooks, manifests, and the legal example
- [Port and frontend modules](PORT_MODULES.md): exact-ROM host extensions, validated semantic transactions, and renderer-independent presentation
- [Data-mod packages](DATA_MODS.md): ROM-free package identity, compatibility, deterministic ordering, content hashes, and provenance
- [Project backlog](TODO.md): prioritized remaining work
- [Code improvement audit](docs/CODE_IMPROVEMENT_AUDIT_2026-07-12.md): detailed technical audit and P0 remediation evidence

## Development

Build and run the repository-owned regression suite with:

```bash
cmake -G Ninja -B build-tests . -DBUILD_TESTS=ON
ninja -C build-tests
ctest --test-dir build-tests --output-on-failure
```

Contributors and coding agents should read [AGENTS.md](AGENTS.md) before changing analyzer, code-generation, or hardware behavior.

## License

GB Recompiled is available under the [MIT License](LICENSE). Dear ImGui remains under its upstream license in the vendored runtime snapshot.

Game Boy is a trademark of Nintendo. This project is not affiliated with or endorsed by Nintendo.
