#include "gbrt_presentation.h"

#include "gbrt.h"
#include "gbrt_hash.h"
#include "ppu.h"

#include <string.h>

static bool scene_valid(const GBPresentationScene* scene) {
    if (scene == NULL ||
        scene->abi_version != GB_PRESENTATION_ABI_VERSION ||
        scene->kind < GB_PRESENTATION_SCENE_UNKNOWN ||
        scene->kind > GB_PRESENTATION_SCENE_MENU ||
        memchr(scene->scene_id, '\0', sizeof(scene->scene_id)) == NULL ||
        scene->map.region_count > GB_PRESENTATION_MAX_MAP_REGIONS ||
        scene->map.block_count > GB_PRESENTATION_MAX_MAP_BLOCKS ||
        scene->map.sprite_count > GB_PRESENTATION_MAX_MAP_SPRITES) {
        return false;
    }
    for (size_t index = 0; index < scene->map.sprite_count; ++index) {
        const GBPresentationMapSprite* sprite = &scene->map.sprites[index];
        if (sprite->width == 0 || sprite->height == 0 ||
            sprite->priority > GB_PRESENTATION_SPRITE_PRIORITY_HIGH) {
            return false;
        }
    }
    for (size_t index = 0; index < scene->map.region_count; ++index) {
        const GBPresentationMapRegion* region = &scene->map.regions[index];
        if (region->width_blocks == 0 || region->height_blocks == 0 ||
            region->block_count !=
                (uint32_t)region->width_blocks * region->height_blocks ||
            region->block_offset > scene->map.block_count ||
            region->block_count >
                scene->map.block_count - region->block_offset) {
            return false;
        }
    }
    if (scene->kind == GB_PRESENTATION_SCENE_OVERWORLD &&
        !scene->map.valid) {
        return false;
    }
    if (scene->kind == GB_PRESENTATION_SCENE_BATTLE &&
        !scene->battle.valid) {
        return false;
    }
    return true;
}

static bool ui_valid(const GBPortFrame* ui) {
    return ui != NULL && ui->abi_version == GB_PORT_ABI_VERSION &&
           ui->canvas_width > 0 && ui->canvas_height > 0 &&
           ui->command_count <= GB_PORT_MAX_DRAW_COMMANDS;
}

GBPresentationStatus gbrt_presentation_source_init(
    GBPresentationSource* source,
    const GBContext* context,
    const GBPresentationConfig* config) {
    if (source == NULL || context == NULL || config == NULL ||
        config->rom_sha256 == NULL || context->rom == NULL ||
        context->vram == NULL || context->oam == NULL ||
        context->ppu == NULL || context->io == NULL) {
        return GB_PRESENTATION_INVALID_ARGUMENT;
    }
    memset(source, 0, sizeof(*source));
    if (config->abi_version != GB_PRESENTATION_ABI_VERSION) {
        return GB_PRESENTATION_ABI_MISMATCH;
    }
    const size_t hash_length = strlen(config->rom_sha256);
    if (hash_length != 64 || config->rom_size != context->rom_size ||
        !gbrt_sha256_matches_hex(
            context->rom, context->rom_size, config->rom_sha256)) {
        return GB_PRESENTATION_ROM_MISMATCH;
    }
    source->abi_version = GB_PRESENTATION_ABI_VERSION;
    source->context = context;
    memcpy(source->rom_sha256, config->rom_sha256, hash_length + 1);
    source->rom_size = config->rom_size;
    source->headless = config->headless;
    source->active = true;
    return GB_PRESENTATION_OK;
}

static bool surface_valid(const GBPresentationSurface* surface) {
    return surface != NULL && surface->pixels != NULL &&
        surface->width > GB_PRESENTATION_ACCURATE_WIDTH &&
        surface->height >= GB_PRESENTATION_ACCURATE_HEIGHT &&
        surface->stride_pixels >= surface->width;
}

static void fill_rect(
    GBPresentationSurface* surface,
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height,
    uint32_t color) {
    const int32_t left = x < 0 ? 0 : x;
    const int32_t top = y < 0 ? 0 : y;
    const int32_t right =
        x + width > (int32_t)surface->width
        ? (int32_t)surface->width
        : x + width;
    const int32_t bottom =
        y + height > (int32_t)surface->height
        ? (int32_t)surface->height
        : y + height;
    for (int32_t row = top; row < bottom; ++row) {
        uint32_t* pixels =
            surface->pixels + (size_t)row * surface->stride_pixels;
        for (int32_t column = left; column < right; ++column) {
            pixels[column] = color;
        }
    }
}

