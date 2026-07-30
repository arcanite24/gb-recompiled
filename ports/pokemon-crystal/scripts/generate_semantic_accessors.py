#!/usr/bin/env python3
"""Generate the exact-ROM Crystal semantic read and transaction API."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


EXPECTED_ROM = "fdcc3c8c43813cf8731fc037d2a6d191bac75439c34b24ba1c27526e6acdc8a2"
EXPOSED = (
    ("player.location", "CRYSTAL_SEMANTIC_LOCATION", "CrystalLocation"),
    ("player.party", "CRYSTAL_SEMANTIC_PARTY", "CrystalParty"),
    ("player.badges", "CRYSTAL_SEMANTIC_BADGES", "CrystalBadges"),
    ("player.pokedex", "CRYSTAL_SEMANTIC_POKEDEX", "CrystalPokedexProgress"),
)
SPACES = {
    "physical_rom": "GB_SEMANTIC_PHYSICAL_ROM",
    "external_ram": "GB_SEMANTIC_EXTERNAL_RAM",
    "wram": "GB_SEMANTIC_WRAM",
    "banked_wram": "GB_SEMANTIC_BANKED_WRAM",
}


def fail(message: str) -> None:
    raise ValueError(message)


def parse_contract(view_id: str, raw: object) -> tuple[str, int, int, int]:
    if not isinstance(raw, dict):
        fail(f"{view_id} is missing a memory contract")
    space = raw.get("space")
    bank = raw.get("bank")
    address = raw.get("address")
    width = raw.get("width")
    if (
        space not in SPACES
        or not isinstance(bank, int)
        or not isinstance(address, str)
        or re.fullmatch(r"0x[0-9a-f]{4}", address) is None
        or not isinstance(width, int)
        or width <= 0
    ):
        fail(f"{view_id} has an unsupported memory contract")
    return SPACES[space], bank, int(address, 0), width


def render_header() -> str:
    return """\
/* Generated from semantic/package.json. Do not edit. */
#ifndef CRYSTAL_SEMANTIC_H
#define CRYSTAL_SEMANTIC_H

#include "gbrt_semantic.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CRYSTAL_SEMANTIC_ROM_SHA256 \\
    "fdcc3c8c43813cf8731fc037d2a6d191bac75439c34b24ba1c27526e6acdc8a2"
#define CRYSTAL_PARTY_CAPACITY 6u
#define CRYSTAL_BOX_CAPACITY 20u
#define CRYSTAL_BOX_COUNT 14u
#define CRYSTAL_SPECIES_COUNT 251u
#define CRYSTAL_PARTY_RECORD_SIZE 428u
#define CRYSTAL_ACTIVE_BOX_RECORD_SIZE 1102u
#define CRYSTAL_NAME_LENGTH 11u
#define CRYSTAL_MAX_EVOLUTIONS 4u
#define CRYSTAL_MAX_LEVEL_MOVES 32u

typedef enum CrystalSemanticView {
    CRYSTAL_SEMANTIC_LOCATION = 0,
    CRYSTAL_SEMANTIC_PARTY = 1,
    CRYSTAL_SEMANTIC_BADGES = 2,
    CRYSTAL_SEMANTIC_POKEDEX = 3,
    CRYSTAL_SEMANTIC_VIEW_COUNT = 4,
} CrystalSemanticView;

typedef struct CrystalLocation {
    uint8_t map_group;
    uint8_t map_number;
    uint8_t y;
    uint8_t x;
} CrystalLocation;

typedef struct CrystalBattleMon {
    uint8_t species;
    uint8_t level;
} CrystalBattleMon;

typedef struct CrystalBattleContext {
    uint8_t mode;
    uint8_t temp_wild_species;
    uint8_t trainer_class;
    uint8_t battle_type;
} CrystalBattleContext;

typedef struct CrystalParty {
    uint8_t count;
    uint8_t species[CRYSTAL_PARTY_CAPACITY];
} CrystalParty;

typedef struct CrystalBadges {
    uint8_t johto_bits;
    uint8_t kanto_bits;
    uint8_t johto_count;
    uint8_t kanto_count;
    uint8_t total_count;
} CrystalBadges;

typedef struct CrystalPokedexProgress {
    uint16_t caught_count;
    uint16_t seen_count;
} CrystalPokedexProgress;

typedef enum CrystalKnowledgeStatus {
    CRYSTAL_KNOWLEDGE_AVAILABLE = 0,
    CRYSTAL_KNOWLEDGE_NOT_MODELED = 1,
    CRYSTAL_KNOWLEDGE_LOCKED = 2,
} CrystalKnowledgeStatus;

typedef struct CrystalEvolution {
    uint8_t method;
    uint8_t parameter;
    uint8_t condition;
    uint8_t target_species;
} CrystalEvolution;

typedef struct CrystalLevelMove {
    uint8_t level;
    uint8_t move_id;
} CrystalLevelMove;

typedef struct CrystalSpeciesPage {
    uint8_t species_id;
    uint8_t hp;
    uint8_t attack;
    uint8_t defense;
    uint8_t speed;
    uint8_t special_attack;
    uint8_t special_defense;
    uint8_t primary_type;
    uint8_t secondary_type;
    uint8_t catch_rate;
    uint8_t base_experience;
    uint8_t evolution_count;
    CrystalEvolution evolutions[CRYSTAL_MAX_EVOLUTIONS];
    uint8_t level_move_count;
    CrystalLevelMove level_moves[CRYSTAL_MAX_LEVEL_MOVES];
    CrystalKnowledgeStatus encounter_knowledge;
} CrystalSpeciesPage;

size_t crystal_semantic_view_size(CrystalSemanticView view);

GBSemanticStatus crystal_semantic_read_view(
    const GBSemanticReader* reader,
    GBSemanticReadMode mode,
    CrystalSemanticView view,
    void* output,
    size_t output_size);

GBSemanticStatus crystal_semantic_read_location(
    const GBSemanticReader* reader,
    GBSemanticReadMode mode,
    CrystalLocation* output);

GBSemanticStatus crystal_semantic_read_party(
    const GBSemanticReader* reader,
    GBSemanticReadMode mode,
    CrystalParty* output);

GBSemanticStatus crystal_semantic_read_badges(
    const GBSemanticReader* reader,
    GBSemanticReadMode mode,
    CrystalBadges* output);

GBSemanticStatus crystal_semantic_read_pokedex(
    const GBSemanticReader* reader,
    GBSemanticReadMode mode,
    CrystalPokedexProgress* output);

GBSemanticStatus crystal_semantic_read_battle(
    const GBSemanticReader* reader,
    GBSemanticReadMode mode,
    CrystalBattleMon* player,
    CrystalBattleMon* enemy,
    CrystalBattleContext* context);

GBSemanticStatus crystal_semantic_read_species(
    const GBSemanticReader* reader,
    GBSemanticReadMode mode,
    uint8_t species_id,
    CrystalSpeciesPage* output);

