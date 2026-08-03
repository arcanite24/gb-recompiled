#!/usr/bin/env python3
"""Prove a relocated gbrecomp distribution emits a buildable, runnable project."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def run(command: list[str], *, cwd: Path, env: dict[str, str] | None = None) -> None:
    result = subprocess.run(
        command,
        cwd=cwd,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=120,
    )
    if result.returncode != 0:
        print(result.stdout, file=sys.stderr)
        raise RuntimeError(
            f"command exited with {result.returncode}: {' '.join(command)}"
        )


def copy_distribution(
    destination: Path,
    *,
    source_distribution: Path | None,
    gbrecomp: Path | None,
    runtime_dir: Path | None,
) -> Path:
    destination.mkdir(parents=True)
    if source_distribution is not None:
        shutil.copytree(source_distribution, destination, dirs_exist_ok=True)
    else:
        assert gbrecomp is not None and runtime_dir is not None
        shutil.copy2(gbrecomp, destination / gbrecomp.name)
        shutil.copytree(
            runtime_dir,
            destination / "runtime",
            ignore=shutil.ignore_patterns("build", "build_*", ".DS_Store"),
        )
        license_path = runtime_dir.parent / "LICENSE"
        if license_path.is_file():
            shutil.copy2(license_path, destination / "LICENSE")

    executable_names = ("gbrecomp.exe", "gbrecomp")
    for name in executable_names:
        candidate = destination / name
        if candidate.is_file():
            candidate.chmod(candidate.stat().st_mode | 0o111)
            return candidate
    raise RuntimeError("distribution does not contain gbrecomp")


def main() -> int:
    parser = argparse.ArgumentParser()
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--distribution-dir", type=Path)
    source.add_argument("--gbrecomp", type=Path)
    parser.add_argument("--runtime-dir", type=Path)
    parser.add_argument("--fixture-generator", type=Path, required=True)
    parser.add_argument("--cmake-arg", action="append", default=[])
    args = parser.parse_args()

    if args.gbrecomp is not None and args.runtime_dir is None:
        parser.error("--runtime-dir is required with --gbrecomp")

    with tempfile.TemporaryDirectory(prefix="gbrecomp-release-smoke-") as tmp:
        root = Path(tmp)
        installed = root / "installed"
        recompiler = copy_distribution(
            installed,
            source_distribution=args.distribution_dir,
            gbrecomp=args.gbrecomp,
            runtime_dir=args.runtime_dir,
        )

        required = (
            installed / "runtime/include/gbrt.h",
            installed / "runtime/src/gbrt.c",
            installed / "runtime/vendor/imgui/LICENSE.txt",
            installed / "LICENSE",
        )
        missing = [str(path) for path in required if not path.is_file()]
        if missing:
            raise RuntimeError(f"distribution is missing required files: {missing}")

        rom = root / "release_smoke.gb"
        output = root / "generated"
        run(
            [
                sys.executable,
                str(args.fixture_generator.resolve()),
                "--mapper",
                "rom-only",
                "--output",
                str(rom),
            ],
            cwd=root,
        )
        run(
            [str(recompiler), str(rom), "--no-scan", "-o", str(output)],
            cwd=root,
        )

        generated_runtime = output / "runtime"
        if not (generated_runtime / "include/gbrt.h").is_file():
            raise RuntimeError("generated project did not embed the runtime snapshot")
        if (generated_runtime / "build").exists():
            raise RuntimeError("generated runtime snapshot contains build artifacts")

        build = output / "build"
        run(
            [
                "cmake",
                "-G",
                "Ninja",
                "-S",
                str(output),
                "-B",
                str(build),
                "-DGBRECOMP_ENABLE_STRIP=OFF",
                *args.cmake_arg,
            ],
            cwd=root,
        )
        run(["ninja", "-C", str(build)], cwd=root)

        executable_name = "release_smoke.exe" if os.name == "nt" else "release_smoke"
        executable = build / executable_name
        if not executable.is_file():
            matches = list(build.rglob(executable_name))
            if len(matches) != 1:
                raise RuntimeError(f"could not identify generated executable: {matches}")
            executable = matches[0]

        environment = os.environ.copy()
        environment.update(
            {
                "SDL_VIDEODRIVER": "dummy",
                "SDL_AUDIODRIVER": "dummy",
                "GBRECOMP_BENCHMARK": "1",
            }
        )
        environment["PATH"] = str(installed) + os.pathsep + environment.get("PATH", "")
        run(
            [str(executable), "--benchmark", "--limit-frames", "1"],
            cwd=root,
            env=environment,
        )

    print("relocated release generated, built, and ran a self-contained project")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
