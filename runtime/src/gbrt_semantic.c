#include "gbrt_semantic.h"

#include "gbrt.h"
#include "gbrt_data_mod.h"

#include <stdlib.h>
#include <string.h>

static bool gbrt_semantic_read_live(
    void* user,
    GBSemanticMemorySpace space,
    uint16_t bank,
    uint16_t address,
    uint8_t* output,
    size_t width) {
    const GBContext* context = (const GBContext*)user;
    size_t offset = 0;
    const uint8_t* source = NULL;
    size_t source_size = 0;

    if (context == NULL || output == NULL || width == 0) {
        return false;
    }
    switch (space) {
        case GB_SEMANTIC_PHYSICAL_ROM:
            if (bank == 0) {
                if (address >= 0x4000u) {
                    return false;
                }
                offset = address;
            } else {
                if (address < 0x4000u || address >= 0x8000u) {
                    return false;
                }
                offset = (size_t)bank * 0x4000u + (address - 0x4000u);
            }
            return gbrt_data_mod_copy_rom(
                context, offset, output, width, false);
        case GB_SEMANTIC_EXTERNAL_RAM:
            if (address < 0xA000u || address >= 0xC000u) {
                return false;
            }
            offset = (size_t)bank * 0x2000u + (address - 0xA000u);
            source = context->eram;
            source_size = context->eram_size;
            break;
        case GB_SEMANTIC_WRAM:
            if (bank != 0 || address < 0xC000u || address >= 0xD000u) {
                return false;
            }
            offset = address - 0xC000u;
            source = context->wram;
            source_size = 0x8000u;
            break;
        case GB_SEMANTIC_BANKED_WRAM:
            if (bank == 0 || bank > 7 || address < 0xD000u ||
                address >= 0xE000u) {
                return false;
            }
            offset = (size_t)bank * 0x1000u + (address - 0xD000u);
            source = context->wram;
            source_size = 0x8000u;
            break;
        case GB_SEMANTIC_RTC:
            if (width != 1) {
                return false;
            }
            switch (bank) {
                case 8: output[0] = context->rtc.s; return true;
                case 9: output[0] = context->rtc.m; return true;
                case 10: output[0] = context->rtc.h; return true;
                case 11: output[0] = context->rtc.dl; return true;
                case 12: output[0] = context->rtc.dh; return true;
                default: return false;
            }
        default:
            return false;
    }
    if (source == NULL || offset > source_size || width > source_size - offset) {
        return false;
    }
    memcpy(output, source + offset, width);
    return true;
}

static bool gbrt_semantic_read_live_original(
    void* user,
    GBSemanticMemorySpace space,
    uint16_t bank,
    uint16_t address,
    uint8_t* output,
    size_t width) {
    const GBContext* context = (const GBContext*)user;
    if (space != GB_SEMANTIC_PHYSICAL_ROM) {
        return gbrt_semantic_read_live(
            user, space, bank, address, output, width);
    }
    if (context == NULL || output == NULL || width == 0) return false;
    size_t offset = 0;
    if (bank == 0) {
        if (address >= 0x4000u) return false;
        offset = address;
    } else {
        if (address < 0x4000u || address >= 0x8000u) return false;
        offset = (size_t)bank * 0x4000u + (address - 0x4000u);
    }
    return gbrt_data_mod_copy_rom(context, offset, output, width, true);
}

static bool gbrt_semantic_read_save(
    void* user,
    GBSemanticMemorySpace space,
    uint16_t bank,
    uint16_t address,
    uint8_t* output,
    size_t width) {
    const GBSemanticSaveSource* source = (const GBSemanticSaveSource*)user;
    if (source == NULL) {
        return false;
    }
    const uint8_t* data = NULL;
    size_t data_size = 0;
    size_t offset = 0;
    if (space == GB_SEMANTIC_EXTERNAL_RAM) {
        if (source->data == NULL ||
            address < 0xA000u || address >= 0xC000u) {
            return false;
        }
        data = source->data;
        data_size = source->size;
        offset = (size_t)bank * 0x2000u +
                 (size_t)(address - 0xA000u);
    } else if (space == GB_SEMANTIC_PHYSICAL_ROM) {
        if (source->rom == NULL) {
            return false;
        }
        data = source->rom;
        data_size = source->rom_size;
        if (bank == 0) {
            if (address >= 0x4000u) return false;
            offset = address;
        } else {
            if (address < 0x4000u || address >= 0x8000u) return false;
            offset = (size_t)bank * 0x4000u +
                     (size_t)(address - 0x4000u);
        }
    } else {
        return false;
    }
    if (offset > data_size || width > data_size - offset) {
        return false;
    }
    memcpy(output, data + offset, width);
    return true;
}

