#ifndef GBRT_DATA_MOD_H
#define GBRT_DATA_MOD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GB_DATA_MOD_ABI_VERSION 1u

typedef struct GBContext GBContext;

typedef enum GBDataModStatus {
    GB_DATA_MOD_OK = 0,
    GB_DATA_MOD_INVALID_ARGUMENT,
    GB_DATA_MOD_IO_ERROR,
    GB_DATA_MOD_INVALID_ARTIFACT,
    GB_DATA_MOD_ABI_MISMATCH,
    GB_DATA_MOD_ROM_MISMATCH,
    GB_DATA_MOD_SOURCE_MISMATCH,
    GB_DATA_MOD_OUT_OF_MEMORY,
} GBDataModStatus;

/**
 * Load one resolved, immutable data-overlay artifact.
 *
 * Loading is fail-closed: any error removes the previously active overlay.
 * The artifact is accepted only when its exact ROM size, SHA-256, ordered
 * ranges, and expected source bytes match the context's untouched ROM.
 */
GBDataModStatus gbrt_data_mod_load_file(GBContext* context, const char* path);

/** Remove the active overlay. The original ROM is never modified. */
void gbrt_data_mod_unload(GBContext* context);

bool gbrt_data_mod_is_active(const GBContext* context);
size_t gbrt_data_mod_entry_count(const GBContext* context);
const char* gbrt_data_mod_status_string(GBDataModStatus status);

/**
 * Read a physical ROM byte through the active overlay. Pass original=true to
 * bypass the overlay and recover the exact user-provided ROM value.
 */
uint8_t gbrt_data_mod_read_rom(
    const GBContext* context,
    size_t physical_offset,
    bool original);

/** Range form used by semantic readers. */
bool gbrt_data_mod_copy_rom(
    const GBContext* context,
    size_t physical_offset,
    uint8_t* output,
    size_t width,
    bool original);

#ifdef __cplusplus
}
#endif

#endif
