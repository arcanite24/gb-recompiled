#include "gbrt_semantic.h"
#include "gbrt.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static uint8_t persisted_eram[0x4000];
static size_t persisted_size;
static bool persistence_should_fail;

static bool save_fixture(
    GBContext* context,
    const char* save_id,
    const void* data,
    size_t size) {
    (void)context;
    (void)save_id;
    if (persistence_should_fail || size > sizeof(persisted_eram)) {
        return false;
    }
    memcpy(persisted_eram, data, size);
    persisted_size = size;
    return true;
}

static bool read_fixture(void* user, GBSemanticMemorySpace space,
                         uint16_t bank, uint16_t address,
                         uint8_t* output, size_t width) {
    const uint8_t* memory = (const uint8_t*)user;
    if (space != GB_SEMANTIC_BANKED_WRAM || bank != 1 ||
        address != 0xD000 || width != 2) {
        return false;
    }
    memcpy(output, memory, width);
    return true;
}

typedef struct ValidateFixture {
    uint8_t expected_eram;
    uint8_t expected_wram;
    bool reject;
} ValidateFixture;

static GBSemanticStatus validate_fixture(
    const GBSemanticReader* reader,
    void* user) {
    const ValidateFixture* fixture = (const ValidateFixture*)user;
    uint8_t eram_byte = 0;
    uint8_t wram_byte = 0;
    if (gbrt_semantic_read(
            reader,
            reader->rom_sha256,
            GB_SEMANTIC_READ_LIVE,
            GB_SEMANTIC_EXTERNAL_RAM,
            1,
            0xA010,
            &eram_byte,
            1) != GB_SEMANTIC_OK ||
        gbrt_semantic_read(
            reader,
            reader->rom_sha256,
            GB_SEMANTIC_READ_LIVE,
            GB_SEMANTIC_BANKED_WRAM,
            1,
            0xD100,
            &wram_byte,
            1) != GB_SEMANTIC_OK) {
        return GB_SEMANTIC_READ_FAILED;
    }
    if (fixture->reject || eram_byte != fixture->expected_eram ||
        wram_byte != fixture->expected_wram) {
        return GB_SEMANTIC_INVALID_DATA;
    }
    return GB_SEMANTIC_OK;
}

