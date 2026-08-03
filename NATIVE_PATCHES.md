# Native replacement SDK

Native patches are optional, exact-ROM source packages that hook or replace a
discovered generated function. They are intended for durable native ports,
mods, accessibility work, and later higher-level platform integrations. A
normal generated project has no native-patch dispatch in its wrappers.

This is a source ABI: patch code is copied into and rebuilt with the generated
project. It is not a dynamically loaded binary-plugin format.

## Try the legal synthetic example

The repository owns a small ROM fixture whose function `gbfn:v1:0000:0160`
runs across a frame safepoint before returning:

```bash
python3 tests/fixtures/make_mapper_rom.py \
  --mapper native-patch \
  --output output/native-patch.gb

./build/bin/gbrecomp output/native-patch.gb \
  --no-scan \
  --native-patch examples/native_patch/manifest.json \
  -o output/native-patch

cmake -G Ninja -S output/native-patch -B output/native-patch/build
ninja -C output/native-patch/build

./output/native-patch/build/native-patch \
  --headless \
  --no-audio \
  --limit-frames 2 \
  --dump-state logs/native-patch-state.json
```

The first four `hram_ff80_ff90` bytes are `1, 1, 1, 1`: pre and replacement
ran once, the generated original completed once, and post ran only after its
guest `RET`. At a one-frame limit, the bytes are `1, 1, 0, 0`, proving that a C
body yield does not incorrectly trigger post.

## Manifest v1

```json
{
  "schema": "gbrecomp.native-patch",
  "version": 1,
  "patch_id": "org.example.my-port",
  "rom": {
    "sha256": "64 lowercase hexadecimal digits",
    "size": 32768
  },
  "host_configuration": {
    "schema": "gbrecomp.host-configuration",
    "version": 1,
    "policy_id": "example-v1",
    "offset_limit": 5,
    "value_minimum": 1,
    "value_maximum": 100
  },
  "sources": ["patch.c", "patch_config.h"],
  "bindings": [
    {
      "function": "gbfn:v1:0000:0160",
      "pre": "my_pre",
      "replace": "my_replace",
      "post": "my_post"
    }
  ]
}
```

Paths are relative to the manifest, must use portable ASCII filename characters,
and must remain inside its directory after resolving symlinks. C, C++, and
callback symbol names are validated before output is written. One v1 package
can bind each function at most once. A binding can omit any callback, but must
declare at least one. `patch_id` accepts ASCII letters, digits, `.`, `_`, and
`-` so the same value is safe in generated C and diagnostics.

`host_configuration` is optional. When present, generated executables expose
`--host-configuration <file>` and validate canonical JSON against the declared
schema, policy, symmetric offset limit, and value bounds before constructing a
guest context. A missing file is the disabled state. Other failures terminate
before guest execution, and diagnostics retain only stable status, policy, and
content hash. Native callbacks read the applied, path-free identity and scalar
values from `GBContext.config.host_configuration`.

Bindings reached through a verified ROM trampoline rather than a generated
direct `CALL` may opt into `"entry_contract": "return-stack"`. The runtime
then requires the target to be entered with a readable WRAM/HRAM guest return
frame and captures its SP and return PC before invoking callbacks. This mode
is exact-ROM and explicit; an ordinary binding still fails closed when no
generated `CALL`/`RST` marker exists.

Function IDs use physical ROM bank plus CPU address. They survive emitted C
renaming and chunk changes, but are intentionally scoped to the exact ROM hash.
Every generated project exposes discovered IDs and `patchable` status in
`*_metadata.json`; `*_native.h` provides matching numeric C macros. Emitted C
names are diagnostic and are not an SDK contract.

Generation fails for an unsupported schema, malformed manifest, wrong ROM
size/hash, unsafe or missing source, duplicate binding, unknown ID, or a target
that is not a discovered ROM-backed returning function. The embedded ROM is
hashed again at runtime initialization. A mismatch or callback contract error
stops with a native-patch diagnostic; it never silently disables the package or
falls back to the interpreter.

