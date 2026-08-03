# Embedded runtime snapshot

`gbrecomp` copies this directory into each generated project so the output is
self-contained and can be configured after the recompiler distribution is
moved or removed.

The runtime code is part of `gb-recompiled` and is distributed under the MIT
license in `LICENSE`. The vendored Dear ImGui sources retain their upstream
license and provenance in `vendor/imgui/LICENSE.txt` and
`vendor/imgui/UPSTREAM.md`.
