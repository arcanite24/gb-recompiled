#!/usr/bin/env python3

from __future__ import annotations

import copy
import importlib.util
import json
import tempfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
SCRIPT = (
    REPO_ROOT
    / "ports"
    / "pokemon-crystal"
    / "scripts"
    / "validate_semantic_anchors.py"
)
SCHEMA = (
    REPO_ROOT
    / "ports"
    / "pokemon-crystal"
    / "semantic"
    / "anchor-schema.json"
)
MANIFEST = (
    REPO_ROOT
    / "ports"
    / "pokemon-crystal"
    / "semantic"
    / "anchors.json"
)


def load_module():
    spec = importlib.util.spec_from_file_location(
        "validate_semantic_anchors", SCRIPT
    )
    if spec is None or spec.loader is None:
        raise RuntimeError("could not load semantic-anchor validator")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def expect_rejected(module, payload: dict, expected: str) -> None:
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "anchors.json"
        path.write_text(json.dumps(payload), encoding="utf-8")
        try:
            module.validate(path, SCHEMA)
        except RuntimeError as error:
            if expected not in str(error):
                raise AssertionError(
                    f"expected {expected!r}, received {error!r}"
                ) from error
        else:
            raise AssertionError(f"invalid manifest passed: {expected}")


def main() -> int:
    module = load_module()
    valid = json.loads(MANIFEST.read_text(encoding="utf-8"))
    result = module.validate(MANIFEST, SCHEMA)
    assert result["anchor_count"] == 18
    assert result["address_anchor_count"] == 13
    assert result["rtc_anchor_count"] == 5

    impossible = copy.deepcopy(valid)
    impossible["anchors"][0]["address"] = "0xcfff"
    expect_rejected(module, impossible, "impossible banked_wram range")

    overlap = copy.deepcopy(valid)
    overlap["anchors"][1]["address"] = overlap["anchors"][0]["address"]
    expect_rejected(module, overlap, "semantic anchors overlap")

    wrong_rtc = copy.deepcopy(valid)
    wrong_rtc["anchors"][-1]["selector"] = 8
    expect_rejected(module, wrong_rtc, "wrong MBC3 RTC selector")

    wrong_rom = copy.deepcopy(valid)
    wrong_rom["rom_sha256"] = "0" * 64
    expect_rejected(module, wrong_rom, "unsupported ROM")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
