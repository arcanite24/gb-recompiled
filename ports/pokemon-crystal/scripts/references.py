#!/usr/bin/env python3
"""Fetch or verify ignored, commit-pinned Crystal reference checkouts."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
import urllib.error
import urllib.request
from pathlib import Path


PORT_DIR = Path(__file__).resolve().parent.parent
LOCK_PATH = PORT_DIR / "references" / "sources.lock.json"
VENDOR_DIR = PORT_DIR / "references" / "vendor"
CACHE_DIR = PORT_DIR / "references" / "cache"
SAFE_NAME = re.compile(r"^[a-z0-9][a-z0-9-]*$")
GENERATION_SOURCES = {"pokecrystal-symbols"}


def run(*args: str, cwd: Path | None = None, capture: bool = False) -> str:
    result = subprocess.run(
        args,
        cwd=cwd,
        check=True,
        text=True,
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.PIPE if capture else None,
    )
    return result.stdout.strip() if capture else ""


def load_sources() -> list[dict[str, object]]:
    payload = json.loads(LOCK_PATH.read_text(encoding="utf-8"))
    if payload.get("schema") != "crystal-recompiled.references" or payload.get("version") != 1:
        raise RuntimeError(f"Unsupported source lock: {LOCK_PATH}")
    sources = payload.get("sources")
    if not isinstance(sources, list):
        raise RuntimeError(f"Malformed source list: {LOCK_PATH}")
    return sources


def source_path(source: dict[str, object]) -> Path:
    name = source.get("name")
    if not isinstance(name, str) or not SAFE_NAME.fullmatch(name):
        raise RuntimeError(f"Unsafe source name: {name!r}")
    return VENDOR_DIR / name


def generation_source_path(source: dict[str, object], relative: str) -> Path:
    name = source.get("name")
    if not isinstance(name, str) or not SAFE_NAME.fullmatch(name):
        raise RuntimeError(f"Unsafe source name: {name!r}")
    return CACHE_DIR / name / relative


def generation_url(source: dict[str, object], relative: str) -> str:
    url = str(source["url"])
    prefix = "https://github.com/"
    if not url.startswith(prefix) or not url.endswith(".git"):
        raise RuntimeError(f"unsupported generation reference URL: {url}")
    repository = url[len(prefix) : -len(".git")]
    if ".." in Path(relative).parts or relative.startswith("/"):
        raise RuntimeError(f"unsafe generation reference path: {relative}")
    return (
        f"https://raw.githubusercontent.com/{repository}/"
        f"{source['commit']}/{relative}"
    )


def fetch_generation_source(source: dict[str, object]) -> None:
    required_files = source.get("required_files", [])
    expected_hashes = source.get("sha256", {})
    if (
        not isinstance(required_files, list)
        or not isinstance(expected_hashes, dict)
        or set(required_files) != set(expected_hashes)
    ):
        raise RuntimeError("generation reference requires hashes for every file")
    for relative in required_files:
        if not isinstance(relative, str):
            raise RuntimeError("generation reference path is malformed")
        target = generation_source_path(source, relative)
        target.parent.mkdir(parents=True, exist_ok=True)
        try:
            with urllib.request.urlopen(
                generation_url(source, relative),
                timeout=120,
            ) as response:
                content = response.read()
        except (urllib.error.URLError, TimeoutError) as error:
            raise RuntimeError(
                f"cannot fetch generation reference: {relative}"
            ) from error
        actual = hashlib.sha256(content).hexdigest()
        expected = str(expected_hashes[relative])
        if actual != expected:
            raise RuntimeError(
                f"SHA-256 mismatch for generation reference {relative}: "
                f"{actual} != {expected}"
            )
        target.write_bytes(content)


def verify_generation_source(source: dict[str, object]) -> None:
    expected_hashes = source.get("sha256", {})
    if not isinstance(expected_hashes, dict) or not expected_hashes:
        raise RuntimeError("generation reference has no checked files")
    for relative, expected in expected_hashes.items():
        path = generation_source_path(source, str(relative))
        if not path.is_file() or sha256(path) != expected:
            raise RuntimeError(
                f"missing or mismatched generation reference: {relative}"
            )
    print(f"ok  {str(source['name']):24} {source['commit']} (raw locked files)")


def require_clean_checkout(target: Path) -> None:
    dirty = run("git", "status", "--porcelain", cwd=target, capture=True)
    if dirty:
        raise RuntimeError(
            f"Reference checkout has local changes; refusing to move it: {target}"
        )


def fetch_source(source: dict[str, object]) -> None:
    target = source_path(source)
    url = str(source["url"])
    commit = str(source["commit"])

    if target.exists():
        if not (target / ".git").is_dir():
            raise RuntimeError(f"Reference target exists but is not a Git checkout: {target}")
        remote = run("git", "remote", "get-url", "origin", cwd=target, capture=True)
        if remote != url:
            raise RuntimeError(f"Unexpected origin for {target}: {remote}")
        require_clean_checkout(target)
    else:
        run(
            "git",
            "clone",
            "--filter=blob:none",
            "--no-checkout",
            url,
            str(target),
        )

    sparse_paths = source.get("sparse_paths", [])
    if sparse_paths:
        if not isinstance(sparse_paths, list) or not all(
            isinstance(path, str) and path for path in sparse_paths
        ):
            raise RuntimeError(f"Malformed sparse path list for {target.name}")
        run("git", "sparse-checkout", "init", "--cone", cwd=target)
        run("git", "sparse-checkout", "set", *sparse_paths, cwd=target)

    run("git", "fetch", "--depth", "1", "origin", commit, cwd=target)
    run("git", "checkout", "--detach", commit, cwd=target)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def verify_source(source: dict[str, object]) -> None:
    target = source_path(source)
    if not (target / ".git").is_dir():
        raise RuntimeError(f"Missing reference checkout: {target}")

    actual_commit = run("git", "rev-parse", "HEAD", cwd=target, capture=True)
    expected_commit = str(source["commit"])
    if actual_commit != expected_commit:
        raise RuntimeError(
            f"Commit mismatch for {target.name}: {actual_commit} != {expected_commit}"
        )

    required_files = source.get("required_files", [])
    if not isinstance(required_files, list):
        raise RuntimeError(f"Malformed required-files list for {target.name}")
    for relative in required_files:
        if not isinstance(relative, str) or not (target / relative).is_file():
            raise RuntimeError(f"Missing required file for {target.name}: {relative}")

    expected_hashes = source.get("sha256", {})
    if not isinstance(expected_hashes, dict):
        raise RuntimeError(f"Malformed hash table for {target.name}")
    for relative, expected in expected_hashes.items():
        path = target / str(relative)
        actual = sha256(path)
        if actual != expected:
            raise RuntimeError(
                f"SHA-256 mismatch for {target.name}/{relative}: {actual} != {expected}"
            )

    print(f"ok  {target.name:24} {actual_commit}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("fetch", "verify"))
    parser.add_argument(
        "--scope",
        choices=("generation", "all"),
        default="all",
        help="fetch/verify only generation-critical references or every oracle",
    )
    args = parser.parse_args()

    sources = load_sources()
    if args.scope == "generation":
        sources = [
            source
            for source in sources
            if source.get("name") in GENERATION_SOURCES
        ]
        if {source.get("name") for source in sources} != GENERATION_SOURCES:
            raise RuntimeError("generation reference scope is incomplete")
    if args.scope == "generation":
        CACHE_DIR.mkdir(parents=True, exist_ok=True)
        if args.command == "fetch":
            for source in sources:
                fetch_generation_source(source)
        for source in sources:
            verify_generation_source(source)
        return 0

    VENDOR_DIR.mkdir(parents=True, exist_ok=True)
    if args.command == "fetch":
        for source in sources:
            fetch_source(source)
    for source in sources:
        verify_source(source)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (
        KeyError,
        OSError,
        RuntimeError,
        subprocess.CalledProcessError,
    ) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
