#!/usr/bin/env python3
"""Generate a minimal GameBoy ROM that displays a simple pattern on screen."""

import struct

# Minimal GB ROM
rom = bytearray(32768)  # 32KB ROM (smallest valid size)

# Nintendo logo at 0x0104-0x0133 (required for boot)
nintendo_logo = bytes([
    0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B,
    0x03, 0x73, 0x00, 0x83, 0x00, 0x0C, 0x00, 0x0D,
    0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E,
    0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99,
    0xBB, 0xBB, 0x67, 0x63, 0x6E, 0x0E, 0xEC, 0xCC,
    0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E
])

# ROM header at 0x0100
rom[0x0100] = 0x00  # NOP
rom[0x0101] = 0xC3  # JP
rom[0x0102] = 0x50  # Low byte of 0x0150
rom[0x0103] = 0x01  # High byte of 0x0150

# Nintendo logo
rom[0x0104:0x0104 + len(nintendo_logo)] = nintendo_logo

# Title (11 bytes max for new format, 16 for old)
title = b"GFXTEST\x00\x00\x00\x00"
rom[0x0134:0x0134 + 11] = title

# Color GB flag
rom[0x0143] = 0x00  # DMG only

# Licensee code
rom[0x0144] = 0x00
rom[0x0145] = 0x00

# SGB flag
rom[0x0146] = 0x00

# Cartridge type (ROM only)
rom[0x0147] = 0x00

# ROM size (32KB)
rom[0x0148] = 0x00

# RAM size (none)
rom[0x0149] = 0x00

# Destination code (Japanese)
rom[0x014A] = 0x00

# Old licensee code
rom[0x014B] = 0x00

# ROM version
rom[0x014C] = 0x00

# Header checksum (computed)
checksum = 0
for i in range(0x0134, 0x014D):
    checksum = (checksum - rom[i] - 1) & 0xFF
rom[0x014D] = checksum

# Store tile data at 0x1000 in ROM
# We'll copy this to VRAM at 0x8000
# Each tile is 16 bytes (8x8 pixels, 2 bits per pixel)

# Tile 0: Solid fill (all color 3 - darkest)
solid_tile = bytes([0xFF, 0xFF] * 8)

# Tile 1: Checkerboard pattern
checker_tile = bytes([
    0xAA, 0x55,  # Row 0: alternating pattern
    0x55, 0xAA,  # Row 1
    0xAA, 0x55,  # Row 2
    0x55, 0xAA,  # Row 3
    0xAA, 0x55,  # Row 4
    0x55, 0xAA,  # Row 5
    0xAA, 0x55,  # Row 6
    0x55, 0xAA,  # Row 7
])

# Tile 2: Horizontal stripes
hstripe_tile = bytes([
    0xFF, 0xFF,  # Row 0: solid
    0x00, 0x00,  # Row 1: empty
    0xFF, 0xFF,  # Row 2: solid
    0x00, 0x00,  # Row 3: empty
    0xFF, 0xFF,  # Row 4: solid
    0x00, 0x00,  # Row 5: empty
    0xFF, 0xFF,  # Row 6: solid
    0x00, 0x00,  # Row 7: empty
])

# Tile 3: Vertical stripes (color 1)
vstripe_tile = bytes([
    0xAA, 0x00,
    0xAA, 0x00,
    0xAA, 0x00,
    0xAA, 0x00,
    0xAA, 0x00,
    0xAA, 0x00,
    0xAA, 0x00,
    0xAA, 0x00,
])

# Tile 4: Empty (all color 0 - lightest)
empty_tile = bytes([0x00, 0x00] * 8)

# Tile data storage starting at 0x1000
tile_data_offset = 0x1000
tiles = [empty_tile, solid_tile, checker_tile, hstripe_tile, vstripe_tile]
for i, tile in enumerate(tiles):
    rom[tile_data_offset + i * 16 : tile_data_offset + i * 16 + 16] = tile

# Now write the assembly code at 0x0150
# This will:
# 1. Wait for VBlank
# 2. Turn off LCD
# 3. Copy tile data to VRAM
# 4. Fill tilemap with pattern
# 5. Set palette
# 6. Turn on LCD
# 7. Loop forever (waiting for VBlank)

code_offset = 0x0150
code = bytearray()

def emit(data):
    if isinstance(data, int):
        code.append(data)
    else:
        code.extend(data)

# Initialize stack
emit([0x31, 0xFE, 0xFF])  # LD SP, 0xFFFE

# Disable interrupts
emit(0xF3)  # DI

