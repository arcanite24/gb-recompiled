#include "gbrt.h"
#include "gbrt_presentation.h"
#include "ppu.h"

#include <stdint.h>
#include <string.h>

static GBPresentationScene overworld_scene(void) {
    GBPresentationScene scene = {
        .abi_version = GB_PRESENTATION_ABI_VERSION,
        .kind = GB_PRESENTATION_SCENE_OVERWORLD,
        .scene_id = "crystal.overworld",
        .map = {
            .valid = true,
            .current_map_group = 24,
            .current_map_number = 3,
            .player_x = 45,
            .player_y = 12,
            .camera_x = 40,
            .camera_y = 8,
            .region_count = 2,
            .block_count = 3,
            .regions = {
                {
                    .map_group = 24,
                    .map_number = 3,
                    .origin_block_x = 0,
                    .origin_block_y = 0,
                    .width_blocks = 2,
                    .height_blocks = 1,
                    .block_offset = 0,
                    .block_count = 2,
                },
                {
                    .map_group = 24,
                    .map_number = 4,
                    .origin_block_x = 2,
                    .origin_block_y = 0,
                    .width_blocks = 1,
                    .height_blocks = 1,
                    .block_offset = 2,
                    .block_count = 1,
                },
            },
            .blocks = {10, 11, 12},
        },
    };
    return scene;
}

