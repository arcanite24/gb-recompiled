/* Evidence probe for generated Crystal semantic accessors. */
#include "crystal_semantic.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef CRYSTAL_SEMANTIC_STANDALONE
bool gb_context_save_battery_snapshot(
    GBContext* context,
    const uint8_t* data,
    size_t size) {
    (void)context;
    (void)data;
    (void)size;
    return false;
}
#endif

typedef struct LiveSource {
    const uint8_t* wram;
    size_t wram_size;
    const uint8_t* rom;
    size_t rom_size;
} LiveSource;

static bool read_live_bank(
    void* user,
    GBSemanticMemorySpace space,
    uint16_t bank,
    uint16_t address,
    uint8_t* output,
    size_t width) {
    const LiveSource* source = (const LiveSource*)user;
    if (source == NULL) {
        return false;
    }
    const uint8_t* data = NULL;
    size_t data_size = 0;
    size_t offset = 0;
    if (space == GB_SEMANTIC_BANKED_WRAM && bank == 1 &&
        address >= 0xD000u && address < 0xE000u) {
        data = source->wram;
        data_size = source->wram_size;
        offset = address - 0xD000u;
    } else if (space == GB_SEMANTIC_PHYSICAL_ROM &&
               ((bank == 0 && address < 0x4000u) ||
                (bank != 0 && address >= 0x4000u && address < 0x8000u))) {
        data = source->rom;
        data_size = source->rom_size;
        offset = bank == 0
            ? address
            : (size_t)bank * 0x4000u + (size_t)(address - 0x4000u);
    } else {
        return false;
    }
    if (data == NULL || offset > data_size || width > data_size - offset) {
        return false;
    }
    memcpy(output, data + offset, width);
    return true;
}

static bool load_file(
    const char* path,
    uint8_t** output,
    size_t* output_size) {
    FILE* file = fopen(path, "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        if (file != NULL) fclose(file);
        return false;
    }
    const long length = ftell(file);
    if (length <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }
    uint8_t* data = (uint8_t*)malloc((size_t)length);
    const bool ok =
        data != NULL && fread(data, 1, (size_t)length, file) == (size_t)length;
    fclose(file);
    if (!ok) {
        free(data);
        return false;
    }
    *output = data;
    *output_size = (size_t)length;
    return true;
}

static bool read_all(
    const GBSemanticReader* reader,
    GBSemanticReadMode mode,
    CrystalLocation* location,
    CrystalParty* party,
    CrystalBadges* badges,
    CrystalPokedexProgress* pokedex,
    CrystalSpeciesPage* species) {
    if (crystal_semantic_read_location(reader, mode, location) !=
               GB_SEMANTIC_OK ||
        crystal_semantic_read_party(reader, mode, party) !=
            GB_SEMANTIC_OK) {
        return false;
    }
    return crystal_semantic_read_badges(reader, mode, badges) ==
               GB_SEMANTIC_OK &&
           crystal_semantic_read_pokedex(reader, mode, pokedex) ==
               GB_SEMANTIC_OK &&
           party->count > 0 &&
           crystal_semantic_read_species(
               reader, mode, party->species[0], species) ==
               GB_SEMANTIC_OK;
}

