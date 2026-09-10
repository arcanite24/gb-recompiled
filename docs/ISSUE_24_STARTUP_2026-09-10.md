# Issue #24: Contra / Probotector startup

Investigated main at `ac8b983d092b07b5a8e6ea7dd9a5d0320d2c7f81` on macOS
arm64 with SDL 2.32.70, CMake, and Ninja. The report ends with
`[SDL] Creating renderer...` and SIGSEGV. That message also precedes ImGui,
texture setup, and game execution; it does not identify the crashing call.

## Reproduced defect and fix

Freshly generated Probotector (Europe) and Contra (Japan) both failed to finish
`--limit-frames 120` within 45 seconds, in each of headless, normal windowed,
and software-renderer modes. Sampling the headless process located execution
in a generated function repeatedly calling `gbrt_timed_jump`, rather than SDL.

The emitter's unconditional direct-function jump path omitted the safepoint
check after `gbrt_timed_jump`. Probotector's idle `JR -2` at `$01C6` lowers to a
call back into its generated body because that body may change ROM banking.
It therefore kept executing after the runtime requested a stop for a frame,
interrupt, or cycle budget. Tail-call optimization can make this an infinite
host loop; without it, repeated calls can grow the host stack. The original
reporter's SIGSEGV was not reproduced or backtraced on this host.

The fix checks `gbrt_generated_safepoint` immediately after the timed jump and
before direct invocation, as the local-goto and conditional-jump paths already
do. It preserves the guest target PC and jump timing. No SDL, PPU, or ROM-specific
behavior was changed. Existing generated projects must be regenerated and rebuilt.

## Verification

- `generated_jump_safepoints_end_to_end` builds a repository-owned synthetic
  MBC1 ROM whose JP/JR loops use the direct-function path. It times out before
  the fix, and passes afterward. Normal generated dispatch (not single-step)
  returns at cycle-budget, frame, and interrupt boundaries with the expected
  guest PC and no interpreter fallback.
- Root CMake/Ninja build and all **85 CTests pass**, including generated-project,
  native-patch, and release relocation tests.
- A fresh synthetic ROM project generates, configures, builds, and completes
  `--headless --limit-frames 120`.
- Both commercial-ROM reproductions were regenerated and rebuilt. All six
  startup runs now exit zero at the requested 120-frame limit and produce
  final state dumps with zero dispatch fallback. The state snapshot's
  `completed_frames` field is 119 in all six; the CLI reports its 120-frame
  limit. Captures at frame 119 visibly show the PALCOM and KONAMI startup logos.
- Each game's frame-119 capture is byte-identical across headless, default,
  and software rendering. These guest-frame captures do not certify host
  rendering equivalence, later gameplay, or the reporter's platform.
- Before the fix, Probotector matched the interpreter for 100,000 differential
  steps / four frames. This did not detect the defect because single-step
  execution returns before the direct call. Differential mode also shares
  the runtime and is not an independent hardware oracle.

Local artifacts are under `logs/issue24/`, including `startup-results.json`,
`startup-results-after.json`, `jump-regression-before.log`, `ctest-after.log`,
state dumps, captures, and the sampled stack. ROMs and generated game code
remain under ignored local paths and are not part of the source change.

## Remaining issue gate

Keep #24 open pending a regenerated-build retest on the affected machine.
The report does not specify the Contra edition, OS, SDL version, or a
backtrace. If it still crashes, obtain a debugger backtrace and compare
headless and software-renderer startup. The local fix establishes a generated
execution defect and successful startup for the two named ROMs on macOS;
it does not establish the cause of the reported SIGSEGV.
