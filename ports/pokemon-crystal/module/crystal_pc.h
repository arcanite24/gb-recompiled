#ifndef CRYSTAL_PC_H
#define CRYSTAL_PC_H

#include "crystal_semantic.h"

#include <stddef.h>
#include <stdint.h>

typedef enum CrystalPCMove {
    CRYSTAL_PC_PARTY_TO_BOX = 0,
    CRYSTAL_PC_BOX_TO_PARTY = 1,
} CrystalPCMove;

typedef enum CrystalPCSort {
    CRYSTAL_PC_SORT_SPECIES = 0,
    CRYSTAL_PC_SORT_LEVEL = 1,
} CrystalPCSort;

typedef struct CrystalPCRecords {
    uint8_t box_index;
    uint8_t party[CRYSTAL_PARTY_RECORD_SIZE];
    uint8_t box[CRYSTAL_ACTIVE_BOX_RECORD_SIZE];
} CrystalPCRecords;

GBSemanticStatus crystal_pc_load(
    const GBSemanticReader* reader,
    CrystalPCRecords* records);

GBSemanticStatus crystal_pc_load_box(
    const GBSemanticReader* reader,
    uint8_t box_index,
    CrystalPCRecords* records);

size_t crystal_pc_search_box(
    const CrystalPCRecords* records,
    uint8_t species,
    size_t output[CRYSTAL_BOX_CAPACITY]);

GBSemanticStatus crystal_pc_sort_box(
    CrystalPCRecords* records,
    CrystalPCSort sort);

GBSemanticStatus crystal_pc_move(
    CrystalPCRecords* records,
    const GBSemanticReader* reader,
    CrystalPCMove move,
    size_t source_index);

GBSemanticStatus crystal_pc_stage(
    GBSemanticTransaction* transaction,
    void* user);

#endif
