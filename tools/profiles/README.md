# Reproducible profiling inputs

These scripts use the runtime's cycle-anchored `c<cycle>:<buttons>:<duration>` format. Cycle anchors make the workload independent of host frame pacing and are required by `tools/run_nl0_profile.py`.

The scripts contain input timing only. ROMs remain local and are identified by SHA-256 in each generated profile artifact.

- `tetris.input`: small DMG workload
- `links_awakening.input`: mapper-heavy DMG workload
- `tetris_dx.input`: CGB workload; directional performance evidence only while its known interpreter fallbacks remain

Each profile artifact records the input hash, ROM hash, generated binary hashes, build configuration, enabled subsystems, revision/dirty state, state hash, timings, and memory measurements.
