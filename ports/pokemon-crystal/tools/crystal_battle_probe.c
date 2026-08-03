#include "crystal_battle.h"
#include "crystal_semantic.h"
#include "gbrt_presentation.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { WIDTH = 1280, HEIGHT = 720 };

typedef struct ProbeMemory {
    uint8_t wram0[0x1000];
    uint8_t wram1[0x1000];
} ProbeMemory;

static bool probe_read(
    void* user,
    GBSemanticMemorySpace space,
    uint16_t bank,
    uint16_t address,
    uint8_t* output,
    size_t width) {
    ProbeMemory* memory = (ProbeMemory*)user;
    if (address < 0xC000 || address + width > 0xE000) {
        return false;
    }
    const uint8_t* source = NULL;
    size_t offset = 0;
    if (space == GB_SEMANTIC_WRAM && bank == 0 && address < 0xD000) {
        source = memory->wram0;
        offset = address - 0xC000u;
    } else if (space == GB_SEMANTIC_BANKED_WRAM && bank == 1 &&
               address >= 0xD000) {
        source = memory->wram1;
        offset = address - 0xD000u;
    } else {
        return false;
    }
    if (offset + width > 0x1000) {
        return false;
    }
    memcpy(output, source + offset, width);
    return true;
}

static uint8_t* read_file(const char* path, size_t expected, size_t* size) {
    FILE* file = fopen(path, "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) return NULL;
    const long length = ftell(file);
    if (length < 0 || (expected != 0 && (size_t)length != expected) ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    uint8_t* data = malloc((size_t)length);
    if (data == NULL ||
        fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *size = (size_t)length;
    return data;
}

static void rect(
    uint32_t* pixels, int x, int y, int width, int height, uint32_t color) {
    for (int row = y < 0 ? 0 : y;
         row < y + height && row < HEIGHT;
         ++row) {
        for (int column = x < 0 ? 0 : x;
             column < x + width && column < WIDTH;
             ++column) {
            pixels[(size_t)row * WIDTH + column] = color;
        }
    }
}

static void mon(
    uint32_t* pixels, int x, int y, uint16_t species, uint32_t color) {
    for (int row = 0; row < 8; ++row) {
        for (int column = 0; column < 8; ++column) {
            const unsigned bit =
                (unsigned)((row * 5 + column * 3 + species) & 7u);
            if (((species >> bit) & 1u) != 0 || row == 7) {
                rect(
                    pixels,
                    x + column * 18,
                    y + row * 18,
                    16,
                    16,
                    color);
            }
        }
    }
}

static bool write_ppm(const char* path, const uint32_t* pixels) {
    FILE* file = fopen(path, "wb");
    if (file == NULL || fprintf(file, "P6\n%d %d\n255\n", WIDTH, HEIGHT) < 0) {
        if (file != NULL) fclose(file);
        return false;
    }
    for (size_t index = 0; index < (size_t)WIDTH * HEIGHT; ++index) {
        const uint8_t rgb[] = {
            (uint8_t)(pixels[index] >> 24),
            (uint8_t)(pixels[index] >> 16),
            (uint8_t)(pixels[index] >> 8),
        };
        if (fwrite(rgb, 1, 3, file) != 3) {
            fclose(file);
            return false;
        }
    }
    return fclose(file) == 0;
}

int main(int argc, char** argv) {
    if (argc != 6) {
        fprintf(stderr, "usage: %s WRAM0 WRAM1 PANEL AURA OUTPUT\n", argv[0]);
        return 2;
    }
    size_t sizes[4] = {0};
    uint8_t* wram0 = read_file(argv[1], 0x1000, &sizes[0]);
    uint8_t* wram1 = read_file(argv[2], 0x1000, &sizes[1]);
    uint8_t* panel = read_file(argv[3], 0, &sizes[2]);
    uint8_t* aura = read_file(argv[4], 0, &sizes[3]);
    if (wram0 == NULL || wram1 == NULL || panel == NULL || aura == NULL) {
        free(wram0); free(wram1); free(panel); free(aura);
        return 3;
    }
    ProbeMemory memory;
    memcpy(memory.wram0, wram0, 0x1000);
    memcpy(memory.wram1, wram1, 0x1000);
    free(wram0); free(wram1);
    const GBSemanticReader reader = {
        .abi_version = GB_SEMANTIC_READER_ABI_VERSION,
        .rom_sha256 = CRYSTAL_SEMANTIC_ROM_SHA256,
        .mode = GB_SEMANTIC_READ_LIVE,
        .user = &memory,
        .read = probe_read,
    };
    GBPresentationScene scene;
    if (crystal_battle_build_scene(&reader, &scene) != GB_SEMANTIC_OK) {
        free(panel); free(aura);
        return 4;
    }
    GBPresentationReplacementConfig config = {
        .abi_version = GB_PRESENTATION_ABI_VERSION,
        .mode = GB_PRESENTATION_MODE_NATIVE,
        .output_width = WIDTH,
        .output_height = HEIGHT,
        .effect_seed = 1129466195u,
        .asset_count = 2,
        .assets = {
            {
                .asset_id = "crystal.ui.panel-v1",
                .sha256 =
                    "f85b19de942583bac61bc7c318ae16e91c978bc6c4c3ec8aa3234bbb93127678",
                .license_spdx = "CC0-1.0",
            },
            {
                .asset_id = "crystal.battle.aura-v1",
                .sha256 =
                    "0b1353d19cf40c81ab399426aef76c1a8a8bf962fddbc25d8f58c612b9bd84c3",
                .license_spdx = "CC0-1.0",
            },
        },
    };
    memcpy(
        config.rom_sha256,
        CRYSTAL_SEMANTIC_ROM_SHA256,
        sizeof(config.rom_sha256));
    config.assets[0].data = panel;
    config.assets[0].data_size = sizes[2];
    config.assets[1].data = aura;
    config.assets[1].data_size = sizes[3];
    if (gbrt_presentation_validate_replacements(
            &config, CRYSTAL_SEMANTIC_ROM_SHA256) != GB_PRESENTATION_OK) {
        free(panel); free(aura);
        return 5;
    }
    free(panel); free(aura);

    static uint32_t pixels[WIDTH * HEIGHT];
    for (size_t index = 0; index < (size_t)WIDTH * HEIGHT; ++index) {
        const uint32_t band = (uint32_t)((index / WIDTH) / 30u) & 1u;
        pixels[index] = band ? 0x1D2D49FFu : 0x17243AFFu;
    }
    for (int ray = 0; ray < 24; ++ray) {
        rect(
            pixels,
            ray * 56 - 20,
            300 + (ray % 3) * 10,
            28,
            260,
            0x394B63FFu);
    }
    rect(pixels, 40, 40, 520, 150, 0x41627DFFu);
    rect(pixels, 720, 530, 520, 150, 0x41627DFFu);
    rect(pixels, 60, 60, 480, 110, 0x273650FFu);
    rect(pixels, 740, 550, 480, 110, 0x273650FFu);
    mon(
        pixels,
        180,
        390,
        scene.battle.player_species,
        0x66C7A5FFu);
    mon(
        pixels,
        930,
        170,
        scene.battle.enemy_species,
        0xEF718DFFu);
    rect(
        pixels,
        90,
        130,
        scene.battle.enemy_level * 8,
        18,
        0xF6D66EFFu);
    rect(
        pixels,
        770,
        620,
        scene.battle.player_level * 8,
        18,
        0xF6D66EFFu);
    if (!write_ppm(argv[5], pixels)) return 6;
    printf(
        "{\"scene\":\"%s\",\"width\":%d,\"height\":%d,"
        "\"player_species\":%u,\"player_level\":%u,"
        "\"enemy_species\":%u,\"enemy_level\":%u,"
        "\"phase\":%u,\"assets\":2,\"effect_seed\":%u}\n",
        scene.scene_id,
        WIDTH,
        HEIGHT,
        scene.battle.player_species,
        scene.battle.player_level,
        scene.battle.enemy_species,
        scene.battle.enemy_level,
        scene.battle.phase,
        config.effect_seed);
    return 0;
}
