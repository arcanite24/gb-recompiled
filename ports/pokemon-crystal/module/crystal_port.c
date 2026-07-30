#include "crystal_semantic.h"
#include "crystal_pc.h"
#include "gbrt_port.h"

#include <stdio.h>
#include <string.h>

typedef struct CrystalPortState {
    bool visible;
    bool native_pokedex;
    bool location_available;
    bool party_available;
    bool badges_available;
    bool pokedex_available;
    bool species_available;
    CrystalLocation location;
    CrystalParty party;
    CrystalBadges badges;
    CrystalPokedexProgress pokedex;
    CrystalSpeciesPage species;
    uint8_t selected_species;
    bool pc_available;
    bool pc_pending;
    uint8_t pc_focus;
    uint8_t pc_filter_species;
    CrystalPCSort pc_sort;
    size_t pc_party_index;
    size_t pc_box_index;
    bool pc_box_initialized;
    uint8_t pc_box_number;
    CrystalPCRecords pc_records;
    char pc_status[96];
} CrystalPortState;

static CrystalPortState crystal_port_state;

enum {
    CRYSTAL_PC_FOCUS_PARTY = 0,
    CRYSTAL_PC_FOCUS_BOX = 1,
    CRYSTAL_PC_FOCUS_SEARCH = 2,
    CRYSTAL_PC_FOCUS_SORT = 3,
    CRYSTAL_PC_FOCUS_COUNT = 4,
};

static void crystal_port_log_status(
    const GBPortServices* services,
    CrystalPortState* state,
    GBPortLogLevel level,
    const char* status) {
    snprintf(state->pc_status, sizeof(state->pc_status), "%s", status);
    services->log(
        services->host_user,
        level,
        "crystal-workbench",
        status);
}

static void crystal_port_pc_accept(
    CrystalPortState* state,
    const GBPortServices* services) {
    if (!state->pc_available) {
        crystal_port_log_status(
            services, state, GB_PORT_LOG_WARNING, "PC data unavailable");
        return;
    }
    if (state->pc_pending) {
        const GBSemanticStatus status = services->run_semantic_edit(
            services->semantic_edit_user,
            CRYSTAL_SEMANTIC_ROM_SHA256,
            crystal_pc_stage,
            &state->pc_records);
        state->pc_pending = false;
        state->pc_available = status == GB_SEMANTIC_OK;
        crystal_port_log_status(
            services,
            state,
            status == GB_SEMANTIC_OK
                ? GB_PORT_LOG_INFO
                : GB_PORT_LOG_ERROR,
            status == GB_SEMANTIC_OK
                ? "PC edit committed"
                : "PC edit rejected");
        return;
    }

    GBSemanticStatus status = GB_SEMANTIC_INVALID_ARGUMENT;
    if (state->pc_focus == CRYSTAL_PC_FOCUS_PARTY) {
        status = crystal_pc_move(
            &state->pc_records,
            services->semantic_reader,
            CRYSTAL_PC_PARTY_TO_BOX,
            state->pc_party_index);
    } else if (state->pc_focus == CRYSTAL_PC_FOCUS_BOX) {
        status = crystal_pc_move(
            &state->pc_records,
            services->semantic_reader,
            CRYSTAL_PC_BOX_TO_PARTY,
            state->pc_box_index);
    } else if (state->pc_focus == CRYSTAL_PC_FOCUS_SORT) {
        status = crystal_pc_sort_box(
            &state->pc_records, state->pc_sort);
    } else if (state->pc_focus == CRYSTAL_PC_FOCUS_SEARCH) {
        CrystalPCRecords candidate;
        for (uint8_t step = 1; step <= CRYSTAL_BOX_COUNT; ++step) {
            const uint8_t box_index = (uint8_t)(
                (state->pc_box_number + step) % CRYSTAL_BOX_COUNT);
            if (crystal_pc_load_box(
                    services->semantic_reader,
                    box_index,
                    &candidate) != GB_SEMANTIC_OK) {
                continue;
            }
            size_t matches[CRYSTAL_BOX_CAPACITY];
            if (crystal_pc_search_box(
                    &candidate,
                    state->pc_filter_species,
                    matches) == 0) {
                continue;
            }
            state->pc_records = candidate;
            state->pc_box_number = box_index;
            state->pc_box_index = matches[0];
            crystal_port_log_status(
                services,
                state,
                GB_PORT_LOG_INFO,
                "PC search advanced to matching box");
            return;
        }
        crystal_port_log_status(
            services,
            state,
            GB_PORT_LOG_INFO,
            "PC search found no matching box");
        return;
    }
    if (status == GB_SEMANTIC_OK) {
        state->pc_pending = true;
        crystal_port_log_status(
            services,
            state,
            GB_PORT_LOG_INFO,
            "PC edit staged - press Accept again to confirm");
    } else {
        crystal_port_log_status(
            services,
            state,
            GB_PORT_LOG_WARNING,
            "PC edit unavailable (capacity, last party member, or mail)");
    }
}

