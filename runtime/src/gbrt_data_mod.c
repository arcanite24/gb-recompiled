#include "gbrt_data_mod.h"

#include "gbrt.h"
#include "gbrt_hash.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GB_DATA_MOD_HEADER_SIZE 92u
#define GB_DATA_MOD_MAX_ENTRIES 4096u
#define GB_DATA_MOD_MAX_PATCH_BYTES (1024u * 1024u)

typedef struct GBDataModEntry {
    size_t offset;
    size_t size;
    uint8_t* replacement;
} GBDataModEntry;

typedef struct GBDataModState {
    uint8_t package_set_sha256[32];
    size_t entry_count;
    GBDataModEntry* entries;
} GBDataModState;

static const uint8_t gbrt_data_mod_magic[8] = {
    'G', 'B', 'D', 'M', 'O', 'D', '1', '\0'
};

static uint32_t read_le32(const uint8_t* data) {
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8u) |
           ((uint32_t)data[2] << 16u) |
           ((uint32_t)data[3] << 24u);
}

static uint64_t read_le64(const uint8_t* data) {
    uint64_t value = 0;
    for (size_t index = 0; index < 8u; ++index) {
        value |= (uint64_t)data[index] << (index * 8u);
    }
    return value;
}

static bool read_exact(FILE* file, void* output, size_t size) {
    return size == 0 || (file != NULL && output != NULL &&
                         fread(output, 1, size, file) == size);
}

static void destroy_state(GBDataModState* state) {
    if (state == NULL) return;
    for (size_t index = 0; index < state->entry_count; ++index) {
        free(state->entries[index].replacement);
    }
    free(state->entries);
    free(state);
}

void gbrt_data_mod_unload(GBContext* context) {
    if (context == NULL) return;
    destroy_state((GBDataModState*)context->data_mod_state);
    context->data_mod_state = NULL;
}

static GBDataModStatus fail(
    GBContext* context,
    FILE* file,
    GBDataModState* state,
    GBDataModStatus status) {
    if (file != NULL) fclose(file);
    destroy_state(state);
    if (context != NULL) context->data_mod_state = NULL;
    return status;
}

GBDataModStatus gbrt_data_mod_load_file(GBContext* context, const char* path) {
    if (context == NULL || path == NULL || path[0] == '\0' ||
        context->rom == NULL || context->rom_size == 0) {
        if (context != NULL) gbrt_data_mod_unload(context);
        return GB_DATA_MOD_INVALID_ARGUMENT;
    }
    gbrt_data_mod_unload(context);

    FILE* file = fopen(path, "rb");
    if (file == NULL) return GB_DATA_MOD_IO_ERROR;
    uint8_t header[GB_DATA_MOD_HEADER_SIZE];
    if (!read_exact(file, header, sizeof(header)) ||
        memcmp(header, gbrt_data_mod_magic, sizeof(gbrt_data_mod_magic)) != 0) {
        return fail(context, file, NULL, GB_DATA_MOD_INVALID_ARTIFACT);
    }
    if (read_le32(header + 8u) != GB_DATA_MOD_ABI_VERSION) {
        return fail(context, file, NULL, GB_DATA_MOD_ABI_MISMATCH);
    }
    if (read_le32(header + 12u) != GB_DATA_MOD_HEADER_SIZE) {
        return fail(context, file, NULL, GB_DATA_MOD_INVALID_ARTIFACT);
    }
    if (read_le64(header + 16u) != (uint64_t)context->rom_size) {
        return fail(context, file, NULL, GB_DATA_MOD_ROM_MISMATCH);
    }
    uint8_t actual_rom_sha256[32];
    gbrt_sha256(context->rom, context->rom_size, actual_rom_sha256);
    if (memcmp(actual_rom_sha256, header + 24u, 32u) != 0) {
        return fail(context, file, NULL, GB_DATA_MOD_ROM_MISMATCH);
    }

    const uint32_t entry_count = read_le32(header + 88u);
    if (entry_count == 0 || entry_count > GB_DATA_MOD_MAX_ENTRIES) {
        return fail(context, file, NULL, GB_DATA_MOD_INVALID_ARTIFACT);
    }
    GBDataModState* state = (GBDataModState*)calloc(1, sizeof(*state));
    if (state == NULL) {
        return fail(context, file, NULL, GB_DATA_MOD_OUT_OF_MEMORY);
    }
    memcpy(state->package_set_sha256, header + 56u, 32u);
    state->entries =
        (GBDataModEntry*)calloc(entry_count, sizeof(*state->entries));
    if (state->entries == NULL) {
        return fail(context, file, state, GB_DATA_MOD_OUT_OF_MEMORY);
    }

    size_t total_patch_bytes = 0;
    size_t prior_end = 0;
    uint8_t entry_header[8];
    for (uint32_t index = 0; index < entry_count; ++index) {
        if (!read_exact(file, entry_header, sizeof(entry_header))) {
            return fail(context, file, state, GB_DATA_MOD_INVALID_ARTIFACT);
        }
        const size_t offset = read_le32(entry_header);
        const size_t size = read_le32(entry_header + 4u);
        if (size == 0 || offset > context->rom_size ||
            size > context->rom_size - offset ||
            (index != 0 && offset < prior_end) ||
            size > GB_DATA_MOD_MAX_PATCH_BYTES - total_patch_bytes) {
            return fail(context, file, state, GB_DATA_MOD_INVALID_ARTIFACT);
        }
        uint8_t* expected = (uint8_t*)malloc(size);
        uint8_t* replacement = (uint8_t*)malloc(size);
        if (expected == NULL || replacement == NULL) {
            free(expected);
            free(replacement);
            return fail(context, file, state, GB_DATA_MOD_OUT_OF_MEMORY);
        }
        if (!read_exact(file, expected, size) ||
            !read_exact(file, replacement, size)) {
            free(expected);
            free(replacement);
            return fail(context, file, state, GB_DATA_MOD_INVALID_ARTIFACT);
        }
        if (memcmp(context->rom + offset, expected, size) != 0) {
            free(expected);
            free(replacement);
            return fail(context, file, state, GB_DATA_MOD_SOURCE_MISMATCH);
        }
        free(expected);
        state->entries[index] = (GBDataModEntry){
            .offset = offset,
            .size = size,
            .replacement = replacement,
        };
        state->entry_count++;
        prior_end = offset + size;
        total_patch_bytes += size;
    }
    if (fgetc(file) != EOF || ferror(file)) {
        return fail(context, file, state, GB_DATA_MOD_INVALID_ARTIFACT);
    }
    fclose(file);
    context->data_mod_state = state;
    return GB_DATA_MOD_OK;
}

