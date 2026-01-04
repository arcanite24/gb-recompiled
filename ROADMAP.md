# GameBoy Recompiler Roadmap

> Last updated: January 3, 2026 (Session 2 Checkpoint)

## Overview

This document tracks the implementation progress of the GameBoy static recompiler based on the architecture defined in [ARCH.md](ARCH.md).

---

## Phase 1: Foundation ✅ COMPLETE

**Goal**: Minimal working recompiler for simple 32KB ROMs (no banking)

| Task | Status | Notes |
|------|--------|-------|
| Project setup (CMake, directory structure) | ✅ | CMake with C++20, modular structure |
| ROM loader with header parsing | ✅ | Title, MBC type, ROM size, checksums |
| MBC type detection | ✅ | Detects ROM ONLY, MBC1-5 |
| SM83 instruction decoder (~500 opcodes) | ✅ | Full opcode + CB-prefix support |
| Basic IR builder | ✅ | Converts decoded instructions to IR |
| CPU context structure | ✅ | GBContext with registers, flags, memory |
| Memory bus implementation (no banking) | ✅ | gb_read8/gb_write8 in runtime |
| C code emitter | ✅ | Generates compilable C from IR |
| Simple test ROM working | ✅ | Test ROM executes correctly |

**Milestone Achieved**: Successfully recompiles and runs a 32KB no-MBC ROM with:
- Register operations (LD, INC, DEC)
- ALU operations (ADD, SUB, AND, OR, XOR)
- Control flow (JP, JR, conditional jumps, loops)
- Memory access (LD (nn),A)
- HALT instruction

---

## Phase 2: Control Flow & Analysis ✅ COMPLETE

**Goal**: Proper function detection and control flow

| Task | Status | Notes |
|------|--------|-------|
| Control flow analyzer | ✅ | Builds CFG from decoded instructions |
| Jump/call target identification | ✅ | Tracks JP, JR, CALL targets |
| Basic block construction | ✅ | Blocks split at jumps/labels |
| Function boundary detection | ✅ | Functions from call_targets |
| Handle conditional jumps (JP cc, JR cc) | ✅ | Correct target calculation |
| RST vector handling | ✅ | RST 00-38 as functions |
| Reachability analysis | ✅ | DFS from entry point |
| Interrupt vector stubs | ✅ | VBlank, LCD, Timer, Serial, Joypad |

**Milestone Achieved**: Handles ROMs with multiple functions, loops, and conditional branches.

---

## Phase 3: Bank Switching ✅ COMPLETE

**Goal**: Support for MBC1/MBC3/MBC5 games

| Task | Status | Notes |
|------|--------|-------|
| Bank tracker implementation | ✅ | Tracks rom_bank in GBContext |
| MBC1 support | ✅ | Bank register at 0x2000-0x3FFF |
| MBC3 support | 🔲 | Pokémon games, includes RTC |
| MBC5 support | ✅ | Same as MBC1 for basic banking |
| Cross-bank call detection | ✅ | Detects jumps between banks |
| Per-bank function generation | ✅ | func_XX_YYYY naming |
| Runtime bank dispatch | ✅ | gb_dispatch with bank switch |
| RAM banking support | ✅ | Basic ERAM with ram_bank |

**Bugs Fixed**:
- DEC_RR/INC_RR used reg8 instead of reg16
- Analyzer didn't mark 0x4000 as call_targets
- Cross-bank jumps to bank 0 weren't detected
- Self-jumps caused infinite recursion
- LD r,(HL) source operand not set

**Milestone**: Tetris DX (512KB, 32 banks) → 118 functions, 1430 blocks

---

## Phase 4: PPU (Graphics) 🟡 MOSTLY COMPLETE

**Goal**: Visual output

| Task | Status | Notes |
|------|--------|-------|
| Background rendering | ✅ | Tile-based with scroll |
| Window rendering | ✅ | Overlay window layer |
| Sprite rendering (8x8, 8x16) | ✅ | OAM-based with priority |
| Scanline timing | ✅ | Mode 0/1/2/3 transitions |
| VBlank interrupt | ✅ | Sets IF bit 0 |
| LCD STAT interrupt | ✅ | LYC=LY and mode interrupts |
| VRAM access timing | 🔲 | Not enforced |
| Palette handling | ✅ | BGP, OBP0, OBP1, DMG green |
| SDL2 rendering backend | ✅ | ARGB8888, 3x scaling |