# Wait for VBlank (LY == 144)
# wait_vblank:
wait_vblank_addr = len(code)
emit([0xF0, 0x44])  # LDH A, (LY) - read from 0xFF44
emit([0xFE, 144])   # CP 144
emit([0x20, 0xFA])  # JR NZ, wait_vblank (relative jump back)

# Turn off LCD (LCDC = 0)
emit([0x3E, 0x00])  # LD A, 0
emit([0xE0, 0x40])  # LDH (LCDC), A - write to 0xFF40

# Copy tile data from ROM (0x1000) to VRAM (0x8000)
# HL = destination (VRAM 0x8000)
# DE = source (ROM 0x1000)
# BC = count (80 bytes = 5 tiles * 16 bytes)
emit([0x21, 0x00, 0x80])  # LD HL, 0x8000
emit([0x11, 0x00, 0x10])  # LD DE, 0x1000
emit([0x01, 80, 0x00])    # LD BC, 80

# copy_loop:
copy_loop_addr = len(code)
emit([0x1A])              # LD A, (DE) - load from source
emit([0x22])              # LD (HL+), A - store to dest, inc HL
emit([0x13])              # INC DE
emit([0x0B])              # DEC BC
emit([0x78])              # LD A, B
emit([0xB1])              # OR C
emit([0x20, 0xF8])        # JR NZ, copy_loop (relative jump back)

# Fill tilemap at 0x9800 with a pattern
# Fill first 20x18 tiles (visible area) with alternating tiles
emit([0x21, 0x00, 0x98])  # LD HL, 0x9800 (tilemap)
emit([0x06, 18])          # LD B, 18 (rows)

# row_loop:
row_loop_addr = len(code)
emit([0x0E, 20])          # LD C, 20 (columns per row)
emit([0x16, 0x00])        # LD D, 0 (tile index for this row)

# col_loop:
col_loop_addr = len(code)
emit([0x7A])              # LD A, D (get tile index)
emit([0xE6, 0x04])        # AND 4 (keep in range 0-4)
emit([0x22])              # LD (HL+), A (store tile index, inc HL)
emit([0x14])              # INC D (next tile type)
emit([0x0D])              # DEC C
emit([0x20, 0xF8])        # JR NZ, col_loop

# Move to next row (skip remaining 12 tiles in the 32-wide tilemap)
emit([0x11, 12, 0x00])    # LD DE, 12
emit([0x19])              # ADD HL, DE
emit([0x05])              # DEC B
emit([0x20, 0xEB])        # JR NZ, row_loop

# Set BGP palette (standard: 11 10 01 00 = 0xE4)
emit([0x3E, 0xE4])        # LD A, 0xE4
emit([0xE0, 0x47])        # LDH (BGP), A - write to 0xFF47

# Set scroll registers to 0
emit([0x3E, 0x00])        # LD A, 0
emit([0xE0, 0x42])        # LDH (SCY), A - write to 0xFF42
emit([0xE0, 0x43])        # LDH (SCX), A - write to 0xFF43

# Turn on LCD with BG enabled (LCDC = 0x91)
# Bit 7: LCD on, Bit 4: BG tile data at 0x8000, Bit 0: BG display on
emit([0x3E, 0x91])        # LD A, 0x91
emit([0xE0, 0x40])        # LDH (LCDC), A

# Main loop: just wait forever
# main_loop:
main_loop_addr = len(code)
# Wait for VBlank
emit([0xF0, 0x44])        # LDH A, (LY)
emit([0xFE, 144])         # CP 144
emit([0x20, 0xFA])        # JR NZ, -6 (wait)

# Now in VBlank - could do updates here
# For now just wait for next frame
# wait_non_vblank:
emit([0xF0, 0x44])        # LDH A, (LY)
emit([0xFE, 144])         # CP 144
emit([0x28, 0xFA])        # JR Z, -6 (wait while still in vblank)

# Loop back to main
emit([0x18, 0xEE])        # JR main_loop

# Write code to ROM
for i, byte in enumerate(code):
    rom[code_offset + i] = byte

print(f"Code size: {len(code)} bytes")

# RST vectors (simple return for each)
for vec in [0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38]:
    rom[vec] = 0xC9  # RET

# Interrupt vectors (RETI for each)
for vec in [0x40, 0x48, 0x50, 0x58, 0x60]:
    rom[vec] = 0xD9  # RETI

# Write ROM file
with open("gfxtest.gb", "wb") as f:
    f.write(rom)

print("Generated gfxtest.gb")
print(f"Tile data at: 0x{tile_data_offset:04X}")
print(f"Code starts at: 0x{code_offset:04X}")
