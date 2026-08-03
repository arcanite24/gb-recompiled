#include "gbrt.h"
#include "gbrt_data_mod.h"
#include "gbrt_hash.h"
#include "gbrt_semantic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void write_le32(uint8_t* output, uint32_t value) {
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8u);
    output[2] = (uint8_t)(value >> 16u);
    output[3] = (uint8_t)(value >> 24u);
}

static void write_le64(uint8_t* output, uint64_t value) {
    for (size_t index = 0; index < 8u; ++index) {
        output[index] = (uint8_t)(value >> (index * 8u));
    }
}

static int write_artifact(
    const char* path,
    const uint8_t* rom,
    size_t rom_size,
    uint32_t offset,
    uint8_t expected,
    uint8_t replacement,
    int trailing_byte) {
    uint8_t header[92] = {0};
    memcpy(header, "GBDMOD1", 7);
    write_le32(header + 8, 1);
    write_le32(header + 12, sizeof(header));
    write_le64(header + 16, rom_size);
    gbrt_sha256(rom, rom_size, header + 24);
    memset(header + 56, 0xA5, 32);
    write_le32(header + 88, 1);
    uint8_t entry[10] = {0};
    write_le32(entry, offset);
    write_le32(entry + 4, 1);
    entry[8] = expected;
    entry[9] = replacement;
    FILE* file = fopen(path, "wb");
    if (!file) return 0;
    const int ok =
        fwrite(header, 1, sizeof(header), file) == sizeof(header) &&
        fwrite(entry, 1, sizeof(entry), file) == sizeof(entry) &&
        (!trailing_byte || fputc(0, file) != EOF);
    return fclose(file) == 0 && ok;
}

int main(void) {
    const char* path = "test-data-mod-overlay.gbdm";
    uint8_t* rom = (uint8_t*)calloc(1, 0x8000);
    if (!rom) return 1;
    rom[0x147] = 0;
    rom[0x4001] = 0x12;
    uint8_t original_digest[32];
    gbrt_sha256(rom, 0x8000, original_digest);

    GBConfig config = {
        .model = GB_MODEL_DMG,
        .enable_audio = false,
        .enable_serial = false,
        .speed_percent = 100,
    };
    GBContext* context = gb_context_create(&config);
    if (!context || !gb_context_load_rom(context, rom, 0x8000) ||
        !write_artifact(path, rom, 0x8000, 0x4001, 0x12, 0x34, 0)) {
        free(rom);
        gb_context_destroy(context);
        return 1;
    }

    if (gbrt_data_mod_load_file(context, path) != GB_DATA_MOD_OK ||
        !gbrt_data_mod_is_active(context) ||
        gbrt_data_mod_entry_count(context) != 1 ||
        gbrt_data_mod_read_rom(context, 0x4001, false) != 0x34 ||
        gbrt_data_mod_read_rom(context, 0x4001, true) != 0x12 ||
        gb_read8(context, 0x4001) != 0x34) {
        fprintf(stderr, "valid overlay was not visible through mapped reads\n");
        remove(path);
        free(rom);
        gb_context_destroy(context);
        return 1;
    }

    GBSemanticReader live = {0};
    GBSemanticReader original = {0};
    uint8_t live_value = 0, original_value = 0;
    const char* rom_id = "fixture-rom";
    if (gbrt_semantic_reader_init_live(&live, context, rom_id) != GB_SEMANTIC_OK ||
        gbrt_semantic_reader_init_live_original(
            &original, context, rom_id) != GB_SEMANTIC_OK ||
        gbrt_semantic_read(
            &live, rom_id, GB_SEMANTIC_READ_LIVE,
            GB_SEMANTIC_PHYSICAL_ROM, 1, 0x4001, &live_value, 1) !=
            GB_SEMANTIC_OK ||
        gbrt_semantic_read(
            &original, rom_id, GB_SEMANTIC_READ_LIVE_ORIGINAL,
            GB_SEMANTIC_PHYSICAL_ROM, 1, 0x4001, &original_value, 1) !=
            GB_SEMANTIC_OK ||
        live_value != 0x34 || original_value != 0x12) {
        fprintf(stderr, "semantic overlay/original views disagreed\n");
        remove(path);
        free(rom);
        gb_context_destroy(context);
        return 1;
    }
    uint8_t context_digest[32];
    gbrt_sha256(context->rom, context->rom_size, context_digest);
    if (memcmp(context_digest, original_digest, sizeof(context_digest)) != 0) {
        fprintf(stderr, "overlay mutated the owned ROM\n");
        remove(path);
        free(rom);
        gb_context_destroy(context);
        return 1;
    }

    if (!write_artifact(path, rom, 0x8000, 0x4001, 0x99, 0x55, 0) ||
        gbrt_data_mod_load_file(context, path) !=
            GB_DATA_MOD_SOURCE_MISMATCH ||
        gbrt_data_mod_is_active(context) ||
        gb_read8(context, 0x4001) != 0x12) {
        fprintf(stderr, "source mismatch did not fail closed to vanilla\n");
        remove(path);
        free(rom);
        gb_context_destroy(context);
        return 1;
    }
    if (!write_artifact(path, rom, 0x8000, 0x4001, 0x12, 0x34, 1) ||
        gbrt_data_mod_load_file(context, path) !=
            GB_DATA_MOD_INVALID_ARTIFACT ||
        gbrt_data_mod_is_active(context)) {
        fprintf(stderr, "trailing artifact data was accepted\n");
        remove(path);
        free(rom);
        gb_context_destroy(context);
        return 1;
    }

    remove(path);
    free(rom);
    gb_context_destroy(context);
    return 0;
}