static void print_view(
    const char* name,
    const CrystalLocation* location,
    const CrystalParty* party,
    const CrystalBadges* badges,
    const CrystalPokedexProgress* pokedex,
    const CrystalSpeciesPage* species,
    bool trailing_comma) {
    printf(
        "  \"%s\": {"
        "\"location\":{\"map_group\":%u,\"map_number\":%u,\"y\":%u,\"x\":%u},"
        "\"party\":{\"count\":%u,\"species\":[",
        name,
        location->map_group,
        location->map_number,
        location->y,
        location->x,
        party->count);
    for (size_t index = 0; index < CRYSTAL_PARTY_CAPACITY; ++index) {
        printf("%s%u", index ? "," : "", party->species[index]);
    }
    printf(
        "]},\"badges\":{\"johto_bits\":%u,\"kanto_bits\":%u,"
        "\"johto_count\":%u,\"kanto_count\":%u,\"total_count\":%u},"
        "\"pokedex\":{\"caught_count\":%u,\"seen_count\":%u},"
        "\"species\":{\"species_id\":%u,\"hp\":%u,\"attack\":%u,"
        "\"defense\":%u,\"speed\":%u,\"special_attack\":%u,"
        "\"special_defense\":%u,\"primary_type\":%u,"
        "\"secondary_type\":%u,\"catch_rate\":%u,"
        "\"base_experience\":%u,\"encounter_knowledge\":%u,"
        "\"evolutions\":[",
        badges->johto_bits,
        badges->kanto_bits,
        badges->johto_count,
        badges->kanto_count,
        badges->total_count,
        pokedex->caught_count,
        pokedex->seen_count,
        species->species_id,
        species->hp,
        species->attack,
        species->defense,
        species->speed,
        species->special_attack,
        species->special_defense,
        species->primary_type,
        species->secondary_type,
        species->catch_rate,
        species->base_experience,
        (unsigned)species->encounter_knowledge);
    for (size_t index = 0; index < species->evolution_count; ++index) {
        const CrystalEvolution* evolution = &species->evolutions[index];
        printf(
            "%s{\"method\":%u,\"parameter\":%u,\"condition\":%u,"
            "\"target_species\":%u}",
            index ? "," : "",
            evolution->method,
            evolution->parameter,
            evolution->condition,
            evolution->target_species);
    }
    printf("],\"level_moves\":[");
    for (size_t index = 0; index < species->level_move_count; ++index) {
        const CrystalLevelMove* move = &species->level_moves[index];
        printf(
            "%s{\"level\":%u,\"move_id\":%u}",
            index ? "," : "",
            move->level,
            move->move_id);
    }
    printf("]}}%s\n", trailing_comma ? "," : "");
}

int main(int argc, char** argv) {
    if (argc != 4) {
        fprintf(
            stderr,
            "usage: semantic_probe <wram-bank-1.bin> <save.sav> <game.gbc>\n");
        return 2;
    }
    uint8_t* wram = NULL;
    size_t wram_size = 0;
    uint8_t* save = NULL;
    size_t save_size = 0;
    uint8_t* rom = NULL;
    size_t rom_size = 0;
    if (!load_file(argv[1], &wram, &wram_size) ||
        !load_file(argv[2], &save, &save_size) ||
        !load_file(argv[3], &rom, &rom_size) ||
        wram_size != 0x1000u || save_size != 0x8000u ||
        rom_size != 0x200000u) {
        fprintf(stderr, "invalid semantic evidence input\n");
        free(wram);
        free(save);
        free(rom);
        return 2;
    }

    LiveSource live_source = {
        .wram = wram,
        .wram_size = wram_size,
        .rom = rom,
        .rom_size = rom_size,
    };
    GBSemanticReader live = {
        .abi_version = GB_SEMANTIC_READER_ABI_VERSION,
        .rom_sha256 = CRYSTAL_SEMANTIC_ROM_SHA256,
        .mode = GB_SEMANTIC_READ_LIVE,
        .user = &live_source,
        .read = read_live_bank,
    };
    GBSemanticSaveSource save_source = {
        .data = save,
        .size = save_size,
        .rom = rom,
        .rom_size = rom_size,
    };
    GBSemanticReader saved = {0};
    if (gbrt_semantic_reader_init_save(
            &saved, &save_source, CRYSTAL_SEMANTIC_ROM_SHA256) !=
        GB_SEMANTIC_OK) {
        free(wram);
        free(save);
        free(rom);
        return 3;
    }

    CrystalLocation live_location = {0}, saved_location = {0};
    CrystalParty live_party = {0}, saved_party = {0};
    CrystalBadges live_badges = {0}, saved_badges = {0};
    CrystalPokedexProgress live_pokedex = {0}, saved_pokedex = {0};
    CrystalSpeciesPage live_species = {0}, saved_species = {0};
    if (!read_all(
            &live,
            GB_SEMANTIC_READ_LIVE,
            &live_location,
            &live_party,
            &live_badges,
            &live_pokedex,
            &live_species) ||
        !read_all(
            &saved,
            GB_SEMANTIC_READ_SAVE,
            &saved_location,
            &saved_party,
            &saved_badges,
            &saved_pokedex,
            &saved_species)) {
        free(wram);
        free(save);
        free(rom);
        return 4;
    }

    printf("{\n");
    print_view(
        "live",
        &live_location,
        &live_party,
        &live_badges,
        &live_pokedex,
        &live_species,
        true);
    print_view(
        "save",
        &saved_location,
        &saved_party,
        &saved_badges,
        &saved_pokedex,
        &saved_species,
        false);
    printf("}\n");
    free(wram);
    free(save);
    free(rom);
    return 0;
}
