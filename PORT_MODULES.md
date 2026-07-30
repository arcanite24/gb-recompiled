# Port and frontend modules

GB Recompiled can compile one exact-ROM native port module and a bounded set
of exact-ROM source-built extensions into a generated project. This host
functionality is optional: the generated game, accurate PPU path, and headless
execution remain available independently.

The v2 ABI is declared in `runtime/include/gbrt_port.h`. It deliberately does
not expose SDL, ImGui, a graphics API, generated function names, `GBContext`,
or writable guest-memory pointers. In addition to bounded read services, a
reviewed source-built exact-ROM integration can submit one synchronous
semantic edit through the runtime-owned transaction service.

## Activation boundary

A module exports:

```c
const GBPortModule* gb_port_module_get(void);
```

Before `activate` runs, the runtime validates:

- port ABI version 2;
- module and generated metadata ROM size;
- module and generated metadata ROM SHA-256;
- the SHA-256 of the actual embedded ROM bytes; and
- availability of an exact-ROM live semantic reader.

Any mismatch fails activation. The module is never called partially.

## Services

`GBPortServices` supplies:

- immutable game ID, title, ROM size, and ROM SHA-256 metadata;
- an exact-ROM `GBSemanticReader` for bounded read-only views;
- a one-shot `run_semantic_edit` callback and opaque service token;
- a headless flag;
- host-owned structured logging; and
- an opaque host user value.

The module receives input, per-frame update, and render callbacks. Input uses
versioned semantic actions rather than platform key codes. In addition to
toggle, close, navigation, accept, and back, `GB_PORT_INPUT_OPEN_UI` and
`GB_PORT_INPUT_OPEN_PC` let verified native function bindings request a
specific host presentation without teaching the guest runtime about the
module's internal screens. Rendering
produces a bounded `GBPortFrame` containing renderer-independent panel and
text commands. A host may submit those commands to a renderer, capture them
for tests, or omit the submit callback entirely in headless mode.

The v1 frame holds at most 128 commands and 128 bytes per text command.
Overflow is rejected by the frame helpers.

The semantic-edit callback receives an ephemeral staged transaction. A module
may perform bounded generated staging and validation during that callback, but
must not retain the transaction pointer or commit/abort it itself. The runtime
verifies exact ROM identity, owns begin/commit/abort, and commits only a
still-active validated transaction after the callback returns. Guest execution
is paused at this synchronous host-input safepoint.

The SDL host adapter renders panels and text through its ImGui foreground
layer. F2 sends the renderer-independent base-module toggle and F3 sends the
encounter-extension toggle. The same final command frame is retained in
`--port-state` JSON, so headless evidence can validate the exact presentation
without requiring a graphics device or inspecting pixels produced by one host
renderer.

## Lifecycle

1. The generated executable initializes the ordinary game context.
2. `gbrt_port_attach` validates exact identity and activates the module.
3. Each completed guest frame dispatches one update and one render.
4. Host input is delivered with `gbrt_port_input`.
5. `gbrt_port_detach` runs before the context is destroyed.

Port state is host state. It is excluded from guest savestates, differential
comparison, guest state dumps, and battery persistence.

## Source-built modules and extensions

The first supported composition model is intentionally narrow: one module is
compiled from reviewed source into the generated project. Crystal Recompiled
uses `ports/pokemon-crystal/module/port-module.json`, whose source hash,
ABI, and exact ROM identity are validated before generation.

Port extension ABI v1 adds independently packaged behavior around that one
host module. Generation validates every manifest and source hash, requires the
exact host module and ROM, resolves dependencies and conflicts, then sorts the
set by `(priority, extension ID)`. It emits a static registry and records the
ordered manifests and sources in the generation receipt. The runtime repeats
the ABI, descriptor, order, and exact-ROM checks before activating anything.
Activation runs base-first then in resolved order; partial failure unwinds
already active extensions in reverse. Input, update, and render use the same
order, and detach uses reverse order.

Extensions receive a reduced v2 service view and the bounded command frame.
The first capability profile permits host input, host draw commands, logging,
metadata, and semantic reads.
It exposes no SDL or graphics API, `GBContext`, generated function entry,
guest-memory pointer, filesystem, network, native-patch call, or semantic
write service. Native-patch scheduling and `gb_native_call_original()`
safepoints are therefore unchanged.

Dynamic native libraries and portable sandboxed bytecode remain unsupported.
The retained source-built model is documented with its concrete Crystal
consumer and rejected alternatives in
`ports/pokemon-crystal/NATIVE_EXTENSIONS.md`. Data-mod composition remains a
separate startup overlay contract.
