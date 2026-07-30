# Crystal Recompiled packaging and lifecycle

Crystal Recompiled platform archives contain the original standalone port
source, a compatible platform-specific GB Recompiled SDK, launch wrappers, and
complete source/SDK/package inventories. They contain no ROM, save, generated
game, symbol dump, extracted game asset, or prebuilt ROM-derived executable.

## Clean-machine prerequisites

Local recompilation requires:

- Python 3.11 or newer, including Tk for the graphical ROM picker;
- CMake 3.20 or newer and Ninja;
- a C11/C++20 compiler;
- SDL2 development headers and libraries; and
- network access on the first launch to fetch the one commit-addressed,
  SHA-locked symbol input directly from its owner.

Use the platform package matching the host:

| Host | Package | Typical prerequisites |
| --- | --- | --- |
| Linux x64 | `crystal-recompiled-linux-x64.tar.gz` | `python3`, `python3-tk`, `cmake`, `ninja-build`, `build-essential`, `libsdl2-dev` |
| macOS Intel | `crystal-recompiled-macos-x64.tar.gz` | Python with Tk, CMake, Ninja, Xcode command-line tools, SDL2 |
| macOS Apple silicon | `crystal-recompiled-macos-arm64.tar.gz` | Python with Tk, CMake, Ninja, Xcode command-line tools, SDL2 |
| Windows x64 | `crystal-recompiled-windows-x64.zip` | 64-bit Python with Tk, CMake, Ninja, Visual Studio C++ tools, SDL2 development package |

The package includes the Windows SDL runtime DLL but not the compiler-facing
SDL development package.

## Launch

Extract the archive to a writable directory and use:

```text
Linux:   ./launch-crystal.sh
macOS:   ./launch-crystal.command
Windows: launch-crystal.bat
```

The first launch verifies the embedded SDK inventory and ABI contract, fetches
and hashes the permitted symbol input, asks for the exact Pokémon Crystal
US/Europe Rev 1 ROM, then generates and builds in the platform's private
Crystal Recompiled cache. Ordinary bootstrap/compiler output is not retained.

For automation:

```bash
./launch-crystal.sh \
  --rom /path/to/user-owned-rev1.gbc \
  --cache-dir /private/cache \
  --prepare-only

./launch-crystal.sh \
  --cache-dir /private/cache \
  --headless-smoke
```

An optional precompiled exact-ROM data overlay can be selected at launch:

```bash
./launch-crystal.sh \
  --cache-dir /private/cache \
  --data-mod /private/cache/mods/example.gbdm
```

Omitting `--data-mod` restores vanilla ROM reads without regenerating or
changing the save.

## Physical-controller acceptance

After the exact-ROM packaged verifier has prepared a private cache, connect a
physical controller and run:

```bash
python3 ports/pokemon-crystal/scripts/verify_controller_release.py \
  --package-root /path/to/extracted/crystal-recompiled \
  --cache /path/to/the/verified/private-cache \
  --output /path/to/controller-verification.json \
  --attest-controller-only
```

Use only the controller during the prompted gameplay window. Exercise D-pad
Up/Down/Left/Right, A, B, Start, and Select. The verifier requires SDL to
report an accepted controller and requires all eight actions in the runtime's
cycle-anchored input recording. Its public JSON contains hashes, host/package
identity, controller profile, action counts, and an explicit operator
attestation; it contains neither the controller name nor private paths or
recorded input.

## Relocation, saves, and uninstall

Generated source, executable, and user data live outside the extracted package
in the private cache. The launcher always supplies `--save-dir` pointing at
the cache's mode-`0700` `user-data` directory. Moving or deleting the extracted
package therefore does not move or delete saves.

To relocate, move the extracted package and run the wrapper from the new
location. To uninstall the package while preserving play data, delete only
the extracted package. To remove all private generated content and saves, use
the operating system's normal file manager to delete the documented Crystal
Recompiled cache directory after making any desired backup.

The launcher never deletes a cache. If an existing cache receipt or executable
does not match the installed SDK/runtime identity, it fails closed rather than
silently rebuilding over saved state.

## Reproducibility and CI boundary

`scripts/create_release.py` verifies the ejected source inventory, SDK
inventory, exact ABI/features/runtime hash, and target platform before
packaging. It writes every archive member in sorted order with normalized
timestamps, ownership, and modes. Repeating the package operation with the
same inputs must produce the same archive SHA-256.

The release workflow builds and extracts Linux x64, macOS x64/arm64, and
Windows x64 packages and exercises their relocated launcher CLI. Exact-ROM
first run, route, persistence, mod loading, and controller evidence require a
user-supplied ROM and are recorded separately; structural CI must not simulate
that proof with distributed commercial content.
