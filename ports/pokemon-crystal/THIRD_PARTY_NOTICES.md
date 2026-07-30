# Third-party notices

Crystal Recompiled's original launcher, port module, native extensions,
schemas, tests, documentation, and presentation assets are distributed under
the repository's MIT license unless a file says otherwise. The presentation
asset manifest identifies the project-owned replacement assets as CC0-1.0.

The embedded GB Recompiled SDK contains a minimal Dear ImGui 1.90.4 source
snapshot. Its upstream notice and MIT license are retained at:

- `sdk/gb-recompiled/runtime/vendor/imgui/UPSTREAM.md`
- `sdk/gb-recompiled/runtime/vendor/imgui/LICENSE.txt`

SDL2 is an external build/runtime dependency under the zlib license. Linux and
macOS packages do not redistribute SDL2. The Windows package includes
`sdk/gb-recompiled/SDL2.dll` and the corresponding upstream license copied
from the same pinned SDL2 2.30.6 development archive at
`sdk/gb-recompiled/THIRD_PARTY/SDL2-LICENSE.txt`.

The release does not redistribute Pokémon Crystal, extracted game content,
pret disassembly files, SameBoy, PKHeX, RGBDS, or any other local reference
checkout. URLs, commit identities, licenses, and the permitted fetch boundary
are documented in `ports/pokemon-crystal/REFERENCES.md` and
`ports/pokemon-crystal/references/sources.lock.json`.