## Callback ABI

Patch packages may contain C/C++ translation units and private headers. All
declared files are copied into the relocatable generated project; only `.c`,
`.cc`, `.cpp`, and `.cxx` entries are compiled directly. Header entries use
`.h`, `.hh`, `.hpp`, or `.hxx` and remain subject to the same containment and
portable-name checks.

Patch sources include `gbrt_native_patch.h` and use the declaration macros:

```c
GB_NATIVE_HOOK(my_pre) {
    GBContext* ctx = gb_native_context(call);
    return GB_NATIVE_STATUS_OK;
}

GB_NATIVE_REPLACEMENT(my_replace) {
    return gb_native_call_original(call);
}

GB_NATIVE_HOOK(my_post) {
    GBContext* ctx = gb_native_context(call);
    return GB_NATIVE_STATUS_OK;
}
```

Execution order is pre, replacement or original, then post. Crucially,
`gb_native_call_original()` returns a disposition; it does not call the
generated body on the callback's C stack. Generated execution can yield at a
frame or interrupt safepoint, so the runtime retains the invocation and runs
post only after the matching guest return.

Returning `GB_NATIVE_REPLACE_HANDLED` means the replacement owns the complete
guest callee contract: CPU/memory effects, elapsed guest timing, and the final
`RET`. The runtime verifies the captured return PC and SP before post runs.
`GB_NATIVE_REPLACE_USE_ORIGINAL` preserves the generated function's timing and
fallback behavior and is the recommended starting point.

`gb_native_use_host_presentation(call)` reports the generated executable's
`--native-presentation native|original` selection. It is only a presentation
policy query: a patch that requests host UI should still use
`gb_native_call_original()` whenever the original callee owns timing, mapper,
or persistence effects.

V1 directly exposes the source-compatible `GBContext` and runtime helpers.
Patches must be rebuilt when the ABI changes. Exceptions must not cross the C
callback boundary, and callbacks must not retain `GBNativeCall*` after return.

## Bounded gameplay mutations

Pre hooks that need to change reviewed guest inputs can include
`gbrt_gameplay_mutation.h`. The v1 API accepts an exact-ROM event
specification, stable function ID, one to eight named typed fields, per-field
ranges, and a matching value request. V1 deliberately supports only `uint8_t`
fields; adding another representation requires an ABI review rather than an
implicit cast.

`gb_native_apply_gameplay_mutation()` is valid only during the named binding's
pre phase. It rejects the wrong ABI, ROM identity, function, field set, type,
or range before beginning a transaction. It then stages every field, verifies
the staged values, optionally runs a patch-owned semantic validator, and
commits all fields together. Every result other than
`GB_GAMEPLAY_MUTATION_APPLIED` leaves live guest memory without a partial
write. The exact ROM is already rehashed by native-patch entry validation
before a callback can receive its opaque `GBNativeCall`.

The pre hook should propose only inputs consumed by the original function and
then return `GB_NATIVE_STATUS_OK`; the generated original body remains
responsible for derived state and guest timing. A rejected optional gameplay
rule may log its stable mutation status and preserve the original path. A
native-patch contract failure, by contrast, remains a fatal runtime error as
described above.

This is not a general writable-memory API. A package must enumerate the exact
semantic fields and lifecycle hook it owns. Crystal's first consumer binds the
wild-battle `LoadEnemyMon` pre hook, changes only `wCurPartyLevel`, and lets the
original body calculate `wEnemyMonLevel`, HP, and battle stats.

## Scope limits

V1 intentionally excludes copied-RAM or mid-block hooks, multiple-package
composition, hot loading, direct renderer/GPU access, and cross-ROM function
identity. A binding may request a separately compiled exact-ROM port surface;
the patch ABI itself does not expose that surface's renderer. The accurate
generic runtime and original-presentation mode remain the compatibility
oracle.
