#include "crystal_overworld.h"
#include "crystal_semantic.h"
#include "gbrt_presentation.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ProbeMemory {
    uint8_t* rom;
    size_t rom_size;
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
    size_t offset = 0;
    const uint8_t* source = NULL;
    size_t size = 0;
    if (space == GB_SEMANTIC_PHYSICAL_ROM && address >= 0x4000) {
        offset = (size_t)bank * 0x4000u + (address - 0x4000u);
        source = memory->rom;
        size = memory->rom_size;
    } else if (space == GB_SEMANTIC_BANKED_WRAM && bank == 1 &&
               address >= 0xD000) {
        offset = address - 0xD000u;
        source = memory->wram1;
        size = sizeof(memory->wram1);
    } else {
        return false;
    }
    if (offset > size || width > size - offset) {
        return false;
    }
    memcpy(output, source + offset, width);
    return true;
}

static uint8_t* read_file(const char* path, size_t* size) {
    FILE* file = fopen(path, "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        if (file != NULL) {
            fclose(file);
        }
        return NULL;
    }
    const long length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET) != 0) {
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

static bool write_ppm(
    const char* path,
    const uint32_t* pixels,
    uint32_t width,
    uint32_t height) {
    FILE* file = fopen(path, "wb");
    if (file == NULL ||
        fprintf(file, "P6\n%u %u\n255\n", width, height) < 0) {
        if (file != NULL) {
            fclose(file);
        }
        return false;
    }
    for (size_t index = 0; index < (size_t)width * height; ++index) {
        const uint8_t rgb[3] = {
            (uint8_t)(pixels[index] >> 24),
            (uint8_t)(pixels[index] >> 16),
            (uint8_t)(pixels[index] >> 8),
        };
        if (fwrite(rgb, 1, sizeof(rgb), file) != sizeof(rgb)) {
            fclose(file);
            return false;
        }
    }
    return fclose(file) == 0;
}

int main(int argc, char** argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: %s ROM WRAM1.bin OUTPUT.ppm\n", argv[0]);
        return 2;
    }
    ProbeMemory memory = {0};
    memory.rom = read_file(argv[1], &memory.rom_size);
    size_t wram_size = 0;
    uint8_t* wram = read_file(argv[2], &wram_size);
    if (memory.rom == NULL || memory.rom_size != 2097152u ||
        wram == NULL || wram_size != sizeof(memory.wram1)) {
        free(memory.rom);
        free(wram);
        return 3;
    }
    memcpy(memory.wram1, wram, sizeof(memory.wram1));
    free(wram);
    const GBSemanticReader reader = {
        .abi_version = GB_SEMANTIC_READER_ABI_VERSION,
        .rom_sha256 = CRYSTAL_SEMANTIC_ROM_SHA256,
        .mode = GB_SEMANTIC_READ_LIVE,
        .user = &memory,
        .read = probe_read,
    };
    GBPresentationFrame frame = {
        .abi_version = GB_PRESENTATION_ABI_VERSION,
        .hardware = {
            .authority = GB_PRESENTATION_SHADOW_ACCURATE_PPU,
            .lcd_enabled = true,
        },
    };
    const CrystalOverworldStatus build_status =
        crystal_overworld_build_new_bark_scene(
            &reader, -128, 32, false, false, &frame.scene);
    if (build_status != CRYSTAL_OVERWORLD_OK) {
        fprintf(stderr, "scene status=%d\n", build_status);
        free(memory.rom);
        return 4;
    }
    enum {
        WIDTH = 256,
        HEIGHT = 144,
    };
    static uint32_t pixels[WIDTH * HEIGHT];
    GBPresentationSurface surface = {
        .pixels = pixels,
        .width = WIDTH,
        .height = HEIGHT,
        .stride_pixels = WIDTH,
    };
    const GBPresentationWidescreenStyle style = crystal_overworld_style();
    const GBPresentationComposeResult compose_status =
        gbrt_presentation_compose_widescreen(&frame, &style, &surface);
    if (compose_status != GB_PRESENTATION_COMPOSED) {
        fprintf(stderr, "compose status=%d\n", compose_status);
        free(memory.rom);
        return 5;
    }
    if (!write_ppm(argv[3], pixels, WIDTH, HEIGHT)) {
        free(memory.rom);
        return 6;
    }
    printf(
        "{\"scene\":\"%s\",\"width\":%u,\"height\":%u,"
        "\"regions\":%zu,\"blocks\":%zu,\"sprites\":%zu,"
        "\"connection\":\"route29-west\",\"compose_status\":%d}\n",
        frame.scene.scene_id,
        WIDTH,
        HEIGHT,
        frame.scene.map.region_count,
        frame.scene.map.block_count,
        frame.scene.map.sprite_count,
        compose_status);
    free(memory.rom);
    return 0;
}