GBSemanticStatus crystal_semantic_read_party_record(
    const GBSemanticReader* reader,
    GBSemanticReadMode mode,
    uint8_t output[CRYSTAL_PARTY_RECORD_SIZE]);

GBSemanticStatus crystal_semantic_read_active_box_record(
    const GBSemanticReader* reader,
    uint8_t output[CRYSTAL_ACTIVE_BOX_RECORD_SIZE]);

GBSemanticStatus crystal_semantic_read_box_record(
    const GBSemanticReader* reader,
    uint8_t box_index,
    uint8_t output[CRYSTAL_ACTIVE_BOX_RECORD_SIZE]);

GBSemanticStatus crystal_semantic_read_current_box(
    const GBSemanticReader* reader,
    uint8_t* box_index);

GBSemanticStatus crystal_semantic_encode_name(
    const char* ascii,
    uint8_t output[CRYSTAL_NAME_LENGTH]);

GBSemanticStatus crystal_semantic_stage_party(
    GBSemanticTransaction* transaction,
    const uint8_t* party,
    size_t party_size);

GBSemanticStatus crystal_semantic_stage_active_box(
    GBSemanticTransaction* transaction,
    const uint8_t* box,
    size_t box_size);

GBSemanticStatus crystal_semantic_stage_box(
    GBSemanticTransaction* transaction,
    uint8_t box_index,
    const uint8_t* box,
    size_t box_size);

GBSemanticStatus crystal_semantic_validate_transaction(
    const GBSemanticReader* staged_reader,
    void* user);

#ifdef __cplusplus
}
#endif

#endif
"""


def render_contract(space: str, bank: int, address: int, width: int) -> str:
    return (
        "{true, "
        f"{space}, {bank}u, 0x{address:04X}u, {width}u"
        "}"
    )


def render_source(views: dict[str, dict]) -> str:
    contracts: list[str] = []
    widths: list[int] = []
    for view_id, _, _ in EXPOSED:
        view = views[view_id]
        live = parse_contract(view_id, view.get("memory"))
        save = parse_contract(view_id, view.get("save_memory"))
        backup = parse_contract(view_id, view.get("backup_memory"))
        if live[3] != save[3] or live[3] != backup[3]:
            fail(f"{view_id} live/save/backup widths differ")
        widths.append(live[3])
        contracts.append(
            "    {"
            f"{render_contract(*live)}, {render_contract(*save)}, "
            f"{render_contract(*backup)}"
            "},"
        )
    contract_values = "\n".join(contracts)
    party_view = views["player.party"]
    party_backup = parse_contract(
        "player.party backup", party_view.get("backup_memory")
    )
    expected_backups = {
        "player.location": (
            "GB_SEMANTIC_EXTERNAL_RAM", 0, 0xBA43, 4
        ),
        "player.party": (
            "GB_SEMANTIC_EXTERNAL_RAM", 0, 0xBA65, 428
        ),
        "player.badges": (
            "GB_SEMANTIC_EXTERNAL_RAM", 0, 0xB5E5, 2
        ),
        "player.pokedex": (
            "GB_SEMANTIC_EXTERNAL_RAM", 0, 0xBC27, 64
        ),
    }
    active_box = parse_contract(
        "storage.active_box", views["storage.active_box"].get("memory")
    )
    canonical_box = views["storage.active_box"].get("canonical_memory")
    if (
        party_view.get("access") != "transactional_write"
        or widths[1] != 428
        or party_backup
        != ("GB_SEMANTIC_EXTERNAL_RAM", 0, 0xBA65, 428)
        or any(
            parse_contract(
                f"{view_id} backup",
                views[view_id].get("backup_memory"),
            )
            != expected
            for view_id, expected in expected_backups.items()
        )
        or views["storage.active_box"].get("access")
        != "transactional_write"
        or active_box
        != ("GB_SEMANTIC_EXTERNAL_RAM", 1, 0xAD10, 1102)
        or canonical_box
        != {
            "space": "external_ram",
            "first_bank": 2,
            "bank_count": 2,
            "address": "0xa000",
            "stride": 1102,
            "items_per_bank": 7,
            "width": 1102,
            "selector_memory": {
                "space": "external_ram",
                "bank": 1,
                "address": "0xa700",
                "width": 1,
            },
            "selector_max": 13,
        }
    ):
        fail("unsupported Crystal transactional party or box layout")
    base_data = parse_contract(
        "species.base_data", views["species.base_data"].get("memory")
    )
    evolution_pointers = parse_contract(
        "species.evolutions_and_moves",
        views["species.evolutions_and_moves"].get("memory"),
    )
    if (
        base_data[0] != "GB_SEMANTIC_PHYSICAL_ROM"
        or base_data[3] != 251 * 32
        or evolution_pointers[0] != "GB_SEMANTIC_PHYSICAL_ROM"
        or evolution_pointers[3] != 251 * 2
    ):
        fail("unsupported Crystal species table layout")
    battle_player = parse_contract(
        "battle.player", views["battle.player"].get("memory")
    )
    battle_active_slot = parse_contract(
        "battle.active_slot", views["battle.active_slot"].get("memory")
    )
    battle_enemy = parse_contract(
        "battle.enemy", views["battle.enemy"].get("memory")
    )
    battle_context = parse_contract(
        "battle.context", views["battle.context"].get("memory")
    )
    if (
        battle_player != ("GB_SEMANTIC_WRAM", 0, 0xC62C, 14)
        or battle_active_slot
        != ("GB_SEMANTIC_BANKED_WRAM", 1, 0xD0D4, 1)
        or battle_enemy != ("GB_SEMANTIC_BANKED_WRAM", 1, 0xD206, 14)
        or battle_context != ("GB_SEMANTIC_BANKED_WRAM", 1, 0xD22D, 4)
    ):
        fail("unsupported Crystal battle layout")
    return f"""\
/* Generated from semantic/package.json. Do not edit. */
#include "crystal_semantic.h"

#include <stdbool.h>
#include <string.h>

typedef struct CrystalReadContract {{
    bool supported;
    GBSemanticMemorySpace space;
    uint16_t bank;
    uint16_t address;
    size_t width;
}} CrystalReadContract;

static const CrystalReadContract
    crystal_contracts[CRYSTAL_SEMANTIC_VIEW_COUNT][3] = {{
{contract_values}
}};

static uint8_t crystal_popcount8(uint8_t value) {{
    uint8_t count = 0;
    while (value != 0) {{
        count = (uint8_t)(count + (value & 1u));
        value = (uint8_t)(value >> 1u);
    }}
    return count;
}}

size_t crystal_semantic_view_size(CrystalSemanticView view) {{
    switch (view) {{
        case CRYSTAL_SEMANTIC_LOCATION: return sizeof(CrystalLocation);
        case CRYSTAL_SEMANTIC_PARTY: return sizeof(CrystalParty);
        case CRYSTAL_SEMANTIC_BADGES: return sizeof(CrystalBadges);
        case CRYSTAL_SEMANTIC_POKEDEX: return sizeof(CrystalPokedexProgress);
        default: return 0;
    }}
}}