bool gbrt_data_mod_is_active(const GBContext* context) {
    return context != NULL && context->data_mod_state != NULL;
}

size_t gbrt_data_mod_entry_count(const GBContext* context) {
    const GBDataModState* state =
        context == NULL ? NULL : (const GBDataModState*)context->data_mod_state;
    return state == NULL ? 0 : state->entry_count;
}

uint8_t gbrt_data_mod_read_rom(
    const GBContext* context,
    size_t physical_offset,
    bool original) {
    if (context == NULL || context->rom == NULL ||
        physical_offset >= context->rom_size) {
        return 0xFF;
    }
    if (!original) {
        const GBDataModState* state =
            (const GBDataModState*)context->data_mod_state;
        if (state != NULL) {
            for (size_t index = 0; index < state->entry_count; ++index) {
                const GBDataModEntry* entry = &state->entries[index];
                if (physical_offset < entry->offset) break;
                if (physical_offset - entry->offset < entry->size) {
                    return entry->replacement[physical_offset - entry->offset];
                }
            }
        }
    }
    return context->rom[physical_offset];
}

bool gbrt_data_mod_copy_rom(
    const GBContext* context,
    size_t physical_offset,
    uint8_t* output,
    size_t width,
    bool original) {
    if (context == NULL || output == NULL || width == 0 ||
        physical_offset > context->rom_size ||
        width > context->rom_size - physical_offset) {
        return false;
    }
    if (original || context->data_mod_state == NULL) {
        memcpy(output, context->rom + physical_offset, width);
        return true;
    }
    for (size_t index = 0; index < width; ++index) {
        output[index] =
            gbrt_data_mod_read_rom(context, physical_offset + index, false);
    }
    return true;
}

const char* gbrt_data_mod_status_string(GBDataModStatus status) {
    switch (status) {
        case GB_DATA_MOD_OK: return "ok";
        case GB_DATA_MOD_INVALID_ARGUMENT: return "invalid argument";
        case GB_DATA_MOD_IO_ERROR: return "I/O error";
        case GB_DATA_MOD_INVALID_ARTIFACT: return "invalid artifact";
        case GB_DATA_MOD_ABI_MISMATCH: return "ABI mismatch";
        case GB_DATA_MOD_ROM_MISMATCH: return "ROM mismatch";
        case GB_DATA_MOD_SOURCE_MISMATCH: return "source byte mismatch";
        case GB_DATA_MOD_OUT_OF_MEMORY: return "out of memory";
        default: return "unknown";
    }
}
