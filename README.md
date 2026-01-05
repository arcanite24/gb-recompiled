# GameBoy Static Recompiler

A static recompiler for GameBoy ROMs that translates them into C code compliant with modern standards.

## Project Structure

- `recompiler/`: Source for the recompiler tool (`gbrecomp`).
- `runtime/`: The `gbrt` library linked by recompiled games.
- `roms/`: Place your original ROM files here (e.g., `tetris.gb`).
- `* _test/`: Generated C projects from recompiled ROMs.

## Building

This project uses CMake and Ninja.

```bash
cmake -G Ninja -B build .
ninja -C build
```

## Usage

Run the recompiler on a ROM to generate a C project:

```bash
./build/bin/gbrecomp roms/game.gb -o game_test
```

Then build the generated project:

```bash
cmake -G Ninja -S game_test -B game_test/build
ninja -C game_test/build
```

### Debugging & Troubleshooting

If you encounter issues during recompilation (e.g., the tool hangs or crashes), you can use the debug flags:

- **Trace Execution**:
  ```bash
  ./build/bin/gbrecomp roms/game.gb --trace
  ```
  This prints every instructions as it is analyzed. Useful for seeing where the analyzer gets lost (e.g., interpreting data as code).

- **Limit Instructions**:
  ```bash
  ./build/bin/gbrecomp roms/game.gb --limit 5000
  ```
  Stops analysis after the specified number of instructions. Use this if the analyzer falls into an infinite loop.

- **Check for Errors**:
  Look for `[ERROR] Undefined instruction` logs in the output, which indicate the analyzer has reached invalid code or data.

## Known Limitations

- **RAM Execution**: The recompiler cannot handle code executed from RAM (e.g., self-modifying code or code copied to WRAM). ROMs that rely on this (like `cpu_instrs.gb`) will fail or exit early.
- **Computed Jumps**: Complex jump tables (e.g., `JP HL`) require heuristic detection. If the analyzer misses a table, large chunks of code may be omitted.

## Roadmap

See [ROADMAP.md](ROADMAP.md) for current status and future plans.
