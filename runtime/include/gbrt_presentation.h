#ifndef GBRT_PRESENTATION_H
#define GBRT_PRESENTATION_H

#include "gbrt_port.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GB_PRESENTATION_ABI_VERSION 1u
#define GB_PRESENTATION_ROM_SHA256_CAPACITY 65u
#define GB_PRESENTATION_MAX_MAP_REGIONS 9u
#define GB_PRESENTATION_MAX_MAP_BLOCKS 4096u
#define GB_PRESENTATION_MAX_MAP_SPRITES 32u
#define GB_PRESENTATION_MAX_REPLACEMENT_ASSETS 16u
#define GB_PRESENTATION_MAX_ASSET_ID 64u
#define GB_PRESENTATION_MAX_LICENSE_ID 32u
#define GB_PRESENTATION_MAX_SCENE_ID 64u
#define GB_PRESENTATION_TILE_DATA_BYTES 0x1800u
#define GB_PRESENTATION_TILE_MAP_BYTES 0x400u
#define GB_PRESENTATION_SPRITE_COUNT 40u
#define GB_PRESENTATION_ACCURATE_WIDTH 160u
#define GB_PRESENTATION_ACCURATE_HEIGHT 144u
#define GB_PRESENTATION_ACCURATE_PIXEL_COUNT \
    (GB_PRESENTATION_ACCURATE_WIDTH * GB_PRESENTATION_ACCURATE_HEIGHT)

typedef struct GBContext GBContext;

typedef enum GBPresentationStatus {
    GB_PRESENTATION_OK = 0,
    GB_PRESENTATION_INVALID_ARGUMENT,
    GB_PRESENTATION_ABI_MISMATCH,
    GB_PRESENTATION_ROM_MISMATCH,
    GB_PRESENTATION_NOT_AT_FRAME_BOUNDARY,
    GB_PRESENTATION_INVALID_SCENE,
    GB_PRESENTATION_INVALID_UI,
    GB_PRESENTATION_INVALID_ASSET,
} GBPresentationStatus;

typedef enum GBPresentationSceneKind {
    GB_PRESENTATION_SCENE_UNKNOWN = 0,
    GB_PRESENTATION_SCENE_OVERWORLD = 1,
    GB_PRESENTATION_SCENE_BATTLE = 2,
    GB_PRESENTATION_SCENE_MENU = 3,
} GBPresentationSceneKind;

typedef enum GBPresentationShadowAuthority {
    /*
     * The ordinary PPU continues to execute every dot and remains the sole
     * source of guest-visible registers, VRAM/OAM arbitration, DMA behavior,
     * interrupts, and accurate 160x144 pixels.
     */
    GB_PRESENTATION_SHADOW_ACCURATE_PPU = 1,
} GBPresentationShadowAuthority;

typedef struct GBPresentationConfig {
    uint32_t abi_version;
    const char* rom_sha256;
    size_t rom_size;
    bool headless;
} GBPresentationConfig;

typedef struct GBPresentationSource {
    uint32_t abi_version;
    const GBContext* context;
    char rom_sha256[GB_PRESENTATION_ROM_SHA256_CAPACITY];
    size_t rom_size;
    bool headless;
    bool active;
} GBPresentationSource;

typedef struct GBPresentationMapRegion {
    uint16_t map_group;
    uint16_t map_number;
    int16_t origin_block_x;
    int16_t origin_block_y;
    uint16_t width_blocks;
    uint16_t height_blocks;
    uint32_t block_offset;
    uint32_t block_count;
} GBPresentationMapRegion;

typedef enum GBPresentationMapSpritePriority {
    GB_PRESENTATION_SPRITE_PRIORITY_LOW = 0,
    GB_PRESENTATION_SPRITE_PRIORITY_NORMAL = 1,
    GB_PRESENTATION_SPRITE_PRIORITY_HIGH = 2,
} GBPresentationMapSpritePriority;

/*
 * Renderer-independent semantic sprite state. Coordinates are world pixels
 * in the same origin space as map regions (one block is 32 pixels).
 * `behind_background` requires a scene-specific occlusion mask; the generic
 * compositor therefore falls back rather than guessing.
 */
typedef struct GBPresentationMapSprite {
    uint16_t sprite_id;
    int16_t world_x;
    int16_t world_y;
    uint8_t width;
    uint8_t height;
    uint32_t color_rgba;
    GBPresentationMapSpritePriority priority;
    bool visible;
    bool behind_background;
} GBPresentationMapSprite;

typedef struct GBPresentationMapState {
    bool valid;
    bool transition_active;
    bool raster_effect_active;
    bool sprites_valid;
    uint16_t current_map_group;
    uint16_t current_map_number;
    int16_t player_x;
    int16_t player_y;
    int16_t camera_x;
    int16_t camera_y;
    size_t region_count;
    GBPresentationMapRegion regions[GB_PRESENTATION_MAX_MAP_REGIONS];
    size_t block_count;
    uint16_t blocks[GB_PRESENTATION_MAX_MAP_BLOCKS];
    size_t sprite_count;
    GBPresentationMapSprite sprites[GB_PRESENTATION_MAX_MAP_SPRITES];
} GBPresentationMapState;

typedef struct GBPresentationBattleState {
    bool valid;
    uint16_t player_species;
    uint16_t enemy_species;
    uint16_t player_level;
    uint16_t enemy_level;
    uint32_t phase;
} GBPresentationBattleState;

/*
 * Game-specific semantic state submitted at a completed-frame safepoint.
 * The runtime validates bounds and copies it; it does not infer game meaning
 * from raw WRAM or generated symbol names.
 */
