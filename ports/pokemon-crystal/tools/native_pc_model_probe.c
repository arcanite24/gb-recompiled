/* Portable synthetic edit-matrix probe for Crystal's native PC model. */
#include "crystal_pc.h"
#include "gbrt.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

bool gb_context_save_battery_snapshot(
    GBContext* context,
    const uint8_t* data,
    size_t size) {
    (void)context;
    (void)data;
    (void)size;
    return true;
}

static bool synthetic_read(
    void* user,
    GBSemanticMemorySpace space,
    uint16_t bank,
    uint16_t address,
    uint8_t* output,
    size_t width) {
    (void)user;
    memset(output, 0, width);
    if (space != GB_SEMANTIC_PHYSICAL_ROM) return false;
    if (bank == 20u && width == 32u && address >= 0x5424u) {
        const uint16_t delta = (uint16_t)(address - 0x5424u);
        if (delta % 32u != 0u || delta / 32u >= 251u) return false;
        const uint8_t species = (uint8_t)(delta / 32u + 1u);
        output[0] = species;
        output[1] = (uint8_t)(40u + species % 30u);
        output[2] = (uint8_t)(45u + species % 30u);
        output[3] = (uint8_t)(35u + species % 30u);
        output[4] = (uint8_t)(50u + species % 30u);
        output[5] = (uint8_t)(55u + species % 30u);
        output[6] = (uint8_t)(45u + species % 30u);
        return true;
    }
    if (bank == 16u && width == 2u &&
        address >= 0x65B1u && address < 0x67A7u) {
        output[0] = 0x00u;
        output[1] = 0x70u;
        return true;
    }
    if (bank == 16u && width == 1u &&
        (address == 0x7000u || address == 0x7001u)) {
        output[0] = 0;
        return true;
    }
    return false;
}

static GBSemanticReader reader(void) {
    return (GBSemanticReader){
        .abi_version = GB_SEMANTIC_READER_ABI_VERSION,
        .rom_sha256 = CRYSTAL_SEMANTIC_ROM_SHA256,
        .mode = GB_SEMANTIC_READ_LIVE,
        .read = synthetic_read,
    };
}

static void name(uint8_t* output, uint8_t letter) {
    memset(output, 0x50, CRYSTAL_NAME_LENGTH);
    output[0] = letter;
}

static void box_mon(
    CrystalPCRecords* records,
    size_t index,
    uint8_t species,
    uint8_t level,
    uint8_t item) {
    records->box[1u + index] = species;
    uint8_t* mon = records->box + 22u + index * 32u;
    memset(mon, 0, 32);
    mon[0] = species;
    mon[1] = item;
    mon[21] = 0xABu;
    mon[22] = 0xCDu;
    mon[31] = level;
    name(records->box + 662u + index * 11u, (uint8_t)(0x80u + index));
    name(records->box + 882u + index * 11u, (uint8_t)(0x90u + index));
}

static void finish_box(CrystalPCRecords* records, size_t count) {
    records->box[0] = (uint8_t)count;
    records->box[1u + count] = 0xFFu;
}

static void party_mon(
    CrystalPCRecords* records,
    size_t index,
    uint8_t species,
    uint8_t level,
    uint8_t item) {
    records->party[1u + index] = species;
    uint8_t* mon = records->party + 8u + index * 48u;
    memset(mon, 0, 48);
    mon[0] = species;
    mon[1] = item;
    mon[21] = 0xABu;
    mon[22] = 0xCDu;
    mon[31] = level;
    mon[35] = 20;
    mon[37] = 20;
    name(records->party + 296u + index * 11u, (uint8_t)(0x80u + index));
    name(records->party + 362u + index * 11u, (uint8_t)(0x90u + index));
}

static void finish_party(CrystalPCRecords* records, size_t count) {
    records->party[0] = (uint8_t)count;
    records->party[1u + count] = 0xFFu;
}

static int test_search_and_sort(void) {
    CrystalPCRecords records = {0};
    box_mon(&records, 0, 3, 30, 1);
    box_mon(&records, 1, 1, 10, 2);
    box_mon(&records, 2, 2, 20, 3);
    finish_box(&records, 3);
    size_t matches[CRYSTAL_BOX_CAPACITY] = {0};
    if (crystal_pc_search_box(&records, 2, matches) != 1 ||
        matches[0] != 2 ||
        crystal_pc_search_box(&records, 0, matches) != 3) {
        return 1;
    }
    if (crystal_pc_sort_box(&records, CRYSTAL_PC_SORT_LEVEL) !=
            GB_SEMANTIC_OK ||
        records.box[1] != 1 || records.box[2] != 2 ||
        records.box[3] != 3 ||
        records.box[22 + 1] != 2 ||
        records.box[22 + 32 + 1] != 3 ||
        records.box[22 + 64 + 1] != 1 ||
        records.box[662] != 0x81u ||
        records.box[882] != 0x91u) {
        return 2;
    }
    return 0;
}

