#include "crystal_overworld.h"

#include "crystal_semantic.h"

#include <string.h>

enum {
    CRYSTAL_MAP_ATTRIBUTE_BANK = 0x25,
    CRYSTAL_NEW_BARK_ATTRIBUTES = 0x4DD8,
    CRYSTAL_ROUTE_29_ATTRIBUTES = 0x4FDC,
    CRYSTAL_NEW_BARK_GROUP = 24,
    CRYSTAL_NEW_BARK_NUMBER = 4,
    CRYSTAL_ROUTE_29_GROUP = 24,
    CRYSTAL_ROUTE_29_NUMBER = 3,
    CRYSTAL_NEW_BARK_WIDTH = 10,
    CRYSTAL_NEW_BARK_HEIGHT = 9,
    CRYSTAL_ROUTE_29_WIDTH = 30,
    CRYSTAL_ROUTE_29_HEIGHT = 9,
    CRYSTAL_OBJECT_BANK = 1,
    CRYSTAL_OBJECT_ADDRESS = 0xD4D6,
    CRYSTAL_OBJECT_LENGTH = 40,
    CRYSTAL_OBJECT_COUNT = 13,
};

static bool semantic_read(
    const GBSemanticReader* reader,
    GBSemanticMemorySpace space,
    uint16_t bank,
    uint16_t address,
    uint8_t* output,
    size_t width) {
    return gbrt_semantic_read(
               reader,
               CRYSTAL_SEMANTIC_ROM_SHA256,
               reader->mode,
               space,
               bank,
               address,
               output,
               width) == GB_SEMANTIC_OK;
}

static bool load_map(
    const GBSemanticReader* reader,
    uint16_t attributes_address,
    uint8_t expected_width,
    uint8_t expected_height,
    uint8_t* blocks,
    size_t block_capacity,
    uint8_t attributes[36]) {
    if (!semantic_read(
            reader,
            GB_SEMANTIC_PHYSICAL_ROM,
            CRYSTAL_MAP_ATTRIBUTE_BANK,
            attributes_address,
            attributes,
            36)) {
        return false;
    }
    if (attributes[1] != expected_height ||
        attributes[2] != expected_width) {
        return false;
    }
    const size_t count = (size_t)expected_width * expected_height;
    if (count > block_capacity) {
        return false;
    }
    const uint16_t pointer =
        (uint16_t)attributes[4] | ((uint16_t)attributes[5] << 8);
    return semantic_read(
        reader,
        GB_SEMANTIC_PHYSICAL_ROM,
        attributes[3],
        pointer,
        blocks,
        count);
}

static uint32_t sprite_color(uint8_t sprite_id) {
    static const uint32_t colors[] = {
        0xF5D76EFFu,
        0xE86A92FFu,
        0x6CC4A1FFu,
        0x7196E5FFu,
        0xF09A54FFu,
        0xA477D4FFu,
    };
    return colors[sprite_id % (sizeof(colors) / sizeof(colors[0]))];
}

