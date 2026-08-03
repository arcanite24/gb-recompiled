#include "crystal_pc.h"

#include <stdbool.h>
#include <string.h>

enum {
    SPECIES_OFFSET = 1,
    PARTY_MONS_OFFSET = 8,
    PARTY_MON_SIZE = 48,
    PARTY_OT_OFFSET = 296,
    PARTY_NICK_OFFSET = 362,
    BOX_MONS_OFFSET = 22,
    BOX_MON_SIZE = 32,
    BOX_OT_OFFSET = 662,
    BOX_NICK_OFFSET = 882,
};

static bool crystal_pc_is_mail(uint8_t item) {
    return item == 0x9Eu || (item >= 0xB5u && item <= 0xBDu);
}

static uint16_t read_be16(const uint8_t* value) {
    return (uint16_t)(((uint16_t)value[0] << 8u) | value[1]);
}

static void write_be16(uint8_t* output, uint16_t value) {
    output[0] = (uint8_t)(value >> 8u);
    output[1] = (uint8_t)value;
}

static uint8_t ceil_sqrt16(uint16_t value) {
    for (uint16_t root = 1; root < 255u; ++root) {
        if (root * root >= value) return (uint8_t)root;
    }
    return 255u;
}

static uint8_t hp_dv(const uint8_t* mon) {
    const uint8_t attack = (uint8_t)(mon[21] >> 4u);
    const uint8_t defense = (uint8_t)(mon[21] & 0x0Fu);
    const uint8_t speed = (uint8_t)(mon[22] >> 4u);
    const uint8_t special = (uint8_t)(mon[22] & 0x0Fu);
    return (uint8_t)(((attack & 1u) << 3u) |
                     ((defense & 1u) << 2u) |
                     ((speed & 1u) << 1u) |
                     (special & 1u));
}

static uint16_t calculate_stat(
    uint8_t base,
    uint8_t dv,
    uint16_t stat_exp,
    uint8_t level,
    bool hp) {
    const uint16_t effort = (uint16_t)(ceil_sqrt16(stat_exp) >> 2u);
    uint32_t value =
        ((uint32_t)((uint16_t)(base + dv) * 2u + effort) * level) /
        100u;
    value += hp ? (uint32_t)level + 10u : 5u;
    return (uint16_t)(value > 999u ? 999u : value);
}

static void remove_record(
    uint8_t* data,
    size_t offset,
    size_t width,
    size_t count,
    size_t index) {
    if (index + 1u < count) {
        memmove(
            data + offset + index * width,
            data + offset + (index + 1u) * width,
            (count - index - 1u) * width);
    }
    memset(data + offset + (count - 1u) * width, 0, width);
}

static void swap_record(
    uint8_t* data,
    size_t offset,
    size_t width,
    size_t left,
    size_t right) {
    uint8_t temporary[PARTY_MON_SIZE];
    memcpy(temporary, data + offset + left * width, width);
    memcpy(
        data + offset + left * width,
        data + offset + right * width,
        width);
    memcpy(data + offset + right * width, temporary, width);
}

GBSemanticStatus crystal_pc_load(
    const GBSemanticReader* reader,
    CrystalPCRecords* records) {
    if (reader == NULL || records == NULL) {
        return GB_SEMANTIC_INVALID_ARGUMENT;
    }
    GBSemanticStatus status = crystal_semantic_read_current_box(
        reader, &records->box_index);
    if (status != GB_SEMANTIC_OK) return status;
    return crystal_pc_load_box(reader, records->box_index, records);
}

GBSemanticStatus crystal_pc_load_box(
    const GBSemanticReader* reader,
    uint8_t box_index,
    CrystalPCRecords* records) {
    if (reader == NULL || records == NULL ||
        box_index >= CRYSTAL_BOX_COUNT) {
        return GB_SEMANTIC_INVALID_ARGUMENT;
    }
    GBSemanticStatus status = crystal_semantic_read_party_record(
        reader, GB_SEMANTIC_READ_LIVE, records->party);
    if (status != GB_SEMANTIC_OK) return status;
    status = crystal_semantic_read_box_record(
        reader, box_index, records->box);
    if (status == GB_SEMANTIC_OK) records->box_index = box_index;
    return status;
}

size_t crystal_pc_search_box(
    const CrystalPCRecords* records,
    uint8_t species,
    size_t output[CRYSTAL_BOX_CAPACITY]) {
    if (records == NULL || output == NULL) return 0;
    size_t count = 0;
    for (size_t index = 0; index < records->box[0]; ++index) {
        if (species == 0 ||
            records->box[SPECIES_OFFSET + index] == species) {
            output[count++] = index;
        }
    }
    return count;
}

