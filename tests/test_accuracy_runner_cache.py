#!/usr/bin/env python3
"""Behavior tests for accuracy-runner cache invalidation."""

from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "gbrecomp_run_tests_cache", ROOT / "tools" / "run_tests.py"
)
assert SPEC and SPEC.loader
RUN_TESTS = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = RUN_TESTS
SPEC.loader.exec_module(RUN_TESTS)


class AccuracyRunnerCacheTests(unittest.TestCase):
    def test_cache_tracks_every_build_input(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            rom = root / "fixture.gb"
            gbrecomp = root / "gbrecomp"
            runtime = root / "runtime"
            output = root / "generated"
            runtime.mkdir()
            output.mkdir()

            rom.write_bytes(b"rom-v1")
            gbrecomp.write_bytes(b"recompiler-v1")
            (runtime / "gbrt.c").write_bytes(b"runtime-v1")

            fingerprint = RUN_TESTS.build_cache_fingerprint(
                rom, gbrecomp, runtime, model="dmg"
            )
            RUN_TESTS.write_cache_manifest(output, fingerprint)
            self.assertTrue(RUN_TESTS.cache_is_current(output, fingerprint))

            mutations = [
                (runtime / "gbrt.c", b"runtime-v2"),
                (rom, b"rom-v2"),
                (gbrecomp, b"recompiler-v2"),
            ]
            for changed_path, changed_content in mutations:
                with self.subTest(path=changed_path.name):
                    original = changed_path.read_bytes()
                    changed_path.write_bytes(changed_content)
                    changed = RUN_TESTS.build_cache_fingerprint(
                        rom, gbrecomp, runtime, model="dmg"
                    )
                    self.assertFalse(RUN_TESTS.cache_is_current(output, changed))
                    changed_path.write_bytes(original)

            other_model = RUN_TESTS.build_cache_fingerprint(
                rom, gbrecomp, runtime, model="cgb"
            )
            self.assertFalse(RUN_TESTS.cache_is_current(output, other_model))


if __name__ == "__main__":
    unittest.main()