int main(void) {
    static const char* hash =
        "9f64a747e1b97f131fabb6b447296c9b6f0201e79fb3c5356e6c77e89b6a806a";
    uint8_t rom[4] = {1, 2, 3, 4};
    uint8_t vram[VRAM_SIZE * 2u] = {0};
    uint8_t oam[OAM_SIZE] = {0};
    uint8_t io[0x80] = {0};
    GBPPU ppu = {0};
    GBContext context = {
        .rom = rom,
        .rom_size = sizeof(rom),
        .vram = vram,
        .oam = oam,
        .io = io,
        .ppu = &ppu,
        .frame_done = 1,
        .completed_frames = 7,
        .total_cycles = 491568,
        .frame_cycles = 70224,
        .cgb_double_speed = 1,
    };
    ppu.lcdc = LCDC_LCD_ENABLE | LCDC_OBJ_SIZE;
    ppu.stat = PPU_MODE_VBLANK;
    ppu.mode = PPU_MODE_VBLANK;
    ppu.scanline = 144;
    ppu.ly = 144;
    ppu.mode_cycles = 12;
    ppu.mode3_length = 180;
    ppu.hblank_length = 196;
    ppu.draw_x = 160;
    vram[0] = 0x11;
    vram[VRAM_SIZE] = 0x22;
    vram[0x1800] = 0x31;
    vram[0x1C00] = 0x32;
    vram[VRAM_SIZE + 0x1800] = 0x41;
    vram[VRAM_SIZE + 0x1C00] = 0x42;
    oam[0] = 20;
    oam[1] = 16;
    oam[2] = 9;
    oam[3] = OAM_CGB_BANK | OAM_FLIP_X | OAM_PRIORITY | 3;
    ppu.bg_palette_ram[0] = 0x55;
    ppu.obj_palette_ram[0] = 0x66;
    ppu.color_framebuffer[0] = 0x1234;

    GBPresentationConfig config = {
        .abi_version = GB_PRESENTATION_ABI_VERSION,
        .rom_sha256 = hash,
        .rom_size = sizeof(rom),
        .headless = true,
    };
    GBPresentationSource source = {0};
    config.abi_version++;
    if (gbrt_presentation_source_init(&source, &context, &config) !=
        GB_PRESENTATION_ABI_MISMATCH) {
        return 1;
    }
    config.abi_version = GB_PRESENTATION_ABI_VERSION;
    config.rom_sha256 = "0000000000000000000000000000000000000000000000000000000000000000";
    if (gbrt_presentation_source_init(&source, &context, &config) !=
        GB_PRESENTATION_ROM_MISMATCH) {
        return 2;
    }
    config.rom_sha256 = hash;
    if (gbrt_presentation_source_init(&source, &context, &config) !=
        GB_PRESENTATION_OK) {
        return 3;
    }

    GBPresentationScene scene = overworld_scene();
    GBPortFrame ui = {
        .abi_version = GB_PORT_ABI_VERSION,
        .canvas_width = 1280,
        .canvas_height = 720,
    };
    if (!gbrt_port_frame_text(&ui, 10, 20, 0xFFFFFFFFu, "native-ui")) {
        return 4;
    }
    static GBPresentationFrame frame;
    context.frame_done = 0;
    if (gbrt_presentation_capture(&source, &scene, &ui, &frame) !=
        GB_PRESENTATION_NOT_AT_FRAME_BOUNDARY) {
        return 5;
    }
    context.frame_done = 1;
    scene.abi_version++;
    if (gbrt_presentation_capture(&source, &scene, &ui, &frame) !=
        GB_PRESENTATION_ABI_MISMATCH) {
        return 6;
    }
    scene.abi_version = GB_PRESENTATION_ABI_VERSION;
    scene.map.regions[0].block_count = 1;
    if (gbrt_presentation_capture(&source, &scene, &ui, &frame) !=
        GB_PRESENTATION_INVALID_SCENE) {
        return 7;
    }
    scene.map.regions[0].block_count = 2;
    ui.command_count = GB_PORT_MAX_DRAW_COMMANDS + 1u;
    if (gbrt_presentation_capture(&source, &scene, &ui, &frame) !=
        GB_PRESENTATION_INVALID_UI) {
        return 8;
    }
    ui.command_count = 1;
    if (gbrt_presentation_capture(&source, &scene, &ui, &frame) !=
        GB_PRESENTATION_OK) {
        return 9;
    }
    const GBPresentationSprite* sprite = &frame.hardware.sprites[0];
    if (frame.abi_version != GB_PRESENTATION_ABI_VERSION ||
        !frame.headless ||
        strcmp(frame.rom_sha256, hash) != 0 ||
        frame.scene.map.region_count != 2 ||
        frame.scene.map.blocks[2] != 12 ||
        frame.ui.command_count != 1 ||
        strcmp(frame.ui.commands[0].text, "native-ui") != 0 ||
        frame.timing.completed_frames != 7 ||
        frame.timing.total_cycles != 491568 ||
        !frame.timing.cgb_double_speed ||
        frame.hardware.authority != GB_PRESENTATION_SHADOW_ACCURATE_PPU ||
        frame.hardware.guest_writeback_allowed ||
        !frame.hardware.lcd_enabled ||
        frame.hardware.tile_data[0][0] != 0x11 ||
        frame.hardware.tile_data[1][0] != 0x22 ||
        frame.hardware.tile_ids[0][0] != 0x31 ||
        frame.hardware.tile_ids[1][0] != 0x32 ||
        frame.hardware.tile_attributes[0][0] != 0x41 ||
        frame.hardware.tile_attributes[1][0] != 0x42 ||
        sprite->screen_y != 4 || sprite->screen_x != 8 ||
        sprite->tile != 9 || sprite->height != 16 ||
        sprite->vram_bank != 1 || sprite->palette != 3 ||
        !sprite->flip_x || !sprite->behind_background ||
        frame.hardware.background_palettes[0] != 0x55 ||
        frame.hardware.object_palettes[0] != 0x66 ||
        frame.hardware.accurate_pixels[0] != 0x1234) {
        return 10;
    }
    frame.hardware.tile_data[0][0] = 0xFF;
    frame.scene.map.blocks[0] = 0xFFFF;
    if (vram[0] != 0x11 || scene.map.blocks[0] != 10) {
        return 11;
    }
    rom[0] = 5;
    if (gbrt_presentation_capture(&source, &scene, &ui, &frame) !=
        GB_PRESENTATION_ROM_MISMATCH) {
        return 12;
    }
    return 0;
}
