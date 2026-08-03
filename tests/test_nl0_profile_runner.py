#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import os
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "tools" / "run_nl0_profile.py"
sys.path.insert(0, str(MODULE_PATH.parent))
SPEC = importlib.util.spec_from_file_location("run_nl0_profile", MODULE_PATH)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


def main() -> int:
    MODULE.psutil = None
    assert MODULE.total_rss_bytes(os.getpid()) > 0
    assert MODULE.command_version(sys.executable, "--version").startswith("Python ")
    MODULE.validate_cycle_input("c100:S:20,c500:A+R:40")
    for invalid in ("", "10:S:2", "c10:S:0", "c10:S"):
        try:
            MODULE.validate_cycle_input(invalid)
        except ValueError:
            continue
        raise AssertionError(f"invalid cycle input was accepted: {invalid!r}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