static const GBPresentationMapRegion* region_at(
    const GBPresentationMapState* map,
    int32_t block_x,
    int32_t block_y,
    uint16_t* block_id) {
    for (size_t index = 0; index < map->region_count; ++index) {
        const GBPresentationMapRegion* region = &map->regions[index];
        const int32_t local_x = block_x - region->origin_block_x;
        const int32_t local_y = block_y - region->origin_block_y;
        if (local_x < 0 || local_y < 0 ||
            local_x >= region->width_blocks ||
            local_y >= region->height_blocks) {
            continue;
        }
        const size_t offset = region->block_offset +
            (size_t)local_y * region->width_blocks + (size_t)local_x;
        if (offset >= map->block_count) {
            return NULL;
        }
        *block_id = map->blocks[offset];
        return region;
    }
    return NULL;
}

GBPresentationComposeResult gbrt_presentation_compose_widescreen(
    const GBPresentationFrame* frame,
    const GBPresentationWidescreenStyle* style,
    GBPresentationSurface* surface) {
    if (frame == NULL || style == NULL || !surface_valid(surface) ||
        frame->abi_version != GB_PRESENTATION_ABI_VERSION ||
        style->abi_version != GB_PRESENTATION_ABI_VERSION ||
        frame->scene.kind != GB_PRESENTATION_SCENE_OVERWORLD ||
        !scene_valid(&frame->scene) || !frame->scene.map.sprites_valid) {
        return GB_PRESENTATION_FALLBACK_INVALID_SCENE;
    }
    const GBPresentationMapState* map = &frame->scene.map;
    if (map->transition_active) {
        return GB_PRESENTATION_FALLBACK_TRANSITION;
    }
    if (map->raster_effect_active) {
        return GB_PRESENTATION_FALLBACK_RASTER_EFFECT;
    }
    if (!frame->hardware.lcd_enabled) {
        return GB_PRESENTATION_FALLBACK_LCD_DISABLED;
    }
    for (size_t index = 0; index < map->sprite_count; ++index) {
        if (map->sprites[index].visible &&
            map->sprites[index].behind_background) {
            return GB_PRESENTATION_FALLBACK_UNMODELED_OCCLUSION;
        }
    }

    const int32_t camera_x = map->camera_x;
    const int32_t camera_y = map->camera_y;
    const int32_t first_block_x =
        camera_x >= 0 ? camera_x / 32 : (camera_x - 31) / 32;
    const int32_t first_block_y =
        camera_y >= 0 ? camera_y / 32 : (camera_y - 31) / 32;
    const int32_t last_pixel_x =
        camera_x + (int32_t)surface->width - 1;
    const int32_t last_pixel_y =
        camera_y + (int32_t)surface->height - 1;
    const int32_t last_block_x =
        last_pixel_x >= 0
        ? last_pixel_x / 32
        : (last_pixel_x - 31) / 32;
    const int32_t last_block_y =
        last_pixel_y >= 0
        ? last_pixel_y / 32
        : (last_pixel_y - 31) / 32;
    for (int32_t y = first_block_y; y <= last_block_y; ++y) {
        for (int32_t x = first_block_x; x <= last_block_x; ++x) {
            uint16_t block_id = 0;
            if (region_at(map, x, y, &block_id) == NULL) {
                return GB_PRESENTATION_FALLBACK_UNCOVERED_CAMERA;
            }
        }
    }

    fill_rect(
        surface,
        0,
        0,
        (int32_t)surface->width,
        (int32_t)surface->height,
        style->clear_color_rgba);
    for (int32_t y = first_block_y; y <= last_block_y; ++y) {
        for (int32_t x = first_block_x; x <= last_block_x; ++x) {
            uint16_t block_id = 0;
            (void)region_at(map, x, y, &block_id);
            const int32_t screen_x = x * 32 - camera_x;
            const int32_t screen_y = y * 32 - camera_y;
            fill_rect(
                surface,
                screen_x,
                screen_y,
                32,
                32,
                style->block_colors_rgba[block_id & 3u]);
            for (int tile = 0; tile < 4; ++tile) {
                if ((block_id & (1u << (tile + 2))) != 0) {
                    fill_rect(
                        surface,
                        screen_x + (tile & 1) * 16 + 4,
                        screen_y + (tile >> 1) * 16 + 4,
                        8,
                        8,
                        style->grid_color_rgba);
                }
            }
        }
    }
    for (int priority = GB_PRESENTATION_SPRITE_PRIORITY_LOW;
         priority <= GB_PRESENTATION_SPRITE_PRIORITY_HIGH;
         ++priority) {
        for (size_t index = 0; index < map->sprite_count; ++index) {
            const GBPresentationMapSprite* sprite = &map->sprites[index];
            if (!sprite->visible || (int)sprite->priority != priority) {
                continue;
            }
            fill_rect(
                surface,
                sprite->world_x - camera_x,
                sprite->world_y - camera_y,
                sprite->width,
                sprite->height,
                sprite->color_rgba);
        }
    }
    return GB_PRESENTATION_COMPOSED;
}

