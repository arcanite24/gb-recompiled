# GB Recompiled

GB Recompiled turns Game Boy and Game Boy Color ROMs into portable C projects.
It analyzes LR35902 code, emits generated C, and links that code with a shared
SDL2 runtime for video, audio, input, persistence, and hardware timing.

<table>
  <tr>
    <td width="50%"><img src="dino.png" alt="A Game Boy game running in a generated GB Recompiled executable"></td>
    <td width="50%"><img src="ports/pokemon-crystal/screenshots/challenge-mode.png" alt="Crystal Recompiled running with its controller-first Challenge Mode panel"></td>
  </tr>
  <tr>
    <td align="center"><sub>Generated desktop runtime</sub></td>
    <td align="center"><sub>Exact-ROM port with host-native Challenge Mode</sub></td>
  </tr>
</table>

The project contains no ROMs. Use only ROM images that you are legally allowed
to use.

## What it does

- Decodes and analyzes banked LR35902 programs, then emits reviewable C and
  machine-readable metadata.
- Generates self-contained desktop projects with DMG/CGB execution, common
  cartridge mappers, SDL video/audio/input, battery saves, RTC data, and
  savestates.
- Falls back safely to the interpreter when static analysis did not compile a
  target.
- Exposes opt-in exact-ROM APIs for native function replacement, semantic data
  access, deterministic data mods, and host-native presentation.
- Ships differential, replay, frame/audio capture, benchmark, and external-test
  tooling so claims can be tied to reproducible evidence.

This is an experimental recompiler, not a whole-catalogue compatibility claim.
Hardware accuracy and game coverage remain active work, and successful code
generation does not guarantee that every scene will run correctly.

## Get started

Prebuilt `gbrecomp` archives are available on the
[GB Recompiled 0.1.0 release](https://github.com/arcanite24/gb-recompiled/releases/tag/v0.1.0):

| Platform | Archive |
| --- | --- |
| Linux x64 | `gb-recompiled-linux-x64.tar.gz` |
| macOS Intel | `gb-recompiled-macos-x64.tar.gz` |
| macOS Apple silicon | `gb-recompiled-macos-arm64.tar.gz` |
| Windows x64 | `gb-recompiled-windows-x64.zip` |

To build from source, install CMake 3.20 or newer, Ninja, SDL2 development
files, and a C11/C++20 compiler:

```bash
git clone https://github.com/arcanite24/gb-recompiled.git
cd gb-recompiled
cmake -G Ninja -B build .
ninja -C build
```

Generate, build, and run a project from a locally supplied ROM:

```bash
./build/bin/gbrecomp path/to/game.gb -o output/game
cmake -G Ninja -S output/game -B output/game/build
ninja -C output/game/build
./output/game/build/game
```

The same flow accepts `.gbc` ROMs. Passing a directory instead creates a shared
multi-ROM launcher; use `--list-games` and `--game <id>` for scripted launches.
Runtime controls and diagnostic flags are documented in
[Runtime usage](RUNTIME.md).

## Crystal Recompiled

The repository includes a ROM-free Pokémon Crystal flagship port that exercises
the project beyond generic hardware emulation. It adds reviewed semantic views,
transactional save edits, native Pokédex and PC surfaces, deterministic data
mods, an Encounter Lens extension, and controller-configurable Challenge Mode.

Users provide the exact supported US/Europe Rev 1 ROM locally. The port verifies
that input before generating any private ROM-derived output. Its checked routes
and native features are release evidence, not a whole-game compatibility claim;
the source-only alpha is currently verified end to end on macOS arm64, with
other hosts documented as best-effort.

Read the [Crystal Recompiled overview](ports/pokemon-crystal/README.md) or the
[distribution boundary](ports/pokemon-crystal/LEGAL.md).

## How it fits together

```text
ROM -> decoder and analyzer -> IR + metadata -> generated C project
                                              + versioned runtime snapshot
                                              -> host compiler -> executable
```

The generated path and interpreter share runtime devices, so differential mode
is useful for finding compiler/runtime divergence but is not an independent
hardware oracle. Repository tests cover isolated invariants; external suites,
SameBoy comparisons, and deterministic frame/audio/state evidence provide
separate validation layers.

Run the repository-owned suite with:

```bash
cmake -G Ninja -B build-tests . -DBUILD_TESTS=ON
ninja -C build-tests
ctest --test-dir build-tests --output-on-failure
```

## Documentation

- [Accuracy](ACCURACY.md) — current external-test evidence and limitations
- [Game Boy Color status](GBC.md) — implemented CGB behavior and remaining gaps
- [Runtime usage](RUNTIME.md) — controls, persistence, diagnostics, and profiling
- [Native replacement SDK](NATIVE_PATCHES.md) — exact-ROM hooks and fail-closed manifests
- [Port modules](PORT_MODULES.md) — semantic access and host presentation
- [Data-mod packages](DATA_MODS.md) — deterministic ROM-free overlays
- [Android](ANDROID.md) — single-ROM Android generation and APK workflow
- [Project backlog](TODO.md) — prioritized remaining work

Contributors and coding agents should read [AGENTS.md](AGENTS.md) before changing
analysis, code generation, or hardware behavior.

## License

GB Recompiled is available under the [MIT License](LICENSE). Dear ImGui remains
under its upstream license in the vendored runtime snapshot.

Game Boy is a trademark of Nintendo. This project is not affiliated with or
endorsed by Nintendo.