typedef struct GBPresentationScene {
    uint32_t abi_version;
    GBPresentationSceneKind kind;
    char scene_id[GB_PRESENTATION_MAX_SCENE_ID];
    GBPresentationMapState map;
    GBPresentationBattleState battle;
} GBPresentationScene;

typedef struct GBPresentationSprite {
    uint8_t oam_index;
    uint8_t raw_y;
    uint8_t raw_x;
    uint8_t tile;
    uint8_t attributes;
    int16_t screen_y;
    int16_t screen_x;
    uint8_t height;
    uint8_t vram_bank;
    uint8_t palette;
    bool flip_x;
    bool flip_y;
    bool behind_background;
} GBPresentationSprite;

typedef struct GBPresentationHardwareSnapshot {
    GBPresentationShadowAuthority authority;
    bool guest_writeback_allowed;
    bool lcd_enabled;
    uint8_t lcdc;
    uint8_t stat;
    uint8_t scy;
    uint8_t scx;
    uint8_t ly;
    uint8_t lyc;
    uint8_t wy;
    uint8_t wx;
    uint8_t mode;
    uint32_t mode_cycles;
    uint16_t mode3_length;
    uint16_t hblank_length;
    uint16_t draw_x;
    uint8_t tile_data[2][GB_PRESENTATION_TILE_DATA_BYTES];
    uint8_t tile_ids[2][GB_PRESENTATION_TILE_MAP_BYTES];
    uint8_t tile_attributes[2][GB_PRESENTATION_TILE_MAP_BYTES];
    GBPresentationSprite sprites[GB_PRESENTATION_SPRITE_COUNT];
    uint8_t background_palettes[0x40];
    uint8_t object_palettes[0x40];
    uint16_t accurate_pixels[GB_PRESENTATION_ACCURATE_PIXEL_COUNT];
} GBPresentationHardwareSnapshot;

typedef struct GBPresentationTiming {
    uint64_t completed_frames;
    uint64_t total_cycles;
    uint32_t frame_cycles;
    bool cgb_double_speed;
    uint8_t scanline;
    uint8_t ppu_mode;
    uint32_t mode_cycles;
} GBPresentationTiming;

typedef struct GBPresentationFrame {
    uint32_t abi_version;
    char rom_sha256[GB_PRESENTATION_ROM_SHA256_CAPACITY];
    size_t rom_size;
    bool headless;
    GBPresentationScene scene;
    GBPortFrame ui;
    GBPresentationTiming timing;
    GBPresentationHardwareSnapshot hardware;
} GBPresentationFrame;

typedef enum GBPresentationComposeResult {
    GB_PRESENTATION_COMPOSED = 0,
    GB_PRESENTATION_FALLBACK_INVALID_SCENE,
    GB_PRESENTATION_FALLBACK_TRANSITION,
    GB_PRESENTATION_FALLBACK_RASTER_EFFECT,
    GB_PRESENTATION_FALLBACK_LCD_DISABLED,
    GB_PRESENTATION_FALLBACK_UNCOVERED_CAMERA,
    GB_PRESENTATION_FALLBACK_UNMODELED_OCCLUSION,
} GBPresentationComposeResult;

typedef struct GBPresentationSurface {
    uint32_t* pixels;
    uint32_t width;
    uint32_t height;
    size_t stride_pixels;
} GBPresentationSurface;

typedef struct GBPresentationWidescreenStyle {
    uint32_t abi_version;
    uint32_t clear_color_rgba;
    uint32_t block_colors_rgba[4];
    uint32_t grid_color_rgba;
} GBPresentationWidescreenStyle;

typedef enum GBPresentationMode {
    GB_PRESENTATION_MODE_ORIGINAL = 0,
    GB_PRESENTATION_MODE_NATIVE = 1,
} GBPresentationMode;

typedef struct GBPresentationReplacementAsset {
    char asset_id[GB_PRESENTATION_MAX_ASSET_ID];
    char sha256[GB_PRESENTATION_ROM_SHA256_CAPACITY];
    char license_spdx[GB_PRESENTATION_MAX_LICENSE_ID];
    const uint8_t* data;
    size_t data_size;
} GBPresentationReplacementAsset;

typedef struct GBPresentationReplacementConfig {
    uint32_t abi_version;
    GBPresentationMode mode;
    char rom_sha256[GB_PRESENTATION_ROM_SHA256_CAPACITY];
    uint32_t output_width;
    uint32_t output_height;
    uint32_t effect_seed;
    size_t asset_count;
    GBPresentationReplacementAsset
        assets[GB_PRESENTATION_MAX_REPLACEMENT_ASSETS];
} GBPresentationReplacementConfig;

GBPresentationStatus gbrt_presentation_source_init(
    GBPresentationSource* source,
    const GBContext* context,
    const GBPresentationConfig* config);

GBPresentationStatus gbrt_presentation_capture(
    const GBPresentationSource* source,
    const GBPresentationScene* scene,
    const GBPortFrame* ui,
    GBPresentationFrame* output);

/*
 * Deterministic CPU compositor used by headless tests and as a graphics-API
 * neutral reference implementation. It never mutates the captured frame or
 * guest state. The procedural block style is project-owned; ROM-derived block
 * IDs and semantic sprites are inputs, not distributed assets.
 */
GBPresentationComposeResult gbrt_presentation_compose_widescreen(
    const GBPresentationFrame* frame,
    const GBPresentationWidescreenStyle* style,
    GBPresentationSurface* surface);

GBPresentationStatus gbrt_presentation_validate_replacements(
    const GBPresentationReplacementConfig* config,
    const char* expected_rom_sha256);

#ifdef __cplusplus
}
#endif

#endif
