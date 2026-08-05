# Recompiler correctness roadmap

Updated: 2026-08-04

## Goal

Keep five layers semantically aligned:

1. ROM decoding
2. analyzer state and bank resolution
3. IR and generated C
4. interpreter fallback
5. mapper and device timing

Coverage is necessary but insufficient. A compiled address can still use the
wrong operand, bank, cycle phase, flag behavior, or hardware side effect.

## Verified foundation

The repository now has focused coverage for:

- generated-versus-interpreter comparison of CPU, memory, mapper, and device
  state;
- fail-closed external-ROM tests with deterministic state dumps;
- analyzer joins, mapper-aware bank resolution, and 9-bit MBC5 identities;
- final-M-cycle memory access and shared bus phases for stack, control flow,
  SP-relative, and `(HL)` operations;
- timer overflow/reload edges, interrupt entry, HALT transitions, and DMG OAM
  corruption;
- event-aware PPU phases, model-aware OAM DMA, and scheduler-invariant PCM;
- fresh generated-project, multi-ROM, native-patch, relocation, and release
  fixtures.

The [accuracy report](../ACCURACY.md) is the external-test snapshot. The
[code improvement audit](CODE_IMPROVEMENT_AUDIT_2026-07-12.md) retains the
completed P0 findings and implementation evidence. Neither a successful build
nor interpreter agreement is an independent hardware oracle.

## Remaining correctness risks

### CGB boot and I/O edges

The configured post-boot state does not emulate a CGB boot ROM, and several
undocumented I/O masks remain incomplete.

Next work:

- distinguish configured post-boot state from boot-ROM execution in DIV tests;
- complete model-specific readback for the remaining unused-I/O cases;
- validate KEY0/KEY1 and speed-switch phases against Pan Docs and SameBoy.

### Bank-aware direct targets

Unknown targets currently use safe dispatch. That is slower, but preferable to
persisting an unsound bank.

Next work:

- fix physical-ROM reads used by banked indirect-jump tables;
- persist direct `CALL` and `JP` banks only when mapper state proves them;
- add same-address, different-bank fixtures before enabling faster dispatch.

### Operand representation and duplicate semantics

Magic operand indices and old lowering paths make it possible to fix one
execution path while leaving another inconsistent.

Next work:

- replace magic register values with typed operand variants;
- add exhaustive IR instruction and operand validation;
- remove or isolate obsolete emitter, generator, and lowering paths;
- remove public analyzer or optimizer options that are not implemented.

### Validation independence and cost

Differential execution compares two paths that share mapper and device code,
while full mutable-memory comparison is expensive.

Next work:

- use region hashes or dirty ranges as a fast mismatch gate;
- retain an explicit strict full-memory mode;
- add injected-mismatch tests for first-divergence localization;
- compare selected state, frame, and audio hashes with independent tests or
  SameBoy.

### Trace-assisted discovery

The current PyBoy helper samples one program counter per frame. It is a code
discovery hint, not instruction coverage or semantic proof.

Next work:

- capture instruction-level traces where the reference API permits it;
- record mapper bank and input provenance with each trace;
- rename sampled output so its fidelity is explicit;
- prefer trusted symbols and annotations when they provide reviewed boundaries.

## Validation ladder

Every correctness change should use the smallest relevant layers and finish
with an independent signal:

| Layer | Required evidence |
| --- | --- |
| Unit or synthetic | Exact state, mapper, bus-phase, or metadata assertions |
| Generated smoke | Fresh generation, configure, build, and bounded headless run |
| Differential | No divergence, with unexpected fallback rejected |
| External ROM | Relevant Mooneye or Blargg case reaches its real pass protocol |
| Game-specific | Cycle-anchored replay plus state, frame, or audio evidence |

Do not use coverage percentage, successful compilation, or interpreter
agreement alone as a hardware-accuracy claim.

## Execution order

1. Fix the remaining CGB boot and unused-I/O failures.
2. Make bank-aware direct targets sound before optimizing indirect dispatch.
3. Replace ambiguous operand representation before broad analyzer refactors.
4. Reduce differential cost without weakening strict comparison.
5. Improve trace fidelity only where it adds discovery coverage beyond trusted
   annotations.

Open implementation work is tracked in the [project backlog](../TODO.md).
Measured performance work follows the
[native recompilation strategy](NATIVE_RECOMPILATION_STRATEGY_2026-07-14.md).