**Status**: Screen flashes visible, VRAM writes confirmed (tiles=4096, map=13)

---

## Phase 5: Interrupts & Timing 🟡 PARTIAL

**Goal**: Accurate timing and interrupt handling

| Task | Status | Notes |
|------|--------|-------|
| Full interrupt controller | 🟡 | VBlank/STAT work |
| Joypad input | 🟡 | Returns "released" only |
| Cycle-accurate yielding | ✅ | gb_tick advances PPU |
| Timer (DIV, TIMA, TMA, TAC) | 🔲 | |
| Timer interrupt | 🔲 | |
| Joypad input | 🔲 | |
| Joypad interrupt | 🔲 | |
| DMA transfers | 🔲 | OAM DMA |
| Cycle-accurate yielding | 🔲 | Cooperative multitasking |

**Target**: Timing-sensitive games work

---

## Phase 6: Audio 🔲 NOT STARTED

**Goal**: Sound output

| Task | Status | Notes |
|------|--------|-------|
| Channel 1 (Pulse + sweep) | 🔲 | |
| Channel 2 (Pulse) | 🔲 | |
| Channel 3 (Wave) | 🔲 | |
| Channel 4 (Noise) | 🔲 | |
| Audio mixing | 🔲 | |
| SDL2 audio backend | 🔲 | |

**Target**: Games have sound

---

## Phase 7: Polish & Optimization 🔲 NOT STARTED

**Goal**: Production quality

| Task | Status | Notes |
|------|--------|-------|
| IR optimization passes | 🔲 | Const prop, dead code elim |
| Test ROM compatibility | 🔲 | Blargg's, Mooneye tests |
| Commercial game testing | 🔲 | |
| Debug overlay (ImGui) | 🔲 | |
| Performance profiling | 🔲 | |
| Save state support | 🔲 | |
| Save file support | 🔲 | Battery-backed RAM |
| Documentation | 🟡 | ARCH.md exists |

**Target**: Release-ready recompiler

---

## Future Enhancements (Post-MVP)

| Feature | Status | Priority |
|---------|--------|----------|
| LLVM backend | 🔲 | Medium |
| Game Boy Color support | 🔲 | High |
| Super Game Boy support | 🔲 | Low |
| Link cable emulation | 🔲 | Low |
| Debugger integration | 🔲 | Medium |
| Web/WASM target | 🔲 | Medium |

---

## Current Capabilities

### What Works Now ✅
```
ROM Loading → Decoding → Multi-Bank Analysis → IR → C Generation → Compilation → Graphics Display
```

- **Input**: GameBoy ROM up to 512KB with MBC1/MBC5
- **Output**: Portable C code + runtime library + SDL2 graphics
- **Tested**: Tetris DX - boots, writes VRAM, screen flashes visible

### Test Command
```bash
./build/bin/gbrecomp roms/tetrisdx.gbc -o test_output_tetris
cd test_output_tetris && mkdir build && cd build
cmake -G Ninja .. && ninja
./tetrisdx
```

### Current Test Results (Tetris DX)
```
ROM Size: 512KB (32 banks)
Functions: 118
IR Blocks: 1430
VRAM: tiles=4096, map=13
Frame Rate: ~40 FPS
```

---

## Known Issues / Next Steps

1. **CGB Palettes not implemented** - Game uses CGB color palettes (BCPS/BGPD), causing blank periods
2. **DMG palette working** - When BGP is set, graphics render correctly
3. **Joypad input working** - SDL keyboard now connected to joypad register
4. **No timer interrupts** - DIV/TIMA not implemented
5. **No audio** - Completely unimplemented

---

## Recent Debug Session Results

- **Nintendo logo** displays correctly for ~2 seconds (dark green)
- **Fade effects** work (BGP set to 0x00 for all-white)
- **CGB mode** enabled (A=0x11 at boot)
- **Frame rendering** verified at ~60 FPS
- **RGB conversion** correct (tile data → framebuffer → SDL texture)

---

## Legend

| Symbol | Meaning |
|--------|---------|
| ✅ | Complete |
| 🟡 | Partial / In Progress |
| 🔲 | Not Started |

---

## Quick Stats

| Metric | Value |
|--------|-------|
| Phases Complete | 4 of 7 |
| Core Recompiler | Working |
| Bank Switching | Working |
| PPU Rendering | Working (DMG mode) |
| CGB Palettes | Not implemented |
| Sound | Not implemented |
