/**
 * @file gbrt_debug.h
 * @brief Debug logging infrastructure for GameBoy runtime
 * 
 * Enable debug logging by defining GB_DEBUG before including this header,
 * or define specific debug categories:
 *   GB_DEBUG_PPU    - PPU state and rendering
 *   GB_DEBUG_MEM    - Memory reads/writes (verbose!)
 *   GB_DEBUG_VRAM   - VRAM writes for tile/tilemap changes
 *   GB_DEBUG_FRAME  - Frame rendering events
 *   GB_DEBUG_REGS   - LCD register changes
 *   GB_DEBUG_ALL    - Enable everything
 */

#ifndef GBRT_DEBUG_H
#define GBRT_DEBUG_H

#include <stdio.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Debug Configuration
 * ========================================================================== */

/* Uncomment to enable specific debug categories */
/* All debug disabled for performance in cpu_instrs testing */
// #define GB_DEBUG_PPU
// #define GB_DEBUG_MEM  
// #define GB_DEBUG_VRAM
// #define GB_DEBUG_FRAME
// #define GB_DEBUG_REGS
// #define GB_DEBUG_ALL

#ifdef GB_DEBUG_ALL
#define GB_DEBUG_PPU
#define GB_DEBUG_MEM
#define GB_DEBUG_VRAM
#define GB_DEBUG_FRAME
#define GB_DEBUG_REGS
#endif

#ifdef GB_DEBUG
#define GB_DEBUG_PPU
#define GB_DEBUG_VRAM
#define GB_DEBUG_FRAME
#define GB_DEBUG_REGS
#endif

/* ============================================================================
 * Debug Logging Macros
 * ========================================================================== */

#ifdef GB_DEBUG_PPU
#define DBG_PPU(fmt, ...) fprintf(stderr, "[PPU] " fmt "\n", ##__VA_ARGS__)
#else
#define DBG_PPU(fmt, ...) ((void)0)
#endif

#ifdef GB_DEBUG_MEM
#define DBG_MEM(fmt, ...) fprintf(stderr, "[MEM] " fmt "\n", ##__VA_ARGS__)
#else
#define DBG_MEM(fmt, ...) ((void)0)
#endif

#ifdef GB_DEBUG_VRAM
#define DBG_VRAM(fmt, ...) fprintf(stderr, "[VRAM] " fmt "\n", ##__VA_ARGS__)
#else
#define DBG_VRAM(fmt, ...) ((void)0)
#endif

#ifdef GB_DEBUG_FRAME
#define DBG_FRAME(fmt, ...) fprintf(stderr, "[FRAME] " fmt "\n", ##__VA_ARGS__)
#else
#define DBG_FRAME(fmt, ...) ((void)0)
#endif

#ifdef GB_DEBUG_REGS
#define DBG_REGS(fmt, ...) fprintf(stderr, "[REGS] " fmt "\n", ##__VA_ARGS__)
#else
#define DBG_REGS(fmt, ...) ((void)0)
#endif

/* General debug (always available, can be disabled at compile time) */
#if defined(GB_DEBUG) || defined(GB_DEBUG_PPU) || defined(GB_DEBUG_VRAM) || \
    defined(GB_DEBUG_FRAME) || defined(GB_DEBUG_REGS) || defined(GB_DEBUG_MEM)
#define DBG_GENERAL(fmt, ...) fprintf(stderr, "[GB] " fmt "\n", ##__VA_ARGS__)
#else
#define DBG_GENERAL(fmt, ...) ((void)0)
#endif

/* ============================================================================
 * Debug Helper Functions
 * ========================================================================== */

/**
 * @brief Get PPU mode name for debugging
 */
static inline const char* ppu_mode_name(int mode) {
    switch (mode) {
        case 0: return "HBLANK";
        case 1: return "VBLANK";
        case 2: return "OAM";
        case 3: return "DRAW";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Dump first few bytes of VRAM tile data
 */
static inline void dbg_dump_vram_tiles(const uint8_t* vram, int count) {
#ifdef GB_DEBUG_VRAM
    fprintf(stderr, "[VRAM] First %d tile bytes: ", count);
    for (int i = 0; i < count && i < 64; i++) {
        fprintf(stderr, "%02X ", vram[i]);
    }
    fprintf(stderr, "\n");
#else
    (void)vram; (void)count;
#endif
}

/**
 * @brief Dump tilemap entries
 */
static inline void dbg_dump_tilemap(const uint8_t* vram, int tilemap_offset, int count) {
#ifdef GB_DEBUG_VRAM
    fprintf(stderr, "[VRAM] Tilemap at 0x%04X: ", 0x8000 + tilemap_offset);
    for (int i = 0; i < count && i < 32; i++) {
        fprintf(stderr, "%02X ", vram[tilemap_offset + i]);
    }
    fprintf(stderr, "\n");
#else
    (void)vram; (void)tilemap_offset; (void)count;
#endif
}

/**
 * @brief Dump framebuffer pixel data sample
 */
static inline void dbg_dump_framebuffer(const uint8_t* fb, int width) {
#ifdef GB_DEBUG_FRAME
    fprintf(stderr, "[FRAME] First scanline samples: ");
    for (int i = 0; i < width && i < 20; i++) {
        fprintf(stderr, "%d ", fb[i]);
    }
    fprintf(stderr, "\n");
#else
    (void)fb; (void)width;
#endif
}

/**
 * @brief Check if any non-zero pixels in framebuffer
 */
static inline bool dbg_has_nonzero_pixels(const uint8_t* fb, int size) {
    for (int i = 0; i < size; i++) {
        if (fb[i] != 0) return true;
    }
    return false;
}

/**
 * @brief Check if VRAM has tile data
 */
static inline bool dbg_has_tile_data(const uint8_t* vram, int size) {
    for (int i = 0; i < size && i < 0x1800; i++) {  /* Tile data area */
        if (vram[i] != 0) return true;
    }
    return false;
}

#ifdef __cplusplus
}
#endif

#endif /* GBRT_DEBUG_H */