static void crystal_port_pc_input(
    CrystalPortState* state,
    const GBPortServices* services,
    GBPortInputAction action) {
    if (action == GB_PORT_INPUT_BACK) {
        if (state->pc_pending) {
            state->pc_pending = false;
            state->pc_available = false;
            crystal_port_log_status(
                services,
                state,
                GB_PORT_LOG_INFO,
                "PC edit canceled - no write");
        } else {
            state->pc_filter_species = 0;
            crystal_port_log_status(
                services,
                state,
                GB_PORT_LOG_INFO,
                "PC search cleared");
        }
        return;
    }
    if (state->pc_pending) {
        if (action == GB_PORT_INPUT_ACCEPT) {
            crystal_port_pc_accept(state, services);
        }
        return;
    }
    if (action == GB_PORT_INPUT_LEFT) {
        state->pc_focus = state->pc_focus == 0
            ? CRYSTAL_PC_FOCUS_COUNT - 1u
            : (uint8_t)(state->pc_focus - 1u);
    } else if (action == GB_PORT_INPUT_RIGHT) {
        state->pc_focus =
            (uint8_t)((state->pc_focus + 1u) % CRYSTAL_PC_FOCUS_COUNT);
        if (state->pc_focus == CRYSTAL_PC_FOCUS_SEARCH &&
            state->pc_filter_species == 0) {
            if (state->pc_records.box[0] > 0) {
                state->pc_filter_species =
                    state->pc_records.box[
                        1u + state->pc_box_index];
            } else if (state->pc_records.party[0] > 0) {
                state->pc_filter_species =
                    state->pc_records.party[
                        1u + state->pc_party_index];
            }
        }
    } else if (action == GB_PORT_INPUT_UP) {
        if (state->pc_focus == CRYSTAL_PC_FOCUS_PARTY &&
            state->pc_records.party[0] > 0) {
            state->pc_party_index =
                state->pc_party_index == 0
                    ? state->pc_records.party[0] - 1u
                    : state->pc_party_index - 1u;
        } else if (state->pc_focus == CRYSTAL_PC_FOCUS_BOX &&
                   state->pc_records.box[0] > 0) {
            state->pc_box_index =
                state->pc_box_index == 0
                    ? state->pc_records.box[0] - 1u
                    : state->pc_box_index - 1u;
        } else if (state->pc_focus == CRYSTAL_PC_FOCUS_SEARCH) {
            state->pc_filter_species =
                state->pc_filter_species <= 1
                    ? CRYSTAL_SPECIES_COUNT
                    : (uint8_t)(state->pc_filter_species - 1u);
        } else if (state->pc_focus == CRYSTAL_PC_FOCUS_SORT) {
            state->pc_sort = state->pc_sort == CRYSTAL_PC_SORT_SPECIES
                ? CRYSTAL_PC_SORT_LEVEL
                : CRYSTAL_PC_SORT_SPECIES;
        }
    } else if (action == GB_PORT_INPUT_DOWN) {
        if (state->pc_focus == CRYSTAL_PC_FOCUS_PARTY &&
            state->pc_records.party[0] > 0) {
            state->pc_party_index =
                (state->pc_party_index + 1u) %
                state->pc_records.party[0];
        } else if (state->pc_focus == CRYSTAL_PC_FOCUS_BOX &&
                   state->pc_records.box[0] > 0) {
            state->pc_box_index =
                (state->pc_box_index + 1u) %
                state->pc_records.box[0];
        } else if (state->pc_focus == CRYSTAL_PC_FOCUS_SEARCH) {
            state->pc_filter_species =
                state->pc_filter_species >= CRYSTAL_SPECIES_COUNT
                    ? 0
                    : (uint8_t)(state->pc_filter_species + 1u);
        } else if (state->pc_focus == CRYSTAL_PC_FOCUS_SORT) {
            state->pc_sort = state->pc_sort == CRYSTAL_PC_SORT_SPECIES
                ? CRYSTAL_PC_SORT_LEVEL
                : CRYSTAL_PC_SORT_SPECIES;
        }
    } else if (action == GB_PORT_INPUT_ACCEPT) {
        crystal_port_pc_accept(state, services);
    }
}