GBSemanticStatus crystal_pc_sort_box(
    CrystalPCRecords* records,
    CrystalPCSort sort) {
    if (records == NULL ||
        (sort != CRYSTAL_PC_SORT_SPECIES &&
         sort != CRYSTAL_PC_SORT_LEVEL)) {
        return GB_SEMANTIC_INVALID_ARGUMENT;
    }
    const size_t count = records->box[0];
    for (size_t right = 1; right < count; ++right) {
        size_t cursor = right;
        while (cursor > 0) {
            const size_t left = cursor - 1u;
            const uint8_t left_key =
                sort == CRYSTAL_PC_SORT_SPECIES
                    ? records->box[SPECIES_OFFSET + left]
                    : records->box[
                          BOX_MONS_OFFSET + left * BOX_MON_SIZE + 31u];
            const uint8_t right_key =
                sort == CRYSTAL_PC_SORT_SPECIES
                    ? records->box[SPECIES_OFFSET + cursor]
                    : records->box[
                          BOX_MONS_OFFSET + cursor * BOX_MON_SIZE + 31u];
            if (left_key <= right_key) break;
            const uint8_t species =
                records->box[SPECIES_OFFSET + left];
            records->box[SPECIES_OFFSET + left] =
                records->box[SPECIES_OFFSET + cursor];
            records->box[SPECIES_OFFSET + cursor] = species;
            swap_record(
                records->box,
                BOX_MONS_OFFSET,
                BOX_MON_SIZE,
                left,
                cursor);
            swap_record(
                records->box,
                BOX_OT_OFFSET,
                CRYSTAL_NAME_LENGTH,
                left,
                cursor);
            swap_record(
                records->box,
                BOX_NICK_OFFSET,
                CRYSTAL_NAME_LENGTH,
                left,
                cursor);
            cursor = left;
        }
    }
    return GB_SEMANTIC_OK;
}

static GBSemanticStatus party_to_box(
    CrystalPCRecords* records,
    size_t source_index) {
    const size_t party_count = records->party[0];
    const size_t box_count = records->box[0];
    if (source_index >= party_count || party_count <= 1u ||
        box_count >= CRYSTAL_BOX_CAPACITY) {
        return GB_SEMANTIC_OUT_OF_RANGE;
    }
    const uint8_t* party_mon =
        records->party + PARTY_MONS_OFFSET +
        source_index * PARTY_MON_SIZE;
    if (crystal_pc_is_mail(party_mon[1])) {
        return GB_SEMANTIC_INVALID_DATA;
    }

    records->box[SPECIES_OFFSET + box_count] =
        records->party[SPECIES_OFFSET + source_index];
    records->box[SPECIES_OFFSET + box_count + 1u] = 0xFFu;
    memcpy(
        records->box + BOX_MONS_OFFSET + box_count * BOX_MON_SIZE,
        party_mon,
        BOX_MON_SIZE);
    memcpy(
        records->box + BOX_OT_OFFSET +
            box_count * CRYSTAL_NAME_LENGTH,
        records->party + PARTY_OT_OFFSET +
            source_index * CRYSTAL_NAME_LENGTH,
        CRYSTAL_NAME_LENGTH);
    memcpy(
        records->box + BOX_NICK_OFFSET +
            box_count * CRYSTAL_NAME_LENGTH,
        records->party + PARTY_NICK_OFFSET +
            source_index * CRYSTAL_NAME_LENGTH,
        CRYSTAL_NAME_LENGTH);
    records->box[0] = (uint8_t)(box_count + 1u);

    memmove(
        records->party + SPECIES_OFFSET + source_index,
        records->party + SPECIES_OFFSET + source_index + 1u,
        party_count - source_index);
    remove_record(
        records->party,
        PARTY_MONS_OFFSET,
        PARTY_MON_SIZE,
        party_count,
        source_index);
    remove_record(
        records->party,
        PARTY_OT_OFFSET,
        CRYSTAL_NAME_LENGTH,
        party_count,
        source_index);
    remove_record(
        records->party,
        PARTY_NICK_OFFSET,
        CRYSTAL_NAME_LENGTH,
        party_count,
        source_index);
    records->party[0] = (uint8_t)(party_count - 1u);
    return GB_SEMANTIC_OK;
}

