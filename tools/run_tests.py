#!/usr/bin/env python3
"""
GameBoy Recompiler Accuracy Test Runner

Tests accuracy against Blargg and Mooneye test ROMs by:
  1. Recompiling each ROM with gbrecomp
  2. Building the generated project with CMake + Ninja
  3. Running the binary with a frame limit and capturing serial output
  4. Determining PASS/FAIL based on the test suite's protocol

Mooneye pass signal: serial bytes 0x03,0x05,0x08,0x0d,0x15,0x22 (Fibonacci).
Blargg pass signal:  "Passed" substring in serial text output.
Rendered-only Blargg tests use a pinned framebuffer hash at a stable verdict frame.

Usage:
    python3 tools/run_tests.py                  # run all tests
    python3 tools/run_tests.py --json           # dump JSON results
    python3 tools/run_tests.py --md             # write ACCURACY.md
    python3 tools/run_tests.py --filter accept  # only tests matching pattern
"""

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path
from typing import Optional

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
WORKSPACE = Path(__file__).parent.parent.resolve()
GBRECOMP   = WORKSPACE / "build" / "bin" / "gbrecomp"
MOONEYE_BASE = WORKSPACE / "roms" / "mooneye" / "mts-20240926-1737-443f6e1"
TEST_OUTPUT  = WORKSPACE / "output" / "test_run"
CACHE_MANIFEST = ".gbrecomp-test-cache.json"
CACHE_SCHEMA = 1

# Mooneye PASS sequence: Fibonacci 3,5,8,13,21,34
MOONEYE_PASS = bytes([0x03, 0x05, 0x08, 0x0D, 0x15, 0x22])

# ---------------------------------------------------------------------------
# Test catalogue
# ---------------------------------------------------------------------------

# Blargg tests in roms/ root — each one is a single ROM.
# Frame limits are generous: Blargg tests can take several minutes of game-time.
BLARGG_ROMS = [
    ("cpu_instrs",     "roms/cpu_instrs.gb",    3600, "blargg"),
    ("01-special",     "roms/01-special.gb",    1800, "blargg"),
    ("instr_timing",   "roms/instr_timing.gb",  1800, "blargg"),
    ("mem_timing-1",   "roms/mem_timing1.gb",   1800, "blargg"),
    ("mem_timing-2",   "roms/mem_timing2.gb",   1800, "blargg"),
    ("halt_bug",       "roms/halt_bug.gb",      1800, "blargg"),
    ("oam_bug",        "roms/oam_bug.gb",       1800, "blargg"),
    ("interrupt_time", "roms/interrupt_time.gb",1800, "blargg"),
]

# These ROMs exercise model-specific hardware behavior even when their header
# advertises broader compatibility. Keep the catalogue explicit so an auto
# model selection cannot turn a required DMG effect into a false failure.
BLARGG_MODEL_OVERRIDES = {
    "oam_bug": "dmg",
}

# Some Blargg aggregate shells render their verdict without publishing it over
# the serial port. Pin a stable completed frame so the runner verifies the real
# guest output instead of treating an empty serial stream as a failure.
BLARGG_RENDERED_VERDICTS = {
    "halt_bug": (299, "28BBA01F"),
    "interrupt_time": (100, "D17F2340"),
    "mem_timing-2": (299, "9E0E8400"),
}

