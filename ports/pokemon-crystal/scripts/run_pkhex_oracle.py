#!/usr/bin/env python3
"""Build and run the pinned PKHeX.Core save oracle out of process."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
from pathlib import Path
from xml.sax.saxutils import escape


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--save", type=Path, required=True)
    parser.add_argument("--pkhex-dir", type=Path, required=True)
    parser.add_argument("--dotnet", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[3]
    save_path = args.save.resolve()
    pkhex_dir = args.pkhex_dir.resolve()
    dotnet = args.dotnet.resolve()
    output_dir = args.output_dir.resolve()
    source = root / "ports/pokemon-crystal/tools/pkhex_save_oracle.cs"
    core_project = pkhex_dir / "PKHeX.Core/PKHeX.Core.csproj"
    if (
        not dotnet.is_file()
        or not core_project.is_file()
        or save_path.stat().st_size != 0x8000
    ):
        raise ValueError("missing dotnet, pinned PKHeX.Core, or valid save")

    output_dir.mkdir(parents=True, exist_ok=True)
    project = output_dir / "PKHeXSaveOracle.csproj"
    project.write_text(
        f"""\
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>Exe</OutputType>
    <TargetFramework>net10.0</TargetFramework>
    <ImplicitUsings>disable</ImplicitUsings>
    <Nullable>enable</Nullable>
    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>
  </PropertyGroup>
  <ItemGroup>
    <Compile Include="{escape(str(source))}" />
    <ProjectReference Include="{escape(str(core_project))}" />
  </ItemGroup>
</Project>
""",
        encoding="utf-8",
    )
    build_dir = output_dir / "build"
    environment = {
        **os.environ,
        "DOTNET_CLI_TELEMETRY_OPTOUT": "1",
        "DOTNET_NOLOGO": "1",
    }
    subprocess.run(
        [
            str(dotnet),
            "build",
            str(project),
            "--configuration",
            "Release",
            "--output",
            str(build_dir),
            "--nologo",
        ],
        check=True,
        env=environment,
    )
    assembly = build_dir / "PKHeXSaveOracle.dll"
    completed = subprocess.run(
        [str(dotnet), str(assembly), str(save_path)],
        text=True,
        capture_output=True,
        check=False,
        env=environment,
    )
    if completed.returncode not in {0, 3}:
        raise RuntimeError(completed.stderr or "PKHeX oracle failed")
    decoded = json.loads(completed.stdout)
    result = {
        **decoded,
        "save_sha256": sha256(save_path),
        "pkhex_commit": subprocess.check_output(
            ["git", "-C", str(pkhex_dir), "rev-parse", "HEAD"],
            text=True,
        ).strip(),
        "pkhex_core_assembly_sha256": sha256(
            build_dir / "PKHeX.Core.dll"
        ),
        "oracle_assembly_sha256": sha256(assembly),
    }
    result_path = args.output_dir / "result.json"
    result_path.write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0 if result["accepted"] else 3


if __name__ == "__main__":
    raise SystemExit(main())
