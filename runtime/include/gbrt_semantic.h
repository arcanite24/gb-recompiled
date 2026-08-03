#ifndef GBRT_SEMANTIC_H
#define GBRT_SEMANTIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GB_SEMANTIC_READER_ABI_VERSION 1u
#define GB_SEMANTIC_TRANSACTION_ABI_VERSION 1u
#ifndef GB_SEMANTIC_TRANSACTION_MAX_DIRTY_RANGES
#define GB_SEMANTIC_TRANSACTION_MAX_DIRTY_RANGES 16u
#endif

typedef struct GBContext GBContext;

typedef enum GBSemanticReadMode {
    GB_SEMANTIC_READ_LIVE = 0,
    GB_SEMANTIC_READ_SAVE = 1,
    GB_SEMANTIC_READ_LIVE_ORIGINAL = 2,
} GBSemanticReadMode;

typedef enum GBSemanticMemorySpace {
    GB_SEMANTIC_PHYSICAL_ROM = 0,
    GB_SEMANTIC_EXTERNAL_RAM = 1,
    GB_SEMANTIC_WRAM = 2,
    GB_SEMANTIC_BANKED_WRAM = 3,
    GB_SEMANTIC_RTC = 4,
} GBSemanticMemorySpace;

typedef enum GBSemanticStatus {
    GB_SEMANTIC_OK = 0,
    GB_SEMANTIC_INVALID_ARGUMENT,
    GB_SEMANTIC_ABI_MISMATCH,
    GB_SEMANTIC_ROM_MISMATCH,
    GB_SEMANTIC_WRONG_MODE,
    GB_SEMANTIC_OUT_OF_RANGE,
    GB_SEMANTIC_READ_FAILED,
    GB_SEMANTIC_INVALID_DATA,
    GB_SEMANTIC_NOT_ACTIVE,
    GB_SEMANTIC_NOT_VALIDATED,
    GB_SEMANTIC_TOO_MANY_DIRTY_RANGES,
    GB_SEMANTIC_COMMIT_FAILED,
} GBSemanticStatus;

typedef bool (*GBSemanticReadFn)(
    void* user,
    GBSemanticMemorySpace space,
    uint16_t bank,
    uint16_t address,
    uint8_t* output,
    size_t width);

typedef struct GBSemanticReader {
    uint32_t abi_version;
    const char* rom_sha256;
    GBSemanticReadMode mode;
    void* user;
    GBSemanticReadFn read;
} GBSemanticReader;

typedef struct GBSemanticSaveSource {
    const uint8_t* data;
    size_t size;
    const uint8_t* rom;
    size_t rom_size;
} GBSemanticSaveSource;

typedef struct GBSemanticDirtyRange {
    GBSemanticMemorySpace space;
    uint16_t bank;
    uint16_t address;
    size_t width;
} GBSemanticDirtyRange;

typedef struct GBSemanticTransaction {
    uint32_t abi_version;
    GBContext* context;
    char rom_sha256[65];
    uint8_t* staged_eram;
    size_t eram_size;
    uint8_t* staged_wram;
    size_t wram_size;
    GBSemanticDirtyRange
        dirty_ranges[GB_SEMANTIC_TRANSACTION_MAX_DIRTY_RANGES];
    size_t dirty_range_count;
    uint64_t sequence;
    bool active;
    bool validated;
} GBSemanticTransaction;

/*
 * Transactions are synchronous safepoint operations. Guest execution must
 * remain paused from begin through validate and commit or abort.
 */

typedef GBSemanticStatus (*GBSemanticValidateFn)(
    const GBSemanticReader* staged_reader,
    void* user);

GBSemanticStatus gbrt_semantic_reader_init_live(
    GBSemanticReader* reader,
    const GBContext* context,
    const char* rom_sha256);

/** Initialize a live reader that bypasses active data overlays for ROM reads. */
GBSemanticStatus gbrt_semantic_reader_init_live_original(
    GBSemanticReader* reader,
    const GBContext* context,
    const char* rom_sha256);

GBSemanticStatus gbrt_semantic_reader_init_save(
    GBSemanticReader* reader,
    const GBSemanticSaveSource* source,
    const char* rom_sha256);

GBSemanticStatus gbrt_semantic_read(
    const GBSemanticReader* reader,
    const char* expected_rom_sha256,
    GBSemanticReadMode expected_mode,
    GBSemanticMemorySpace space,
    uint16_t bank,
    uint16_t address,
    uint8_t* output,
    size_t width);

GBSemanticStatus gbrt_semantic_transaction_begin(
    GBSemanticTransaction* transaction,
    GBContext* context,
    const char* actual_rom_sha256,
    const char* expected_rom_sha256);

GBSemanticStatus gbrt_semantic_transaction_write(
    GBSemanticTransaction* transaction,
    GBSemanticMemorySpace space,
    uint16_t bank,
    uint16_t address,
    const uint8_t* data,
    size_t width);

GBSemanticStatus gbrt_semantic_transaction_reader(
    GBSemanticTransaction* transaction,
    GBSemanticReader* reader);

GBSemanticStatus gbrt_semantic_transaction_validate(
    GBSemanticTransaction* transaction,
    GBSemanticValidateFn validate,
    void* user);

GBSemanticStatus gbrt_semantic_transaction_commit(
    GBSemanticTransaction* transaction);

GBSemanticStatus gbrt_semantic_transaction_abort(
    GBSemanticTransaction* transaction);

#ifdef __cplusplus
}
#endif

#endif