static GBSemanticStatus box_to_party(
    CrystalPCRecords* records,
    const GBSemanticReader* reader,
    size_t source_index) {
    const size_t party_count = records->party[0];
    const size_t box_count = records->box[0];
    if (reader == NULL || source_index >= box_count ||
        party_count >= CRYSTAL_PARTY_CAPACITY) {
        return GB_SEMANTIC_OUT_OF_RANGE;
    }
    const uint8_t* box_mon =
        records->box + BOX_MONS_OFFSET + source_index * BOX_MON_SIZE;
    if (crystal_pc_is_mail(box_mon[1])) {
        return GB_SEMANTIC_INVALID_DATA;
    }
    CrystalSpeciesPage species;
    GBSemanticStatus status = crystal_semantic_read_species(
        reader,
        GB_SEMANTIC_READ_LIVE,
        box_mon[0],
        &species);
    if (status != GB_SEMANTIC_OK) return status;

    uint8_t* party_mon =
        records->party + PARTY_MONS_OFFSET + party_count * PARTY_MON_SIZE;
    memcpy(party_mon, box_mon, BOX_MON_SIZE);
    party_mon[32] = 0;
    party_mon[33] = 0;
    const uint8_t level = party_mon[31];
    const uint8_t dvs[6] = {
        hp_dv(party_mon),
        (uint8_t)(party_mon[21] >> 4u),
        (uint8_t)(party_mon[21] & 0x0Fu),
        (uint8_t)(party_mon[22] >> 4u),
        (uint8_t)(party_mon[22] & 0x0Fu),
        (uint8_t)(party_mon[22] & 0x0Fu),
    };
    const uint8_t bases[6] = {
        species.hp,
        species.attack,
        species.defense,
        species.speed,
        species.special_attack,
        species.special_defense,
    };
    const size_t stat_exp_offsets[6] = {11, 13, 15, 17, 19, 19};
    uint16_t stats[6];
    for (size_t index = 0; index < 6; ++index) {
        stats[index] = calculate_stat(
            bases[index],
            dvs[index],
            read_be16(party_mon + stat_exp_offsets[index]),
            level,
            index == 0);
        write_be16(party_mon + 36u + index * 2u, stats[index]);
    }
    write_be16(party_mon + 34, stats[0]);

    records->party[SPECIES_OFFSET + party_count] =
        records->box[SPECIES_OFFSET + source_index];
    records->party[SPECIES_OFFSET + party_count + 1u] = 0xFFu;
    memcpy(
        records->party + PARTY_OT_OFFSET +
            party_count * CRYSTAL_NAME_LENGTH,
        records->box + BOX_OT_OFFSET +
            source_index * CRYSTAL_NAME_LENGTH,
        CRYSTAL_NAME_LENGTH);
    memcpy(
        records->party + PARTY_NICK_OFFSET +
            party_count * CRYSTAL_NAME_LENGTH,
        records->box + BOX_NICK_OFFSET +
            source_index * CRYSTAL_NAME_LENGTH,
        CRYSTAL_NAME_LENGTH);
    records->party[0] = (uint8_t)(party_count + 1u);

    memmove(
        records->box + SPECIES_OFFSET + source_index,
        records->box + SPECIES_OFFSET + source_index + 1u,
        box_count - source_index);
    remove_record(
        records->box,
        BOX_MONS_OFFSET,
        BOX_MON_SIZE,
        box_count,
        source_index);
    remove_record(
        records->box,
        BOX_OT_OFFSET,
        CRYSTAL_NAME_LENGTH,
        box_count,
        source_index);
    remove_record(
        records->box,
        BOX_NICK_OFFSET,
        CRYSTAL_NAME_LENGTH,
        box_count,
        source_index);
    records->box[0] = (uint8_t)(box_count - 1u);
    return GB_SEMANTIC_OK;
}

GBSemanticStatus crystal_pc_move(
    CrystalPCRecords* records,
    const GBSemanticReader* reader,
    CrystalPCMove move,
    size_t source_index) {
    if (records == NULL) return GB_SEMANTIC_INVALID_ARGUMENT;
    if (move == CRYSTAL_PC_PARTY_TO_BOX) {
        return party_to_box(records, source_index);
    }
    if (move == CRYSTAL_PC_BOX_TO_PARTY) {
        return box_to_party(records, reader, source_index);
    }
    return GB_SEMANTIC_INVALID_ARGUMENT;
}

GBSemanticStatus crystal_pc_stage(
    GBSemanticTransaction* transaction,
    void* user) {
    CrystalPCRecords* records = (CrystalPCRecords*)user;
    if (transaction == NULL || records == NULL) {
        return GB_SEMANTIC_INVALID_ARGUMENT;
    }
    GBSemanticStatus status = crystal_semantic_stage_party(
        transaction, records->party, sizeof(records->party));
    if (status != GB_SEMANTIC_OK) return status;
    status = crystal_semantic_stage_box(
        transaction,
        records->box_index,
        records->box,
        sizeof(records->box));
    if (status != GB_SEMANTIC_OK) return status;
    return gbrt_semantic_transaction_validate(
        transaction,
        crystal_semantic_validate_transaction,
        NULL);
}