static GBSemanticStatus crystal_read_raw(
    const GBSemanticReader* reader,
    GBSemanticReadMode mode,
    CrystalSemanticView view,
    uint8_t* output,
    size_t width) {{
    if (view < 0 || view >= CRYSTAL_SEMANTIC_VIEW_COUNT ||
        (mode != GB_SEMANTIC_READ_LIVE && mode != GB_SEMANTIC_READ_SAVE)) {{
        return GB_SEMANTIC_INVALID_ARGUMENT;
    }}
    size_t contract_index = (size_t)mode;
    if (mode == GB_SEMANTIC_READ_SAVE) {{
        enum {{
            SAVE_PAYLOAD_SIZE = 2938,
        }};
        uint8_t payload[SAVE_PAYLOAD_SIZE];
        bool primary_valid = false;
        bool backup_valid = false;
        const uint16_t banks[2] = {{1u, 0u}};
        const uint16_t check1[2] = {{0xA008u, 0xB208u}};
        const uint16_t starts[2] = {{0xA009u, 0xB209u}};
        const uint16_t checksums[2] = {{0xAD0Du, 0xBF0Du}};
        const uint16_t check2[2] = {{0xAD0Fu, 0xBF0Fu}};
        bool* validity[2] = {{&primary_valid, &backup_valid}};
        for (size_t copy = 0; copy < 2; ++copy) {{
            uint8_t first = 0;
            uint8_t last = 0;
            uint8_t encoded[2] = {{0}};
            GBSemanticStatus status = gbrt_semantic_read(
                reader,
                CRYSTAL_SEMANTIC_ROM_SHA256,
                GB_SEMANTIC_READ_SAVE,
                GB_SEMANTIC_EXTERNAL_RAM,
                banks[copy],
                check1[copy],
                &first,
                1);
            if (status != GB_SEMANTIC_OK) return status;
            status = gbrt_semantic_read(
                reader,
                CRYSTAL_SEMANTIC_ROM_SHA256,
                GB_SEMANTIC_READ_SAVE,
                GB_SEMANTIC_EXTERNAL_RAM,
                banks[copy],
                starts[copy],
                payload,
                sizeof(payload));
            if (status != GB_SEMANTIC_OK) return status;
            status = gbrt_semantic_read(
                reader,
                CRYSTAL_SEMANTIC_ROM_SHA256,
                GB_SEMANTIC_READ_SAVE,
                GB_SEMANTIC_EXTERNAL_RAM,
                banks[copy],
                checksums[copy],
                encoded,
                sizeof(encoded));
            if (status != GB_SEMANTIC_OK) return status;
            status = gbrt_semantic_read(
                reader,
                CRYSTAL_SEMANTIC_ROM_SHA256,
                GB_SEMANTIC_READ_SAVE,
                GB_SEMANTIC_EXTERNAL_RAM,
                banks[copy],
                check2[copy],
                &last,
                1);
            if (status != GB_SEMANTIC_OK) return status;
            uint16_t calculated = 0;
            for (size_t index = 0; index < sizeof(payload); ++index) {{
                calculated = (uint16_t)(calculated + payload[index]);
            }}
            const uint16_t stored =
                (uint16_t)(encoded[0] | ((uint16_t)encoded[1] << 8u));
            *validity[copy] =
                first == 99u && last == 127u && stored == calculated;
        }}
        if (!primary_valid && !backup_valid) {{
            return GB_SEMANTIC_INVALID_DATA;
        }}
        contract_index = primary_valid ? 1u : 2u;
    }}
    const CrystalReadContract* contract =
        &crystal_contracts[view][contract_index];
    if (!contract->supported || width != contract->width) {{
        return GB_SEMANTIC_OUT_OF_RANGE;
    }}
    return gbrt_semantic_read(
        reader,
        CRYSTAL_SEMANTIC_ROM_SHA256,
        mode,
        contract->space,
        contract->bank,
        contract->address,
        output,
        width);
}}

GBSemanticStatus crystal_semantic_read_location(
    const GBSemanticReader* reader,
    GBSemanticReadMode mode,
    CrystalLocation* output) {{
    uint8_t raw[{widths[0]}];
    if (output == NULL) return GB_SEMANTIC_INVALID_ARGUMENT;
    GBSemanticStatus status = crystal_read_raw(
        reader, mode, CRYSTAL_SEMANTIC_LOCATION, raw, sizeof(raw));
    if (status != GB_SEMANTIC_OK) return status;
    *output = (CrystalLocation){{raw[0], raw[1], raw[2], raw[3]}};
    return GB_SEMANTIC_OK;
}}

GBSemanticStatus crystal_semantic_read_party(
    const GBSemanticReader* reader,
    GBSemanticReadMode mode,
    CrystalParty* output) {{
    uint8_t raw[{widths[1]}];
    if (output == NULL) return GB_SEMANTIC_INVALID_ARGUMENT;
    GBSemanticStatus status = crystal_read_raw(
        reader, mode, CRYSTAL_SEMANTIC_PARTY, raw, sizeof(raw));
    if (status != GB_SEMANTIC_OK) return status;
    if (raw[0] > CRYSTAL_PARTY_CAPACITY) return GB_SEMANTIC_INVALID_DATA;
    memset(output, 0, sizeof(*output));
    output->count = raw[0];
    memcpy(output->species, raw + 1, CRYSTAL_PARTY_CAPACITY);
    return GB_SEMANTIC_OK;
}}

GBSemanticStatus crystal_semantic_read_badges(
    const GBSemanticReader* reader,
    GBSemanticReadMode mode,
    CrystalBadges* output) {{
    uint8_t raw[{widths[2]}];
    if (output == NULL) return GB_SEMANTIC_INVALID_ARGUMENT;
    GBSemanticStatus status = crystal_read_raw(
        reader, mode, CRYSTAL_SEMANTIC_BADGES, raw, sizeof(raw));
    if (status != GB_SEMANTIC_OK) return status;
    output->johto_bits = raw[0];
    output->kanto_bits = raw[1];
    output->johto_count = crystal_popcount8(raw[0]);
    output->kanto_count = crystal_popcount8(raw[1]);
    output->total_count =
        (uint8_t)(output->johto_count + output->kanto_count);
    return GB_SEMANTIC_OK;
}}

GBSemanticStatus crystal_semantic_read_pokedex(
    const GBSemanticReader* reader,
    GBSemanticReadMode mode,
    CrystalPokedexProgress* output) {{
    uint8_t raw[{widths[3]}];
    uint16_t caught = 0;
    uint16_t seen = 0;
    if (output == NULL) return GB_SEMANTIC_INVALID_ARGUMENT;
    GBSemanticStatus status = crystal_read_raw(
        reader, mode, CRYSTAL_SEMANTIC_POKEDEX, raw, sizeof(raw));
    if (status != GB_SEMANTIC_OK) return status;
    for (size_t index = 0; index < 32; ++index) {{
        caught = (uint16_t)(caught + crystal_popcount8(raw[index]));
        seen = (uint16_t)(seen + crystal_popcount8(raw[index + 32]));
    }}
    output->caught_count = caught;
    output->seen_count = seen;
    return GB_SEMANTIC_OK;
}}