int main(void) {
    const char* rom =
        "fdcc3c8c43813cf8731fc037d2a6d191bac75439c34b24ba1c27526e6acdc8a2";
    const uint8_t memory[2] = {0x12, 0x34};
    uint8_t output[2] = {0};
    GBSemanticReader reader = {
        .abi_version = GB_SEMANTIC_READER_ABI_VERSION,
        .rom_sha256 = rom,
        .mode = GB_SEMANTIC_READ_LIVE,
        .user = (void*)memory,
        .read = read_fixture,
    };
    if (gbrt_semantic_read(&reader, rom, GB_SEMANTIC_READ_LIVE,
                           GB_SEMANTIC_BANKED_WRAM,
                           1, 0xD000, output, sizeof(output)) !=
            GB_SEMANTIC_OK ||
        output[0] != 0x12 || output[1] != 0x34) {
        return 1;
    }
    reader.abi_version++;
    if (gbrt_semantic_read(&reader, rom, GB_SEMANTIC_READ_LIVE,
                           GB_SEMANTIC_BANKED_WRAM,
                           1, 0xD000, output, sizeof(output)) !=
        GB_SEMANTIC_ABI_MISMATCH) {
        return 1;
    }
    reader.abi_version = GB_SEMANTIC_READER_ABI_VERSION;
    if (gbrt_semantic_read(&reader, "wrong-rom", GB_SEMANTIC_READ_LIVE,
                           GB_SEMANTIC_BANKED_WRAM, 1, 0xD000,
                           output, sizeof(output)) !=
        GB_SEMANTIC_ROM_MISMATCH) {
        return 1;
    }
    if (gbrt_semantic_read(&reader, rom, GB_SEMANTIC_READ_SAVE,
                           GB_SEMANTIC_BANKED_WRAM, 1, 0xD000,
                           output, sizeof(output)) !=
        GB_SEMANTIC_WRONG_MODE) {
        return 1;
    }
    if (gbrt_semantic_read(&reader, rom, GB_SEMANTIC_READ_LIVE,
                           GB_SEMANTIC_BANKED_WRAM, 2, 0xD000,
                           output, sizeof(output)) !=
        GB_SEMANTIC_READ_FAILED) {
        return 1;
    }

    uint8_t rom_data[0x8000] = {0};
    uint8_t eram[0x4000] = {0};
    uint8_t wram[0x8000] = {0};
    rom_data[0x4000] = 0x42;
    eram[0x2000] = 0x53;
    wram[0x1000] = 0x64;
    GBContext context = {0};
    context.rom = rom_data;
    context.rom_size = sizeof(rom_data);
    context.eram = eram;
    context.eram_size = sizeof(eram);
    context.wram = wram;
    context.rtc.s = 17;
    if (gbrt_semantic_reader_init_live(&reader, &context, rom) !=
        GB_SEMANTIC_OK) {
        return 1;
    }
    uint8_t byte = 0;
    if (gbrt_semantic_read(
            &reader, rom, GB_SEMANTIC_READ_LIVE,
            GB_SEMANTIC_PHYSICAL_ROM, 1, 0x4000, &byte, 1) !=
            GB_SEMANTIC_OK ||
        byte != 0x42 ||
        gbrt_semantic_read(
            &reader, rom, GB_SEMANTIC_READ_LIVE,
            GB_SEMANTIC_EXTERNAL_RAM, 1, 0xA000, &byte, 1) !=
            GB_SEMANTIC_OK ||
        byte != 0x53 ||
        gbrt_semantic_read(
            &reader, rom, GB_SEMANTIC_READ_LIVE,
            GB_SEMANTIC_BANKED_WRAM, 1, 0xD000, &byte, 1) !=
            GB_SEMANTIC_OK ||
        byte != 0x64 ||
        gbrt_semantic_read(
            &reader, rom, GB_SEMANTIC_READ_LIVE,
            GB_SEMANTIC_RTC, 8, 0, &byte, 1) != GB_SEMANTIC_OK ||
        byte != 17) {
        return 1;
    }
    const uint8_t save_data[0x2000] = {0x71};
    const GBSemanticSaveSource save_source = {
        .data = save_data,
        .size = sizeof(save_data),
        .rom = rom_data,
        .rom_size = sizeof(rom_data),
    };
    if (gbrt_semantic_reader_init_save(&reader, &save_source, rom) !=
            GB_SEMANTIC_OK ||
        gbrt_semantic_read(
            &reader, rom, GB_SEMANTIC_READ_SAVE,
            GB_SEMANTIC_PHYSICAL_ROM, 1, 0x4000, &byte, 1) !=
            GB_SEMANTIC_OK ||
        byte != 0x42 ||
        gbrt_semantic_read(
            &reader, rom, GB_SEMANTIC_READ_SAVE,
            GB_SEMANTIC_EXTERNAL_RAM, 0, 0xA000, &byte, 1) !=
            GB_SEMANTIC_OK ||
        byte != 0x71) {
        return 1;
    }

    context.callbacks.save_battery_ram = save_fixture;
    strcpy(context.save_id, "semantic-test");
    GBSemanticTransaction transaction = {0};
    if (gbrt_semantic_transaction_begin(
            &transaction, &context, rom, "wrong-rom") !=
        GB_SEMANTIC_ROM_MISMATCH) {
        return 1;
    }
    if (gbrt_semantic_transaction_begin(
            &transaction, &context, rom, rom) != GB_SEMANTIC_OK) {
        return 1;
    }
    const uint8_t eram_write[2] = {0x91, 0x92};
    const uint8_t wram_write = 0xA3;
    if (gbrt_semantic_transaction_write(
            &transaction,
            GB_SEMANTIC_EXTERNAL_RAM,
            1,
            0xA010,
            eram_write,
            sizeof(eram_write)) != GB_SEMANTIC_OK ||
        gbrt_semantic_transaction_write(
            &transaction,
            GB_SEMANTIC_BANKED_WRAM,
            1,
            0xD100,
            &wram_write,
            1) != GB_SEMANTIC_OK ||
        gbrt_semantic_transaction_write(
            &transaction,
            GB_SEMANTIC_PHYSICAL_ROM,
            1,
            0x4000,
            &wram_write,
            1) != GB_SEMANTIC_OUT_OF_RANGE ||
        eram[0x2010] != 0 || wram[0x1100] != 0 ||
        gbrt_semantic_transaction_commit(&transaction) !=
            GB_SEMANTIC_NOT_VALIDATED) {
        return 1;
    }
    const ValidateFixture valid = {0x91, 0xA3, false};
    if (gbrt_semantic_transaction_validate(
            &transaction, validate_fixture, (void*)&valid) !=
            GB_SEMANTIC_OK ||
        gbrt_semantic_transaction_commit(&transaction) != GB_SEMANTIC_OK ||
        eram[0x2010] != 0x91 || eram[0x2011] != 0x92 ||
        wram[0x1100] != 0xA3 || persisted_size != sizeof(eram) ||
        persisted_eram[0x2010] != 0x91 ||
        context.semantic_transaction_sequence != 1 ||
        context.semantic_transaction_outcome !=
            GB_SEMANTIC_TRANSACTION_COMMITTED ||
        context.semantic_transaction_dirty_count != 2) {
        return 1;
    }

    if (gbrt_semantic_transaction_begin(
            &transaction, &context, rom, rom) != GB_SEMANTIC_OK) {
        return 1;
    }
    const uint8_t rejected_write = 0xB4;
    if (gbrt_semantic_transaction_write(
            &transaction,
            GB_SEMANTIC_EXTERNAL_RAM,
            1,
            0xA010,
            &rejected_write,
            1) != GB_SEMANTIC_OK) {
        return 1;
    }
    const ValidateFixture invalid = {0xB4, 0xA3, true};
    if (gbrt_semantic_transaction_validate(
            &transaction, validate_fixture, (void*)&invalid) !=
            GB_SEMANTIC_INVALID_DATA ||
        transaction.active || eram[0x2010] != 0x91 ||
        persisted_eram[0x2010] != 0x91 ||
        context.semantic_transaction_sequence != 2 ||
        context.semantic_transaction_outcome !=
            GB_SEMANTIC_TRANSACTION_VALIDATION_FAILED) {
        return 1;
    }

    if (gbrt_semantic_transaction_begin(
            &transaction, &context, rom, rom) != GB_SEMANTIC_OK ||
        gbrt_semantic_transaction_write(
            &transaction,
            GB_SEMANTIC_EXTERNAL_RAM,
            1,
            0xA010,
            &rejected_write,
            1) != GB_SEMANTIC_OK) {
        return 1;
    }
    persistence_should_fail = true;
    const ValidateFixture valid_but_unpersistable = {0xB4, 0xA3, false};
    if (gbrt_semantic_transaction_validate(
            &transaction,
            validate_fixture,
            (void*)&valid_but_unpersistable) != GB_SEMANTIC_OK ||
        gbrt_semantic_transaction_commit(&transaction) !=
            GB_SEMANTIC_COMMIT_FAILED ||
        transaction.active || eram[0x2010] != 0x91 ||
        persisted_eram[0x2010] != 0x91 ||
        context.semantic_transaction_sequence != 3 ||
        context.semantic_transaction_outcome !=
            GB_SEMANTIC_TRANSACTION_COMMIT_FAILED) {
        return 1;
    }
    persistence_should_fail = false;

    if (gbrt_semantic_transaction_begin(
            &transaction, &context, rom, rom) != GB_SEMANTIC_OK ||
        gbrt_semantic_transaction_write(
            &transaction,
            GB_SEMANTIC_EXTERNAL_RAM,
            1,
            0xA010,
            &rejected_write,
            1) != GB_SEMANTIC_OK ||
        gbrt_semantic_transaction_abort(&transaction) != GB_SEMANTIC_OK ||
        eram[0x2010] != 0x91 || persisted_eram[0x2010] != 0x91 ||
        context.semantic_transaction_sequence != 4 ||
        context.semantic_transaction_outcome !=
            GB_SEMANTIC_TRANSACTION_ABORTED) {
        return 1;
    }

    if (gbrt_semantic_transaction_begin(
            &transaction, &context, rom, rom) != GB_SEMANTIC_OK) {
        return 1;
    }
    for (uint16_t index = 0;
         index < GB_SEMANTIC_TRANSACTION_MAX_DIRTY_RANGES;
         ++index) {
        const uint8_t value = (uint8_t)index;
        if (gbrt_semantic_transaction_write(
                &transaction,
                GB_SEMANTIC_EXTERNAL_RAM,
                0,
                (uint16_t)(0xA100u + index * 2u),
                &value,
                1) != GB_SEMANTIC_OK) {
            return 1;
        }
    }
    const uint8_t overflow_value = 0xFF;
    if (gbrt_semantic_transaction_write(
            &transaction,
            GB_SEMANTIC_EXTERNAL_RAM,
            0,
            0xA200,
            &overflow_value,
            1) != GB_SEMANTIC_TOO_MANY_DIRTY_RANGES ||
        gbrt_semantic_transaction_abort(&transaction) != GB_SEMANTIC_OK ||
        context.semantic_transaction_dirty_count !=
            GB_SEMANTIC_TRANSACTION_MAX_DIRTY_RANGES) {
        return 1;
    }
    return 0;
}