GBSemanticStatus gbrt_semantic_reader_init_live(
    GBSemanticReader* reader,
    const GBContext* context,
    const char* rom_sha256) {
    if (reader == NULL || context == NULL || rom_sha256 == NULL ||
        context->rom == NULL || context->wram == NULL) {
        return GB_SEMANTIC_INVALID_ARGUMENT;
    }
    *reader = (GBSemanticReader){
        .abi_version = GB_SEMANTIC_READER_ABI_VERSION,
        .rom_sha256 = rom_sha256,
        .mode = GB_SEMANTIC_READ_LIVE,
        .user = (void*)context,
        .read = gbrt_semantic_read_live,
    };
    return GB_SEMANTIC_OK;
}

GBSemanticStatus gbrt_semantic_reader_init_save(
    GBSemanticReader* reader,
    const GBSemanticSaveSource* source,
    const char* rom_sha256) {
    if (reader == NULL || source == NULL || source->data == NULL ||
        source->size == 0 || rom_sha256 == NULL) {
        return GB_SEMANTIC_INVALID_ARGUMENT;
    }
    *reader = (GBSemanticReader){
        .abi_version = GB_SEMANTIC_READER_ABI_VERSION,
        .rom_sha256 = rom_sha256,
        .mode = GB_SEMANTIC_READ_SAVE,
        .user = (void*)source,
        .read = gbrt_semantic_read_save,
    };
    return GB_SEMANTIC_OK;
}

GBSemanticStatus gbrt_semantic_reader_init_live_original(
    GBSemanticReader* reader,
    const GBContext* context,
    const char* rom_sha256) {
    if (reader == NULL || context == NULL || rom_sha256 == NULL ||
        context->rom == NULL || context->wram == NULL) {
        return GB_SEMANTIC_INVALID_ARGUMENT;
    }
    *reader = (GBSemanticReader){
        .abi_version = GB_SEMANTIC_READER_ABI_VERSION,
        .rom_sha256 = rom_sha256,
        .mode = GB_SEMANTIC_READ_LIVE_ORIGINAL,
        .user = (void*)context,
        .read = gbrt_semantic_read_live_original,
    };
    return GB_SEMANTIC_OK;
}

GBSemanticStatus gbrt_semantic_read(
    const GBSemanticReader* reader,
    const char* expected_rom_sha256,
    GBSemanticReadMode expected_mode,
    GBSemanticMemorySpace space,
    uint16_t bank,
    uint16_t address,
    uint8_t* output,
    size_t width) {
    if (reader == NULL || expected_rom_sha256 == NULL || output == NULL ||
        width == 0 || reader->read == NULL || reader->rom_sha256 == NULL) {
        return GB_SEMANTIC_INVALID_ARGUMENT;
    }
    if (reader->abi_version != GB_SEMANTIC_READER_ABI_VERSION) {
        return GB_SEMANTIC_ABI_MISMATCH;
    }
    if (strcmp(reader->rom_sha256, expected_rom_sha256) != 0) {
        return GB_SEMANTIC_ROM_MISMATCH;
    }
    if (reader->mode != expected_mode) {
        return GB_SEMANTIC_WRONG_MODE;
    }
    if ((size_t)address + width > 0x10000u) {
        return GB_SEMANTIC_OUT_OF_RANGE;
    }
    if (!reader->read(reader->user, space, bank, address, output, width)) {
        return GB_SEMANTIC_READ_FAILED;
    }
    return GB_SEMANTIC_OK;
}

static bool gbrt_semantic_transaction_resolve(
    const GBSemanticTransaction* transaction,
    GBSemanticMemorySpace space,
    uint16_t bank,
    uint16_t address,
    uint8_t** data,
    size_t* size,
    size_t* offset) {
    if (transaction == NULL || data == NULL || size == NULL || offset == NULL) {
        return false;
    }
    switch (space) {
        case GB_SEMANTIC_EXTERNAL_RAM:
            if (address < 0xA000u || address >= 0xC000u) return false;
            *data = transaction->staged_eram;
            *size = transaction->eram_size;
            *offset = (size_t)bank * 0x2000u + (address - 0xA000u);
            return true;
        case GB_SEMANTIC_WRAM:
            if (bank != 0 || address < 0xC000u || address >= 0xD000u) {
                return false;
            }
            *data = transaction->staged_wram;
            *size = transaction->wram_size;
            *offset = address - 0xC000u;
            return true;
        case GB_SEMANTIC_BANKED_WRAM:
            if (bank == 0 || bank > 7 || address < 0xD000u ||
                address >= 0xE000u) {
                return false;
            }
            *data = transaction->staged_wram;
            *size = transaction->wram_size;
            *offset = (size_t)bank * 0x1000u + (address - 0xD000u);
            return true;
        default:
            return false;
    }
}

