# GameBoy Recompiler — Accuracy Report

> Generated: 2026-07-13
>
> Recompiler binary: `build/bin/gbrecomp`
>
> Test suites: [Mooneye MTS 2024-09-26](https://github.com/Gekkio/mooneye-test-suite), [Blargg GB Test ROMs](https://github.com/retrio/gb-test-roms)

This is the complete configured external-ROM catalogue, not a percentage of commercial-game compatibility. Repository-owned CTest results are tracked separately because this generator does not run them.

## Summary

| Suite | Passed | Total | Pass Rate |
|-------|--------|-------|-----------|
| Blargg | 8 | 8 | 100% |
| Mooneye catalogue | 63 | 67 | 94% |
| **Total** | **71** | **75** | **95%** |

Pass/fail is determined by each suite's real protocol. Build failures, timeouts, missing state dumps, incomplete execution, and an empty selection fail closed and are never counted as passes.

---

## Blargg CPU / Timing Tests

Most ROMs output ASCII text via the serial port. "Passed" in that output = pass.
`oam_bug` uses Blargg's signed `$A000` memory verdict, while rendered-only cases
use the pinned completed-frame hash documented in the runner.

| Test | Result | Verdict evidence |
|------|--------|---------------|
| 01-special | ✅ PASS | 01-special ·  ·  · Passed |
| cpu_instrs | ✅ PASS | cpu_instrs ·  · 01:ok  02:ok  03:ok  04:ok  05:ok  06:ok  07:ok  08:ok  09:ok  1 |
| halt_bug | ✅ PASS | rendered verdict hash 28BBA01F |
| instr_timing | ✅ PASS | instr_timing ·  ·  · Passed |
| interrupt_time | ✅ PASS | rendered verdict hash D17F2340 |
| mem_timing-1 | ✅ PASS | mem_timing ·  · 01:ok  02:ok  03:ok   ·  · Passed |
| mem_timing-2 | ✅ PASS | rendered verdict hash 9E0E8400 |
| oam_bug [dmg] | ✅ PASS | memory verdict 00 DE B0 61 |

---

## Mooneye Acceptance Tests

Mooneye tests signal pass by writing the Fibonacci sequence `03 05 08 0D 15 22` to the serial port.
Tests marked **GS** target DMG/SGB hardware specifically. Curated CGB entries are run with `--model cgb`.

### bits
| Test | Result | Notes |
|------|--------|-------|
| mem_oam | ✅ PASS |  |
| reg_f | ✅ PASS |  |
| unused_hwio-C [cgb] | ❌ FAIL |  |
| unused_hwio-GS | ❌ FAIL |  |

### instructions
| Test | Result | Notes |
|------|--------|-------|
| daa | ✅ PASS |  |

### interrupts
| Test | Result | Notes |
|------|--------|-------|
| ie_push | ✅ PASS |  |

### OAM DMA
| Test | Result | Notes |
|------|--------|-------|
| basic | ✅ PASS |  |
| oam_dma_restart | ✅ PASS |  |
| oam_dma_start | ✅ PASS |  |
| oam_dma_timing | ✅ PASS |  |
| reg_read | ✅ PASS |  |
| sources-GS | ✅ PASS |  |

### PPU
| Test | Result | Notes |
|------|--------|-------|
| hblank_ly_scx_timing-GS | ✅ PASS |  |
| intr_1_2_timing-GS | ✅ PASS |  |
| intr_2_0_timing | ✅ PASS |  |
| intr_2_mode0_timing | ✅ PASS |  |
| intr_2_mode0_timing_sprites | ✅ PASS |  |
| intr_2_mode3_timing | ✅ PASS |  |
| intr_2_oam_ok_timing | ✅ PASS |  |
| lcdon_timing-GS | ✅ PASS |  |
| lcdon_write_timing-GS | ✅ PASS |  |
| stat_irq_blocking | ✅ PASS |  |
| stat_lyc_onoff | ✅ PASS |  |
| vblank_stat_intr-C [cgb] | ✅ PASS |  |
| vblank_stat_intr-GS | ✅ PASS |  |

### Timer
| Test | Result | Notes |
|------|--------|-------|
| div_write | ✅ PASS |  |
| rapid_toggle | ✅ PASS |  |
| tim00 | ✅ PASS |  |
| tim00_div_trigger | ✅ PASS |  |
| tim01 | ✅ PASS |  |
| tim01_div_trigger | ✅ PASS |  |
| tim10 | ✅ PASS |  |
| tim10_div_trigger | ✅ PASS |  |
| tim11 | ✅ PASS |  |
| tim11_div_trigger | ✅ PASS |  |
| tima_reload | ✅ PASS |  |
| tima_write_reloading | ✅ PASS |  |
| tma_write_reloading | ✅ PASS |  |

### Misc timing
| Test | Result | Notes |
|------|--------|-------|
| add_sp_e_timing | ✅ PASS |  |
| boot_regs-cgb [cgb] | ✅ PASS |  |
| call_cc_timing | ✅ PASS |  |
| call_cc_timing2 | ✅ PASS |  |
| call_timing | ✅ PASS |  |
| call_timing2 | ✅ PASS |  |
| di_timing-GS | ✅ PASS |  |
| div_timing | ✅ PASS |  |
| ei_sequence | ✅ PASS |  |
| ei_timing | ✅ PASS |  |
| halt_ime0_ei | ✅ PASS |  |
| halt_ime0_nointr_timing | ✅ PASS |  |
| halt_ime1_timing | ✅ PASS |  |
| halt_ime1_timing2-GS | ✅ PASS |  |
| if_ie_registers | ✅ PASS |  |
| intr_timing | ✅ PASS |  |
| jp_cc_timing | ✅ PASS |  |
| jp_timing | ✅ PASS |  |
| ld_hl_sp_e_timing | ✅ PASS |  |
| pop_timing | ✅ PASS |  |
| push_timing | ✅ PASS |  |
| rapid_di_ei | ✅ PASS |  |
| ret_cc_timing | ✅ PASS |  |
| ret_timing | ✅ PASS |  |
| reti_intr_timing | ✅ PASS |  |
| reti_timing | ✅ PASS |  |
| rst_timing | ✅ PASS |  |
| boot_div-cgb0 [cgb] | ❌ FAIL |  |
| boot_div-cgbABCDE [cgb] | ❌ FAIL |  |

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