GBPresentationStatus gbrt_presentation_validate_replacements(
    const GBPresentationReplacementConfig* config,
    const char* expected_rom_sha256) {
    if (config == NULL || expected_rom_sha256 == NULL) {
        return GB_PRESENTATION_INVALID_ARGUMENT;
    }
    if (config->abi_version != GB_PRESENTATION_ABI_VERSION) {
        return GB_PRESENTATION_ABI_MISMATCH;
    }
    if (strlen(expected_rom_sha256) != 64 ||
        memchr(config->rom_sha256, '\0', sizeof(config->rom_sha256)) == NULL ||
        strcmp(config->rom_sha256, expected_rom_sha256) != 0) {
        return GB_PRESENTATION_ROM_MISMATCH;
    }
    if (config->mode < GB_PRESENTATION_MODE_ORIGINAL ||
        config->mode > GB_PRESENTATION_MODE_NATIVE ||
        config->output_width == 0 || config->output_height == 0 ||
        config->asset_count > GB_PRESENTATION_MAX_REPLACEMENT_ASSETS ||
        (config->mode == GB_PRESENTATION_MODE_NATIVE &&
         config->asset_count == 0)) {
        return GB_PRESENTATION_INVALID_ASSET;
    }
    for (size_t index = 0; index < config->asset_count; ++index) {
        const GBPresentationReplacementAsset* asset =
            &config->assets[index];
        if (asset->data == NULL || asset->data_size == 0 ||
            memchr(asset->asset_id, '\0', sizeof(asset->asset_id)) == NULL ||
            asset->asset_id[0] == '\0' ||
            memchr(
                asset->license_spdx,
                '\0',
                sizeof(asset->license_spdx)) == NULL ||
            asset->license_spdx[0] == '\0' ||
            memchr(asset->sha256, '\0', sizeof(asset->sha256)) == NULL ||
            !gbrt_sha256_matches_hex(
                asset->data, asset->data_size, asset->sha256)) {
            return GB_PRESENTATION_INVALID_ASSET;
        }
        for (size_t prior = 0; prior < index; ++prior) {
            if (strcmp(
                    asset->asset_id,
                    config->assets[prior].asset_id) == 0) {
                return GB_PRESENTATION_INVALID_ASSET;
            }
        }
    }
    return GB_PRESENTATION_OK;
}