static bool gbrt_semantic_read_transaction(
    void* user,
    GBSemanticMemorySpace space,
    uint16_t bank,
    uint16_t address,
    uint8_t* output,
    size_t width) {
    const GBSemanticTransaction* transaction =
        (const GBSemanticTransaction*)user;
    if (transaction == NULL || !transaction->active || output == NULL ||
        width == 0 || transaction->context == NULL) {
        return false;
    }
    if (space == GB_SEMANTIC_PHYSICAL_ROM || space == GB_SEMANTIC_RTC) {
        return gbrt_semantic_read_live(
            transaction->context, space, bank, address, output, width);
    }
    uint8_t* data = NULL;
    size_t size = 0;
    size_t offset = 0;
    if (!gbrt_semantic_transaction_resolve(
            transaction, space, bank, address, &data, &size, &offset) ||
        data == NULL || offset > size || width > size - offset) {
        return false;
    }
    memcpy(output, data + offset, width);
    return true;
}

static void gbrt_semantic_transaction_publish(
    GBSemanticTransaction* transaction,
    GBSemanticTransactionOutcome outcome) {
    GBContext* context = transaction->context;
    if (context == NULL) return;
    context->semantic_transaction_sequence = transaction->sequence;
    context->semantic_transaction_outcome = outcome;
    context->semantic_transaction_dirty_count =
        (uint8_t)transaction->dirty_range_count;
    memset(
        context->semantic_transaction_dirty,
        0,
        sizeof(context->semantic_transaction_dirty));
    for (size_t index = 0; index < transaction->dirty_range_count; ++index) {
        const GBSemanticDirtyRange* source = &transaction->dirty_ranges[index];
        GBSemanticTransactionRangeMetadata* destination =
            &context->semantic_transaction_dirty[index];
        destination->space = (uint8_t)source->space;
        destination->bank = source->bank;
        destination->address = source->address;
        destination->width = (uint32_t)source->width;
    }
}

static void gbrt_semantic_transaction_release(
    GBSemanticTransaction* transaction) {
    free(transaction->staged_eram);
    free(transaction->staged_wram);
    transaction->staged_eram = NULL;
    transaction->staged_wram = NULL;
    transaction->active = false;
    transaction->validated = false;
}

static bool gbrt_semantic_ranges_touch(
    const GBSemanticDirtyRange* left,
    const GBSemanticDirtyRange* right) {
    if (left->space != right->space || left->bank != right->bank) return false;
    const size_t left_start = left->address;
    const size_t left_end = left_start + left->width;
    const size_t right_start = right->address;
    const size_t right_end = right_start + right->width;
    return left_start <= right_end && right_start <= left_end;
}

static GBSemanticStatus gbrt_semantic_transaction_record_range(
    GBSemanticTransaction* transaction,
    GBSemanticDirtyRange range) {
    for (size_t index = 0; index < transaction->dirty_range_count; ++index) {
        GBSemanticDirtyRange* current = &transaction->dirty_ranges[index];
        if (!gbrt_semantic_ranges_touch(current, &range)) continue;
        const size_t start =
            current->address < range.address ? current->address : range.address;
        const size_t current_end = (size_t)current->address + current->width;
        const size_t range_end = (size_t)range.address + range.width;
        const size_t end = current_end > range_end ? current_end : range_end;
        current->address = (uint16_t)start;
        current->width = end - start;

        for (size_t other = index + 1;
             other < transaction->dirty_range_count;) {
            if (!gbrt_semantic_ranges_touch(
                    current, &transaction->dirty_ranges[other])) {
                ++other;
                continue;
            }
            GBSemanticDirtyRange merged = transaction->dirty_ranges[other];
            const size_t merged_start =
                current->address < merged.address
                    ? current->address
                    : merged.address;
            const size_t merged_current_end =
                (size_t)current->address + current->width;
            const size_t merged_other_end =
                (size_t)merged.address + merged.width;
            current->address = (uint16_t)merged_start;
            current->width =
                (merged_current_end > merged_other_end
                     ? merged_current_end
                     : merged_other_end) -
                merged_start;
            memmove(
                &transaction->dirty_ranges[other],
                &transaction->dirty_ranges[other + 1],
                (transaction->dirty_range_count - other - 1) *
                    sizeof(transaction->dirty_ranges[0]));
            transaction->dirty_range_count--;
        }
        return GB_SEMANTIC_OK;
    }
    if (transaction->dirty_range_count >=
        GB_SEMANTIC_TRANSACTION_MAX_DIRTY_RANGES) {
        return GB_SEMANTIC_TOO_MANY_DIRTY_RANGES;
    }
    transaction->dirty_ranges[transaction->dirty_range_count++] = range;
    return GB_SEMANTIC_OK;
}

