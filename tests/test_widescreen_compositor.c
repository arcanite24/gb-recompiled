#include "gbrt_presentation.h"

#include <stdint.h>
#include <string.h>

enum {
    WIDTH = 256,
    HEIGHT = 144,
};

static GBPresentationFrame fixture(void) {
    GBPresentationFrame frame = {
        .abi_version = GB_PRESENTATION_ABI_VERSION,
        .scene = {
            .abi_version = GB_PRESENTATION_ABI_VERSION,
            .kind = GB_PRESENTATION_SCENE_OVERWORLD,
            .scene_id = "fixture.connected-overworld",
            .map = {
                .valid = true,
                .sprites_valid = true,
                .current_map_group = 24,
                .current_map_number = 3,
                .camera_x = 64,
                .camera_y = 8,
                .region_count = 2,
                .regions = {
                    {
                        .map_group = 24,
                        .map_number = 3,
                        .origin_block_x = 0,
                        .origin_block_y = 0,
                        .width_blocks = 6,
                        .height_blocks = 5,
                        .block_offset = 0,
                        .block_count = 30,
                    },
                    {
                        .map_group = 24,
                        .map_number = 4,
                        .origin_block_x = 6,
                        .origin_block_y = 0,
                        .width_blocks = 4,
                        .height_blocks = 5,
                        .block_offset = 30,
                        .block_count = 20,
                    },
                },
                .block_count = 50,
                .sprite_count = 2,
                .sprites = {
                    {
                        .sprite_id = 1,
                        .world_x = 100,
                        .world_y = 80,
                        .width = 16,
                        .height = 16,
                        .color_rgba = 0x11223344u,
                        .priority = GB_PRESENTATION_SPRITE_PRIORITY_LOW,
                        .visible = true,
                    },
                    {
                        .sprite_id = 2,
                        .world_x = 100,
                        .world_y = 80,
                        .width = 8,
                        .height = 8,
                        .color_rgba = 0xAABBCCDDu,
                        .priority = GB_PRESENTATION_SPRITE_PRIORITY_HIGH,
                        .visible = true,
                    },
                },
            },
        },
        .hardware = {
            .authority = GB_PRESENTATION_SHADOW_ACCURATE_PPU,
            .lcd_enabled = true,
        },
    };
    for (size_t index = 0; index < frame.scene.map.block_count; ++index) {
        frame.scene.map.blocks[index] = (uint16_t)(index + 1u);
    }
    frame.hardware.accurate_pixels[0] = 0x1234;
    return frame;
}

int main(void) {
    static uint32_t pixels[WIDTH * HEIGHT];
    GBPresentationSurface surface = {
        .pixels = pixels,
        .width = WIDTH,
        .height = HEIGHT,
        .stride_pixels = WIDTH,
    };
    const GBPresentationWidescreenStyle style = {
        .abi_version = GB_PRESENTATION_ABI_VERSION,
        .clear_color_rgba = 0x010101FFu,
        .block_colors_rgba = {
            0x102030FFu,
            0x203040FFu,
            0x304050FFu,
            0x405060FFu,
        },
        .grid_color_rgba = 0xFFFFFFFFu,
    };
    GBPresentationFrame frame = fixture();
    const GBPresentationFrame original = frame;
    if (gbrt_presentation_compose_widescreen(
            &frame, &style, &surface) != GB_PRESENTATION_COMPOSED) {
        return 1;
    }
    if (pixels[(80 - 8) * WIDTH + (100 - 64)] != 0xAABBCCDDu) {
        return 2;
    }
    if (memcmp(&frame, &original, sizeof(frame)) != 0 ||
        frame.hardware.accurate_pixels[0] != 0x1234) {
        return 3;
    }

    frame.scene.map.region_count = 1;
    if (gbrt_presentation_compose_widescreen(
            &frame, &style, &surface) !=
        GB_PRESENTATION_FALLBACK_UNCOVERED_CAMERA) {
        return 4;
    }
    frame = fixture();
    frame.scene.map.camera_x = -1;
    if (gbrt_presentation_compose_widescreen(
            &frame, &style, &surface) !=
        GB_PRESENTATION_FALLBACK_UNCOVERED_CAMERA) {
        return 5;
    }
    frame = fixture();
    frame.scene.map.transition_active = true;
    if (gbrt_presentation_compose_widescreen(
            &frame, &style, &surface) !=
        GB_PRESENTATION_FALLBACK_TRANSITION) {
        return 6;
    }
    frame = fixture();
    frame.scene.map.raster_effect_active = true;
    if (gbrt_presentation_compose_widescreen(
            &frame, &style, &surface) !=
        GB_PRESENTATION_FALLBACK_RASTER_EFFECT) {
        return 7;
    }
    frame = fixture();
    frame.scene.map.sprites[0].behind_background = true;
    if (gbrt_presentation_compose_widescreen(
            &frame, &style, &surface) !=
        GB_PRESENTATION_FALLBACK_UNMODELED_OCCLUSION) {
        return 8;
    }
    frame = fixture();
    frame.hardware.lcd_enabled = false;
    if (gbrt_presentation_compose_widescreen(
            &frame, &style, &surface) !=
        GB_PRESENTATION_FALLBACK_LCD_DISABLED) {
        return 9;
    }
    frame = fixture();
    frame.scene.map.sprites_valid = false;
    if (gbrt_presentation_compose_widescreen(
            &frame, &style, &surface) !=
        GB_PRESENTATION_FALLBACK_INVALID_SCENE) {
        return 10;
    }
    return 0;
}