GBSemanticStatus crystal_semantic_read_battle(
    const GBSemanticReader* reader,
    GBSemanticReadMode mode,
    CrystalBattleMon* player,
    CrystalBattleMon* enemy,
    CrystalBattleContext* context) {{
    uint8_t player_raw[{battle_player[3]}];
    uint8_t active_slot = 0;
    uint8_t party_mon[48];
    uint8_t enemy_raw[{battle_enemy[3]}];
    uint8_t context_raw[{battle_context[3]}];
    if (reader == NULL || player == NULL || enemy == NULL ||
        context == NULL || mode != GB_SEMANTIC_READ_LIVE) {{
        return GB_SEMANTIC_INVALID_ARGUMENT;
    }}
    GBSemanticStatus status = gbrt_semantic_read(
        reader,
        CRYSTAL_SEMANTIC_ROM_SHA256,
        mode,
        {battle_player[0]},
        {battle_player[1]}u,
        0x{battle_player[2]:04X}u,
        player_raw,
        sizeof(player_raw));
    if (status != GB_SEMANTIC_OK) return status;
    if (player_raw[0] == 0 || player_raw[13] == 0) {{
        status = gbrt_semantic_read(
            reader,
            CRYSTAL_SEMANTIC_ROM_SHA256,
            mode,
            {battle_active_slot[0]},
            {battle_active_slot[1]}u,
            0x{battle_active_slot[2]:04X}u,
            &active_slot,
            sizeof(active_slot));
        if (status != GB_SEMANTIC_OK) return status;
        if (active_slot >= CRYSTAL_PARTY_CAPACITY) {{
            return GB_SEMANTIC_INVALID_DATA;
        }}
        status = gbrt_semantic_read(
            reader,
            CRYSTAL_SEMANTIC_ROM_SHA256,
            mode,
            GB_SEMANTIC_BANKED_WRAM,
            1u,
            (uint16_t)(0xDCDFu + (uint16_t)active_slot * 48u),
            party_mon,
            sizeof(party_mon));
        if (status != GB_SEMANTIC_OK) return status;
        player_raw[0] = party_mon[0];
        player_raw[13] = party_mon[31];
    }}
    status = gbrt_semantic_read(
        reader,
        CRYSTAL_SEMANTIC_ROM_SHA256,
        mode,
        {battle_enemy[0]},
        {battle_enemy[1]}u,
        0x{battle_enemy[2]:04X}u,
        enemy_raw,
        sizeof(enemy_raw));
    if (status != GB_SEMANTIC_OK) return status;
    status = gbrt_semantic_read(
        reader,
        CRYSTAL_SEMANTIC_ROM_SHA256,
        mode,
        {battle_context[0]},
        {battle_context[1]}u,
        0x{battle_context[2]:04X}u,
        context_raw,
        sizeof(context_raw));
    if (status != GB_SEMANTIC_OK) return status;
    if (context_raw[0] == 0 || player_raw[0] == 0 ||
        enemy_raw[0] == 0 || player_raw[13] == 0 ||
        enemy_raw[13] == 0) {{
        return GB_SEMANTIC_INVALID_DATA;
    }}
    *player = (CrystalBattleMon){{player_raw[0], player_raw[13]}};
    *enemy = (CrystalBattleMon){{enemy_raw[0], enemy_raw[13]}};
    *context = (CrystalBattleContext){{
        context_raw[0], context_raw[1], context_raw[2], context_raw[3]
    }};
    return GB_SEMANTIC_OK;
}}

static GBSemanticStatus crystal_read_rom(
    const GBSemanticReader* reader,
    GBSemanticReadMode mode,
    uint16_t bank,
    uint16_t address,
    uint8_t* output,
    size_t width) {{
    if (mode != GB_SEMANTIC_READ_LIVE &&
        mode != GB_SEMANTIC_READ_SAVE) {{
        return GB_SEMANTIC_INVALID_ARGUMENT;
    }}
    return gbrt_semantic_read(
        reader,
        CRYSTAL_SEMANTIC_ROM_SHA256,
        mode,
        GB_SEMANTIC_PHYSICAL_ROM,
        bank,
        address,
        output,
        width);
}}

GBSemanticStatus crystal_semantic_read_species(
    const GBSemanticReader* reader,
    GBSemanticReadMode mode,
    uint8_t species_id,
    CrystalSpeciesPage* output) {{
    enum {{
        CRYSTAL_BASE_DATA_SIZE = 32,
        CRYSTAL_EVOLVE_LEVEL = 1,
        CRYSTAL_EVOLVE_ITEM = 2,
        CRYSTAL_EVOLVE_TRADE = 3,
        CRYSTAL_EVOLVE_HAPPINESS = 4,
        CRYSTAL_EVOLVE_STAT = 5,
    }};
    uint8_t raw[CRYSTAL_BASE_DATA_SIZE];
    uint8_t pointer[2];
    if (output == NULL || species_id == 0 ||
        species_id > CRYSTAL_SPECIES_COUNT) {{
        return GB_SEMANTIC_INVALID_ARGUMENT;
    }}
    memset(output, 0, sizeof(*output));
    GBSemanticStatus status = crystal_read_rom(
        reader,
        mode,
        {base_data[1]}u,
        (uint16_t)(0x{base_data[2]:04X}u +
                   (uint16_t)(species_id - 1u) * CRYSTAL_BASE_DATA_SIZE),
        raw,
        sizeof(raw));
    if (status != GB_SEMANTIC_OK) return status;
    if (raw[0] != species_id) return GB_SEMANTIC_INVALID_DATA;

    output->species_id = species_id;
    output->hp = raw[1];
    output->attack = raw[2];
    output->defense = raw[3];
    output->speed = raw[4];
    output->special_attack = raw[5];
    output->special_defense = raw[6];
    output->primary_type = raw[7];
    output->secondary_type = raw[8];
    output->catch_rate = raw[9];
    output->base_experience = raw[10];
    output->encounter_knowledge = CRYSTAL_KNOWLEDGE_NOT_MODELED;

    status = crystal_read_rom(
        reader,
        mode,
        {evolution_pointers[1]}u,
        (uint16_t)(0x{evolution_pointers[2]:04X}u +
                   (uint16_t)(species_id - 1u) * 2u),
        pointer,
        sizeof(pointer));
    if (status != GB_SEMANTIC_OK) return status;
    uint16_t cursor = (uint16_t)(pointer[0] | ((uint16_t)pointer[1] << 8u));
    if (cursor < 0x4000u || cursor >= 0x8000u) {{
        return GB_SEMANTIC_INVALID_DATA;
    }}

    for (;;) {{
        uint8_t method = 0;
        status = crystal_read_rom(
            reader, mode, {evolution_pointers[1]}u, cursor, &method, 1);
        if (status != GB_SEMANTIC_OK) return status;
        cursor++;
        if (method == 0) break;
        if (output->evolution_count >= CRYSTAL_MAX_EVOLUTIONS ||
            method < CRYSTAL_EVOLVE_LEVEL ||
            method > CRYSTAL_EVOLVE_STAT) {{
            return GB_SEMANTIC_INVALID_DATA;
        }}
        const size_t payload_size =
            method == CRYSTAL_EVOLVE_STAT ? 3u : 2u;
        uint8_t payload[3] = {{0}};
        status = crystal_read_rom(
            reader,
            mode,
            {evolution_pointers[1]}u,
            cursor,
            payload,
            payload_size);
        if (status != GB_SEMANTIC_OK) return status;
        cursor = (uint16_t)(cursor + payload_size);
        CrystalEvolution* evolution =
            &output->evolutions[output->evolution_count++];
        evolution->method = method;
        evolution->parameter = payload[0];
        if (method == CRYSTAL_EVOLVE_STAT) {{
            evolution->condition = payload[1];
            evolution->target_species = payload[2];
        }} else {{
            evolution->target_species = payload[1];
        }}
        if (evolution->target_species == 0 ||
            evolution->target_species > CRYSTAL_SPECIES_COUNT) {{
            return GB_SEMANTIC_INVALID_DATA;
        }}
    }}

    for (;;) {{
        uint8_t pair[2] = {{0}};
        status = crystal_read_rom(
            reader, mode, {evolution_pointers[1]}u, cursor, pair, 1);
        if (status != GB_SEMANTIC_OK) return status;
        if (pair[0] == 0) break;
        if (output->level_move_count >= CRYSTAL_MAX_LEVEL_MOVES) {{
            return GB_SEMANTIC_INVALID_DATA;
        }}
        status = crystal_read_rom(
            reader, mode, {evolution_pointers[1]}u, cursor, pair, sizeof(pair));
        if (status != GB_SEMANTIC_OK) return status;
        cursor = (uint16_t)(cursor + sizeof(pair));
        if (pair[0] > 100 || pair[1] == 0) {{
            return GB_SEMANTIC_INVALID_DATA;
        }}
        CrystalLevelMove* move =
            &output->level_moves[output->level_move_count++];
        move->level = pair[0];
        move->move_id = pair[1];
    }}
    return GB_SEMANTIC_OK;
}}

