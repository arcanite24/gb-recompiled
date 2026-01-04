# Agent Rules for gb-recompiled

## Project Overview
This project is a static recompiler for GameBoy ROMs, translating them into C code compliant with modern standards.

- **Root Structure**:
  - `recompiler/`: Source for the recompiler tool (`gbrecomp`).
  - `runtime/`: The `gbrt` library linked by recompiled games.
  - `roms/`: Contains original ROM files (e.g., `tetris.gb`).
  - `tetris_test/`: The active test project generated from `roms/tetris.gb`.

## Build Standards
- **Build System**: CMake + Ninja. **ALWAYS** use Ninja.
- **Main Project**:
  - Configure: `cmake -G Ninja -B build .`
  - Build: `ninja -C build`

## Workflow: Test ROM Recompilation
To ensure `tetris_test` reflects the latest recompiler/runtime changes, follow this strictly:

1. **Rebuild Tools**:
   ```bash
   ninja -C build
   ```

2. **Regenerate C Code**:
   Run the recompiler on the test ROM to update source files in `tetris_test`.
   ```bash
   ./build/bin/gbrecomp roms/tetris.gb -o tetris_test
   ```

3. **Rebuild Test Artifact**:
   Rebuild the generated project to produce the final executable.
   ```bash
   cmake -G Ninja -S tetris_test -B tetris_test/build
   ninja -C tetris_test/build
   ```

## Development Guidelines
- **Sync**: "Make sure the recompiled project is always up to date." If you modify `recompiler` logic or `runtime` headers, trigger the *Test ROM Recompilation* workflow.
- **Resources**: Consult `ROADMAP.md` for current progress and `ARCH.md` for architectural constraints.
- **Paths**: Usage of absolute paths is preferred for tool calls, but shell commands should be executed from the workspace root for clarity.
