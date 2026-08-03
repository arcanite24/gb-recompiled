# Beyond emulation: product directions for GB Recompiled

Date: 2026-07-14

Status: strategy companion; informs work beyond the active NL-5 native replacement SDK without replacing the execution order in the native-recompilation strategy

## Purpose

This document is for contributors choosing product and architecture experiments after the measured optimization program.

After reading it, a contributor should be able to select a contained experiment that exploits ahead-of-time knowledge, define a falsifiable keep gate, and explain why the result is more than an emulator feature implemented through generated C.

The central decision is to change the unit of output. If GB Recompiled continues to produce the same ROM experience through a different execution mechanism, it remains emulator-adjacent. It becomes a different kind of product when it produces an authorable, embeddable, verifiable project for an exact game.

## Decision lens

A proposed differentiator should exploit at least one property that a generic emulator does not naturally possess:

- exact ROM identity
- a stable, bank-aware function and data map
- whole-program analysis and ahead-of-time code layout
- a native linker and build system
- deterministic generated metadata
- game-specific schemas, annotations, or porting knowledge

If a feature could be added to a generic emulator without knowing the game's functions, symbols, tables, or exact revision, it may still be useful, but it should not be presented as the reason to use static recompilation.

## Recommended product lanes

| Direction | What changes | NL-5 dependency |
| --- | --- | --- |
| Semantic game library | Other programs can call, inspect, and simulate an exact recompiled game | Mostly independent |
| Schema-driven data and asset lifting | ROM data becomes editable project source | Independent |
| Certified AOT builds | The project proves what was compiled and removes unneeded generic machinery | Independent |
| Native reverse-engineering workbench | Stable functions and data become debugger and testing surfaces | Independent |
| Versioned mod packages | Code, data, and assets become distributable exact-ROM extensions | Uses NL-5 for code hooks |
| Reusable idiom recognition | Knowledge about common routines can transfer across games | Independent initially |

## 1. Generate a semantic game library

The strongest non-NL-5 goal is to generate each game as a platform-neutral native library as well as an SDL application.

The generated project should contain:

- a core static or shared library with no SDL or ImGui dependency
- a thin SDL frontend that consumes the public library API
- a stable C ABI that hides runtime implementation details
- host callbacks for input, video, audio, persistence, and logging

The API should eventually support:

- creating and destroying isolated game instances
- supplying input and advancing to a frame, cycle, function, or semantic event
- reading named game data through stable metadata identifiers
- deterministic snapshots and restores
- retrieving video and audio without owning a window or audio device
- invoking selected game functions when a safe call contract exists

The semantic portion is essential. An API limited to buttons, frame stepping, pixels, and audio would be a cleaner emulator core. Named functions, named data, exact-ROM metadata, and semantic run conditions make it a recompiled game library.

This enables:

- automated playtesting and bots
- randomizer and seed validation
- save editors and state inspection tools
- AI and reinforcement-learning environments
- server-side or headless simulation
- embedding a recompiled game inside another engine or application
- later multi-instance execution for large deterministic test batches

### First tracer bullet

Generate one legal synthetic game into both a core library and the ordinary SDL executable. Add a small no-SDL host that supplies input, advances execution, reads one named state value, and retrieves the resulting frame and PCM data.

### Keep gate

- The SDL frontend and no-SDL host produce identical state, frame, and PCM hashes.
- Two contexts can be interleaved without mutable global-state leakage.
- The public ABI does not expose the internal runtime context structure.
- The core library has no SDL or ImGui link dependency.
- The existing generated-project workflow remains available through the thin frontend.

## 2. Make ROM data into editable project source

Add an annotation-assisted schema system for known game data. This should not attempt to infer an entire game automatically.

A port project should be able to declare:

- structs and enums
- pointer, byte, and string tables
- maps, items, species, scripts, and tilesets
- ROM bank, address, length, and alignment
- extraction and encoding rules
- whether edits are in-place, relocatable, or native-port-only

The tooling can then generate editable JSON, PNG, TMX, or other suitable assets plus typed native accessors. Existing symbols, trusted annotations, and metadata sidecars provide the initial address and provenance layer.

### First tracer bullet

Create a synthetic ROM containing:

- a pointer table
- a string table
- a small array of typed gameplay records
- several 2bpp tiles

Describe these structures in one schema, extract them, and rebuild them after an edit.

### Keep gate

- Extracting and rebuilding without edits is byte-identical.
- Editing one field or tile changes only the intended data.
- Repeated extraction and generation are deterministic.
- User-owned schemas and assets survive regeneration unchanged.
- Invalid ranges, overlapping data, and pointer overflow fail closed.
- The project can distinguish data that may be emitted back into a ROM from native-port-only expanded data.

### Longer-term opportunity

Data-only projects could target both the native port and a BPS-compatible ROM patch. The same authoring source could therefore support original hardware and the recompiled port where storage and layout constraints permit it.

## 3. Add a certified-AOT mode

Static recompilation should become an auditable property rather than an assumption based on generated source volume.

Generate a machine-readable certificate containing:

- exact ROM, toolchain, annotations, input, and executable hashes
- compiled and interpreter-resolved entry points
- unresolved indirect jumps and calls
- detected RAM overlays and copied code
- statically proven targets versus dynamically observed targets
- mapper, MMIO, and device behavior used by the build
- explicit reasons that code or data remained conservative