static bool crystal_valid_species(uint8_t species, bool allow_egg) {{
    return (species >= 1u && species <= CRYSTAL_SPECIES_COUNT) ||
           (allow_egg && species == 0xFDu);
}}

static bool crystal_valid_encoded_name(const uint8_t* name) {{
    if (name == NULL) return false;
    for (size_t index = 0; index < CRYSTAL_NAME_LENGTH; ++index) {{
        if (name[index] == 0x50u) return true;
        if (name[index] < 0x60u) return false;
    }}
    return false;
}}

static GBSemanticStatus crystal_validate_party_bytes(
    const uint8_t* party,
    size_t party_size) {{
    enum {{
        SPECIES_OFFSET = 1,
        MONS_OFFSET = 8,
        PARTY_MON_SIZE = 48,
        PARTY_MON_LEVEL_OFFSET = 31,
        PARTY_MON_HP_OFFSET = 34,
        PARTY_MON_MAX_HP_OFFSET = 36,
        OT_NAMES_OFFSET = 296,
        NICKNAMES_OFFSET = 362,
    }};
    if (party == NULL || party_size != CRYSTAL_PARTY_RECORD_SIZE ||
        party[0] > CRYSTAL_PARTY_CAPACITY ||
        party[SPECIES_OFFSET + party[0]] != 0xFFu) {{
        return GB_SEMANTIC_INVALID_DATA;
    }}
    for (size_t index = 0; index < party[0]; ++index) {{
        const uint8_t listed_species = party[SPECIES_OFFSET + index];
        const uint8_t* mon = party + MONS_OFFSET + index * PARTY_MON_SIZE;
        if (!crystal_valid_species(listed_species, true) ||
            !crystal_valid_species(mon[0], false) ||
            (listed_species != 0xFDu && listed_species != mon[0]) ||
            mon[PARTY_MON_LEVEL_OFFSET] == 0 ||
            mon[PARTY_MON_LEVEL_OFFSET] > 100) {{
            return GB_SEMANTIC_INVALID_DATA;
        }}
        const uint16_t hp =
            (uint16_t)(((uint16_t)mon[PARTY_MON_HP_OFFSET] << 8u) |
                       mon[PARTY_MON_HP_OFFSET + 1]);
        const uint16_t max_hp =
            (uint16_t)(((uint16_t)mon[PARTY_MON_MAX_HP_OFFSET] << 8u) |
                       mon[PARTY_MON_MAX_HP_OFFSET + 1]);
        if (max_hp == 0 || hp > max_hp ||
            !crystal_valid_encoded_name(
                party + OT_NAMES_OFFSET + index * CRYSTAL_NAME_LENGTH) ||
            !crystal_valid_encoded_name(
                party + NICKNAMES_OFFSET + index * CRYSTAL_NAME_LENGTH)) {{
            return GB_SEMANTIC_INVALID_DATA;
        }}
    }}
    return GB_SEMANTIC_OK;
}}

static GBSemanticStatus crystal_validate_box_bytes(
    const uint8_t* box,
    size_t box_size) {{
    enum {{
        SPECIES_OFFSET = 1,
        MONS_OFFSET = 22,
        BOX_MON_SIZE = 32,
        BOX_MON_LEVEL_OFFSET = 31,
        OT_NAMES_OFFSET = 662,
        NICKNAMES_OFFSET = 882,
    }};
    if (box == NULL || box_size != CRYSTAL_ACTIVE_BOX_RECORD_SIZE ||
        box[0] > CRYSTAL_BOX_CAPACITY ||
        box[SPECIES_OFFSET + box[0]] != 0xFFu) {{
        return GB_SEMANTIC_INVALID_DATA;
    }}
    for (size_t index = 0; index < box[0]; ++index) {{
        const uint8_t listed_species = box[SPECIES_OFFSET + index];
        const uint8_t* mon = box + MONS_OFFSET + index * BOX_MON_SIZE;
        if (!crystal_valid_species(listed_species, true) ||
            !crystal_valid_species(mon[0], false) ||
            (listed_species != 0xFDu && listed_species != mon[0]) ||
            mon[BOX_MON_LEVEL_OFFSET] == 0 ||
            mon[BOX_MON_LEVEL_OFFSET] > 100 ||
            !crystal_valid_encoded_name(
                box + OT_NAMES_OFFSET + index * CRYSTAL_NAME_LENGTH) ||
            !crystal_valid_encoded_name(
                box + NICKNAMES_OFFSET + index * CRYSTAL_NAME_LENGTH)) {{
            return GB_SEMANTIC_INVALID_DATA;
        }}
    }}
    return GB_SEMANTIC_OK;
}}