MOONEYE_ACCEPTANCE = [
    # bits
    "acceptance/bits/mem_oam.gb",
    "acceptance/bits/reg_f.gb",
    "acceptance/bits/unused_hwio-GS.gb",
    # instr
    "acceptance/instr/daa.gb",
    # interrupts
    "acceptance/interrupts/ie_push.gb",
    # oam_dma
    "acceptance/oam_dma/basic.gb",
    "acceptance/oam_dma/reg_read.gb",
    "acceptance/oam_dma/sources-GS.gb",
    # ppu
    "acceptance/ppu/hblank_ly_scx_timing-GS.gb",
    "acceptance/ppu/intr_1_2_timing-GS.gb",
    "acceptance/ppu/intr_2_0_timing.gb",
    "acceptance/ppu/intr_2_mode0_timing.gb",
    "acceptance/ppu/intr_2_mode0_timing_sprites.gb",
    "acceptance/ppu/intr_2_mode3_timing.gb",
    "acceptance/ppu/intr_2_oam_ok_timing.gb",
    "acceptance/ppu/lcdon_timing-GS.gb",
    "acceptance/ppu/lcdon_write_timing-GS.gb",
    "acceptance/ppu/stat_irq_blocking.gb",
    "acceptance/ppu/stat_lyc_onoff.gb",
    "acceptance/ppu/vblank_stat_intr-GS.gb",
    # timing
    "acceptance/add_sp_e_timing.gb",
    "acceptance/call_cc_timing.gb",
    "acceptance/call_cc_timing2.gb",
    "acceptance/call_timing.gb",
    "acceptance/call_timing2.gb",
    "acceptance/di_timing-GS.gb",
    "acceptance/div_timing.gb",
    "acceptance/ei_sequence.gb",
    "acceptance/ei_timing.gb",
    "acceptance/halt_ime0_ei.gb",
    "acceptance/halt_ime0_nointr_timing.gb",
    "acceptance/halt_ime1_timing.gb",
    "acceptance/halt_ime1_timing2-GS.gb",
    "acceptance/if_ie_registers.gb",
    "acceptance/intr_timing.gb",
    "acceptance/jp_cc_timing.gb",
    "acceptance/jp_timing.gb",
    "acceptance/ld_hl_sp_e_timing.gb",
    "acceptance/oam_dma_restart.gb",
    "acceptance/oam_dma_start.gb",
    "acceptance/oam_dma_timing.gb",
    "acceptance/pop_timing.gb",
    "acceptance/push_timing.gb",
    "acceptance/rapid_di_ei.gb",
    "acceptance/ret_cc_timing.gb",
    "acceptance/ret_timing.gb",
    "acceptance/reti_intr_timing.gb",
    "acceptance/reti_timing.gb",
    "acceptance/rst_timing.gb",
    "acceptance/timer/div_write.gb",
    "acceptance/timer/rapid_toggle.gb",
    "acceptance/timer/tim00.gb",
    "acceptance/timer/tim00_div_trigger.gb",
    "acceptance/timer/tim01.gb",
    "acceptance/timer/tim01_div_trigger.gb",
    "acceptance/timer/tim10.gb",
    "acceptance/timer/tim10_div_trigger.gb",
    "acceptance/timer/tim11.gb",
    "acceptance/timer/tim11_div_trigger.gb",
    "acceptance/timer/tima_reload.gb",
    "acceptance/timer/tima_write_reloading.gb",
    "acceptance/timer/tma_write_reloading.gb",
]

MOONEYE_CGB = [
    "misc/boot_regs-cgb.gb",
    "misc/boot_div-cgb0.gb",
    "misc/boot_div-cgbABCDE.gb",
    "misc/bits/unused_hwio-C.gb",
    "misc/ppu/vblank_stat_intr-C.gb",
]


def sanitize(name: str) -> str:
    return "".join(c if c.isalnum() or c in "-_" else "_" for c in name)