GBPresentationStatus gbrt_presentation_capture(
    const GBPresentationSource* source,
    const GBPresentationScene* scene,
    const GBPortFrame* ui,
    GBPresentationFrame* output) {
    if (source == NULL || output == NULL || !source->active ||
        source->context == NULL) {
        return GB_PRESENTATION_INVALID_ARGUMENT;
    }
    if (source->abi_version != GB_PRESENTATION_ABI_VERSION) {
        return GB_PRESENTATION_ABI_MISMATCH;
    }
    const GBContext* context = source->context;
    if (context->rom == NULL || context->rom_size != source->rom_size ||
        !gbrt_sha256_matches_hex(
            context->rom, context->rom_size, source->rom_sha256)) {
        return GB_PRESENTATION_ROM_MISMATCH;
    }
    if (!context->frame_done) {
        return GB_PRESENTATION_NOT_AT_FRAME_BOUNDARY;
    }
    if (!scene_valid(scene)) {
        return scene != NULL &&
                       scene->abi_version != GB_PRESENTATION_ABI_VERSION
            ? GB_PRESENTATION_ABI_MISMATCH
            : GB_PRESENTATION_INVALID_SCENE;
    }
    if (!ui_valid(ui)) {
        return ui != NULL && ui->abi_version != GB_PORT_ABI_VERSION
            ? GB_PRESENTATION_ABI_MISMATCH
            : GB_PRESENTATION_INVALID_UI;
    }
    const GBPPU* ppu = (const GBPPU*)context->ppu;
    memset(output, 0, sizeof(*output));
    output->abi_version = GB_PRESENTATION_ABI_VERSION;
    memcpy(output->rom_sha256, source->rom_sha256, sizeof(output->rom_sha256));
    output->rom_size = source->rom_size;
    output->headless = source->headless;
    output->scene = *scene;
    output->ui = *ui;
    output->timing = (GBPresentationTiming){
        .completed_frames = context->completed_frames,
        .total_cycles = context->total_cycles,
        .frame_cycles = context->frame_cycles,
        .cgb_double_speed = context->cgb_double_speed != 0,
        .scanline = ppu->scanline,
        .ppu_mode = (uint8_t)ppu->mode,
        .mode_cycles = ppu->mode_cycles,
    };
    GBPresentationHardwareSnapshot* hardware = &output->hardware;
    hardware->authority = GB_PRESENTATION_SHADOW_ACCURATE_PPU;
    hardware->guest_writeback_allowed = false;
    hardware->lcd_enabled = (ppu->lcdc & LCDC_LCD_ENABLE) != 0;
    hardware->lcdc = ppu->lcdc;
    hardware->stat = ppu->stat;
    hardware->scy = ppu->scy;
    hardware->scx = ppu->scx;
    hardware->ly = ppu->ly;
    hardware->lyc = ppu->lyc;
    hardware->wy = ppu->wy;
    hardware->wx = ppu->wx;
    hardware->mode = (uint8_t)ppu->mode;
    hardware->mode_cycles = ppu->mode_cycles;
    hardware->mode3_length = ppu->mode3_length;
    hardware->hblank_length = ppu->hblank_length;
    hardware->draw_x = ppu->draw_x;
    for (size_t bank = 0; bank < 2; ++bank) {
        const uint8_t* vram = context->vram + bank * VRAM_SIZE;
        memcpy(
            hardware->tile_data[bank],
            vram,
            GB_PRESENTATION_TILE_DATA_BYTES);
    }
    for (size_t map_index = 0; map_index < 2; ++map_index) {
        memcpy(
            hardware->tile_ids[map_index],
            context->vram + 0x1800u + map_index * 0x400u,
            GB_PRESENTATION_TILE_MAP_BYTES);
        memcpy(
            hardware->tile_attributes[map_index],
            context->vram + VRAM_SIZE + 0x1800u + map_index * 0x400u,
            GB_PRESENTATION_TILE_MAP_BYTES);
    }
    const uint8_t sprite_height =
        (ppu->lcdc & LCDC_OBJ_SIZE) != 0 ? 16u : 8u;
    for (size_t index = 0; index < GB_PRESENTATION_SPRITE_COUNT; ++index) {
        const size_t offset = index * 4u;
        const uint8_t raw_y = context->oam[offset];
        const uint8_t raw_x = context->oam[offset + 1u];
        const uint8_t attributes = context->oam[offset + 3u];
        hardware->sprites[index] = (GBPresentationSprite){
            .oam_index = (uint8_t)index,
            .raw_y = raw_y,
            .raw_x = raw_x,
            .tile = context->oam[offset + 2u],
            .attributes = attributes,
            .screen_y = (int16_t)raw_y - 16,
            .screen_x = (int16_t)raw_x - 8,
            .height = sprite_height,
            .vram_bank = (attributes & OAM_CGB_BANK) != 0,
            .palette = attributes & OAM_CGB_PALETTE,
            .flip_x = (attributes & OAM_FLIP_X) != 0,
            .flip_y = (attributes & OAM_FLIP_Y) != 0,
            .behind_background = (attributes & OAM_PRIORITY) != 0,
        };
    }
    memcpy(
        hardware->background_palettes,
        ppu->bg_palette_ram,
        sizeof(hardware->background_palettes));
    memcpy(
        hardware->object_palettes,
        ppu->obj_palette_ram,
        sizeof(hardware->object_palettes));
    memcpy(
        hardware->accurate_pixels,
        ppu->color_framebuffer,
        sizeof(hardware->accurate_pixels));
    return GB_PRESENTATION_OK;
}
