#!/usr/bin/env python3
"""End-to-end keep gates for the NL-5 exact-ROM native replacement SDK."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def run(
    command: list[str],
    *,
    cwd: Path,
    expect_success: bool = True,
    timeout: int = 180,
) -> subprocess.CompletedProcess[str]:
    env = os.environ.copy()
    env.update(
        {
            "SDL_VIDEODRIVER": "dummy",
            "SDL_AUDIODRIVER": "dummy",
        }
    )
    result = subprocess.run(
        command,
        cwd=cwd,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=timeout,
    )
    if expect_success != (result.returncode == 0):
        print(result.stdout, file=sys.stderr)
        expectation = "success" if expect_success else "failure"
        raise RuntimeError(
            f"expected {expectation}, got exit {result.returncode}: {' '.join(command)}"
        )
    return result


def write_manifest(
    path: Path,
    *,
    rom: Path,
    source_name: str,
    function: str = "gbfn:v1:0000:0160",
    sha256: str | None = None,
) -> None:
    rom_bytes = rom.read_bytes()
    payload = {
        "schema": "gbrecomp.native-patch",
        "version": 1,
        "patch_id": "org.gbrecompiled.nl5.synthetic",
        "rom": {
            "sha256": sha256 or hashlib.sha256(rom_bytes).hexdigest(),
            "size": len(rom_bytes),
        },
        "sources": [source_name],
        "bindings": [
            {
                "function": function,
                "pre": "nl5_pre",
                "replace": "nl5_replace",
                "post": "nl5_post",
            }
        ],
    }
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


PATCH_SOURCE = r'''#include "gbrt_native_patch.h"

GB_NATIVE_HOOK(nl5_pre) {
    GBContext* ctx = gb_native_context(call);
    ctx->hram[0]++;
    return GB_NATIVE_STATUS_OK;
}

GB_NATIVE_REPLACEMENT(nl5_replace) {
    GBContext* ctx = gb_native_context(call);
    ctx->hram[1]++;
    return gb_native_call_original(call);
}

GB_NATIVE_HOOK(nl5_post) {
    GBContext* ctx = gb_native_context(call);
    ctx->hram[2]++;
    return GB_NATIVE_STATUS_OK;
}
'''

FAILING_PATCH_SOURCE = r'''#include "gbrt_native_patch.h"

GB_NATIVE_HOOK(nl5_pre) {
    (void)call;
    return GB_NATIVE_STATUS_ERROR;
}

GB_NATIVE_REPLACEMENT(nl5_replace) {
    return gb_native_call_original(call);
}

GB_NATIVE_HOOK(nl5_post) {
    (void)call;
    return GB_NATIVE_STATUS_OK;
}
'''


def configure_and_build(project: Path) -> Path:
    build = project / "build"
    run(
        [
            "cmake",
            "-G",
            "Ninja",
            "-S",
            str(project),
            "-B",
            str(build),
            "-DGBRECOMP_ENABLE_STRIP=OFF",
        ],
        cwd=project.parent,
    )
    run(["ninja", "-C", str(build)], cwd=project.parent)
    executable = build / ("native_patch.exe" if os.name == "nt" else "native_patch")
    if not executable.is_file():
        raise RuntimeError(f"missing generated executable: {executable}")
    return executable


def run_state(
    executable: Path, output: Path, *, frames: int = 2
) -> dict[str, object]:
    run(
        [
            str(executable),
            "--headless",
            "--no-audio",
            "--limit-frames",
            str(frames),
            "--dump-state",
            str(output),
        ],
        cwd=executable.parent,
    )
    return json.loads(output.read_text(encoding="utf-8"))


def generate(
    gbrecomp: Path,
    rom: Path,
    output: Path,
    *,
    manifest: Path | None,
    extra_args: tuple[str, ...] = (),
    expect_success: bool = True,
) -> subprocess.CompletedProcess[str]:
    command = [
        str(gbrecomp),
        str(rom),
        "--no-scan",
        *extra_args,
        "-o",
        str(output),
    ]
    if manifest is not None:
        command.extend(["--native-patch", str(manifest)])
    return run(command, cwd=output.parent, expect_success=expect_success)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--gbrecomp", type=Path, required=True)
    parser.add_argument("--fixture-generator", type=Path, required=True)
    args = parser.parse_args()

    gbrecomp = args.gbrecomp.resolve()
    fixture_generator = args.fixture_generator.resolve()

    with tempfile.TemporaryDirectory(prefix="gbrecomp-native-patch-") as tmp:
        root = Path(tmp)
        rom = root / "native_patch.gb"
        run(
            [
                sys.executable,
                str(fixture_generator),
                "--mapper",
                "native-patch",
                "--output",
                str(rom),
            ],
            cwd=root,
        )

        patch_dir = root / "patch"
        patch_dir.mkdir()
        (patch_dir / "patch.c").write_text(PATCH_SOURCE, encoding="utf-8")
        manifest = patch_dir / "manifest.json"
        write_manifest(manifest, rom=rom, source_name="patch.c")

        generated = root / "generated-original-location"
        generate(
            gbrecomp,
            rom,
            generated,
            manifest=manifest,
            # Split the returning target at its loop body. Patchability must
            # follow the generated control-flow component, not stop at the
            # first strong-entry wrapper fragment.
            extra_args=("--add-entry-point", "0:0163"),
        )

        metadata = json.loads(
            (generated / "native_patch_metadata.json").read_text(encoding="utf-8")
        )
        assert metadata["schema_version"] == 2
        assert metadata["rom"]["sha256"] == hashlib.sha256(rom.read_bytes()).hexdigest()
        target = next(
            item for item in metadata["functions"] if item["id"] == "gbfn:v1:0000:0160"
        )
        assert target["patchable"] is True
        assert (generated / "native_patch/manifest.json").is_file()
        assert (generated / "native_patch/patch.c").is_file()

        generated_sources = "\n".join(
            path.read_text(encoding="utf-8")
            for path in generated.glob("native_patch_funcs_*.c")
        )
        assert "gbrt_native_patch_enter" in generated_sources
        assert "gbrt_native_patch_mark_call" in generated_sources
        assert generated_sources.count("static void body_") >= 1
        assert generated_sources.count("static void body_0160(") == 1

        # Configure after relocation: the copied manifest and sources must be
        # sufficient without the original patch directory.
        relocated = root / "relocated" / "native_patch"
        relocated.parent.mkdir()
        shutil.move(str(generated), relocated)
        patched_executable = configure_and_build(relocated)
        yielded_state = run_state(
            patched_executable, root / "patched-yielded-state.json", frames=1
        )
        assert yielded_state["hram_ff80_ff90"][:4] == [0x01, 0x01, 0x00, 0x00]
        patched_state = run_state(patched_executable, root / "patched-state.json")
        assert patched_state["hram_ff80_ff90"][:4] == [0x01, 0x01, 0x01, 0x01]
        replay_state = run_state(patched_executable, root / "patched-replay-state.json")
        assert replay_state == patched_state

        # Runtime identity is checked against the bytes actually embedded in
        # the executable, not only against generation-time inputs.
        tampered = root / "tampered" / "native_patch"
        shutil.copytree(relocated, tampered, ignore=shutil.ignore_patterns("build"))
        rom_source = tampered / "native_patch_rom.c"
        rom_text = rom_source.read_text(encoding="utf-8")
        rom_source.write_text(
            rom_text.replace("    0x00,", "    0x01,", 1), encoding="utf-8"
        )
        tampered_executable = configure_and_build(tampered)
        result = run(
            [str(tampered_executable), "--headless", "--limit-frames", "1"],
            cwd=tampered_executable.parent,
            expect_success=False,
        )
        assert "loaded ROM SHA-256 does not match" in result.stdout

        unpatched = root / "unpatched"
        generate(gbrecomp, rom, unpatched, manifest=None)
        unpatched_executable = configure_and_build(unpatched)
        unpatched_state = run_state(unpatched_executable, root / "unpatched-state.json")
        assert unpatched_state["hram_ff80_ff90"][:4] == [0x00, 0x00, 0x00, 0x01]
        for key in ("a", "f", "b", "c", "d", "e", "h", "l", "sp", "pc", "cycles"):
            assert patched_state[key] == unpatched_state[key], key

        # The default path retains strict differential behavior and contains no
        # generated native dispatch call.
        run(
            [str(unpatched_executable), "--differential", "5000", "--differential-fail-on-fallback"],
            cwd=unpatched_executable.parent,
        )
        injected_mismatch = run(
            [
                str(unpatched_executable),
                "--differential",
                "5000",
                "--differential-fail-on-fallback",
                "--differential-inject-mismatch",
                "10",
            ],
            cwd=unpatched_executable.parent,
            expect_success=False,
        )
        assert "[DIFF] Injected mismatch at step 10" in injected_mismatch.stdout
        assert "[DIFF] Mismatch at step 10" in injected_mismatch.stdout
        differential_seed = root / "differential-seed.gbs"
        run(
            [
                str(unpatched_executable),
                "--headless",
                "--limit-frames",
                "2",
                "--save-state-file",
                str(differential_seed),
            ],
            cwd=unpatched_executable.parent,
        )
        assert differential_seed.is_file()
        seeded_differential = run(
            [
                str(unpatched_executable),
                "--differential",
                "5000",
                "--differential-state",
                str(differential_seed),
                "--differential-fail-on-fallback",
            ],
            cwd=unpatched_executable.parent,
        )
        assert "[DIFF] Loaded comparison state" in seeded_differential.stdout
        unpatched_sources = "\n".join(
            path.read_text(encoding="utf-8")
            for path in unpatched.glob("native_patch_funcs_*.c")
        )
        assert "gbrt_native_patch_enter" not in unpatched_sources
        assert "gbrt_native_patch_mark_call" not in unpatched_sources

        # C++ callbacks use the same C-linkage declaration macros.
        cpp_dir = root / "patch-cpp"
        cpp_dir.mkdir()
        (cpp_dir / "patch.cpp").write_text(PATCH_SOURCE, encoding="utf-8")
        cpp_manifest = cpp_dir / "manifest.json"
        write_manifest(cpp_manifest, rom=rom, source_name="patch.cpp")
        cpp_generated = root / "cpp-generated"
        generate(gbrecomp, rom, cpp_generated, manifest=cpp_manifest)
        configure_and_build(cpp_generated)

        # Runtime callback failures must terminate with a nonzero status rather
        # than being cleared by the next frame-loop iteration.
        failing_dir = root / "patch-failing"
        failing_dir.mkdir()
        (failing_dir / "patch.c").write_text(FAILING_PATCH_SOURCE, encoding="utf-8")
        failing_manifest = failing_dir / "manifest.json"
        write_manifest(failing_manifest, rom=rom, source_name="patch.c")
        failing_generated = root / "failing-generated"
        generate(gbrecomp, rom, failing_generated, manifest=failing_manifest)
        failing_executable = configure_and_build(failing_generated)
        result = run(
            [str(failing_executable), "--headless", "--limit-frames", "2"],
            cwd=failing_executable.parent,
            expect_success=False,
            timeout=10,
        )
        assert "pre callback returned an error" in result.stdout

        # Generation-time configuration failures are fail-closed.
        mismatch_manifest = patch_dir / "mismatch.json"
        write_manifest(
            mismatch_manifest,
            rom=rom,
            source_name="patch.c",
            sha256="0" * 64,
        )
        mismatch_output = root / "mismatch-output"
        result = generate(
            gbrecomp, rom, mismatch_output, manifest=mismatch_manifest, expect_success=False
        )
        assert "SHA-256 mismatch" in result.stdout
        assert not mismatch_output.exists()

        unknown_manifest = patch_dir / "unknown.json"
        write_manifest(
            unknown_manifest,
            rom=rom,
            source_name="patch.c",
            function="gbfn:v1:0000:0170",
        )
        result = generate(
            gbrecomp,
            rom,
            root / "unknown-output",
            manifest=unknown_manifest,
            expect_success=False,
        )
        assert "was not discovered" in result.stdout

        invalid_contract = patch_dir / "invalid-entry-contract.json"
        write_manifest(invalid_contract, rom=rom, source_name="patch.c")
        invalid_contract_payload = json.loads(
            invalid_contract.read_text(encoding="utf-8")
        )
        invalid_contract_payload["bindings"][0]["entry_contract"] = "unchecked"
        invalid_contract.write_text(
            json.dumps(invalid_contract_payload), encoding="utf-8"
        )
        result = generate(
            gbrecomp,
            rom,
            root / "invalid-entry-contract-output",
            manifest=invalid_contract,
            expect_success=False,
        )
        assert "entry_contract" in result.stdout

        malformed = patch_dir / "malformed.json"
        malformed.write_text('{"schema":', encoding="utf-8")
        result = generate(
            gbrecomp,
            rom,
            root / "malformed-output",
            manifest=malformed,
            expect_success=False,
        )
        assert "Native patch manifest" in result.stdout

        escaping = patch_dir / "escaping.json"
        write_manifest(escaping, rom=rom, source_name="patch.c")
        escaping_payload = json.loads(escaping.read_text(encoding="utf-8"))
        escaping_payload["sources"] = ["../outside.c"]
        escaping.write_text(json.dumps(escaping_payload), encoding="utf-8")
        result = generate(
            gbrecomp,
            rom,
            root / "escaping-output",
            manifest=escaping,
            expect_success=False,
        )
        assert "contained relative path" in result.stdout

        # Containment is based on the resolved file, so a symlink cannot point
        # outside the source package even when its lexical name looks safe.
        outside = root / "outside.c"
        outside.write_text(PATCH_SOURCE, encoding="utf-8")
        symlink = patch_dir / "linked.c"
        try:
            symlink.symlink_to(outside)
        except (NotImplementedError, OSError):
            pass
        else:
            symlink_manifest = patch_dir / "symlink.json"
            write_manifest(symlink_manifest, rom=rom, source_name="linked.c")
            result = generate(
                gbrecomp,
                rom,
                root / "symlink-output",
                manifest=symlink_manifest,
                expect_success=False,
            )
            assert "resolves outside its package" in result.stdout

        unsafe_name = patch_dir / "unsafe-name.json"
        write_manifest(unsafe_name, rom=rom, source_name="patch.c")
        unsafe_payload = json.loads(unsafe_name.read_text(encoding="utf-8"))
        unsafe_payload["sources"] = ["patch;injected.c"]
        unsafe_name.write_text(json.dumps(unsafe_payload), encoding="utf-8")
        result = generate(
            gbrecomp,
            rom,
            root / "unsafe-name-output",
            manifest=unsafe_name,
            expect_success=False,
        )
        assert "portable path name" in result.stdout

        leading_zero = patch_dir / "leading-zero.json"
        leading_zero.write_text(
            manifest.read_text(encoding="utf-8").replace('"version": 1', '"version": 01'),
            encoding="utf-8",
        )
        result = generate(
            gbrecomp,
            rom,
            root / "leading-zero-output",
            manifest=leading_zero,
            expect_success=False,
        )
        assert "leading zero in JSON number" in result.stdout

    print("native patch generation, relocation, C/C++, behavior, and fail-closed gates passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