CrystalOverworldStatus crystal_overworld_build_new_bark_scene(
    const GBSemanticReader* reader,
    int16_t camera_x,
    int16_t camera_y,
    bool transition_active,
    bool raster_effect_active,
    GBPresentationScene* scene) {
    if (reader == NULL || scene == NULL || reader->read == NULL ||
        reader->abi_version != GB_SEMANTIC_READER_ABI_VERSION) {
        return CRYSTAL_OVERWORLD_INVALID_ARGUMENT;
    }
    if (reader->rom_sha256 == NULL ||
        strcmp(reader->rom_sha256, CRYSTAL_SEMANTIC_ROM_SHA256) != 0) {
        return CRYSTAL_OVERWORLD_ROM_MISMATCH;
    }
    CrystalLocation location = {0};
    if (crystal_semantic_read_location(
            reader, reader->mode, &location) != GB_SEMANTIC_OK) {
        return CRYSTAL_OVERWORLD_READ_FAILED;
    }
    if (location.map_group != CRYSTAL_NEW_BARK_GROUP ||
        location.map_number != CRYSTAL_NEW_BARK_NUMBER) {
        return CRYSTAL_OVERWORLD_UNSUPPORTED_MAP;
    }

    uint8_t new_bark_attributes[36] = {0};
    uint8_t route_29_attributes[36] = {0};
    uint8_t new_bark_blocks[
        CRYSTAL_NEW_BARK_WIDTH * CRYSTAL_NEW_BARK_HEIGHT] = {0};
    uint8_t route_29_blocks[
        CRYSTAL_ROUTE_29_WIDTH * CRYSTAL_ROUTE_29_HEIGHT] = {0};
    if (!load_map(
            reader,
            CRYSTAL_NEW_BARK_ATTRIBUTES,
            CRYSTAL_NEW_BARK_WIDTH,
            CRYSTAL_NEW_BARK_HEIGHT,
            new_bark_blocks,
            sizeof(new_bark_blocks),
            new_bark_attributes) ||
        !load_map(
            reader,
            CRYSTAL_ROUTE_29_ATTRIBUTES,
            CRYSTAL_ROUTE_29_WIDTH,
            CRYSTAL_ROUTE_29_HEIGHT,
            route_29_blocks,
            sizeof(route_29_blocks),
            route_29_attributes)) {
        return CRYSTAL_OVERWORLD_READ_FAILED;
    }
    /*
     * Attribute byte 11 is the connection mask. The first New Bark record is
     * west and must identify Route 29. Its target width is also carried by the
     * original connection record at byte 19.
     */
    if (new_bark_attributes[11] != 0x03 ||
        new_bark_attributes[12] != CRYSTAL_ROUTE_29_GROUP ||
        new_bark_attributes[13] != CRYSTAL_ROUTE_29_NUMBER ||
        new_bark_attributes[19] != CRYSTAL_ROUTE_29_WIDTH ||
        route_29_attributes[11] != 0x0B) {
        return CRYSTAL_OVERWORLD_LAYOUT_MISMATCH;
    }

    memset(scene, 0, sizeof(*scene));
    scene->abi_version = GB_PRESENTATION_ABI_VERSION;
    scene->kind = GB_PRESENTATION_SCENE_OVERWORLD;
    memcpy(
        scene->scene_id,
        "crystal.new-bark-route29",
        sizeof("crystal.new-bark-route29"));
    GBPresentationMapState* map = &scene->map;
    map->valid = true;
    map->transition_active = transition_active;
    map->raster_effect_active = raster_effect_active;
    map->sprites_valid = true;
    map->current_map_group = location.map_group;
    map->current_map_number = location.map_number;
    map->player_x = (int16_t)location.x * 16;
    map->player_y = (int16_t)location.y * 16;
    map->camera_x = camera_x;
    map->camera_y = camera_y;
    map->region_count = 2;
    map->regions[0] = (GBPresentationMapRegion){
        .map_group = CRYSTAL_NEW_BARK_GROUP,
        .map_number = CRYSTAL_NEW_BARK_NUMBER,
        .origin_block_x = 0,
        .origin_block_y = 0,
        .width_blocks = CRYSTAL_NEW_BARK_WIDTH,
        .height_blocks = CRYSTAL_NEW_BARK_HEIGHT,
        .block_offset = 0,
        .block_count = sizeof(new_bark_blocks),
    };
    map->regions[1] = (GBPresentationMapRegion){
        .map_group = CRYSTAL_ROUTE_29_GROUP,
        .map_number = CRYSTAL_ROUTE_29_NUMBER,
        .origin_block_x = -CRYSTAL_ROUTE_29_WIDTH,
        .origin_block_y = 0,
        .width_blocks = CRYSTAL_ROUTE_29_WIDTH,
        .height_blocks = CRYSTAL_ROUTE_29_HEIGHT,
        .block_offset = sizeof(new_bark_blocks),
        .block_count = sizeof(route_29_blocks),
    };
    for (size_t index = 0; index < sizeof(new_bark_blocks); ++index) {
        map->blocks[index] = new_bark_blocks[index];
    }
    for (size_t index = 0; index < sizeof(route_29_blocks); ++index) {
        map->blocks[sizeof(new_bark_blocks) + index] =
            route_29_blocks[index];
    }
    map->block_count = sizeof(new_bark_blocks) + sizeof(route_29_blocks);

    uint8_t objects[CRYSTAL_OBJECT_COUNT * CRYSTAL_OBJECT_LENGTH] = {0};
    if (!semantic_read(
            reader,
            GB_SEMANTIC_BANKED_WRAM,
            CRYSTAL_OBJECT_BANK,
            CRYSTAL_OBJECT_ADDRESS,
            objects,
            sizeof(objects))) {
        return CRYSTAL_OVERWORLD_READ_FAILED;
    }
    for (size_t index = 0; index < CRYSTAL_OBJECT_COUNT; ++index) {
        const uint8_t* object = objects + index * CRYSTAL_OBJECT_LENGTH;
        const uint8_t flags1 = object[4];
        const uint8_t flags2 = object[5];
        if (object[0] == 0 || (flags1 & 0x01u) != 0 ||
            (flags2 & 0x40u) != 0) {
            continue;
        }
        GBPresentationMapSprite* sprite =
            &map->sprites[map->sprite_count++];
        sprite->sprite_id = object[0];
        sprite->world_x = ((int16_t)object[0x10] - 4) * 16;
        sprite->world_y = ((int16_t)object[0x11] - 4) * 16 - 16;
        sprite->width = 16;
        sprite->height = 16;
        sprite->color_rgba = sprite_color(object[0]);
        sprite->visible = true;
        sprite->behind_background = (flags2 & 0x88u) != 0;
        sprite->priority = (flags2 & 0x02u) != 0
            ? GB_PRESENTATION_SPRITE_PRIORITY_HIGH
            : (flags2 & 0x01u) != 0
                ? GB_PRESENTATION_SPRITE_PRIORITY_LOW
                : GB_PRESENTATION_SPRITE_PRIORITY_NORMAL;
    }
    return CRYSTAL_OVERWORLD_OK;
}

GBPresentationWidescreenStyle crystal_overworld_style(void) {
    return (GBPresentationWidescreenStyle){
        .abi_version = GB_PRESENTATION_ABI_VERSION,
        .clear_color_rgba = 0x15242BFFu,
        .block_colors_rgba = {
            0x477A5BFFu,
            0x5C8F68FFu,
            0xA3B86CFFu,
            0x557E9AFFu,
        },
        .grid_color_rgba = 0xD9E6B8FFu,
    };
}
