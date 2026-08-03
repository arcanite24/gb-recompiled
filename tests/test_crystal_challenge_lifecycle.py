#!/usr/bin/env python3
"""Synthetic save/removal/reinstall proof for the Challenge lifecycle verifier."""

from __future__ import annotations

import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VERIFIER = ROOT / "ports/pokemon-crystal/scripts/verify_challenge_lifecycle.py"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path: Path, value: object, *, compact: bool = False) -> None:
    text = (
        json.dumps(value, separators=(",", ":"))
        if compact
        else json.dumps(value, indent=2, sort_keys=True)
    )
    path.write_text(text + "\n", encoding="utf-8")


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="challenge-lifecycle-") as raw:
        root = Path(raw)
        executable = root / "mock-crystal"
        vanilla_receipt = root / "vanilla-receipt.json"
        challenge_receipt = root / "challenge-receipt.json"
        baseline_save = root / "baseline.sav"
        inputs = root / "restart-continue.json"
        configuration = root / "challenge.json"
        output = root / "evidence"
        baseline_save.write_bytes(bytes(range(256)) * 128)
        write_json(inputs, [{"cycle": 0, "buttons": "A", "duration": 4}])
        configuration_value = {
            "schema": "gbrecomp.host-configuration",
            "version": 1,
            "policy_id": "challenge-v1",
            "applied": True,
            "enabled": True,
            "offset": -2,
            "minimum": 1,
            "maximum": 100,
        }
        write_json(configuration, configuration_value, compact=True)
        executable.write_text(
            f"""#!/usr/bin/env python3
import hashlib
import json
import sys
from pathlib import Path

args = sys.argv[1:]
def value(flag):
    return args[args.index(flag) + 1]

configuration = None
if "--host-configuration" in args:
    path = Path(value("--host-configuration"))
    try:
        configuration = json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        raise SystemExit(3)
    if configuration.get("schema") != "gbrecomp.host-configuration":
        raise SystemExit(3)

save_dir = Path(value("--save-dir"))
save = save_dir / "pokemon_crystal.sav"
if not save.is_file() or len(save.read_bytes()) != 32768:
    raise SystemExit(4)
prefix = Path(value("--screenshot-prefix"))
for frame in value("--dump-frames").split(","):
    (prefix.parent / f"{{prefix.name}}_{{int(frame):05d}}.ppm").write_bytes(
        b"P6\\n1 1\\n255\\n\\x12\\x34\\x56"
    )
state = {{"registers": {{"pc": 4660}}, "completed_frames": 3499}}
if configuration is not None:
    state["host_configuration"] = {{
        "present": True,
        "applied": True,
        "enabled": True,
        "policy_id": "challenge-v1",
        "sha256": hashlib.sha256(Path(value("--host-configuration")).read_bytes()).hexdigest(),
    }}
else:
    state["host_configuration"] = {{
        "present": False,
        "applied": False,
        "enabled": False,
        "policy_id": "",
        "sha256": "",
    }}
Path(value("--dump-state")).write_text(json.dumps(state) + "\\n", encoding="utf-8")
Path(value("--log-file")).write_text(
    "[INTERP] Fallback inventory: sites=0 dropped=0 complete=yes\\n"
    "[INTERP] No interpreter fallback recorded.\\n",
    encoding="utf-8",
)
""",
            encoding="utf-8",
        )
        executable.chmod(0o755)
        receipt_base = {
            "schema": "crystal-recompiled.generation",
            "version": 1,
            "rom": {"sha256": "a" * 64},
            "generated": {
                "metadata_sha256": "b" * 64,
                "source_inventory_sha256": "c" * 64,
            },
            "native_patch": {"kind": "none"},
        }
        write_json(vanilla_receipt, receipt_base)
        challenge_value = dict(receipt_base)
        challenge_value["native_patch"] = {
            "kind": "file",
            "name": "challenge.json",
            "sha256": "d" * 64,
        }
        write_json(challenge_receipt, challenge_value)

        completed = subprocess.run(
            [
                sys.executable,
                str(VERIFIER),
                "--vanilla-executable",
                str(executable),
                "--challenge-executable",
                str(executable),
                "--vanilla-receipt",
                str(vanilla_receipt),
                "--challenge-receipt",
                str(challenge_receipt),
                "--baseline-save",
                str(baseline_save),
                "--enabled-configuration",
                str(configuration),
                "--restart-input",
                str(inputs),
                "--output",
                str(output),
            ],
            text=True,
            capture_output=True,
            check=False,
        )
        if completed.returncode != 0:
            raise AssertionError(completed.stdout + completed.stderr)
        result_path = output / "result.json"
        result = json.loads(result_path.read_text(encoding="utf-8"))
        assert result["passed"] is True
        assert result["lifecycle_order"] == [
            "vanilla",
            "disabled",
            "enabled_first",
            "enabled_restart",
            "removed",
            "reinstalled",
        ]
        assert set(result["modes"]) == set(result["lifecycle_order"])
        assert len({mode["comparable_state_sha256"] for mode in result["modes"].values()}) == 1
        assert all(
            mode["save_after_sha256"] == sha256(baseline_save)
            for mode in result["modes"].values()
        )
        assert result["malformed_configuration"]["rejected_before_guest"] is True
        assert str(root) not in result_path.read_text(encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