def find_executable(build_dir: Path) -> Optional[Path]:
    skip_exts = {".a", ".ninja", ".cmake", ".txt", ".sav"}
    for p in build_dir.iterdir():
        if p.is_file() and os.access(p, os.X_OK) and p.suffix not in skip_exts and not p.name.startswith("."):
            return p
    return None


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for chunk in iter(lambda: file.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _sha256_runtime_tree(runtime_dir: Path) -> str:
    digest = hashlib.sha256()
    tracked_suffixes = {".c", ".cpp", ".h"}
    tracked_names = {"CMakeLists.txt"}
    paths = sorted(
        path
        for path in runtime_dir.rglob("*")
        if path.is_file()
        and (path.suffix in tracked_suffixes or path.name in tracked_names)
    )
    for path in paths:
        relative = path.relative_to(runtime_dir).as_posix().encode("utf-8")
        digest.update(len(relative).to_bytes(4, "little"))
        digest.update(relative)
        digest.update(bytes.fromhex(_sha256_file(path)))
    return digest.hexdigest()


def build_cache_fingerprint(
    rom_path: Path,
    gbrecomp_path: Path,
    runtime_dir: Path,
    model: Optional[str],
) -> dict:
    """Describe every source input that can change a generated test binary."""
    return {
        "schema": CACHE_SCHEMA,
        "rom_sha256": _sha256_file(rom_path),
        "gbrecomp_sha256": _sha256_file(gbrecomp_path),
        "runtime_sha256": _sha256_runtime_tree(runtime_dir),
        "model": model or "auto",
        "generator": "Ninja",
        "platform": sys.platform,
        "cc": os.environ.get("CC", ""),
        "cxx": os.environ.get("CXX", ""),
    }


def cache_is_current(output_dir: Path, fingerprint: dict) -> bool:
    manifest = output_dir / CACHE_MANIFEST
    try:
        return json.loads(manifest.read_text()) == fingerprint
    except (OSError, json.JSONDecodeError):
        return False


def write_cache_manifest(output_dir: Path, fingerprint: dict) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    manifest = output_dir / CACHE_MANIFEST
    temporary = manifest.with_suffix(manifest.suffix + ".tmp")
    temporary.write_text(json.dumps(fingerprint, indent=2, sort_keys=True) + "\n")
    temporary.replace(manifest)


def run_test(name: str, rom_path: Path, frame_limit: int, suite: str,
             model: Optional[str] = None,
             dry_run: bool = False, rebuild: bool = False) -> dict:
    result = {
        "name":        name,
        "suite":       suite,
        "rom":         str(rom_path.relative_to(WORKSPACE)),
        "model":       model or "auto",
        "status":      "unknown",
        "serial_hex":  "",
        "serial_text": "",
        "frame_hash":  "",
        "state": {},
        "error":       "",
        "elapsed":     0.0,
    }

    t0 = time.time()
    safe = sanitize(name)
    out_dir = TEST_OUTPUT / safe

    if dry_run:
        result["status"] = "skip"
        return result

    fingerprint = build_cache_fingerprint(
        rom_path, GBRECOMP, WORKSPACE / "runtime", model
    )
    generated_cache_current = (
        (out_dir / "CMakeLists.txt").exists()
        and cache_is_current(out_dir, fingerprint)
    )

    # ------------------------------------------------------------------
    # Step 1: recompile unless every generated-project input still matches
    # ------------------------------------------------------------------
    if rebuild or not generated_cache_current:
        if out_dir.exists():
            shutil.rmtree(out_dir)
        try:
            r = subprocess.run(
                [str(GBRECOMP), str(rom_path), "-o", str(out_dir)],
                capture_output=True, text=True, timeout=90,
            )
            if r.returncode != 0:
                result["status"] = "compile_error"
                result["error"]  = r.stderr[-400:]
                result["elapsed"] = time.time() - t0
                return result
        except subprocess.TimeoutExpired:
            result["status"] = "compile_timeout"
            result["elapsed"] = time.time() - t0
            return result

    # ------------------------------------------------------------------
    # Step 2: always reconfigure and invoke Ninja. A no-op build is cheap, and
    # it lets CMake/Ninja notice runtime, toolchain, and dependency changes
    # instead of trusting the mere existence of yesterday's executable.
    # ------------------------------------------------------------------
    build_dir = out_dir / "build"
    build_dir.mkdir(parents=True, exist_ok=True)
    try:
        r = subprocess.run(
            ["cmake", "-G", "Ninja", f"-S{out_dir}", f"-B{build_dir}", "-Wno-dev"],
            capture_output=True, text=True, timeout=60,
        )
        if r.returncode != 0:
            result["status"] = "cmake_error"
            result["error"]  = r.stderr[-400:]
            result["elapsed"] = time.time() - t0
            return result

        r = subprocess.run(
            ["ninja", f"-C{build_dir}"],
            capture_output=True, text=True, timeout=180,
        )
        if r.returncode != 0:
            result["status"] = "build_error"
            result["error"]  = r.stderr[-400:]
            result["elapsed"] = time.time() - t0
            return result
    except subprocess.TimeoutExpired:
        result["status"] = "build_timeout"
        result["elapsed"] = time.time() - t0
        return result

    binary = find_executable(build_dir)

    if binary is None:
        result["status"] = "no_executable"
        result["elapsed"] = time.time() - t0
        return result

    write_cache_manifest(out_dir, fingerprint)

    # ------------------------------------------------------------------
    # Step 3: run with frame limit, capture stdout (serial + runtime msgs)
    # ------------------------------------------------------------------
    rendered_verdict = BLARGG_RENDERED_VERDICTS.get(name)
    effective_frame_limit = (
        min(frame_limit, rendered_verdict[0] + 5)
        if rendered_verdict
        else frame_limit
    )

    # Give the binary a wall-clock budget proportional to the frame limit
    # (60fps ≈ 1s of game time per 60 frames; add 30s overhead for SDL init).
    run_timeout = max(60, effective_frame_limit // 60 * 2 + 30)
    state_path = out_dir / "test_state.json"
    state_path.unlink(missing_ok=True)
    rendered_prefix = WORKSPACE / "logs" / f"accuracy_{safe}"
    rendered_frame_path = None
    if rendered_verdict:
        rendered_frame_path = Path(
            f"{rendered_prefix}_{rendered_verdict[0]:05d}.ppm"
        )
        rendered_frame_path.unlink(missing_ok=True)
    try:
        argv = [
            str(binary),
            "--limit-frames",
            str(effective_frame_limit),
            "--dump-state",
            str(state_path),
        ]
        if rendered_verdict:
            argv.extend([
                "--dump-frames",
                str(rendered_verdict[0]),
                "--screenshot-prefix",
                str(rendered_prefix.relative_to(WORKSPACE)),
            ])
        else:
            argv.insert(1, "--headless")
        if suite == "blargg":
            argv.extend(["--serial-stdout", "--stop-on-serial-verdict"])
        if suite == "mooneye":
            argv.append("--stop-on-test-breakpoint")
        if model:
            argv.extend(["--model", model])
        environment = os.environ.copy()
        environment.setdefault("SDL_VIDEODRIVER", "dummy")
        environment.setdefault("SDL_AUDIODRIVER", "dummy")
        r = subprocess.run(
            argv, cwd=WORKSPACE,
            capture_output=True, timeout=run_timeout, env=environment,
        )
        raw = r.stdout
        if rendered_frame_path:
            rendered_frame_path.unlink(missing_ok=True)
        if r.returncode != 0:
            result["status"] = "runtime_error"
            result["error"] = r.stderr.decode("utf-8", errors="replace")[-400:]
            result["elapsed"] = time.time() - t0
            return result
    except subprocess.TimeoutExpired:
        if rendered_frame_path:
            rendered_frame_path.unlink(missing_ok=True)
        result["status"] = "run_timeout"
        result["elapsed"] = time.time() - t0
        return result

    try:
        state = json.loads(state_path.read_text())
    except (OSError, json.JSONDecodeError) as error:
        result["status"] = "missing_state"
        result["error"] = str(error)
        result["elapsed"] = time.time() - t0
        return result
    result["state"] = state

    # Strip the leading "Frame limit: NNN\n" line emitted by the runtime, and
    # also remove any [GBRT] / [SDL] / [LIMIT] / [DIFF] status lines that the
    # runtime prints to stdout alongside the ROM's actual serial output.
    def _filter_serial(raw: bytes) -> bytes:
        lines = raw.split(b"\n")
        kept = []
        for line in lines:
            stripped = line.lstrip()
            if (stripped.startswith(b"Frame limit:")
                    or stripped.startswith(b"[GBRT]")
                    or stripped.startswith(b"[SDL]")
                    or stripped.startswith(b"[LIMIT]")
                    or stripped.startswith(b"[DIFF]")
                    or stripped.startswith(b"[AUTO]")
                    or stripped.startswith(b"[TRACE]")):
                continue
            kept.append(line)
        return b"\n".join(kept)

    serial_raw = _filter_serial(raw)

    result["serial_hex"]  = serial_raw.hex()
    result["serial_text"] = serial_raw.decode("ascii", errors="replace").strip()
    if rendered_verdict:
        marker = f"[AUTO] Frame {rendered_verdict[0]} hash: ".encode("ascii")
        for line in raw.splitlines():
            if line.startswith(marker):
                result["frame_hash"] = line[len(marker):].decode(
                    "ascii", errors="replace"
                ).strip().upper()
                break
    result["elapsed"]     = time.time() - t0

    # ------------------------------------------------------------------
    # Determine PASS / FAIL
    # ------------------------------------------------------------------
    if suite == "mooneye":
        result["status"] = "pass" if mooneye_state_passes(state) else "fail"
    elif suite == "blargg":
        text = result["serial_text"]
        memory_verdict = (
            blargg_memory_verdict(state) if name == "oam_bug" else None
        )
        result["memory_verdict"] = memory_verdict
        if memory_verdict is not None:
            result["status"] = memory_verdict
        elif "Passed" in text:
            result["status"] = "pass"
        elif "Failed" in text:
            result["status"] = "fail"
        elif rendered_verdict:
            result["status"] = (
                "pass"
                if result["frame_hash"] == rendered_verdict[1]
                else "fail"
            )
        else:
            # Serial output present but no final verdict — test ran out of frames
            result["status"] = "incomplete" if serial_raw.strip() else "fail"
    else:
        result["status"] = "unknown"

    return result


def build_test_list(filter_str: Optional[str] = None) -> list:
    tests = []

    # Blargg root-level ROMs
    for name, rom_rel, frames, suite in BLARGG_ROMS:
        rom = WORKSPACE / rom_rel
        if rom.exists():
            tests.append(
                (name, rom, frames, suite, BLARGG_MODEL_OVERRIDES.get(name))
            )

    # Mooneye acceptance + timer suite
    for rel in MOONEYE_ACCEPTANCE:
        rom = MOONEYE_BASE / rel
        if rom.exists():
            name = rom.stem
            # Mooneye tests complete within 120 frames typically
            tests.append((name, rom, 300, "mooneye", None))

    for rel in MOONEYE_CGB:
        rom = MOONEYE_BASE / rel
        if rom.exists():
            name = rom.stem
            tests.append((name, rom, 300, "mooneye", "cgb"))

    if filter_str:
        needle = filter_str.lower()
        tests = [
            (n, r, f, s, m)
            for n, r, f, s, m in tests
            if needle in n.lower()
            or needle in r.relative_to(WORKSPACE).as_posix().lower()
        ]

    return tests


def suite_exit_code(results: list[dict]) -> int:
    """Return success only for a non-empty suite whose tests all passed."""
    return 0 if results and all(r.get("status") == "pass" for r in results) else 1


def mooneye_state_passes(state: dict) -> bool:
    """Mooneye's automated pass protocol leaves Fibonacci values in B..L."""
    return [state.get(register) for register in ("b", "c", "d", "e", "h", "l")] == [
        3,
        5,
        8,
        13,
        21,
        34,
    ]


def blargg_memory_verdict(state: dict) -> Optional[str]:
    """Decode Blargg's documented $A000 status/signature protocol."""
    prefix = state.get("eram_a000_a0ff")
    if not isinstance(prefix, list) or len(prefix) < 4:
        return None
    if any(
        isinstance(value, bool)
        or not isinstance(value, int)
        or value < 0
        or value > 0xFF
        for value in prefix[:4]
    ):
        return None
    if prefix[1:4] != [0xDE, 0xB0, 0x61]:
        return None
    if prefix[0] == 0:
        return "pass"
    if prefix[0] == 0x80:
        return "incomplete"
    return "fail"


def print_result_line(result: dict):
    icon  = "✓" if result["status"] == "pass" else ("?" if result["status"] == "incomplete" else "✗")
    suite = result["suite"].upper()[:7].ljust(7)
    label = result["name"] if result.get("model", "auto") == "auto" else f"{result['name']} [{result['model']}]"
    name  = label[:45].ljust(45)
    st    = result["status"].upper()[:10].ljust(10)
    secs  = f"{result['elapsed']:5.1f}s"
    print(f"  {icon} [{suite}] {name} {st} {secs}")


def generate_accuracy_md(results: list, output_path: Path):
    date_str = time.strftime("%Y-%m-%d")
    try:
        recompiler_label = GBRECOMP.relative_to(WORKSPACE).as_posix()
    except ValueError:
        recompiler_label = str(GBRECOMP)

    total  = len(results)
    passed = sum(1 for r in results if r["status"] == "pass")
    failed = sum(1 for r in results if r["status"] == "fail")
    other  = total - passed - failed

    mooneye_results = [r for r in results if r["suite"] == "mooneye"]
    blargg_results  = [r for r in results if r["suite"] == "blargg"]

    m_pass = sum(1 for r in mooneye_results if r["status"] == "pass")
    b_pass = sum(1 for r in blargg_results  if r["status"] == "pass")

    def table_rows(rows):
        lines = []
        for r in sorted(rows, key=lambda x: (x["status"] != "pass", x["name"])):
            badge = "✅ PASS" if r["status"] == "pass" else (
                    "⚠️ INCOMPLETE" if r["status"] == "incomplete" else
                    "❌ FAIL" if r["status"] == "fail" else
                    f"🔧 {r['status'].upper()}")
            preview = ""
            if r["suite"] == "blargg" and r.get("memory_verdict"):
                prefix = r.get("state", {}).get("eram_a000_a0ff", [])[:4]
                preview = "memory verdict " + " ".join(
                    f"{value:02X}" for value in prefix
                )
            elif r["suite"] == "blargg" and r["serial_text"]:
                preview = r["serial_text"].replace("\n", " · ")[:80]
            elif r["suite"] == "blargg" and r.get("frame_hash"):
                preview = f"rendered verdict hash {r['frame_hash']}"
            label = r["name"] if r.get("model", "auto") == "auto" else f"{r['name']} [{r['model']}]"
            lines.append(f"| {label} | {badge} | {preview} |")
        return "\n".join(lines)

    md = f"""# GameBoy Recompiler — Accuracy Report

> Generated: {date_str}
>
> Recompiler binary: `{recompiler_label}`
>
> Test suites: [Mooneye MTS 2024-09-26](https://github.com/Gekkio/mooneye-test-suite), [Blargg GB Test ROMs](https://github.com/retrio/gb-test-roms)

This is the complete configured external-ROM catalogue, not a percentage of commercial-game compatibility. Repository-owned CTest results are tracked separately because this generator does not run them.

## Summary

| Suite | Passed | Total | Pass Rate |
|-------|--------|-------|-----------|
| Blargg | {b_pass} | {len(blargg_results)} | {b_pass/max(len(blargg_results),1)*100:.0f}% |
| Mooneye catalogue | {m_pass} | {len(mooneye_results)} | {m_pass/max(len(mooneye_results),1)*100:.0f}% |
| **Total** | **{passed}** | **{total}** | **{passed/max(total,1)*100:.0f}%** |

Pass/fail is determined by each suite's real protocol. Build failures, timeouts, missing state dumps, incomplete execution, and an empty selection fail closed and are never counted as passes.

---

## Blargg CPU / Timing Tests

Most ROMs output ASCII text via the serial port. "Passed" in that output = pass.
`oam_bug` uses Blargg's signed `$A000` memory verdict, while rendered-only cases
use the pinned completed-frame hash documented in the runner.

| Test | Result | Verdict evidence |
|------|--------|---------------|
{table_rows(blargg_results)}

---

## Mooneye Acceptance Tests

Mooneye tests signal pass by writing the Fibonacci sequence `03 05 08 0D 15 22` to the serial port.
Tests marked **GS** target DMG/SGB hardware specifically. Curated CGB entries are run with `--model cgb`.

### bits
| Test | Result | Notes |
|------|--------|-------|
{table_rows([r for r in mooneye_results if "bits/" in r["rom"] or r["name"] in ("mem_oam","reg_f","unused_hwio-GS")])}

### instructions
| Test | Result | Notes |
|------|--------|-------|
{table_rows([r for r in mooneye_results if "instr/" in r["rom"] or r["name"] == "daa"])}

### interrupts
| Test | Result | Notes |
|------|--------|-------|
{table_rows([r for r in mooneye_results if "interrupts/" in r["rom"] or r["name"] == "ie_push"])}

### OAM DMA
| Test | Result | Notes |
|------|--------|-------|
{table_rows([r for r in mooneye_results if "oam_dma" in r["rom"]])}

### PPU
| Test | Result | Notes |
|------|--------|-------|
{table_rows([r for r in mooneye_results if "ppu/" in r["rom"]])}

### Timer
| Test | Result | Notes |
|------|--------|-------|
{table_rows([r for r in mooneye_results if "timer/" in r["rom"]])}

### Misc timing
| Test | Result | Notes |
|------|--------|-------|
{table_rows([r for r in mooneye_results if "timer/" not in r["rom"] and "ppu/" not in r["rom"] and "oam_dma" not in r["rom"] and "interrupts/" not in r["rom"] and "instr/" not in r["rom"] and "bits/" not in r["rom"]])}

---

## Known Limitations

- **Boot ROM**: The runtime starts from configured post-boot state rather than executing a DMG or CGB boot ROM. Boot-initialization tests can therefore fail even when later runtime behavior is correct.
- **Undocumented I/O**: The configured DMG and CGB unused-hardware-I/O cases still have model-specific readback gaps.
- **Wall-clock limits**: Every test has both a guest-frame limit and a proportional wall-clock timeout. A timeout is an error, not evidence that the test would pass with a longer run.
- **Shared implementation**: Generated-vs-interpreter differential checks are valuable additional evidence, but both paths share runtime devices and are not an independent hardware oracle.

## Reproduce

```bash
cmake -G Ninja -B build .
ninja -C build
python3 tools/run_tests.py --rebuild --md
```

Use `--filter <substring>` for investigation, but only an unfiltered run should replace this full-catalogue report.
"""
    output_path.write_text(md)
    print(f"\n  → Written to {output_path}")


def main():
    global GBRECOMP
    parser = argparse.ArgumentParser(description="GB Recompiler accuracy test runner")
    parser.add_argument("--filter",   default=None, help="Only run tests matching this substring")
    parser.add_argument("--json",     action="store_true", help="Dump JSON results to stdout")
    parser.add_argument("--md",       action="store_true", help="Write ACCURACY.md")
    parser.add_argument("--rebuild",  action="store_true", help="Force recompile/rebuild even if cached")
    parser.add_argument("--dry-run",  action="store_true", help="List tests without running them")
    parser.add_argument(
        "--gbrecomp",
        type=Path,
        default=GBRECOMP,
        help="Recompiler executable (default: build/bin/gbrecomp)",
    )
    args = parser.parse_args()
    GBRECOMP = args.gbrecomp.expanduser().resolve()

    if not GBRECOMP.exists():
        print(f"[ERROR] gbrecomp not found at {GBRECOMP}. Run: ninja -C build", file=sys.stderr)
        sys.exit(1)

    TEST_OUTPUT.mkdir(parents=True, exist_ok=True)

    tests = build_test_list(args.filter)
    print(f"Running {len(tests)} tests (output: {TEST_OUTPUT})\n")

    results = []
    for i, (name, rom, frames, suite, model) in enumerate(tests):
        tag = f"[{i+1}/{len(tests)}]"
        mode_suffix = "" if model is None else f", model={model}"
        print(f"  {tag} {name} ({suite}, {frames}fr{mode_suffix}) ...", end="", flush=True)
        r = run_test(name, rom, frames, suite, model=model, dry_run=args.dry_run, rebuild=args.rebuild)
        results.append(r)
        icon = "✓" if r["status"] == "pass" else ("?" if r["status"] == "incomplete" else "✗")
        label = name if model is None else f"{name} [{model}]"
        print(f"\r  {tag} {icon} {label:<45} {r['status']:<12} {r['elapsed']:4.1f}s")

    passed    = sum(1 for r in results if r["status"] == "pass")
    failed    = sum(1 for r in results if r["status"] == "fail")
    incomplete = sum(1 for r in results if r["status"] == "incomplete")
    errors    = len(results) - passed - failed - incomplete

    print(f"\n{'='*60}")
    print(f"  PASS: {passed}  FAIL: {failed}  INCOMPLETE: {incomplete}  ERROR: {errors}  / {len(results)}")
    print(f"{'='*60}")

    if args.json:
        print(json.dumps(results, indent=2))

    if args.md:
        generate_accuracy_md(results, WORKSPACE / "ACCURACY.md")

    # A compatibility run is only successful when at least one selected test
    # actually passes and every selected test reaches that passing verdict.
    # Treating build errors, timeouts, incomplete runs, or an empty selection as
    # success makes CI and generated accuracy reports false-green.
    return suite_exit_code(results)


if __name__ == "__main__":
    sys.exit(main())
