#!/usr/bin/env python3
"""Behavior tests for the accuracy runner's process-exit contract."""

from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "gbrecomp_run_tests", ROOT / "tools" / "run_tests.py"
)
assert SPEC and SPEC.loader
RUN_TESTS = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = RUN_TESTS
SPEC.loader.exec_module(RUN_TESTS)


class AccuracyRunnerExitPolicyTests(unittest.TestCase):
    def test_only_an_all_pass_nonempty_run_succeeds(self) -> None:
        self.assertEqual(0, RUN_TESTS.suite_exit_code([{"status": "pass"}]))
        self.assertEqual(
            0,
            RUN_TESTS.suite_exit_code(
                [{"status": "pass"}, {"status": "pass"}]
            ),
        )

        rejected_statuses = [
            "fail",
            "incomplete",
            "compile_error",
            "compile_timeout",
            "cmake_error",
            "build_error",
            "build_timeout",
            "no_executable",
            "run_timeout",
            "runtime_error",
            "missing_state",
            "unknown",
        ]
        self.assertEqual(1, RUN_TESTS.suite_exit_code([]))
        for status in rejected_statuses:
            with self.subTest(status=status):
                self.assertEqual(
                    1,
                    RUN_TESTS.suite_exit_code(
                        [{"status": "pass"}, {"status": status}]
                    ),
                )

    def test_mooneye_uses_the_register_protocol(self) -> None:
        passing = {"b": 3, "c": 5, "d": 8, "e": 13, "h": 21, "l": 34}
        self.assertTrue(RUN_TESTS.mooneye_state_passes(passing))
        self.assertFalse(RUN_TESTS.mooneye_state_passes({**passing, "l": 33}))

    def test_filter_matches_catalogue_paths_as_well_as_names(self) -> None:
        ppu_tests = RUN_TESTS.build_test_list("ppu")
        self.assertGreater(len(ppu_tests), 0)
        self.assertTrue(
            all("/ppu/" in rom.as_posix() for _, rom, _, _, _ in ppu_tests)
        )

    def test_dmg_only_blargg_oam_bug_is_forced_to_dmg(self) -> None:
        tests = RUN_TESTS.build_test_list("oam_bug")
        self.assertEqual(1, len(tests))
        self.assertEqual("dmg", tests[0][4])

    def test_blargg_memory_protocol_is_fail_closed(self) -> None:
        prefix = [0x80, 0xDE, 0xB0, 0x61]
        self.assertIsNone(RUN_TESTS.blargg_memory_verdict({}))
        self.assertIsNone(
            RUN_TESTS.blargg_memory_verdict(
                {"eram_a000_a0ff": [0x00, 0xDE, 0xB0, 0x60]}
            )
        )
        self.assertEqual(
            "incomplete",
            RUN_TESTS.blargg_memory_verdict(
                {"eram_a000_a0ff": prefix}
            ),
        )
        self.assertEqual(
            "pass",
            RUN_TESTS.blargg_memory_verdict(
                {"eram_a000_a0ff": [0x00, 0xDE, 0xB0, 0x61]}
            ),
        )
        self.assertEqual(
            "fail",
            RUN_TESTS.blargg_memory_verdict(
                {"eram_a000_a0ff": [0x05, 0xDE, 0xB0, 0x61]}
            ),
        )

    def test_rendered_blargg_verdict_is_pinned_to_a_stable_frame(self) -> None:
        expected = {
            "halt_bug": (299, "28BBA01F"),
            "interrupt_time": (100, "D17F2340"),
            "mem_timing-2": (299, "9E0E8400"),
        }
        for name, verdict in expected.items():
            with self.subTest(name=name):
                self.assertEqual(
                    verdict,
                    RUN_TESTS.BLARGG_RENDERED_VERDICTS[name],
                )


if __name__ == "__main__":
    unittest.main()
