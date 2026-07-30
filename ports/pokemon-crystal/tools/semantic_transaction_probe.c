/* Standalone exact-ROM transaction probe for a user-provided Crystal save. */
#include "crystal_semantic.h"
#include "gbrt.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t durable_save[0x8000];
static bool fail_persistence;

bool gb_context_save_battery_snapshot(
    GBContext* context,
    const uint8_t* data,
    size_t size) {
    (void)context;
    if (fail_persistence || data == NULL || size != sizeof(durable_save)) {
        return false;
    }
    memcpy(durable_save, data, size);
    return true;
}

static bool read_exact(const char* path, uint8_t* data, size_t size) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) return false;
    const bool ok =
        fread(data, 1, size, file) == size && fgetc(file) == EOF;
    fclose(file);
    return ok;
}

static bool write_exact(const char* path, const uint8_t* data, size_t size) {
    FILE* file = fopen(path, "wb");
    if (file == NULL) return false;
    const bool ok =
        fwrite(data, 1, size, file) == size &&
        fflush(file) == 0 &&
        ferror(file) == 0;
    return fclose(file) == 0 && ok;
}

static bool begin(
    GBSemanticTransaction* transaction,
    GBContext* context) {
    return gbrt_semantic_transaction_begin(
               transaction,
               context,
               CRYSTAL_SEMANTIC_ROM_SHA256,
               CRYSTAL_SEMANTIC_ROM_SHA256) == GB_SEMANTIC_OK;
}

static bool unchanged(
    const uint8_t* live_save,
    const uint8_t* expected) {
    return memcmp(live_save, expected, sizeof(durable_save)) == 0 &&
           memcmp(durable_save, expected, sizeof(durable_save)) == 0;
}

int main(int argc, char** argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: probe <save.sav> <rom.gbc> <output.sav>\n");
        return 2;
    }
    uint8_t save[0x8000];
    uint8_t before[0x8000];
    uint8_t wram[0x8000] = {0};
    uint8_t* rom = (uint8_t*)malloc(0x200000);
    if (rom == NULL ||
        !read_exact(argv[1], save, sizeof(save)) ||
        !read_exact(argv[2], rom, 0x200000)) {
        free(rom);
        return 2;
    }
    memcpy(before, save, sizeof(before));
    memcpy(durable_save, save, sizeof(save));
    memcpy(
        wram + 0x1CD7,
        save + 0x2865,
        CRYSTAL_PARTY_RECORD_SIZE);

    GBContext context = {0};
    context.rom = rom;
    context.rom_size = 0x200000;
    context.eram = save;
    context.eram_size = sizeof(save);
    context.wram = wram;
    GBSemanticTransaction transaction = {0};

    if (!begin(&transaction, &context) ||
        crystal_semantic_stage_party(
            &transaction,
            save + 0x2865,
            CRYSTAL_PARTY_RECORD_SIZE) != GB_SEMANTIC_OK ||
        gbrt_semantic_transaction_validate(
            &transaction,
            crystal_semantic_validate_transaction,
            NULL) != GB_SEMANTIC_OK ||
        gbrt_semantic_transaction_commit(&transaction) != GB_SEMANTIC_OK ||
        !unchanged(save, before)) {
        free(rom);
        return 3;
    }
    if (!begin(&transaction, &context) ||
        crystal_semantic_stage_active_box(
            &transaction,
            save + 0x2D10,
            CRYSTAL_ACTIVE_BOX_RECORD_SIZE) != GB_SEMANTIC_OK ||
        gbrt_semantic_transaction_validate(
            &transaction,
            crystal_semantic_validate_transaction,
            NULL) != GB_SEMANTIC_OK ||
        gbrt_semantic_transaction_commit(&transaction) != GB_SEMANTIC_OK ||
        !unchanged(save, before)) {
        free(rom);
        return 4;
    }

    uint8_t invalid_party[CRYSTAL_PARTY_RECORD_SIZE];
    memcpy(invalid_party, save + 0x2865, sizeof(invalid_party));
    invalid_party[0] = 7;
    if (!begin(&transaction, &context) ||
        crystal_semantic_stage_party(
            &transaction, invalid_party, sizeof(invalid_party)) !=
            GB_SEMANTIC_INVALID_DATA ||
        crystal_semantic_stage_party(
            &transaction, save + 0x2865, sizeof(invalid_party) - 1) !=
            GB_SEMANTIC_INVALID_DATA ||
        gbrt_semantic_transaction_abort(&transaction) != GB_SEMANTIC_OK ||
        !unchanged(save, before)) {
        free(rom);
        return 5;
    }
    memcpy(invalid_party, save + 0x2865, sizeof(invalid_party));
    invalid_party[1] = 0;
    if (!begin(&transaction, &context) ||
        crystal_semantic_stage_party(
            &transaction, invalid_party, sizeof(invalid_party)) !=
            GB_SEMANTIC_INVALID_DATA ||
        gbrt_semantic_transaction_abort(&transaction) != GB_SEMANTIC_OK ||
        !unchanged(save, before)) {
        free(rom);
        return 6;
    }

    uint8_t invalid_box[CRYSTAL_ACTIVE_BOX_RECORD_SIZE];
    memcpy(invalid_box, save + 0x2D10, sizeof(invalid_box));
    invalid_box[0] = 21;
    if (!begin(&transaction, &context) ||
        crystal_semantic_stage_active_box(
            &transaction, invalid_box, sizeof(invalid_box)) !=
            GB_SEMANTIC_INVALID_DATA ||
        gbrt_semantic_transaction_abort(&transaction) != GB_SEMANTIC_OK ||
        !unchanged(save, before)) {
        free(rom);
        return 7;
    }

    const uint8_t corrupt =
        (uint8_t)(save[0x2865] ^ 1u);
    if (!begin(&transaction, &context) ||
        gbrt_semantic_transaction_write(
            &transaction,
            GB_SEMANTIC_EXTERNAL_RAM,
            1,
            0xA865,
            &corrupt,
            1) != GB_SEMANTIC_OK ||
        gbrt_semantic_transaction_validate(
            &transaction,
            crystal_semantic_validate_transaction,
            NULL) != GB_SEMANTIC_INVALID_DATA ||
        !unchanged(save, before)) {
        free(rom);
        return 8;
    }

    uint8_t edited_party[CRYSTAL_PARTY_RECORD_SIZE];
    memcpy(edited_party, save + 0x2865, sizeof(edited_party));
    edited_party[362] =
        edited_party[362] == 0x80u ? 0x81u : 0x80u;
    fail_persistence = true;
    if (!begin(&transaction, &context) ||
        crystal_semantic_stage_party(
            &transaction, edited_party, sizeof(edited_party)) !=
            GB_SEMANTIC_OK ||
        gbrt_semantic_transaction_validate(
            &transaction,
            crystal_semantic_validate_transaction,
            NULL) != GB_SEMANTIC_OK ||
        gbrt_semantic_transaction_commit(&transaction) !=
            GB_SEMANTIC_COMMIT_FAILED ||
        context.semantic_transaction_outcome !=
            GB_SEMANTIC_TRANSACTION_COMMIT_FAILED ||
        !unchanged(save, before)) {
        free(rom);
        return 9;
    }
    fail_persistence = false;

    if (!write_exact(argv[3], durable_save, sizeof(durable_save))) {
        free(rom);
        return 10;
    }
    printf(
        "{\"passed\":true,\"committed_noop_transactions\":2,"
        "\"rejected_controls\":5,\"final_sequence\":%llu}\n",
        (unsigned long long)context.semantic_transaction_sequence);
    free(rom);
    return 0;
}