static GBSemanticStatus crystal_read_external(
    const GBSemanticReader* reader,
    uint16_t bank,
    uint16_t address,
    uint8_t* output,
    size_t width) {{
    return gbrt_semantic_read(
        reader,
        CRYSTAL_SEMANTIC_ROM_SHA256,
        GB_SEMANTIC_READ_LIVE,
        GB_SEMANTIC_EXTERNAL_RAM,
        bank,
        address,
        output,
        width);
}}

static GBSemanticStatus crystal_active_box_location(
    const GBSemanticReader* reader,
    uint16_t* bank,
    uint16_t* address) {{
    uint8_t selector = 0;
    if (reader == NULL || bank == NULL || address == NULL) {{
        return GB_SEMANTIC_INVALID_ARGUMENT;
    }}
    GBSemanticStatus status = crystal_read_external(
        reader, 1, 0xA700u, &selector, 1);
    if (status != GB_SEMANTIC_OK) return status;
    if (selector >= CRYSTAL_BOX_COUNT) return GB_SEMANTIC_INVALID_DATA;
    *bank = (uint16_t)(2u + selector / 7u);
    *address = (uint16_t)(0xA000u + (selector % 7u) * 1102u);
    return GB_SEMANTIC_OK;
}}

static GBSemanticStatus crystal_box_location(
    uint8_t box_index,
    uint16_t* bank,
    uint16_t* address) {{
    if (box_index >= CRYSTAL_BOX_COUNT ||
        bank == NULL || address == NULL) {{
        return GB_SEMANTIC_INVALID_ARGUMENT;
    }}
    *bank = (uint16_t)(2u + box_index / 7u);
    *address =
        (uint16_t)(0xA000u + (box_index % 7u) * 1102u);
    return GB_SEMANTIC_OK;
}}

GBSemanticStatus crystal_semantic_read_current_box(
    const GBSemanticReader* reader,
    uint8_t* box_index) {{
    if (box_index == NULL) return GB_SEMANTIC_INVALID_ARGUMENT;
    GBSemanticStatus status = crystal_read_external(
        reader, 1, 0xA700u, box_index, 1);
    if (status != GB_SEMANTIC_OK) return status;
    return *box_index < CRYSTAL_BOX_COUNT
        ? GB_SEMANTIC_OK
        : GB_SEMANTIC_INVALID_DATA;
}}

GBSemanticStatus crystal_semantic_read_party_record(
    const GBSemanticReader* reader,
    GBSemanticReadMode mode,
    uint8_t output[CRYSTAL_PARTY_RECORD_SIZE]) {{
    if (output == NULL) return GB_SEMANTIC_INVALID_ARGUMENT;
    GBSemanticStatus status = crystal_read_raw(
        reader,
        mode,
        CRYSTAL_SEMANTIC_PARTY,
        output,
        CRYSTAL_PARTY_RECORD_SIZE);
    if (status != GB_SEMANTIC_OK) return status;
    return crystal_validate_party_bytes(
        output, CRYSTAL_PARTY_RECORD_SIZE);
}}

GBSemanticStatus crystal_semantic_read_active_box_record(
    const GBSemanticReader* reader,
    uint8_t output[CRYSTAL_ACTIVE_BOX_RECORD_SIZE]) {{
    if (output == NULL) return GB_SEMANTIC_INVALID_ARGUMENT;
    uint8_t canonical[CRYSTAL_ACTIVE_BOX_RECORD_SIZE];
    uint16_t canonical_bank = 0;
    uint16_t canonical_address = 0;
    GBSemanticStatus status = crystal_read_external(
        reader,
        1,
        0xAD10u,
        output,
        CRYSTAL_ACTIVE_BOX_RECORD_SIZE);
    if (status != GB_SEMANTIC_OK) return status;
    status = crystal_active_box_location(
        reader, &canonical_bank, &canonical_address);
    if (status != GB_SEMANTIC_OK) return status;
    status = crystal_read_external(
        reader,
        canonical_bank,
        canonical_address,
        canonical,
        sizeof(canonical));
    if (status != GB_SEMANTIC_OK) return status;
    status = crystal_validate_box_bytes(
        output, CRYSTAL_ACTIVE_BOX_RECORD_SIZE);
    if (status != GB_SEMANTIC_OK) return status;
    status = crystal_validate_box_bytes(canonical, sizeof(canonical));
    if (status != GB_SEMANTIC_OK) return status;
    return memcmp(output, canonical, sizeof(canonical)) == 0
        ? GB_SEMANTIC_OK
        : GB_SEMANTIC_INVALID_DATA;
}}

GBSemanticStatus crystal_semantic_read_box_record(
    const GBSemanticReader* reader,
    uint8_t box_index,
    uint8_t output[CRYSTAL_ACTIVE_BOX_RECORD_SIZE]) {{
    if (reader == NULL || output == NULL) {{
        return GB_SEMANTIC_INVALID_ARGUMENT;
    }}
    uint16_t bank = 0;
    uint16_t address = 0;
    GBSemanticStatus status =
        crystal_box_location(box_index, &bank, &address);
    if (status != GB_SEMANTIC_OK) return status;
    status = crystal_read_external(
        reader, bank, address, output, CRYSTAL_ACTIVE_BOX_RECORD_SIZE);
    if (status != GB_SEMANTIC_OK) return status;
    if (output[0] == 0u || output[0] == 0xFFu) {{
        memset(output, 0, CRYSTAL_ACTIVE_BOX_RECORD_SIZE);
        output[1] = 0xFFu;
    }}
    status = crystal_validate_box_bytes(
        output, CRYSTAL_ACTIVE_BOX_RECORD_SIZE);
    if (status != GB_SEMANTIC_OK) return status;
    uint8_t current_box = 0;
    status = crystal_semantic_read_current_box(reader, &current_box);
    if (status != GB_SEMANTIC_OK || current_box != box_index) return status;
    uint8_t mirror[CRYSTAL_ACTIVE_BOX_RECORD_SIZE];
    status = crystal_read_external(
        reader,
        1,
        0xAD10u,
        mirror,
        sizeof(mirror));
    if (status != GB_SEMANTIC_OK) return status;
    if (mirror[0] == 0u || mirror[0] == 0xFFu) {{
        memset(mirror, 0, sizeof(mirror));
        mirror[1] = 0xFFu;
    }}
    return memcmp(output, mirror, sizeof(mirror)) == 0
        ? GB_SEMANTIC_OK
        : GB_SEMANTIC_INVALID_DATA;
}}

