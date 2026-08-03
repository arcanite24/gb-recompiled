#!/usr/bin/env python3
"""Exercise Crystal's native PC UI through original load/save boundaries."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
from pathlib import Path

from verify_writable_saves import (
    BACKUP,
    PRIMARY,
    checksum_valid,
    cycle_input,
    decode_active_box,
    decode_party,
    load_json,
    sha256,
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--save", type=Path, required=True)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--accessor-dir", type=Path, required=True)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--dotnet", type=Path, required=True)
    parser.add_argument("--pkhex-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[3]
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    compiler = os.environ.get("CC") or shutil.which("cc")
    if compiler is None:
        raise RuntimeError("no C compiler found")

    probe = output / "writable-save-fixture-probe"
    subprocess.run(
        [
            compiler,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(root / "runtime/include"),
            "-I",
            str(args.accessor_dir.resolve()),
            str(root / "runtime/src/gbrt_semantic.c"),
            str(args.accessor_dir.resolve() / "crystal_semantic.c"),
            str(
                root
                / "ports/pokemon-crystal/tools/"
                "writable_save_fixture_probe.c"
            ),
            "-o",
            str(probe),
        ],
        check=True,
    )
    seed = output / "seed-active-box.sav"
    subprocess.run(
        [
            str(probe),
            str(args.save.resolve()),
            str(args.rom.resolve()),
            "active-box-add",
            str(seed),
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    rendered_input = cycle_input(
        root / "ports/pokemon-crystal/route/inputs/restart-continue.json"
    )

    def run(
        scenario: str,
        source: Path,
        events: list[tuple[int, str]],
        expect_transaction: bool,
    ) -> dict:
        directory = output / scenario
        persistence = directory / "persistence"
        persistence.mkdir(parents=True, exist_ok=True)
        durable = persistence / "pokemon_crystal.sav"
        shutil.copy2(source, durable)
        state = directory / "state.json"
        port_state = directory / "port-state.json"
        runtime_log = directory / "runtime.log"
        command = [
            str(args.executable.resolve()),
            "--headless",
            "--no-audio",
            "--limit-frames",
            "3500",
            "--input",
            rendered_input,
            "--save-dir",
            str(persistence),
            "--dump-state",
            str(state),
            "--port-state",
            str(port_state),
            "--log-file",
            str(runtime_log),
            "--rtc-unix-time",
            "1700000000",
            "--ignore-rtc-persistence",
            "--log-frame-fallbacks",
            "--report-interpreter-hotspots",
        ]
        for frame, action in events:
            command.extend(
                ["--port-input-frame", f"{frame}:{action}"]
            )
        subprocess.run(command, check=True)
        state_data = load_json(state)
        port_data = load_json(port_state)
        log = runtime_log.read_text(encoding="utf-8")
        transaction = state_data.get("semantic_transaction")
        if (
            state_data.get("dispatch_fallbacks") != 0
            or "[INTERP] No interpreter fallback recorded." not in log
            or port_data.get("module_version") != 8
            or (
                expect_transaction
                and (
                    not isinstance(transaction, dict)
                    or transaction.get("outcome") != "committed"
                )
            )
            or (
                not expect_transaction
                and (
                    not isinstance(transaction, dict)
                    or transaction.get("sequence") != 0
                    or transaction.get("outcome") != "none"
                )
            )
        ):
            raise AssertionError(f"{scenario}: runtime gate failed")
        data = durable.read_bytes()
        if not checksum_valid(data, PRIMARY) or not checksum_valid(data, BACKUP):
            raise AssertionError(f"{scenario}: invalid primary/backup save")
        _, box = decode_active_box(data, 0)
        return {
            "directory": directory,
            "save": durable,
            "save_sha256": sha256(durable),
            "party": decode_party(data, 0),
            "box": box,
            "state_sha256": sha256(state),
            "port_state_sha256": sha256(port_state),
            "log": log,
            "port": port_data,
            "transaction": transaction,
        }

    cancel = run(
        "cancel",
        seed,
        [(2000, "toggle"), (2010, "right"), (2020, "accept"), (2030, "back")],
        False,
    )
    if (
        cancel["save_sha256"] != sha256(seed)
        or "PC edit canceled - no write" not in cancel["log"]
    ):
        raise AssertionError("cancel changed the save")

    withdraw = run(
        "withdraw",
        seed,
        [(2000, "toggle"), (2010, "right"), (2020, "accept"), (2030, "accept")],
        True,
    )
    if len(withdraw["party"]) != 2 or withdraw["box"]:
        raise AssertionError("withdraw did not move box slot into party")

    deposit = run(
        "deposit",
        withdraw["save"],
        [(2000, "toggle"), (2020, "accept"), (2030, "accept")],
        True,
    )
    if len(deposit["party"]) != 1 or len(deposit["box"]) != 1:
        raise AssertionError("deposit did not move party slot into box")

    search = run(
        "search",
        seed,
        [(2000, "toggle"), (2010, "right"), (2020, "right"), (2030, "accept")],
        False,
    )
    if (
        search["save_sha256"] != sha256(seed)
        or "PC search advanced to matching box" not in search["log"]
    ):
        raise AssertionError("search did not find the boxed species")

    sort = run(
        "sort",
        seed,
        [
            (2000, "toggle"),
            (2010, "right"),
            (2020, "right"),
            (2030, "right"),
            (2040, "accept"),
            (2050, "accept"),
        ],
        True,
    )
    if len(sort["box"]) != 1:
        raise AssertionError("sort lost the active-box record")

    reload_deposit = run(
        "reload-deposit",
        deposit["save"],
        [(2000, "toggle")],
        False,
    )
    if (
        len(reload_deposit["party"]) != 1
        or len(reload_deposit["box"]) != 1
    ):
        raise AssertionError("original Continue path rejected deposited save")

    for scenario in (withdraw, deposit, sort):
        oracle_dir = scenario["directory"] / "pkhex"
        subprocess.run(
            [
                "python3",
                str(
                    root
                    / "ports/pokemon-crystal/scripts/"
                    "run_pkhex_oracle.py"
                ),
                "--save",
                str(scenario["save"]),
                "--pkhex-dir",
                str(args.pkhex_dir.resolve()),
                "--dotnet",
                str(args.dotnet.resolve()),
                "--output-dir",
                str(oracle_dir),
            ],
            check=True,
            capture_output=True,
        )
        oracle = load_json(oracle_dir / "result.json")
        if oracle.get("accepted") is not True:
            raise AssertionError("PKHeX rejected native PC output")
        scenario["pkhex_result_sha256"] = sha256(
            oracle_dir / "result.json"
        )

    original = decode_party(seed.read_bytes(), 0)[0]
    for moved in (withdraw["party"][1], deposit["box"][0]):
        for field in (
            "species",
            "nickname",
            "original_trainer",
            "level",
            "held_item",
            "moves",
        ):
            if moved[field] != original[field]:
                raise AssertionError(
                    f"movement changed {field}: {moved[field]!r}"
                )

    subprocess.run(
        ["python3", str(root / "tests/test_crystal_pc_model.py")],
        check=True,
    )
    result = {
        "schema": "crystal-recompiled.native-pc-verification",
        "version": 1,
        "passed": True,
        "rom_sha256": sha256(args.rom.resolve()),
        "base_save_sha256": sha256(args.save.resolve()),
        "seed_save_sha256": sha256(seed),
        "executable_sha256": sha256(args.executable.resolve()),
        "accessor_source_sha256": sha256(
            args.accessor_dir.resolve() / "crystal_semantic.c"
        ),
        "portable_edit_matrix": True,
        "cancel_byte_identical": True,
        "original_reload_passed": True,
        "primary_backup_valid": True,
        "records_preserved": [
            "species",
            "nickname",
            "original_trainer",
            "level",
            "held_item",
            "moves",
        ],
        "scenarios": {
            name: {
                key: value
                for key, value in scenario.items()
                if key
                in {
                    "save_sha256",
                    "state_sha256",
                    "port_state_sha256",
                    "pkhex_result_sha256",
                    "transaction",
                }
            }
            for name, scenario in {
                "cancel": cancel,
                "withdraw": withdraw,
                "deposit": deposit,
                "search": search,
                "sort": sort,
                "reload_deposit": reload_deposit,
            }.items()
        },
    }
    result_path = output / "result.json"
    result_path.write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (
        OSError,
        ValueError,
        KeyError,
        TypeError,
        subprocess.CalledProcessError,
    ) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