Add a fail-closed port mode that rejects unexpected interpreter fallback. Once a selected exact-ROM project has sufficient proof, specialize the build further by omitting unused mapper families, interpreter support, device paths, and diagnostics.

### First tracer bullet

Produce a certificate for a synthetic ROM with one direct function, one indirect dispatch table, one deliberately unresolved target, and one copied-RAM routine. Verify that each category is represented correctly and that the strict build refuses the unresolved case.

### Keep gate

- Static proof and dynamic observation are never conflated.
- Unexpected fallback terminates with a precise bank, address, and reason.
- The certificate is deterministic for identical inputs.
- A certified synthetic project can build without the interpreter.
- Any size or speed claim is measured separately from the correctness certificate.

## 4. Become a native reverse-engineering and testing workbench

Use stable metadata to make the generated project a better environment for understanding a game, not merely running it.

Potential capabilities include:

- native debugger symbols mapped to bank and address
- call graphs, cross-references, ownership, and data-reference reports
- manifest-selected probes without editing generated C
- deterministic function-entry and named-data traces
- run-until-function and run-until-data-change controls
- generated-versus-interpreter function-level fuzz harnesses for suitable leaf routines

This also improves native replacement work. A porter should be able to inspect a function's callers, dynamic frequency, side effects, and deterministic replay before deciding to replace it.

### First tracer bullet

Allow a native debugger to break on an imported symbol, show the corresponding Game Boy bank and address, inspect one named WRAM value, and display a native call stack using generated names.

### Keep gate

- Debug symbols and trace identifiers remain stable across identical regeneration.
- A manifest can select probes without patching generated files.
- Replaying the same input produces the same semantic event stream.
- Disabled instrumentation has no measurable release overhead or dependency.
- A deliberately injected generated-code error is caught by a focused differential harness.

## 5. Build a versioned mod-package format

Design the package contract alongside NL-5, then implement code-bearing packages after its ABI settles.

A mod package should describe:

- supported ROM hashes
- required runtime, metadata, and hook ABI versions
- replacement and hook identifiers
- data and assets
- dependencies, conflicts, and deterministic load order
- user-facing configuration schema
- requested host capabilities

Start with data-only packages. Add native developer modules after stable hooks exist, then evaluate sandboxed WebAssembly or Lua modules for portable community distribution.

### First tracer bullet

Install a data-only package that changes one extracted table and one tile without regenerating or editing the base project. The package must be toggleable and revision locked.

### Keep gate

- A package for the wrong ROM revision is rejected before execution.
- Disabling every package reproduces the base state, frame, and PCM evidence.
- Load order and conflicts are deterministic and diagnosable.
- Package installation never overwrites generated or user-authored source.
- Redistributable packages contain no copyrighted base-ROM data.

N64Recomp demonstrates the value of treating generated output as input to ports, tools, standalone environments, and fast patch iteration. Zelda64Recomp demonstrates that an exact-game recompilation becomes a distinct product through installable mods, modern controls, rendering enhancements, and an exact-ROM port contract rather than arbitrary-ROM compatibility.

References:

- [N64Recomp](https://github.com/N64Recomp/N64Recomp)
- [Zelda64Recomp](https://github.com/Zelda64Recomp/Zelda64Recomp)
- [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime)

## 6. Recognize reusable game-engine idioms

Build normalized fingerprints for common routines so porting knowledge can transfer across ROMs and revisions.

Initial candidates include:

- memory copy and fill
- data decompression
- text decoding
- tile upload
- sprite and OAM construction
- random-number generation
- fixed-point arithmetic
- known open-source audio or engine drivers

Recognition should first improve names, metadata, and analysis. Optional native intrinsics should come later and retain exact fallback behavior where hardware timing remains observable.

### First tracer bullet

Compile several open or synthetic implementations of copy, fill, text decoding, and tile upload with small layout variations. Detect them using a normalized control-flow and opcode fingerprint rather than raw byte identity.

### Keep gate

- Recognition has no false positives across the repository's synthetic corpus.
- A recognized function records the evidence that produced its classification.
- Relocation or harmless register-allocation variation does not break the fingerprint.
- Any native intrinsic has scalar-or-original fallback and independent differential proof.

## Supporting work that is not the differentiator

The following remain useful, but do not change the product category by themselves:

- WebAssembly and additional platform targets
- shader support and presentation filters
- Android and launcher polish
- binary ROM embedding and streaming generation
- rewind, achievements, and conventional netplay
- additional generic runtime speedups
- prettier generated C without a concrete authoring workflow

These features should support an authorable, embeddable, or extensible exact-game project. They should not substitute for one.

## Recommended sequence

1. Finish the minimal NL-5 replacement and hook ABI.
2. Split platform-neutral execution from SDL and generate a semantic game library.
3. Establish a regeneration-safe port-project manifest and schema-driven data lifting.
4. Add certified-AOT reports and source-aware debugging.
5. Combine NL-5 with lifted data and assets in a distributable mod system.
6. Prove the complete workflow with one legal synthetic showcase or an explicitly selected legal game.

## Experiment rules

Every direction in this document should follow the same evidence discipline as the completed optimization program:

- start with a legal synthetic tracer bullet
- lock behavior to exact input and toolchain identities
- separate correctness proof, measured performance, and inferred product value
- retain a deterministic conservative fallback where applicable
- reject experiments that add substantial complexity without changing the authoring, embedding, verification, or extension boundary

The project moves beyond a generated emulator when recompilation changes what developers can build, inspect, distribute, and integrate—not merely how quickly the original ROM loop executes.