static bool crystal_port_activate(
    void* user,
    const GBPortServices* services) {
    CrystalPortState* state = (CrystalPortState*)user;
    if (state == NULL || services == NULL ||
        services->abi_version != GB_PORT_ABI_VERSION ||
        services->metadata == NULL ||
        services->semantic_reader == NULL ||
        services->semantic_edit_user == NULL ||
        services->run_semantic_edit == NULL ||
        strcmp(services->metadata->rom_sha256,
               CRYSTAL_SEMANTIC_ROM_SHA256) != 0) {
        return false;
    }
    *state = (CrystalPortState){0};
    return true;
}

static void crystal_port_input(
    void* user,
    const GBPortServices* services,
    const GBPortInputEvent* event) {
    CrystalPortState* state = (CrystalPortState*)user;
    if (state == NULL || event == NULL || !event->pressed) return;
    if (event->action == GB_PORT_INPUT_OPEN_UI) {
        state->visible = true;
        state->native_pokedex = true;
        services->log(
            services->host_user,
            GB_PORT_LOG_INFO,
            "crystal-workbench",
            "native Pokedex shown");
    } else if (event->action == GB_PORT_INPUT_OPEN_PC) {
        state->visible = true;
        state->native_pokedex = false;
        state->pc_pending = false;
        state->pc_available = false;
        state->pc_box_initialized = false;
        state->pc_party_index = 0;
        state->pc_box_index = 0;
        state->pc_status[0] = '\0';
        services->log(
            services->host_user,
            GB_PORT_LOG_INFO,
            "crystal-workbench",
            "native PC shown");
    } else if (event->action == GB_PORT_INPUT_TOGGLE_UI) {
        state->visible = !state->visible;
        if (!state->visible) state->native_pokedex = false;
        services->log(
            services->host_user,
            GB_PORT_LOG_INFO,
            "crystal-workbench",
            state->visible ? "native UI shown" : "native UI hidden");
    } else if (event->action == GB_PORT_INPUT_CLOSE_UI) {
        const bool closing_pokedex = state->native_pokedex;
        state->visible = false;
        state->native_pokedex = false;
        state->pc_pending = false;
        state->pc_available = false;
        services->log(
            services->host_user,
            GB_PORT_LOG_INFO,
            "crystal-workbench",
            closing_pokedex
                ? "native Pokedex hidden"
                : "native PC hidden");
    } else if (state->native_pokedex &&
               (event->action == GB_PORT_INPUT_RIGHT ||
                event->action == GB_PORT_INPUT_DOWN)) {
        state->selected_species =
            state->selected_species >= 251
                ? 1
                : (uint8_t)(state->selected_species + 1);
    } else if (state->native_pokedex &&
               (event->action == GB_PORT_INPUT_LEFT ||
                event->action == GB_PORT_INPUT_UP)) {
        state->selected_species =
            state->selected_species <= 1
                ? 251
                : (uint8_t)(state->selected_species - 1);
    }
    if (state->native_pokedex &&
        (event->action == GB_PORT_INPUT_RIGHT ||
         event->action == GB_PORT_INPUT_DOWN ||
         event->action == GB_PORT_INPUT_LEFT ||
         event->action == GB_PORT_INPUT_UP)) {
        char message[64];
        snprintf(
            message,
            sizeof(message),
            "native Pokedex species %u",
            state->selected_species);
        services->log(
            services->host_user,
            GB_PORT_LOG_INFO,
            "crystal-workbench",
            message);
    } else if (state->visible && !state->native_pokedex) {
        crystal_port_pc_input(state, services, event->action);
    }
}

