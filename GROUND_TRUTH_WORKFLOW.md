# Trace-guided code coverage

Trace-guided analysis can seed the recompiler with addresses observed during an emulator run. It is useful when static analysis misses computed control flow, but it is not a semantic correctness proof.

The current PyBoy capture tool samples one program counter at each frame boundary. It does **not** record every instruction executed during the frame. Treat its output as a low-fidelity discovery hint, not literal ground truth or complete coverage.

## When to use it

Use trace guidance when:

- a game reaches generated-to-interpreter fallback at stable code addresses
- a computed jump or table-driven dispatcher is not resolved statically
- you want to compare observed addresses with emitted instruction comments

Prefer trusted symbols and annotations when a disassembly project already provides real function starts and data ranges. Use differential mode for generated-vs-interpreter semantics, and external hardware tests for independent accuracy evidence.

## Requirements

- a built `gbrecomp`
- Python 3
- PyBoy for capture
- CMake, Ninja, SDL2, and a compiler for the generated project

Install PyBoy in an isolated environment using your normal Python environment manager.

## Automated workflow

Keep generated output under `output/`:

```bash
python3 tools/run_ground_truth.py path/to/game.gb \
  --output-dir output/game-trace-guided \
  --frames 18000
```

The script:

1. samples execution points with PyBoy
2. generates a project with `--use-trace`
3. configures and builds the generated project
4. checks whether sampled addresses appear in generated C comments

The coverage comparison does not inspect register values, flags, memory writes, mapper state, or timing. A high percentage means the sampled addresses were emitted, not that their generated semantics are correct.

The script replaces its output directory unless `--keep-temp` is used. Do not point it at a directory containing unrelated work.

## Manual workflow

### 1. Capture observed addresses

```bash
python3 tools/capture_ground_truth.py path/to/game.gb \
  --output logs/game-observed.trace \
  --frames 18000 \
  --random
```

Random input can broaden exploration, but it is nondeterministic and weak at navigating menus or long game sequences. Prefer a deliberate capture path when the missing area is known.

### 2. Generate with the trace

```bash
./build/bin/gbrecomp path/to/game.gb \
  --output output/game-trace-guided \
  --use-trace logs/game-observed.trace

cmake -G Ninja \
  -S output/game-trace-guided \
  -B output/game-trace-guided/build
ninja -C output/game-trace-guided/build
```

### 3. Measure sampled-address coverage

```bash
python3 tools/compare_ground_truth.py \
  --trace logs/game-observed.trace \
  output/game-trace-guided
```

Investigate missing addresses, but do not assume every missing address should become a function entry. RAM-resident code, mapper ambiguity, a bad bank sample, or a data/code boundary can all require a different treatment.

### 4. Validate semantics

After improving discovery, run a deterministic generated-vs-interpreter comparison and check fallback explicitly:

```bash
./output/game-trace-guided/build/game \
  --differential 500000 \
  --differential-log 100000 \
  --differential-fail-on-fallback
```

Then replay a known input path and compare state or frame artifacts. Differential success still needs external hardware-test or reference-emulator evidence for hardware correctness.

## Tool summary

| Tool | Actual role |
| --- | --- |
| `tools/capture_ground_truth.py` | Sample PyBoy bank/PC state once per frame |
| `gbrecomp --use-trace <file>` | Add observed addresses as analyzer seeds |
| `tools/compare_ground_truth.py` | Compare sampled addresses with address comments in generated C |
| generated runtime `--trace-entries` | Record entry points observed in the generated runtime |
| generated runtime `--differential` | Compare generated execution with the shared interpreter |

The term `ground_truth` remains in tool filenames for compatibility. The accurate description of the current workflow is trace-guided sampled-address coverage.
