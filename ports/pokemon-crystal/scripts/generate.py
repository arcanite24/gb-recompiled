#!/usr/bin/env python3
"""Generate one provenance-locked Pokémon Crystal project."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
PORT_DIR = SCRIPT_DIR.parent
REPO_ROOT = PORT_DIR.parent.parent
OUTPUT_ROOT = REPO_ROOT / "output"
DEFAULT_ROM = REPO_ROOT / "roms" / "selected_gbc_top10" / "pokemon_crystal.gbc"
DEFAULT_GBRECOMP = (
    REPO_ROOT
    / "build"
    / "bin"
    / ("gbrecomp.exe" if os.name == "nt" else "gbrecomp")
)
DEFAULT_RUNTIME = REPO_ROOT / "runtime"
DEFAULT_SYMBOLS = (
    PORT_DIR
    / "references"
    / "cache"
    / "pokecrystal-symbols"
    / "pokecrystal11.sym"
)
DEFAULT_ENTRY_POINTS = PORT_DIR / "route" / "analysis-entry-points.json"
DEFAULT_ANNOTATIONS = PORT_DIR / "annotations" / "crystal-route.annotations"
DEFAULT_SEMANTIC_PACKAGE = PORT_DIR / "semantic" / "package.json"
DEFAULT_SEMANTIC_SCHEMA = PORT_DIR / "semantic" / "package-schema.json"
DEFAULT_PORT_MODULE = PORT_DIR / "module" / "port-module.json"
REFERENCE_LOCK = PORT_DIR / "references" / "sources.lock.json"
EXPECTED_ROM_SHA256 = (
    "fdcc3c8c43813cf8731fc037d2a6d191bac75439c34b24ba1c27526e6acdc8a2"
)
RECEIPT_NAME = "crystal-generation.json"
PROFILE_NAME = "crystal-build-profile.cmake"
SEMANTIC_HEADER_NAME = "crystal_semantic.h"
SEMANTIC_SOURCE_NAME = "crystal_semantic.c"
WIDESCREEN_PROBE = PORT_DIR / "tools" / "crystal_widescreen_probe.c"
BATTLE_PROBE = PORT_DIR / "tools" / "crystal_battle_probe.c"
PROBE_DISPATCH = PORT_DIR / "tools" / "crystal_probe_dispatch.c"
PRESENTATION_ASSETS = PORT_DIR / "assets" / "presentation"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def tree_sha256(root: Path, *, excluded_names: frozenset[str] = frozenset()) -> str:
    """Hash a directory by relative path, file size, and file content."""
    digest = hashlib.sha256()
    paths = (candidate for candidate in root.rglob("*") if candidate.is_file())
    for path in sorted(paths, key=lambda candidate: candidate.relative_to(root).as_posix()):
        relative = path.relative_to(root)
        if any(part in {".DS_Store", "__pycache__"} for part in relative.parts):
            continue
        if path.suffix == ".pyc" or relative.as_posix() in excluded_names:
            continue
        encoded = relative.as_posix().encode("utf-8")
        file_digest = bytes.fromhex(sha256_file(path))
        digest.update(len(encoded).to_bytes(4, "big"))
        digest.update(encoded)
        digest.update(path.stat().st_size.to_bytes(8, "big"))
        digest.update(file_digest)
    return digest.hexdigest()


def require_file(path: Path, label: str, *, executable: bool = False) -> Path:
    resolved = path.expanduser().resolve()
    if not resolved.is_file():
        raise RuntimeError(f"missing {label}: {resolved}")
    if executable and not os.access(resolved, os.X_OK):
        raise RuntimeError(f"{label} is not executable: {resolved}")
    return resolved


def require_runtime(path: Path) -> Path:
    resolved = path.expanduser().resolve()
    required = (
        resolved / "CMakeLists.txt",
        resolved / "include" / "gbrt.h",
        resolved / "include" / "gbrt_data_mod.h",
        resolved / "include" / "gbrt_hash.h",
        resolved / "include" / "gbrt_port.h",
        resolved / "include" / "gbrt_presentation.h",
        resolved / "include" / "gbrt_semantic.h",
        resolved / "src" / "gbrt.c",
        resolved / "src" / "gbrt_data_mod.c",
        resolved / "src" / "gbrt_hash.c",
        resolved / "src" / "gbrt_port.c",
        resolved / "src" / "gbrt_presentation.c",
        resolved / "src" / "gbrt_semantic.c",
        resolved / "vendor" / "imgui" / "imgui.cpp",
    )
    missing = [candidate for candidate in required if not candidate.is_file()]
    if missing:
        raise RuntimeError(f"incomplete runtime snapshot input: {missing[0]}")
    return resolved


def require_optional_file(value: str, label: str) -> Path | None:
    if value == "none":
        return None
    return require_file(Path(value), label)


def load_entry_points(path: Path) -> list[str]:
    entry_points_file = require_file(path, "entry-points file")
    try:
        payload = json.loads(entry_points_file.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        raise RuntimeError(f"invalid entry-points JSON: {entry_points_file}") from error
    if (
        not isinstance(payload, dict)
        or payload.get("schema")
        != "gbrecompiled.pokemon-crystal.analysis-entry-points"
        or payload.get("version") != 1
        or not isinstance(payload.get("entry_points"), list)
    ):
        raise RuntimeError("unsupported entry-points schema or version")
    entries = payload["entry_points"]
    if (
        any(
            not isinstance(entry, str)
            or re.fullmatch(r"(?:0|[1-9][0-9]*):[0-9a-f]{4}", entry) is None
            for entry in entries
        )
        or len(entries) != len(set(entries))
    ):
        raise RuntimeError("entry-points file contains invalid or duplicate entries")
    return sorted(entries)


def require_fresh_output(path: Path, *, private_cache: bool = False) -> Path:
    resolved = path.expanduser().resolve()
    if private_cache:
        try:
            resolved.relative_to(REPO_ROOT.resolve())
        except ValueError:
            pass
        else:
            raise RuntimeError("private-cache output must be outside the source tree")
        if resolved.name != "crystal-rev1-v1":
            raise RuntimeError(
                "private-cache output must end in the stable crystal-rev1-v1 name"
            )
        if resolved.exists():
            raise RuntimeError("private-cache output already exists")
        return resolved
    output_root = OUTPUT_ROOT.resolve()
    try:
        relative = resolved.relative_to(output_root)
    except ValueError as error:
        raise RuntimeError(
            f"output must be under {output_root}/pokemon-crystal-*"
        ) from error
    if len(relative.parts) != 1 or not relative.name.startswith("pokemon-crystal-"):
        raise RuntimeError(
            f"output must be a direct {output_root}/pokemon-crystal-* destination"
        )
    if resolved.exists():
        raise RuntimeError(f"output already exists; refusing hidden state: {resolved}")
    return resolved


def run_checked(command: list[str]) -> None:
    print("+ " + " ".join(command), flush=True)
    subprocess.run(command, cwd=REPO_ROOT, check=True)


def named_input(path: Path | None) -> dict[str, str]:
    if path is None:
        return {"kind": "none"}
    return {"kind": "file", "name": path.name, "sha256": sha256_file(path)}


def validate_port_extensions(manifests: list[Path]) -> dict:
    if not manifests:
        return {
            "schema": "gbrecompiled.port-extension-resolution",
            "version": 1,
            "passed": True,
            "extension_abi_version": 1,
            "extensions": [],
            "load_order": [],
        }
    with tempfile.TemporaryDirectory() as raw:
        output = Path(raw) / "extensions.json"
        command = [
            sys.executable,
            str(SCRIPT_DIR / "validate_port_extensions.py"),
            "--output",
            str(output),
        ]
        for manifest in manifests:
            command.extend(["--manifest", str(manifest)])
        run_checked(command)
        return json.loads(output.read_text(encoding="utf-8"))


def extension_receipt(resolution: dict) -> dict:
    return {
        "schema": resolution["schema"],
        "version": resolution["version"],
        "extension_abi_version": resolution["extension_abi_version"],
        "load_order": resolution["load_order"],
        "extensions": [
            {
                "id": extension["id"],
                "version": extension["version"],
                "priority": extension["priority"],
                "manifest_sha256": extension["manifest_sha256"],
                "entry_symbol": extension["entry_symbol"],
                "capabilities": extension["capabilities"],
                "sources": extension["sources"],
            }
            for extension in resolution["extensions"]
        ],
    }


def write_profile(
    path: Path,
    *,
    build_type: str,
    compile_jobs: int,
    opt_level: int,
    ipo: bool,
    strip: bool,
) -> None:
    content = (
        "# Generated by Crystal Recompiled scripts/generate.py.\n"
        f'set(CMAKE_BUILD_TYPE "{build_type}" CACHE STRING "" FORCE)\n'
        f'set(GBRECOMP_GENERATED_COMPILE_JOBS "{compile_jobs}" CACHE STRING "" FORCE)\n'
        f'set(GBRECOMP_GENERATED_OPT_LEVEL "{opt_level}" CACHE STRING "" FORCE)\n'
        f'set(GBRECOMP_ENABLE_IPO {"ON" if ipo else "OFF"} CACHE BOOL "" FORCE)\n'
        f'set(GBRECOMP_ENABLE_STRIP {"ON" if strip else "OFF"} CACHE BOOL "" FORCE)\n'
    )
    path.write_text(content, encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Verify all private inputs and generate a fresh, self-contained "
            "Pokémon Crystal UE Rev 1 project."
        )
    )
    parser.add_argument("--rom", type=Path, default=DEFAULT_ROM)
    parser.add_argument("--gbrecomp", type=Path, default=DEFAULT_GBRECOMP)
    parser.add_argument("--runtime", type=Path, default=DEFAULT_RUNTIME)
    parser.add_argument("--symbols", type=Path, default=DEFAULT_SYMBOLS)
    parser.add_argument(
        "--symbol-policy",
        choices=("names-only", "infer-boundaries"),
        default="names-only",
        help=(
            "whether imported symbols only name discovered addresses or also "
            "seed inferred analyzer boundaries"
        ),
    )
    parser.add_argument(
        "--annotations",
        default=str(DEFAULT_ANNOTATIONS),
        help="annotation file, or 'none' to disable reviewed Crystal boundaries",
    )
    parser.add_argument(
        "--native-patch",
        default="none",
        help="native-patch manifest, or 'none' (default)",
    )
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--private-cache-output",
        action="store_true",
        help="allow the stable output destination in a private cache outside the source tree",
    )
    parser.add_argument(
        "--progress-json",
        type=Path,
        help="privacy-safe JSON Lines destination forwarded to gbrecomp",
    )
    parser.add_argument(
        "--build-type",
        choices=("Debug", "Release", "RelWithDebInfo", "MinSizeRel"),
        default="Release",
    )
    parser.add_argument("--generated-compile-jobs", type=int, default=8)
    parser.add_argument("--generated-opt-level", type=int, choices=range(4), default=3)
    parser.add_argument("--ipo", choices=("on", "off"), default="off")
    parser.add_argument("--strip", choices=("on", "off"), default="off")
    parser.add_argument("--codegen-jobs", type=int, default=0)
    parser.add_argument(
        "--single-function",
        action="store_true",
        help="use the single-function code-layout perturbation",
    )
    parser.add_argument("--scan", choices=("on", "off"), default="on")
    parser.add_argument(
        "--analysis-scope",
        choices=("reachable", "all-banks"),
        default="all-banks",
        help=(
            "limit analysis to discovered and explicit entry points or seed "
            "heuristic entry points in every ROM bank"
        ),
    )
    parser.add_argument(
        "--entry-points",
        type=Path,
        default=DEFAULT_ENTRY_POINTS,
        help="locked analysis entry-points JSON",
    )
    parser.add_argument(
        "--semantic-package", type=Path, default=DEFAULT_SEMANTIC_PACKAGE
    )
    parser.add_argument(
        "--semantic-schema", type=Path, default=DEFAULT_SEMANTIC_SCHEMA
    )
    parser.add_argument(
        "--port-module",
        default=str(DEFAULT_PORT_MODULE),
        help="port-module manifest, or 'none' to build without the extension",
    )
    parser.add_argument(
        "--port-extension",
        type=Path,
        action="append",
        default=[],
        help="repeatable source-built port-extension manifest",
    )
    parser.add_argument(
        "--add-entry-point",
        action="append",
        default=[],
        metavar="BANK:ADDRESS",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.generated_compile_jobs < 0:
        raise RuntimeError("--generated-compile-jobs must be non-negative")
    if args.codegen_jobs < 0:
        raise RuntimeError("--codegen-jobs must be non-negative")

    rom = require_file(args.rom, "ROM")
    gbrecomp = require_file(args.gbrecomp, "GB Recompiled executable", executable=True)
    runtime = require_runtime(args.runtime)
    annotations = require_optional_file(args.annotations, "annotations")
    native_patch = require_optional_file(args.native_patch, "native-patch manifest")
    semantic_package = require_file(args.semantic_package, "semantic package")
    semantic_schema = require_file(args.semantic_schema, "semantic schema")
    port_module = require_optional_file(args.port_module, "port-module manifest")
    port_extensions = [
        require_file(path, "port-extension manifest")
        for path in args.port_extension
    ]
    if port_extensions and port_module is None:
        raise RuntimeError("port extensions require a source-built port module")
    run_checked(
        [
            sys.executable,
            str(SCRIPT_DIR / "validate_semantic_package.py"),
            "--manifest",
            str(semantic_package),
            "--schema",
            str(semantic_schema),
        ]
    )
    if port_module is not None:
        run_checked(
            [
                sys.executable,
                str(SCRIPT_DIR / "validate_port_module.py"),
                "--manifest",
                str(port_module),
            ]
        )
    extension_resolution = validate_port_extensions(port_extensions)
    output = require_fresh_output(
        args.output, private_cache=args.private_cache_output
    )

    run_checked([sys.executable, str(SCRIPT_DIR / "verify_rom.py"), str(rom)])
    if sha256_file(rom) != EXPECTED_ROM_SHA256:
        raise RuntimeError("ROM identity changed after verification")
    run_checked(
        [
            sys.executable,
            str(SCRIPT_DIR / "references.py"),
            "verify",
            "--scope",
            "generation",
        ]
    )
    symbols = require_file(args.symbols, "symbols")

    command = [
        str(gbrecomp),
        str(rom),
        "--runtime-dir",
        str(runtime),
        "--symbols",
        str(symbols),
        "--symbol-policy",
        args.symbol_policy,
        "--jobs",
        str(args.codegen_jobs),
        "--output",
        str(output),
        "--output-prefix",
        "pokemon_crystal",
    ]
    if args.progress_json is not None:
        command.extend(("--progress-json", str(args.progress_json.resolve())))
    if annotations is not None:
        command.extend(("--annotations", str(annotations)))
    if native_patch is not None:
        command.extend(("--native-patch", str(native_patch)))
    if args.scan == "off":
        command.append("--no-scan")
    if args.single_function:
        command.append("--single-function")
    if args.analysis_scope == "reachable":
        command.append("--reachable-only")
    entry_points_file = require_file(args.entry_points, "entry-points file")
    entry_points = sorted(
        set(load_entry_points(entry_points_file) + args.add_entry_point)
    )
    for entry_point in entry_points:
        command.extend(("--add-entry-point", entry_point))

    run_checked(command)
    metadata = require_file(output / "pokemon_crystal_metadata.json", "metadata")
    semantic_output = output / "semantic"
    semantic_output.mkdir()
    shutil.copyfile(semantic_package, semantic_output / "package.json")
    shutil.copyfile(semantic_schema, semantic_output / "package-schema.json")
    run_checked(
        [
            sys.executable,
            str(SCRIPT_DIR / "generate_semantic_accessors.py"),
            "--manifest",
            str(semantic_package),
            "--output-dir",
            str(semantic_output),
        ]
    )
    copied_runtime = require_runtime(output / "runtime")
    cmake_path = require_file(output / "CMakeLists.txt", "generated CMake project")
    presentation_probe: Path | None = None
    battle_presentation_probe: Path | None = None
    presentation_asset_manifest: Path | None = None
    with cmake_path.open("a", encoding="utf-8") as handle:
        handle.write(
            "\n# Exact-ROM Crystal semantic accessor module.\n"
            "target_sources(gbrt PRIVATE\n"
            "    ${CMAKE_CURRENT_SOURCE_DIR}/semantic/crystal_semantic.c\n"
            ")\n"
            "target_include_directories(gbrt PUBLIC\n"
            "    ${CMAKE_CURRENT_SOURCE_DIR}/semantic\n"
            ")\n"
        )
        if port_module is not None:
            port_output = output / "port"
            port_output.mkdir()
            port_payload = json.loads(port_module.read_text(encoding="utf-8"))
            shutil.copyfile(port_module, port_output / "port-module.json")
            compiled_sources: list[str] = []
            for source in port_payload["sources"]:
                source_name = source["path"]
                port_source = require_file(
                    port_module.parent / source_name,
                    "port-module source",
                )
                shutil.copyfile(port_source, port_output / source_name)
                if source_name.endswith(".c"):
                    compiled_sources.append(source_name)
            source_lines = "".join(
                f"    ${{CMAKE_CURRENT_SOURCE_DIR}}/port/{source_name}\n"
                for source_name in compiled_sources
            )
            handle.write(
                "\n# Exact-ROM Crystal native port module.\n"
                "target_sources(gbrt PRIVATE\n"
                f"{source_lines}"
                ")\n"
                "target_compile_definitions(gbrt PUBLIC "
                "GBRT_ENABLE_PORT_MODULE)\n"
            )
            probe_input = require_file(
                WIDESCREEN_PROBE, "widescreen presentation probe"
            )
            tools_output = port_output / "tools"
            tools_output.mkdir()
            presentation_probe = tools_output / probe_input.name
            shutil.copyfile(probe_input, presentation_probe)
            battle_probe_input = require_file(
                BATTLE_PROBE, "native battle presentation probe"
            )
            battle_presentation_probe = tools_output / battle_probe_input.name
            shutil.copyfile(battle_probe_input, battle_presentation_probe)
            probe_dispatch_input = require_file(
                PROBE_DISPATCH, "presentation probe dispatch fallback"
            )
            probe_dispatch = tools_output / probe_dispatch_input.name
            shutil.copyfile(probe_dispatch_input, probe_dispatch)
            assets_output = port_output / "assets" / "presentation"
            assets_output.mkdir(parents=True)
            for asset_name in (
                "manifest.json",
                "ui-panel-v1.json",
                "battle-aura-v1.json",
            ):
                asset_input = require_file(
                    PRESENTATION_ASSETS / asset_name,
                    "native presentation asset",
                )
                shutil.copyfile(asset_input, assets_output / asset_name)
            presentation_asset_manifest = assets_output / "manifest.json"
            handle.write(
                "\n# Headless renderer-neutral widescreen proof tool.\n"
                "add_executable(crystal_widescreen_probe\n"
                "    ${CMAKE_CURRENT_SOURCE_DIR}/port/tools/"
                "crystal_widescreen_probe.c\n"
                "    ${CMAKE_CURRENT_SOURCE_DIR}/port/tools/"
                "crystal_probe_dispatch.c\n"
                ")\n"
                "target_include_directories(crystal_widescreen_probe PRIVATE\n"
                "    ${CMAKE_CURRENT_SOURCE_DIR}/port\n"
                "    ${CMAKE_CURRENT_SOURCE_DIR}/semantic\n"
                ")\n"
                "target_link_libraries(crystal_widescreen_probe PRIVATE gbrt)\n"
                "\nadd_executable(crystal_battle_probe\n"
                "    ${CMAKE_CURRENT_SOURCE_DIR}/port/tools/"
                "crystal_battle_probe.c\n"
                "    ${CMAKE_CURRENT_SOURCE_DIR}/port/tools/"
                "crystal_probe_dispatch.c\n"
                ")\n"
                "target_include_directories(crystal_battle_probe PRIVATE\n"
                "    ${CMAKE_CURRENT_SOURCE_DIR}/port\n"
                "    ${CMAKE_CURRENT_SOURCE_DIR}/semantic\n"
                ")\n"
                "target_link_libraries(crystal_battle_probe PRIVATE gbrt)\n"
            )
        if extension_resolution["extensions"]:
            extension_output = output / "port" / "extensions"
            extension_output.mkdir()
            compiled_sources: list[str] = []
            declarations: list[str] = []
            registrations: list[str] = []
            for index, extension in enumerate(
                extension_resolution["extensions"]
            ):
                package_output = extension_output / f"{index:03d}"
                package_output.mkdir()
                manifest_path = Path(extension["manifest"])
                shutil.copyfile(manifest_path, package_output / "manifest.json")
                for source in extension["sources"]:
                    source_name = source["path"]
                    source_path = require_file(
                        manifest_path.parent / source_name,
                        "port-extension source",
                    )
                    shutil.copyfile(source_path, package_output / source_name)
                    if source_name.endswith(".c"):
                        compiled_sources.append(
                            f"port/extensions/{index:03d}/{source_name}"
                        )
                declarations.append(
                    "extern const GBPortExtension* "
                    f"{extension['entry_symbol']}(void);"
                )
                registrations.append(
                    "    {"
                    f".get = {extension['entry_symbol']}, "
                    f".expected_id = \"{extension['id']}\", "
                    f".expected_version = {int(extension['version'].split('.')[0])}u, "
                    f".expected_priority = {extension['priority']}u"
                    "},"
                )
            registry = extension_output / "registry.c"
            registry.write_text(
                '#include "gbrt_port.h"\n\n'
                + "\n".join(declarations)
                + "\n\nstatic const GBPortExtensionRegistration registrations[] = {\n"
                + "\n".join(registrations)
                + "\n};\n\n"
                + "static const GBPortExtensionSet extension_set = {\n"
                + "    .abi_version = GB_PORT_EXTENSION_ABI_VERSION,\n"
                + "    .count = sizeof(registrations) / sizeof(registrations[0]),\n"
                + "    .registrations = registrations,\n"
                + "};\n\n"
                + "const GBPortExtensionSet* gb_port_extension_set_get(void) {\n"
                + "    return &extension_set;\n"
                + "}\n",
                encoding="utf-8",
            )
            compiled_sources.append("port/extensions/registry.c")
            source_lines = "".join(
                f"    ${{CMAKE_CURRENT_SOURCE_DIR}}/{source}\n"
                for source in compiled_sources
            )
            handle.write(
                "\n# Deterministically composed exact-ROM port extensions.\n"
                "target_sources(gbrt PRIVATE\n"
                f"{source_lines}"
                ")\n"
                "target_compile_definitions(gbrt PUBLIC "
                "GBRT_ENABLE_PORT_EXTENSIONS)\n"
            )
    profile = output / PROFILE_NAME
    write_profile(
        profile,
        build_type=args.build_type,
        compile_jobs=args.generated_compile_jobs,
        opt_level=args.generated_opt_level,
        ipo=args.ipo == "on",
        strip=args.strip == "on",
    )

    receipt_path = output / RECEIPT_NAME
    receipt = {
        "schema": "crystal-recompiled.generation",
        "version": 1,
        "rom": {
            "name": "pokemon_crystal.gbc",
            "size": rom.stat().st_size,
            "sha256": sha256_file(rom),
        },
        "recompiler": {
            "name": "gbrecomp",
            "sha256": sha256_file(gbrecomp),
        },
        "runtime": {
            "source_tree_sha256": tree_sha256(runtime),
            "snapshot_tree_sha256": tree_sha256(copied_runtime),
        },
        "references": {
            "lock_sha256": sha256_file(REFERENCE_LOCK),
            "symbols": named_input(symbols),
            "annotations": named_input(annotations),
        },
        "native_patch": named_input(native_patch),
        "semantic": {
            "package": named_input(semantic_package),
            "schema": named_input(semantic_schema),
            "accessor_header": named_input(
                semantic_output / SEMANTIC_HEADER_NAME
            ),
            "accessor_source": named_input(
                semantic_output / SEMANTIC_SOURCE_NAME
            ),
        },
        "port_module": named_input(port_module),
        "presentation_probe": named_input(presentation_probe),
        "battle_presentation_probe": named_input(
            battle_presentation_probe
        ),
        "presentation_assets": named_input(presentation_asset_manifest),
        "port_extensions": extension_receipt(extension_resolution),
        "analysis": {
            "scan": args.scan == "on",
            "scope": args.analysis_scope,
            "symbol_policy": args.symbol_policy,
            "entry_points": entry_points,
            "entry_points_sha256": sha256_file(entry_points_file),
        },
        "codegen": {
            "jobs": args.codegen_jobs,
            "single_function": args.single_function,
        },
        "build_profile": {
            "build_type": args.build_type,
            "generated_compile_jobs": args.generated_compile_jobs,
            "generated_opt_level": args.generated_opt_level,
            "ipo": args.ipo == "on",
            "strip": args.strip == "on",
        },
        "generated": {
            "metadata_sha256": sha256_file(metadata),
            "source_inventory_sha256": tree_sha256(
                output, excluded_names=frozenset({RECEIPT_NAME})
            ),
        },
    }
    receipt_path.write_text(
        json.dumps(receipt, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(f"ok  generated provenance-locked project: {output}")
    print(f"    receipt={receipt_path.name} sha256={sha256_file(receipt_path)}")
    print(f"    metadata_sha256={receipt['generated']['metadata_sha256']}")
    print(
        "    source_inventory_sha256="
        f"{receipt['generated']['source_inventory_sha256']}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