static void crystal_port_update(
    void* user,
    const GBPortServices* services,
    uint64_t frame_index,
    uint32_t guest_cycles) {
    (void)frame_index;
    (void)guest_cycles;
    CrystalPortState* state = (CrystalPortState*)user;
    if (state == NULL || !state->visible) return;
    state->location_available =
        crystal_semantic_read_location(
            services->semantic_reader,
            GB_SEMANTIC_READ_LIVE,
            &state->location) == GB_SEMANTIC_OK;
    state->party_available =
        crystal_semantic_read_party(
            services->semantic_reader,
            GB_SEMANTIC_READ_LIVE,
            &state->party) == GB_SEMANTIC_OK;
    state->badges_available =
        crystal_semantic_read_badges(
            services->semantic_reader,
            GB_SEMANTIC_READ_LIVE,
            &state->badges) == GB_SEMANTIC_OK;
    state->pokedex_available =
        crystal_semantic_read_pokedex(
            services->semantic_reader,
            GB_SEMANTIC_READ_LIVE,
            &state->pokedex) == GB_SEMANTIC_OK;
    state->species_available =
        (state->selected_species != 0 ||
         (state->party_available && state->party.count > 0)) &&
        crystal_semantic_read_species(
            services->semantic_reader,
            GB_SEMANTIC_READ_LIVE,
            state->selected_species != 0
                ? state->selected_species
                : state->party.species[0],
            &state->species) == GB_SEMANTIC_OK;
    if (state->selected_species == 0 && state->species_available) {
        state->selected_species = state->species.species_id;
    }
    if (!state->native_pokedex && !state->pc_pending) {
        const GBSemanticStatus pc_status =
            state->pc_box_initialized
                ? crystal_pc_load_box(
                      services->semantic_reader,
                      state->pc_box_number,
                      &state->pc_records)
                : crystal_pc_load(
                      services->semantic_reader,
                      &state->pc_records);
        state->pc_available = pc_status == GB_SEMANTIC_OK;
        if (state->pc_available) {
            state->pc_box_initialized = true;
            state->pc_box_number = state->pc_records.box_index;
            if (state->pc_party_index >= state->pc_records.party[0]) {
                state->pc_party_index = 0;
            }
            if (state->pc_box_index >= state->pc_records.box[0]) {
                state->pc_box_index = 0;
            }
        }
    }
}

static void crystal_port_line(
    GBPortFrame* frame,
    int32_t* y,
    uint32_t color,
    const char* text) {
    gbrt_port_frame_text(frame, 44, *y, color, text);
    *y += 26;
}