GBSemanticStatus gbrt_semantic_transaction_begin(
    GBSemanticTransaction* transaction,
    GBContext* context,
    const char* actual_rom_sha256,
    const char* expected_rom_sha256) {
    if (transaction == NULL || context == NULL || actual_rom_sha256 == NULL ||
        expected_rom_sha256 == NULL || context->rom == NULL ||
        context->rom_size == 0 || transaction->active ||
        (context->eram_size != 0 && context->eram == NULL)) {
        return GB_SEMANTIC_INVALID_ARGUMENT;
    }
    if (strcmp(actual_rom_sha256, expected_rom_sha256) != 0) {
        return GB_SEMANTIC_ROM_MISMATCH;
    }
    const size_t hash_size = strlen(actual_rom_sha256);
    if (hash_size == 0 || hash_size >= sizeof(transaction->rom_sha256)) {
        return GB_SEMANTIC_INVALID_ARGUMENT;
    }

    memset(transaction, 0, sizeof(*transaction));
    transaction->abi_version = GB_SEMANTIC_TRANSACTION_ABI_VERSION;
    transaction->context = context;
    memcpy(transaction->rom_sha256, actual_rom_sha256, hash_size + 1);
    transaction->eram_size = context->eram_size;
    transaction->wram_size = context->wram == NULL ? 0 : 0x8000u;
    if (transaction->eram_size != 0) {
        transaction->staged_eram = (uint8_t*)malloc(transaction->eram_size);
    }
    if (transaction->wram_size != 0) {
        transaction->staged_wram = (uint8_t*)malloc(transaction->wram_size);
    }
    if ((transaction->eram_size != 0 && transaction->staged_eram == NULL) ||
        (transaction->wram_size != 0 && transaction->staged_wram == NULL)) {
        gbrt_semantic_transaction_release(transaction);
        memset(transaction, 0, sizeof(*transaction));
        return GB_SEMANTIC_READ_FAILED;
    }
    if (transaction->eram_size != 0) {
        memcpy(
            transaction->staged_eram,
            context->eram,
            transaction->eram_size);
    }
    if (transaction->wram_size != 0) {
        memcpy(
            transaction->staged_wram,
            context->wram,
            transaction->wram_size);
    }
    transaction->sequence = context->semantic_transaction_sequence + 1u;
    transaction->active = true;
    return GB_SEMANTIC_OK;
}

GBSemanticStatus gbrt_semantic_transaction_write(
    GBSemanticTransaction* transaction,
    GBSemanticMemorySpace space,
    uint16_t bank,
    uint16_t address,
    const uint8_t* data,
    size_t width) {
    if (transaction == NULL || data == NULL || width == 0) {
        return GB_SEMANTIC_INVALID_ARGUMENT;
    }
    if (!transaction->active ||
        transaction->abi_version != GB_SEMANTIC_TRANSACTION_ABI_VERSION) {
        return GB_SEMANTIC_NOT_ACTIVE;
    }
    uint8_t* destination = NULL;
    size_t destination_size = 0;
    size_t offset = 0;
    if (!gbrt_semantic_transaction_resolve(
            transaction,
            space,
            bank,
            address,
            &destination,
            &destination_size,
            &offset) ||
        destination == NULL || offset > destination_size ||
        width > destination_size - offset ||
        (size_t)address + width > 0x10000u) {
        return GB_SEMANTIC_OUT_OF_RANGE;
    }
    GBSemanticStatus status = gbrt_semantic_transaction_record_range(
        transaction,
        (GBSemanticDirtyRange){space, bank, address, width});
    if (status != GB_SEMANTIC_OK) return status;
    memcpy(destination + offset, data, width);
    transaction->validated = false;
    return GB_SEMANTIC_OK;
}

