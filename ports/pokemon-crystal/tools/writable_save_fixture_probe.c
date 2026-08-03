/* Export deterministic writable-save fixtures from a user-provided base save. */
#include "crystal_semantic.h"
#include "gbrt.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t durable_save[0x8000];

bool gb_context_save_battery_snapshot(
    GBContext* context,
    const uint8_t* data,
    size_t size) {
    (void)context;
    if (data == NULL || size != sizeof(durable_save)) return false;
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

static bool validate_and_commit(GBSemanticTransaction* transaction) {
    return gbrt_semantic_transaction_validate(
               transaction,
               crystal_semantic_validate_transaction,
               NULL) == GB_SEMANTIC_OK &&
           gbrt_semantic_transaction_commit(transaction) == GB_SEMANTIC_OK;
}

static bool stage_party_nickname(
    GBSemanticTransaction* transaction,
    const uint8_t* save,
    const char* nickname) {
    uint8_t party[CRYSTAL_PARTY_RECORD_SIZE];
    uint8_t encoded[CRYSTAL_NAME_LENGTH];
    memcpy(party, save + 0x2865, sizeof(party));
    if (party[0] == 0 ||
        crystal_semantic_encode_name(nickname, encoded) != GB_SEMANTIC_OK) {
        return false;
    }
    memcpy(party + 362, encoded, sizeof(encoded));
    return crystal_semantic_stage_party(
               transaction, party, sizeof(party)) == GB_SEMANTIC_OK;
}

static bool stage_party_mon_in_active_box(
    GBSemanticTransaction* transaction,
    const uint8_t* save) {
    const uint8_t* party = save + 0x2865;
    if (party[0] == 0 || party[1] == 0 || party[1] == 0xFFu) return false;
    uint8_t box[CRYSTAL_ACTIVE_BOX_RECORD_SIZE] = {0};
    box[0] = 1;
    box[1] = party[1];
    box[2] = 0xFFu;
    memcpy(box + 22, party + 8, 32);
    memcpy(box + 662, party + 296, CRYSTAL_NAME_LENGTH);
    memcpy(box + 882, party + 362, CRYSTAL_NAME_LENGTH);
    return crystal_semantic_stage_active_box(
               transaction, box, sizeof(box)) == GB_SEMANTIC_OK;
}

int main(int argc, char** argv) {
    if (argc != 5) {
        fprintf(
            stderr,
            "usage: fixture-probe <base.sav> <rom.gbc> <kind> <output.sav>\n");
        return 2;
    }
    uint8_t save[0x8000];
    uint8_t wram[0x8000] = {0};
    uint8_t* rom = (uint8_t*)malloc(0x200000);
    if (rom == NULL ||
        !read_exact(argv[1], save, sizeof(save)) ||
        !read_exact(argv[2], rom, 0x200000)) {
        free(rom);
        return 2;
    }
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
    bool ok = true;

    if (strcmp(argv[3], "baseline") == 0) {
        /* An explicit import/export identity fixture. */
    } else if (strcmp(argv[3], "party-nickname") == 0) {
        ok = begin(&transaction, &context) &&
             stage_party_nickname(&transaction, save, "M5PARTY") &&
             validate_and_commit(&transaction);
    } else if (strcmp(argv[3], "active-box-add") == 0) {
        ok = begin(&transaction, &context) &&
             stage_party_mon_in_active_box(&transaction, save) &&
             validate_and_commit(&transaction);
    } else if (strcmp(argv[3], "backup-fallback") == 0) {
        ok = begin(&transaction, &context) &&
             stage_party_nickname(&transaction, save, "BACKUP") &&
             validate_and_commit(&transaction);
        if (ok) {
            /*
             * Model an interrupted primary write after a valid dual-copy
             * transaction. The backup remains the last known-good copy.
             */
            save[0x200Bu] ^= 1u;
        }
    } else {
        fprintf(stderr, "unknown fixture kind: %s\n", argv[3]);
        free(rom);
        return 2;
    }

    if (!ok || !write_exact(argv[4], save, sizeof(save))) {
        free(rom);
        return 3;
    }
    printf(
        "{\"kind\":\"%s\",\"transaction_sequence\":%llu,"
        "\"transaction_outcome\":%u}\n",
        argv[3],
        (unsigned long long)context.semantic_transaction_sequence,
        (unsigned)context.semantic_transaction_outcome);
    free(rom);
    return 0;
}