static GBSemanticStatus crystal_validate_checksum(
    const GBSemanticReader* reader,
    uint16_t bank,
    uint16_t check1_address,
    uint16_t data_address,
    uint16_t data_end,
    uint16_t checksum_address,
    uint16_t check2_address) {{
    uint8_t check1 = 0;
    uint8_t check2 = 0;
    uint8_t checksum[2] = {{0}};
    uint8_t payload[2938];
    const size_t payload_size = (size_t)(data_end - data_address);
    if (payload_size != sizeof(payload) ||
        crystal_read_external(
            reader, bank, check1_address, &check1, 1) != GB_SEMANTIC_OK ||
        crystal_read_external(
            reader, bank, data_address, payload, payload_size) !=
            GB_SEMANTIC_OK ||
        crystal_read_external(
            reader, bank, checksum_address, checksum, sizeof(checksum)) !=
            GB_SEMANTIC_OK ||
        crystal_read_external(
            reader, bank, check2_address, &check2, 1) != GB_SEMANTIC_OK) {{
        return GB_SEMANTIC_READ_FAILED;
    }}
    uint16_t calculated = 0;
    for (size_t index = 0; index < payload_size; ++index) {{
        calculated = (uint16_t)(calculated + payload[index]);
    }}
    const uint16_t stored =
        (uint16_t)(checksum[0] | ((uint16_t)checksum[1] << 8u));
    return check1 == 99u && check2 == 127u && stored == calculated
        ? GB_SEMANTIC_OK
        : GB_SEMANTIC_INVALID_DATA;
}}

GBSemanticStatus crystal_semantic_validate_transaction(
    const GBSemanticReader* staged_reader,
    void* user) {{
    (void)user;
    uint8_t primary_party[CRYSTAL_PARTY_RECORD_SIZE];
    uint8_t backup_party[CRYSTAL_PARTY_RECORD_SIZE];
    uint8_t active_box[CRYSTAL_ACTIVE_BOX_RECORD_SIZE];
    uint8_t canonical_box[CRYSTAL_ACTIVE_BOX_RECORD_SIZE];
    uint16_t canonical_bank = 0;
    uint16_t canonical_address = 0;
    GBSemanticStatus location_status = crystal_active_box_location(
        staged_reader, &canonical_bank, &canonical_address);
    if (location_status != GB_SEMANTIC_OK) return location_status;
    if (crystal_read_external(
            staged_reader,
            1,
            0xA865u,
            primary_party,
            sizeof(primary_party)) != GB_SEMANTIC_OK ||
        crystal_read_external(
            staged_reader,
            0,
            0xBA65u,
            backup_party,
            sizeof(backup_party)) != GB_SEMANTIC_OK ||
        crystal_read_external(
            staged_reader,
            1,
            0xAD10u,
            active_box,
            sizeof(active_box)) != GB_SEMANTIC_OK ||
        crystal_read_external(
            staged_reader,
            canonical_bank,
            canonical_address,
            canonical_box,
            sizeof(canonical_box)) != GB_SEMANTIC_OK) {{
        return GB_SEMANTIC_READ_FAILED;
    }}
    GBSemanticStatus status =
        crystal_validate_party_bytes(primary_party, sizeof(primary_party));
    if (status != GB_SEMANTIC_OK) return status;
    status = crystal_validate_party_bytes(backup_party, sizeof(backup_party));
    if (status != GB_SEMANTIC_OK) return status;
    status = crystal_validate_box_bytes(active_box, sizeof(active_box));
    if (status != GB_SEMANTIC_OK) return status;
    status = crystal_validate_box_bytes(canonical_box, sizeof(canonical_box));
    if (status != GB_SEMANTIC_OK) return status;
    if (memcmp(active_box, canonical_box, sizeof(active_box)) != 0) {{
        return GB_SEMANTIC_INVALID_DATA;
    }}
    for (uint8_t box_index = 0;
         box_index < CRYSTAL_BOX_COUNT;
         ++box_index) {{
        uint16_t box_bank = 0;
        uint16_t box_address = 0;
        status = crystal_box_location(
            box_index, &box_bank, &box_address);
        if (status != GB_SEMANTIC_OK) return status;
        status = crystal_read_external(
            staged_reader,
            box_bank,
            box_address,
            canonical_box,
            sizeof(canonical_box));
        if (status != GB_SEMANTIC_OK) return status;
        if (canonical_box[0] == 0u ||
            canonical_box[0] == 0xFFu) {{
            continue;
        }}
        status = crystal_validate_box_bytes(
            canonical_box, sizeof(canonical_box));
        if (status != GB_SEMANTIC_OK) return status;
    }}
    status = crystal_validate_checksum(
        staged_reader, 1, 0xA008u, 0xA009u, 0xAB83u, 0xAD0Du, 0xAD0Fu);
    if (status != GB_SEMANTIC_OK) return status;
    return crystal_validate_checksum(
        staged_reader, 0, 0xB208u, 0xB209u, 0xBD83u, 0xBF0Du, 0xBF0Fu);
}}

static GBSemanticStatus crystal_stage_checksum(
    GBSemanticTransaction* transaction,
    uint16_t bank,
    uint16_t check1_address,
    uint16_t data_address,
    uint16_t data_end,
    uint16_t checksum_address,
    uint16_t check2_address) {{
    GBSemanticReader reader;
    uint8_t payload[2938];
    const size_t payload_size = (size_t)(data_end - data_address);
    if (payload_size != sizeof(payload)) return GB_SEMANTIC_INVALID_DATA;
    GBSemanticStatus status =
        gbrt_semantic_transaction_reader(transaction, &reader);
    if (status != GB_SEMANTIC_OK) return status;
    status = crystal_read_external(
        &reader, bank, data_address, payload, payload_size);
    if (status != GB_SEMANTIC_OK) return status;
    uint16_t checksum = 0;
    for (size_t index = 0; index < payload_size; ++index) {{
        checksum = (uint16_t)(checksum + payload[index]);
    }}
    const uint8_t check1 = 99u;
    const uint8_t check2 = 127u;
    const uint8_t encoded_checksum[2] = {{
        (uint8_t)checksum,
        (uint8_t)(checksum >> 8u),
    }};
    status = gbrt_semantic_transaction_write(
        transaction,
        GB_SEMANTIC_EXTERNAL_RAM,
        bank,
        check1_address,
        &check1,
        1);
    if (status != GB_SEMANTIC_OK) return status;
    status = gbrt_semantic_transaction_write(
        transaction,
        GB_SEMANTIC_EXTERNAL_RAM,
        bank,
        checksum_address,
        encoded_checksum,
        sizeof(encoded_checksum));
    if (status != GB_SEMANTIC_OK) return status;
    return gbrt_semantic_transaction_write(
        transaction,
        GB_SEMANTIC_EXTERNAL_RAM,
        bank,
        check2_address,
        &check2,
        1);
}}