GBSemanticStatus gbrt_semantic_transaction_reader(
    GBSemanticTransaction* transaction,
    GBSemanticReader* reader) {
    if (transaction == NULL || reader == NULL) {
        return GB_SEMANTIC_INVALID_ARGUMENT;
    }
    if (!transaction->active ||
        transaction->abi_version != GB_SEMANTIC_TRANSACTION_ABI_VERSION) {
        return GB_SEMANTIC_NOT_ACTIVE;
    }
    *reader = (GBSemanticReader){
        .abi_version = GB_SEMANTIC_READER_ABI_VERSION,
        .rom_sha256 = transaction->rom_sha256,
        .mode = GB_SEMANTIC_READ_LIVE,
        .user = transaction,
        .read = gbrt_semantic_read_transaction,
    };
    return GB_SEMANTIC_OK;
}

GBSemanticStatus gbrt_semantic_transaction_validate(
    GBSemanticTransaction* transaction,
    GBSemanticValidateFn validate,
    void* user) {
    if (transaction == NULL || validate == NULL) {
        return GB_SEMANTIC_INVALID_ARGUMENT;
    }
    if (!transaction->active ||
        transaction->abi_version != GB_SEMANTIC_TRANSACTION_ABI_VERSION) {
        return GB_SEMANTIC_NOT_ACTIVE;
    }
    GBSemanticReader reader;
    const GBSemanticStatus reader_status =
        gbrt_semantic_transaction_reader(transaction, &reader);
    if (reader_status != GB_SEMANTIC_OK) return reader_status;
    const GBSemanticStatus status = validate(&reader, user);
    if (status != GB_SEMANTIC_OK) {
        gbrt_semantic_transaction_publish(
            transaction, GB_SEMANTIC_TRANSACTION_VALIDATION_FAILED);
        gbrt_semantic_transaction_release(transaction);
        return status;
    }
    transaction->validated = true;
    return GB_SEMANTIC_OK;
}

GBSemanticStatus gbrt_semantic_transaction_commit(
    GBSemanticTransaction* transaction) {
    if (transaction == NULL) return GB_SEMANTIC_INVALID_ARGUMENT;
    if (!transaction->active ||
        transaction->abi_version != GB_SEMANTIC_TRANSACTION_ABI_VERSION) {
        return GB_SEMANTIC_NOT_ACTIVE;
    }
    if (!transaction->validated) return GB_SEMANTIC_NOT_VALIDATED;

    bool has_eram_write = false;
    for (size_t index = 0; index < transaction->dirty_range_count; ++index) {
        if (transaction->dirty_ranges[index].space ==
            GB_SEMANTIC_EXTERNAL_RAM) {
            has_eram_write = true;
            break;
        }
    }
    if (has_eram_write &&
        !gb_context_save_battery_snapshot(
            transaction->context,
            transaction->staged_eram,
            transaction->eram_size)) {
        gbrt_semantic_transaction_publish(
            transaction, GB_SEMANTIC_TRANSACTION_COMMIT_FAILED);
        gbrt_semantic_transaction_release(transaction);
        return GB_SEMANTIC_COMMIT_FAILED;
    }

    for (size_t index = 0; index < transaction->dirty_range_count; ++index) {
        const GBSemanticDirtyRange* range = &transaction->dirty_ranges[index];
        uint8_t* staged = NULL;
        size_t staged_size = 0;
        size_t offset = 0;
        if (!gbrt_semantic_transaction_resolve(
                transaction,
                range->space,
                range->bank,
                range->address,
                &staged,
                &staged_size,
                &offset)) {
            continue;
        }
        uint8_t* live =
            range->space == GB_SEMANTIC_EXTERNAL_RAM
                ? transaction->context->eram
                : transaction->context->wram;
        memcpy(live + offset, staged + offset, range->width);
    }
    gbrt_semantic_transaction_publish(
        transaction, GB_SEMANTIC_TRANSACTION_COMMITTED);
    gbrt_semantic_transaction_release(transaction);
    return GB_SEMANTIC_OK;
}

GBSemanticStatus gbrt_semantic_transaction_abort(
    GBSemanticTransaction* transaction) {
    if (transaction == NULL) return GB_SEMANTIC_INVALID_ARGUMENT;
    if (!transaction->active ||
        transaction->abi_version != GB_SEMANTIC_TRANSACTION_ABI_VERSION) {
        return GB_SEMANTIC_NOT_ACTIVE;
    }
    gbrt_semantic_transaction_publish(
        transaction, GB_SEMANTIC_TRANSACTION_ABORTED);
    gbrt_semantic_transaction_release(transaction);
    return GB_SEMANTIC_OK;
}
