# NL-5 native replacement SDK result

Date: 2026-07-14

Status: retained tracer bullet

## Decision

Retain the exact-ROM native replacement SDK as a recompilation-specific product
capability. It does not target faster unmodified games. It creates a stable,
regeneration-safe seam for native ports and mods without adding a lookup or
branch to an unpatched generated wrapper.

The selected design is a generation-time, statically linked source package.
Function identity is `gbfn:v1:<physical-bank>:<guest-address>` plus exact ROM
SHA-256 and size. A replacement requests the one generated original body by
returning the disposition from `gb_native_call_original()`. A bounded
per-context invocation stack preserves pre/original/post lifetime across C-body
safepoint returns and recursive or nested guest calls.

The rejected synchronous call-original design was smaller but semantically
wrong: generated C can return at a frame or interrupt boundary before the guest
function executes `RET`. The rejected fully composable registry was a plausible
future SDK, but priorities, opaque register accessors, and extension discovery
were not justified for the first slice. The durable decision is also recorded
in `.gsd/DECISIONS.md`.

## Delivered

- strict versioned JSON parsing with unknown-field, duplicate-key, numeric,
  identifier, portable-name, resolved source-containment, and extension
  validation;
- generation-time and runtime SHA-256 validation against the exact ROM bytes;
- stable metadata schema v2 IDs, numeric IDs, ROM identity, and patchability;
- generated `*_native.h`, normalized copied manifest, and self-contained C/C++
  patch sources;
- pre, replacement, post, handled, error, and deferred original dispositions;
- direct CALL/RST frame markers before safepoints and a 32-deep pending and
  active invocation stack;
- one existing generated original body, with dispatch only in explicitly bound
  wrappers;
- single-ROM desktop and Android generated-CMake integration; multi-ROM patch
  input fails clearly in v1;
- a repository-owned 32 KiB ROM and source package under
  `examples/native_patch/`;
- end-to-end CTest coverage for C, C++, relocation, exact state, safepoint
  deferral, unpatched differential execution, malformed and mismatched input,
  unknown functions, escaping paths, and tampered embedded ROM bytes.

## Correctness evidence

The synthetic target runs for about 84K guest T-cycles, crossing a frame
safepoint. With the patch enabled:

- after one frame, HRAM markers are `1, 1, 0, 0`: pre and replacement ran once,
  while the original and post have not completed;
- after two frames, markers are `1, 1, 1, 1`: the same invocation resumed, the
  one generated body returned, and post ran exactly once;
- the completed patched and unpatched runs have matching CPU registers, PC, SP,
  and cycle count; only the declared hook markers differ;
- a second patched reset-to-two-frames run produces byte-identical state;
- the unpatched generated project passes 5,000 strict differential steps with
  fallback rejection;
- a generated project moved before configuration still builds and runs from
  its copied manifest, sources, and runtime snapshot;
- a modified embedded ROM exits before execution with an exact-ROM mismatch.
- a callback error exits nonzero instead of being cleared by the frame loop.

The fresh root build and all 35 repository CTests pass in `build-nl5`.

## Footprint and performance gate

An unpatched generation contains no `gbrt_native_patch_enter`, call marker, or
native runtime source in its compiled targets. Its wrappers retain the prior
direct `body(ctx)` shape. Metadata and `*_native.h` grow generated artifacts but
are not compiled execution code. Patch runtime state is behind
`GBRT_ENABLE_NATIVE_PATCHES` and is absent from an unpatched `GBContext`.

For the legal 32 KiB fixture, the patch-enabled generated tree adds 2,869 bytes
of C, headers, and JSON over the unpatched tree (375,702 versus 372,833 bytes,
+0.77%). The unstripped generated executable adds 960 bytes (534,256 versus
533,296 bytes, +0.18%). These are patch-enabled costs; the unpatched compiled
targets contain neither the patch runtime nor its context state.

Generation completed in 0.06 seconds in both cases. On this very small input,
the patch-enabled process reported 147,456 more maximum resident bytes and the
same 147,456-byte peak-footprint delta (0.14 MiB). The absolute observation is
more useful than a percentage at this scale.

The no-manifest runtime gate is structural: its generated wrappers retain the
same direct body call, its compiled targets contain no patch entry/call-marker
symbols, and `GBRT_ENABLE_NATIVE_PATCHES` is absent. A stricter patch-enabled
control used a no-op replacement that only requests the original. Ten
order-alternating, profiling-off full-headless trials ran 20,000 frames after
two warmups and required one identical final-state hash across both binaries.
The unpatched median was 3.9621 seconds; the no-op patch median was 3.8986
seconds (-1.60%). Maximum sampled RSS was 8,142,848 versus 8,192,000 bytes
(+49,152 bytes). The target executes once, so the faster result is treated as
code-layout/measurement variance, not an SDK speedup. It establishes no
runtime regression at the 1% keep threshold. Raw evidence is in
`logs/nl5-final-noop-interleaved-20k.json`.

## Boundaries and follow-up

- IDs are stable only within one byte-exact ROM and while analysis still exposes
  the function. Missing discovery fails instead of retargeting.
- V1 patches direct generated CALL/RST contracts. Copied-RAM, mid-block,
  unknown-indirect, and jump-entry hooks remain unsupported.
- A handled replacement owns hardware timing and control-flow accuracy; it does
  not inherit compatibility evidence from the generated original.
- Savestate capture during an active patched invocation is not yet a portable
  continuation contract. Deterministic replay from reset is covered.
- GPU and renderer APIs are deliberately absent. A later game-specific native
  rendering slice should build on this SDK only after choosing a concrete game
  and defining a shadow/timing contract.