static int test_round_trip(void) {
    CrystalPCRecords records = {0};
    party_mon(&records, 0, 1, 8, 4);
    finish_party(&records, 1);
    box_mon(&records, 0, 2, 12, 5);
    finish_box(&records, 1);
    uint8_t source_mon[32];
    uint8_t source_ot[11];
    uint8_t source_nick[11];
    memcpy(source_mon, records.box + 22, sizeof(source_mon));
    memcpy(source_ot, records.box + 662, sizeof(source_ot));
    memcpy(source_nick, records.box + 882, sizeof(source_nick));
    GBSemanticReader semantic_reader = reader();
    if (crystal_pc_move(
            &records,
            &semantic_reader,
            CRYSTAL_PC_BOX_TO_PARTY,
            0) != GB_SEMANTIC_OK ||
        records.party[0] != 2 || records.box[0] != 0 ||
        memcmp(records.party + 8 + 48, source_mon, sizeof(source_mon)) != 0 ||
        memcmp(records.party + 296 + 11, source_ot, sizeof(source_ot)) != 0 ||
        memcmp(
            records.party + 362 + 11,
            source_nick,
            sizeof(source_nick)) != 0 ||
        (records.party[8 + 48 + 36] == 0 &&
         records.party[8 + 48 + 37] == 0)) {
        return 3;
    }
    if (crystal_pc_move(
            &records,
            &semantic_reader,
            CRYSTAL_PC_PARTY_TO_BOX,
            1) != GB_SEMANTIC_OK ||
        records.party[0] != 1 || records.box[0] != 1 ||
        memcmp(records.box + 22, source_mon, sizeof(source_mon)) != 0 ||
        memcmp(records.box + 662, source_ot, sizeof(source_ot)) != 0 ||
        memcmp(records.box + 882, source_nick, sizeof(source_nick)) != 0) {
        return 4;
    }
    return 0;
}

static int test_rejections(void) {
    GBSemanticReader semantic_reader = reader();
    CrystalPCRecords records = {0};
    party_mon(&records, 0, 1, 8, 0);
    finish_party(&records, 1);
    CrystalPCRecords before = records;
    if (crystal_pc_move(
            &records,
            &semantic_reader,
            CRYSTAL_PC_PARTY_TO_BOX,
            0) != GB_SEMANTIC_OUT_OF_RANGE ||
        memcmp(&records, &before, sizeof(records)) != 0) {
        return 5;
    }

    party_mon(&records, 1, 2, 9, 0x9Eu);
    finish_party(&records, 2);
    before = records;
    if (crystal_pc_move(
            &records,
            &semantic_reader,
            CRYSTAL_PC_PARTY_TO_BOX,
            1) != GB_SEMANTIC_INVALID_DATA ||
        memcmp(&records, &before, sizeof(records)) != 0) {
        return 6;
    }

    records = (CrystalPCRecords){0};
    party_mon(&records, 0, 1, 8, 0);
    finish_party(&records, 1);
    box_mon(&records, 0, 2, 9, 0xB5u);
    finish_box(&records, 1);
    before = records;
    if (crystal_pc_move(
            &records,
            &semantic_reader,
            CRYSTAL_PC_BOX_TO_PARTY,
            0) != GB_SEMANTIC_INVALID_DATA ||
        memcmp(&records, &before, sizeof(records)) != 0) {
        return 7;
    }

    records = (CrystalPCRecords){0};
    for (size_t index = 0; index < CRYSTAL_PARTY_CAPACITY; ++index) {
        party_mon(&records, index, (uint8_t)(index + 1u), 10, 0);
    }
    finish_party(&records, CRYSTAL_PARTY_CAPACITY);
    box_mon(&records, 0, 7, 10, 0);
    finish_box(&records, 1);
    before = records;
    if (crystal_pc_move(
            &records,
            &semantic_reader,
            CRYSTAL_PC_BOX_TO_PARTY,
            0) != GB_SEMANTIC_OUT_OF_RANGE ||
        memcmp(&records, &before, sizeof(records)) != 0) {
        return 8;
    }
    return 0;
}

int main(void) {
    int status = test_search_and_sort();
    if (status != 0) return status;
    status = test_round_trip();
    if (status != 0) return status;
    return test_rejections();
}
