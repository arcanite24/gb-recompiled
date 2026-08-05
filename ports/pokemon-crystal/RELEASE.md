# Public release review

This is the release-owner checklist for Crystal Recompiled. It separates
implemented, reproducible controls from approvals and host evidence that still
have to be collected. It is not legal advice.

## Supported compatibility contract

The standalone port requires GB Recompiled 0.1.0 with the exact ABI set and
generation features in
`ports/pokemon-crystal/standalone/gbrecomp-contract.json`. Bootstrap and
packaging fail closed if the CLI identity, runtime source-tree hash, SDK
inventory, target platform, or architecture disagrees.

The only supported game input is the unmodified US/Europe Rev 1 ROM identified
in `ports/pokemon-crystal/README.md`. The ROM is selected and verified locally;
it and all generated content remain outside the source and release archives.
The public compatibility claim is the checked four-segment route, deterministic
mod and persistence contracts, and the explicitly evidenced native features.
It is not a claim of whole-game compatibility.

Current evidence:

- baseline generation, route, restart, and save:
  `ports/pokemon-crystal/evidence/CR-M1-003.md`,
  `ports/pokemon-crystal/evidence/CR-M2-004.md`, and
  `ports/pokemon-crystal/evidence/CR-M2-005.md`;
- semantic, native feature, transaction, and mod contracts:
  `ports/pokemon-crystal/evidence/CR-M4-006.md`,
  `ports/pokemon-crystal/evidence/CR-M5-005.md`, and
  `ports/pokemon-crystal/evidence/CR-M6-005.md`;
- presentation and ROM-private standalone generation:
  `ports/pokemon-crystal/evidence/CR-M7-003.md`,
  `ports/pokemon-crystal/evidence/CR-M7-004.md`, and
  `ports/pokemon-crystal/evidence/CR-M7-005.md`.

## Known limitations

- Only the exact Rev 1 ROM is accepted.
- The checked route is strong release evidence, not whole-game coverage.
- Some code outside the checked route may still use the safe interpreter
  fallback.
- Native presentation is deliberately bounded. Accurate presentation remains
  authoritative for unsupported scenes and guest-visible hardware behavior.
- First run requires Python, CMake, Ninja, a compiler, SDL2 development files,
  and initial network access for one pinned symbol input.
- Native extensions are source-built into a regenerated executable. Arbitrary
  runtime-loaded native code is not supported.
- Data mods are constrained deterministic overlays; they are not general ROM
  patches or executable plugins.
- Controller-first release acceptance requires a physical-device smoke on each
  supported host family; keyboard controls remain available.

## Release provenance

Every source tree, SDK, and platform package has a complete SHA-256 inventory.
The platform package records its target, SDK identity, source-manifest hash,
SDK-manifest hash, entry points, and explicit absence of ROM/generated-game
content in `crystal-release.json`. Release archives normalize order, timestamps,
ownership, and modes. The exact-ROM verifier emits a path-free
`crystal-recompiled.packaged-release-verification` result for each clean host.

The release owner must retain:

- the Git commit and tag;
- the four archive hashes and `crystal-release.json` hashes;
- the four host verification results from Linux x64, macOS x64, macOS arm64,
  and Windows x64;
- the physical-controller verification results for every required host family;
- the GitHub Actions run URL and toolchain/runner identities; and
- the legal-review record and named approver.

## Source-only alpha checklist

A source-only fan-project alpha publishes the standalone repository without
platform archives or ROM-derived executables. It may ship with one currently
verified host while other hosts remain explicitly best-effort.

- [ ] The release commit is clean, reviewed, and tagged.
- [ ] The standalone source inventory contains only intended original/permitted
  files and reproduces from the tagged parent commit.
- [ ] The required GB Recompiled 0.1.0 distribution is publicly available.
- [x] macOS arm64 exact-ROM packaged verification passes for the M8 candidate.
- [x] Automated Challenge Mode panel, wild/trainer route, determinism,
  persistence, vanilla recovery, and privacy gates pass.
- [ ] `THIRD_PARTY_NOTICES.md`, the trademark disclaimer, and the distribution
  boundary have been reviewed for the intended source-only publication.
- [ ] Documentation screenshots have been reviewed for copyright/trademark
  scope and contain no private desktop, path, save, or account information.
- [ ] Release notes link the current evidence and identify Linux, macOS Intel,
  Windows, and combined physical-controller acceptance as incomplete.

## Broad platform-package checklist

This later gate applies when publishing downloadable Crystal Recompiled
platform archives. It does not block a source-only alpha repository.

- [ ] Linux x64 exact-ROM packaged verification passes.
- [ ] macOS x64 exact-ROM packaged verification passes.
- [ ] Windows x64 exact-ROM packaged verification passes.
- [ ] Physical controller selection and gameplay smoke pass on each host
  family represented by the supported packages.
- [ ] All four archives are reproduced from the tagged commit and their
  inventories contain only intended files.
- [ ] `THIRD_PARTY_NOTICES.md`, the ImGui license, and the Windows SDL2 license
  are present where applicable.
- [ ] The trademark disclaimer and distribution boundary have been reviewed.
- [ ] Qualified legal review approves the intended jurisdictions and release
  model.
- [ ] Package release notes repeat the known limitations without broadening
  compatibility, accuracy, or performance claims.

Do not publish broad platform packages while an item in this section is
unchecked. A source-only alpha follows the narrower checklist above.