GBSemanticStatus crystal_semantic_stage_party(
    GBSemanticTransaction* transaction,
    const uint8_t* party,
    size_t party_size) {{
    GBSemanticStatus status =
        crystal_validate_party_bytes(party, party_size);
    if (status != GB_SEMANTIC_OK) return status;
    status = gbrt_semantic_transaction_write(
        transaction,
        GB_SEMANTIC_BANKED_WRAM,
        1,
        0xDCD7u,
        party,
        party_size);
    if (status != GB_SEMANTIC_OK) return status;
    status = gbrt_semantic_transaction_write(
        transaction,
        GB_SEMANTIC_EXTERNAL_RAM,
        1,
        0xA865u,
        party,
        party_size);
    if (status != GB_SEMANTIC_OK) return status;
    status = gbrt_semantic_transaction_write(
        transaction,
        GB_SEMANTIC_EXTERNAL_RAM,
        0,
        0xBA65u,
        party,
        party_size);
    if (status != GB_SEMANTIC_OK) return status;
    status = crystal_stage_checksum(
        transaction, 1, 0xA008u, 0xA009u, 0xAB83u, 0xAD0Du, 0xAD0Fu);
    if (status != GB_SEMANTIC_OK) return status;
    return crystal_stage_checksum(
        transaction, 0, 0xB208u, 0xB209u, 0xBD83u, 0xBF0Du, 0xBF0Fu);
}}

GBSemanticStatus crystal_semantic_stage_active_box(
    GBSemanticTransaction* transaction,
    const uint8_t* box,
    size_t box_size) {{
    GBSemanticStatus status = crystal_validate_box_bytes(box, box_size);
    if (status != GB_SEMANTIC_OK) return status;
    GBSemanticReader reader;
    status = gbrt_semantic_transaction_reader(transaction, &reader);
    if (status != GB_SEMANTIC_OK) return status;
    uint16_t canonical_bank = 0;
    uint16_t canonical_address = 0;
    status = crystal_active_box_location(
        &reader, &canonical_bank, &canonical_address);
    if (status != GB_SEMANTIC_OK) return status;
    status = gbrt_semantic_transaction_write(
        transaction,
        GB_SEMANTIC_EXTERNAL_RAM,
        1,
        0xAD10u,
        box,
        box_size);
    if (status != GB_SEMANTIC_OK) return status;
    return gbrt_semantic_transaction_write(
        transaction,
        GB_SEMANTIC_EXTERNAL_RAM,
        canonical_bank,
        canonical_address,
        box,
        box_size);
}}

GBSemanticStatus crystal_semantic_stage_box(
    GBSemanticTransaction* transaction,
    uint8_t box_index,
    const uint8_t* box,
    size_t box_size) {{
    GBSemanticStatus status = crystal_validate_box_bytes(box, box_size);
    if (status != GB_SEMANTIC_OK) return status;
    uint16_t bank = 0;
    uint16_t address = 0;
    status = crystal_box_location(box_index, &bank, &address);
    if (status != GB_SEMANTIC_OK) return status;
    status = gbrt_semantic_transaction_write(
        transaction,
        GB_SEMANTIC_EXTERNAL_RAM,
        bank,
        address,
        box,
        box_size);
    if (status != GB_SEMANTIC_OK) return status;
    GBSemanticReader reader;
    status = gbrt_semantic_transaction_reader(transaction, &reader);
    if (status != GB_SEMANTIC_OK) return status;
    uint8_t current_box = 0;
    status = crystal_semantic_read_current_box(&reader, &current_box);
    if (status != GB_SEMANTIC_OK || current_box != box_index) return status;
    return gbrt_semantic_transaction_write(
        transaction,
        GB_SEMANTIC_EXTERNAL_RAM,
        1,
        0xAD10u,
        box,
        box_size);
}}

GBSemanticStatus crystal_semantic_encode_name(
    const char* ascii,
    uint8_t output[CRYSTAL_NAME_LENGTH]) {{
    if (ascii == NULL || output == NULL) return GB_SEMANTIC_INVALID_ARGUMENT;
    memset(output, 0x50, CRYSTAL_NAME_LENGTH);
    size_t index = 0;
    for (; ascii[index] != '\\0'; ++index) {{
        if (index >= CRYSTAL_NAME_LENGTH - 1u) {{
            return GB_SEMANTIC_OUT_OF_RANGE;
        }}
        const unsigned char character = (unsigned char)ascii[index];
        if (character >= 'A' && character <= 'Z') {{
            output[index] = (uint8_t)(0x80u + character - 'A');
        }} else if (character >= 'a' && character <= 'z') {{
            output[index] = (uint8_t)(0xA0u + character - 'a');
        }} else if (character >= '0' && character <= '9') {{
            output[index] = (uint8_t)(0xF6u + character - '0');
        }} else if (character == ' ') {{
            output[index] = 0x7Fu;
        }} else {{
            return GB_SEMANTIC_INVALID_DATA;
        }}
    }}
    return GB_SEMANTIC_OK;
}}

GBSemanticStatus crystal_semantic_read_view(
    const GBSemanticReader* reader,
    GBSemanticReadMode mode,
    CrystalSemanticView view,
    void* output,
    size_t output_size) {{
    if (output == NULL || output_size != crystal_semantic_view_size(view)) {{
        return GB_SEMANTIC_OUT_OF_RANGE;
    }}
    switch (view) {{
        case CRYSTAL_SEMANTIC_LOCATION:
            return crystal_semantic_read_location(
                reader, mode, (CrystalLocation*)output);
        case CRYSTAL_SEMANTIC_PARTY:
            return crystal_semantic_read_party(
                reader, mode, (CrystalParty*)output);
        case CRYSTAL_SEMANTIC_BADGES:
            return crystal_semantic_read_badges(
                reader, mode, (CrystalBadges*)output);
        case CRYSTAL_SEMANTIC_POKEDEX:
            return crystal_semantic_read_pokedex(
                reader, mode, (CrystalPokedexProgress*)output);
        default:
            return GB_SEMANTIC_INVALID_ARGUMENT;
    }}
}}
"""


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()

    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    if (
        manifest.get("schema") != "gbrecompiled.semantic-package"
        or manifest.get("schema_version") != 1
        or manifest.get("package") != {"id": "crystal-recompiled", "version": 4}
        or manifest.get("runtime_abi")
        != {"name": "gbrecomp.semantic", "version": 1}
        or manifest.get("rom", {}).get("sha256") != EXPECTED_ROM
    ):
        fail("unsupported semantic package, ABI, or ROM")
    views = {
        view.get("id"): view
        for view in manifest.get("views", [])
        if isinstance(view, dict)
    }
    missing = [view_id for view_id, _, _ in EXPOSED if view_id not in views]
    missing.extend(
        view_id
        for view_id in (
            "storage.active_box",
            "species.base_data",
            "species.evolutions_and_moves",
            "battle.player",
            "battle.active_slot",
            "battle.enemy",
            "battle.context",
        )
        if view_id not in views
    )
    if missing:
        fail(f"missing exposed view: {missing[0]}")

    args.output_dir.mkdir(parents=True, exist_ok=True)
    (args.output_dir / "crystal_semantic.h").write_text(
        render_header(), encoding="utf-8"
    )
    (args.output_dir / "crystal_semantic.c").write_text(
        render_source(views), encoding="utf-8"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
