#!/usr/bin/env python3

from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VERIFIER = (
    ROOT
    / "ports"
    / "pokemon-crystal"
    / "scripts"
    / "verify_determinism.py"
)


def write_json(path: Path, payload: object) -> None:
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="crystal-determinism-") as raw_temp:
        temp = Path(raw_temp)
        manifest = temp / "manifest.json"
        receipt = temp / "generation.json"
        executable = temp / "crystal"
        validator = temp / "fake-route-validator"
        write_json(manifest, {"schema": "route-fixture"})
        write_json(
            receipt,
            {
                "schema": "crystal-recompiled.generation",
                "version": 1,
                "rom": {"sha256": "0" * 64},
                "recompiler": {"sha256": "8" * 64},
                "generated": {
                    "metadata_sha256": "1" * 64,
                    "source_inventory_sha256": "2" * 64,
                },
                "runtime": {
                    "snapshot_tree_sha256": "3" * 64,
                    "source_tree_sha256": "4" * 64,
                },
                "build_profile": {
                    "build_type": "Release",
                    "generated_opt_level": 3,
                    "ipo": False,
                },
                "native_patch": {"kind": "none"},
            },
        )
        executable.write_bytes(b"executable-fixture")
        validator.write_text(
            """#!/usr/bin/env python3
import hashlib
import json
import os
import sys
from pathlib import Path

args = sys.argv[1:]
def value(flag):
    return args[args.index(flag) + 1]
def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

evidence = Path(value("--evidence-dir"))
evidence.mkdir(parents=True)
run_number = int(evidence.name.rsplit("-", 1)[1])
persistence = evidence / "persistence"
persistence.mkdir()
(persistence / "pokemon_crystal.sav").write_bytes(b"stable-save")
result = {
    "schema": "gbrecompiled.pokemon-crystal.route-result",
    "version": 1,
    "passed": True,
    "manifest_sha256": sha256(value("--manifest")),
    "executable_sha256": sha256(value("--executable")),
    "generation_receipt_sha256": sha256(value("--generation-receipt")),
    "segments": [{
        "id": "route",
        "passed": True,
        "input": "inputs/route.json",
        "input_sha256": "5" * 64,
        "command": [
            value("--executable"),
            "--headless",
            "--limit-frames", "10",
            "--debug-audio",
            "--debug-audio-seconds", value("--pcm-seconds"),
        ],
        "checkpoints": [{
            "id": "selected",
            "frame": 5,
            "frame_sha256": "6" * 64,
            "passed": True,
        }],
        "final_state": [{
            "path": "total_cycles",
            "expected": 1234,
            "actual": 9999 if os.environ.get("MISMATCH_RUN") == str(run_number) else 1234,
            "passed": True,
        }],
        "pcm": {
            "bytes": 16,
            "seconds_limit": int(value("--pcm-seconds")),
            "sha256": "7" * 64,
        },
    }],
}
write = evidence / "result.json"
write.write_text(json.dumps(result, indent=2) + "\\n", encoding="utf-8")
""",
            encoding="utf-8",
        )
        validator.chmod(0o755)

        evidence = temp / "evidence"
        completed = subprocess.run(
            [
                sys.executable,
                str(VERIFIER),
                "--route-validator",
                str(validator),
                "--manifest",
                str(manifest),
                "--executable",
                str(executable),
                "--generation-receipt",
                str(receipt),
                "--evidence-dir",
                str(evidence),
                "--runs",
                "3",
                "--pcm-seconds",
                "1",
            ],
            text=True,
            capture_output=True,
            check=False,
        )
        if completed.returncode != 0:
            raise AssertionError(
                f"determinism verifier rejected matching runs:\n"
                f"stdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
            )
        report = json.loads((evidence / "result.json").read_text(encoding="utf-8"))
        assert report["passed"] is True
        assert report["runs"] == 3
        assert len(report["run_results"]) == 3
        assert report["rtc_policy"]["comparable"] is False
        assert report["comparison"]["segments"][0]["pcm"]["sha256"] == "7" * 64

        mismatch_evidence = temp / "mismatch-evidence"
        mismatch_environment = dict(os.environ)
        mismatch_environment["MISMATCH_RUN"] = "3"
        mismatch = subprocess.run(
            [
                sys.executable,
                str(VERIFIER),
                "--route-validator",
                str(validator),
                "--manifest",
                str(manifest),
                "--executable",
                str(executable),
                "--generation-receipt",
                str(receipt),
                "--evidence-dir",
                str(mismatch_evidence),
                "--runs",
                "3",
                "--pcm-seconds",
                "1",
            ],
            env=mismatch_environment,
            text=True,
            capture_output=True,
            check=False,
        )
        if mismatch.returncode == 0:
            raise AssertionError("determinism verifier accepted a state mismatch")
        if (mismatch_evidence / "result.json").exists():
            raise AssertionError("failed determinism run wrote a passing result")
        failure = json.loads(
            (mismatch_evidence / "failure.json").read_text(encoding="utf-8")
        )
        assert failure["passed"] is False
        assert "final_state" in failure["error"]
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