static void crystal_port_render(
    void* user,
    const GBPortServices* services,
    GBPortFrame* frame) {
    (void)services;
    const CrystalPortState* state = (const CrystalPortState*)user;
    if (state == NULL || !state->visible) return;
    char line[128];
    int32_t y = 44;
    gbrt_port_frame_panel(
        frame, 24, 24, 592, 680, 0x18213aeeu);
    crystal_port_line(
        frame,
        &y,
        0xf3f7ffffu,
        state->native_pokedex
            ? "Crystal Recompiled - Native Pokedex"
            : "Crystal Recompiled - Pokegear Workbench");

    if (state->location_available) {
        snprintf(
            line,
            sizeof(line),
            "Location  Map %u:%u  Position %u,%u",
            state->location.map_group,
            state->location.map_number,
            state->location.x,
            state->location.y);
    } else {
        snprintf(line, sizeof(line), "Location  unavailable");
    }
    crystal_port_line(frame, &y, 0xa9c6ffffu, line);

    if (state->party_available) {
        int used = snprintf(
            line, sizeof(line), "Party  %u/6", state->party.count);
        for (size_t index = 0;
             index < state->party.count && used > 0 &&
             (size_t)used < sizeof(line);
             ++index) {
            used += snprintf(
                line + used,
                sizeof(line) - (size_t)used,
                "  #%u",
                state->party.species[index]);
        }
    } else {
        snprintf(line, sizeof(line), "Party  unavailable");
    }
    crystal_port_line(frame, &y, 0xd7e4ffffu, line);

    if (state->badges_available) {
        snprintf(
            line,
            sizeof(line),
            "Badges  %u/16  (Johto %u/8, Kanto %u/8)",
            state->badges.total_count,
            state->badges.johto_count,
            state->badges.kanto_count);
    } else {
        snprintf(line, sizeof(line), "Badges  unavailable");
    }
    crystal_port_line(frame, &y, 0xd7e4ffffu, line);

    if (state->pokedex_available) {
        snprintf(
            line,
            sizeof(line),
            "Pokedex  caught %u/251  seen %u/251",
            state->pokedex.caught_count,
            state->pokedex.seen_count);
    } else {
        snprintf(line, sizeof(line), "Pokedex  unavailable");
    }
    crystal_port_line(frame, &y, 0xd7e4ffffu, line);

    if (!state->species_available) {
        crystal_port_line(
            frame,
            &y,
            0xffc38affu,
            "Species page  unavailable (party empty or ROM data unreadable)");
        return;
    }

    snprintf(
        line,
        sizeof(line),
        "Species #%u  HP %u  Atk %u  Def %u  Spd %u  SpA %u  SpD %u",
        state->species.species_id,
        state->species.hp,
        state->species.attack,
        state->species.defense,
        state->species.speed,
        state->species.special_attack,
        state->species.special_defense);
    crystal_port_line(frame, &y, 0xffdc8affu, line);
    snprintf(
        line,
        sizeof(line),
        "Types #%u/#%u  Catch %u  Base EXP %u",
        state->species.primary_type,
        state->species.secondary_type,
        state->species.catch_rate,
        state->species.base_experience);
    crystal_port_line(frame, &y, 0xd7e4ffffu, line);

    if (state->species.encounter_knowledge ==
        CRYSTAL_KNOWLEDGE_NOT_MODELED) {
        crystal_port_line(
            frame,
            &y,
            0xffc38affu,
            "Encounters  unavailable - encounter tables are not modeled yet");
    } else if (state->species.encounter_knowledge ==
               CRYSTAL_KNOWLEDGE_LOCKED) {
        crystal_port_line(
            frame,
            &y,
            0xffc38affu,
            "Encounters  locked by current game progress");
    }

    if (state->species.evolution_count == 0) {
        crystal_port_line(
            frame, &y, 0xd7e4ffffu, "Evolution  none in ROM table");
    } else {
        for (size_t index = 0;
             index < state->species.evolution_count;
             ++index) {
            const CrystalEvolution* evolution =
                &state->species.evolutions[index];
            snprintf(
                line,
                sizeof(line),
                "Evolution  method #%u  parameter %u%s  -> species #%u",
                evolution->method,
                evolution->parameter,
                evolution->condition != 0 ? " (conditional)" : "",
                evolution->target_species);
            crystal_port_line(frame, &y, 0xb7f3c4ffu, line);
        }
    }

    int used = snprintf(line, sizeof(line), "Level-up moves");
    for (size_t index = 0;
         index < state->species.level_move_count && index < 8 &&
         used > 0 && (size_t)used < sizeof(line);
         ++index) {
        const CrystalLevelMove* move = &state->species.level_moves[index];
        used += snprintf(
            line + used,
            sizeof(line) - (size_t)used,
            "  L%u:#%u",
            move->level,
            move->move_id);
    }
    crystal_port_line(frame, &y, 0xb7f3c4ffu, line);
    if (state->species.level_move_count > 8) {
        snprintf(
            line,
            sizeof(line),
            "Moves  showing 8 of %u ROM entries",
            state->species.level_move_count);
        crystal_port_line(frame, &y, 0x8fa6c4ffu, line);
    }

    if (state->native_pokedex) return;
    if (!state->pc_available) {
        crystal_port_line(
            frame, &y, 0xffc38affu, "Native PC  unavailable");
        return;
    }
    static const char* focus_names[] = {
        "Party", "Box", "Search", "Sort",
    };
    snprintf(
        line,
        sizeof(line),
        "Native PC  Party %u/6  Box %u/14 (%u/20)  Focus %s",
        state->pc_records.party[0],
        (unsigned)(state->pc_box_number + 1u),
        state->pc_records.box[0],
        focus_names[state->pc_focus]);
    crystal_port_line(frame, &y, 0x8ff0ffffu, line);
    snprintf(
        line,
        sizeof(line),
        "Party cursor %u  species #%u  Box cursor %u  species #%u",
        (unsigned)(state->pc_party_index + 1u),
        state->pc_records.party[0] > 0
            ? state->pc_records.party[1u + state->pc_party_index]
            : 0,
        (unsigned)(state->pc_box_index + 1u),
        state->pc_records.box[0] > 0
            ? state->pc_records.box[1u + state->pc_box_index]
            : 0);
    crystal_port_line(frame, &y, 0xd7e4ffffu, line);
    size_t matches[CRYSTAL_BOX_CAPACITY];
    const size_t match_count = crystal_pc_search_box(
        &state->pc_records, state->pc_filter_species, matches);
    snprintf(
        line,
        sizeof(line),
        "Search  species %s%u  matches %u",
        state->pc_filter_species == 0 ? "all " : "#",
        state->pc_filter_species,
        (unsigned)match_count);
    crystal_port_line(frame, &y, 0xb7f3c4ffu, line);
    snprintf(
        line,
        sizeof(line),
        "Sort  %s  | Accept stages, Accept confirms, Back cancels",
        state->pc_sort == CRYSTAL_PC_SORT_SPECIES
            ? "species"
            : "level");
    crystal_port_line(frame, &y, 0xb7f3c4ffu, line);
    if (state->pc_status[0] != '\0') {
        crystal_port_line(
            frame,
            &y,
            state->pc_pending ? 0xffdc8affu : 0xd7e4ffffu,
            state->pc_status);
    }
}

static const GBPortModule crystal_port_module = {
    .abi_version = GB_PORT_ABI_VERSION,
    .module_id = "crystal-workbench",
    .module_version = 8,
    .rom_sha256 = CRYSTAL_SEMANTIC_ROM_SHA256,
    .rom_size = 2097152u,
    .user = &crystal_port_state,
    .activate = crystal_port_activate,
    .input = crystal_port_input,
    .update = crystal_port_update,
    .render = crystal_port_render,
};

const GBPortModule* gb_port_module_get(void) {
    return &crystal_port_module;
}
