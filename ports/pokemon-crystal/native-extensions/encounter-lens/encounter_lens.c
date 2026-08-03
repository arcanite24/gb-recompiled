#include "crystal_semantic.h"
#include "gbrt_port.h"

#include <stdio.h>
#include <string.h>

#define CRYSTAL_ROUTE29_MAP_GROUP 24u
#define CRYSTAL_ROUTE29_MAP_NUMBER 3u
#define CRYSTAL_ROUTE29_ROM_BANK 10u
#define CRYSTAL_ROUTE29_ROM_ADDRESS 0x6dfdu
#define CRYSTAL_TIME_BANK 1u
#define CRYSTAL_TIME_ADDRESS 0xd269u

typedef struct CrystalEncounterLensState {
    bool visible;
    bool location_available;
    bool encounters_available;
    CrystalLocation location;
    uint8_t time_of_day;
    uint8_t encounter_rate;
    uint8_t levels[7];
    uint8_t species[7];
} CrystalEncounterLensState;

static CrystalEncounterLensState encounter_lens_state;

static bool encounter_lens_activate(
    void* user,
    const GBPortServices* services) {
    if (user == NULL || services == NULL ||
        services->abi_version != GB_PORT_ABI_VERSION ||
        services->metadata == NULL ||
        services->semantic_reader == NULL ||
        services->metadata->rom_sha256 == NULL ||
        strcmp(
            services->metadata->rom_sha256,
            CRYSTAL_SEMANTIC_ROM_SHA256) != 0) {
        return false;
    }
    *(CrystalEncounterLensState*)user =
        (CrystalEncounterLensState){0};
    return true;
}

static void encounter_lens_input(
    void* user,
    const GBPortServices* services,
    const GBPortInputEvent* event) {
    CrystalEncounterLensState* state =
        (CrystalEncounterLensState*)user;
    if (state == NULL || services == NULL || event == NULL ||
        !event->pressed ||
        event->action != GB_PORT_INPUT_TOGGLE_ENCOUNTERS) {
        return;
    }
    state->visible = !state->visible;
    services->log(
        services->host_user,
        GB_PORT_LOG_INFO,
        "org.gbrecompiled.crystal.encounter-lens",
        state->visible
            ? "encounter lens shown"
            : "encounter lens hidden");
}

static void encounter_lens_update(
    void* user,
    const GBPortServices* services,
    uint64_t frame_index,
    uint32_t guest_cycles) {
    (void)frame_index;
    (void)guest_cycles;
    CrystalEncounterLensState* state =
        (CrystalEncounterLensState*)user;
    if (state == NULL || services == NULL || !state->visible) return;

    state->location_available =
        crystal_semantic_read_location(
            services->semantic_reader,
            GB_SEMANTIC_READ_LIVE,
            &state->location) == GB_SEMANTIC_OK;
    state->encounters_available = false;
    if (!state->location_available ||
        state->location.map_group != CRYSTAL_ROUTE29_MAP_GROUP ||
        state->location.map_number != CRYSTAL_ROUTE29_MAP_NUMBER) {
        return;
    }

    uint8_t table[47];
    if (gbrt_semantic_read(
            services->semantic_reader,
            CRYSTAL_SEMANTIC_ROM_SHA256,
            GB_SEMANTIC_READ_LIVE,
            GB_SEMANTIC_BANKED_WRAM,
            CRYSTAL_TIME_BANK,
            CRYSTAL_TIME_ADDRESS,
            &state->time_of_day,
            1) != GB_SEMANTIC_OK ||
        state->time_of_day > 2 ||
        gbrt_semantic_read(
            services->semantic_reader,
            CRYSTAL_SEMANTIC_ROM_SHA256,
            GB_SEMANTIC_READ_LIVE,
            GB_SEMANTIC_PHYSICAL_ROM,
            CRYSTAL_ROUTE29_ROM_BANK,
            CRYSTAL_ROUTE29_ROM_ADDRESS,
            table,
            sizeof(table)) != GB_SEMANTIC_OK ||
        table[0] != CRYSTAL_ROUTE29_MAP_GROUP ||
        table[1] != CRYSTAL_ROUTE29_MAP_NUMBER ||
        table[2] != 25 ||
        table[3] != 25 ||
        table[4] != 25) {
        return;
    }

    state->encounter_rate = table[2u + state->time_of_day];
    const size_t slots = 5u + (size_t)state->time_of_day * 14u;
    for (size_t index = 0; index < 7; ++index) {
        state->levels[index] = table[slots + index * 2u];
        state->species[index] = table[slots + index * 2u + 1u];
    }
    state->encounters_available = true;
}

static void encounter_lens_text(
    GBPortFrame* frame,
    int32_t x,
    int32_t* y,
    uint32_t color,
    const char* text) {
    gbrt_port_frame_text(frame, x, *y, color, text);
    *y += 26;
}

static void encounter_lens_render(
    void* user,
    const GBPortServices* services,
    GBPortFrame* frame) {
    (void)services;
    const CrystalEncounterLensState* state =
        (const CrystalEncounterLensState*)user;
    if (state == NULL || frame == NULL || !state->visible) return;
    const int32_t panel_width = 592;
    const int32_t x = frame->canvas_width >= 1280u
        ? (int32_t)frame->canvas_width - panel_width - 24
        : 24;
    int32_t y = 44;
    char line[128];
    gbrt_port_frame_panel(
        frame, x, 24, panel_width, 310, 0x132a24eeu);
    encounter_lens_text(
        frame,
        x + 20,
        &y,
        0xf3f7ffffu,
        "Encounter Lens - live overlaid data");
    if (!state->location_available) {
        encounter_lens_text(
            frame,
            x + 20,
            &y,
            0xffc38affu,
            "Location unavailable");
        return;
    }
    snprintf(
        line,
        sizeof(line),
        "Map %u:%u  Position %u,%u",
        state->location.map_group,
        state->location.map_number,
        state->location.x,
        state->location.y);
    encounter_lens_text(frame, x + 20, &y, 0xa9c6ffffu, line);
    if (!state->encounters_available) {
        encounter_lens_text(
            frame,
            x + 20,
            &y,
            0xffc38affu,
            "No reviewed encounter table for this map");
        return;
    }
    static const char* time_names[] = {"Morning", "Day", "Night"};
    snprintf(
        line,
        sizeof(line),
        "Route 29  %s  encounter rate %u",
        time_names[state->time_of_day],
        state->encounter_rate);
    encounter_lens_text(frame, x + 20, &y, 0xb7f3c4ffu, line);
    for (size_t index = 0; index < 7; ++index) {
        snprintf(
            line,
            sizeof(line),
            "Slot %u  species #%u  level %u",
            (unsigned)(index + 1u),
            state->species[index],
            state->levels[index]);
        encounter_lens_text(frame, x + 20, &y, 0xd7e4ffffu, line);
    }
}

static const GBPortExtension encounter_lens_extension = {
    .abi_version = GB_PORT_EXTENSION_ABI_VERSION,
    .extension_id = "org.gbrecompiled.crystal.encounter-lens",
    .extension_version = 1,
    .priority = 200,
    .rom_sha256 = CRYSTAL_SEMANTIC_ROM_SHA256,
    .rom_size = 2097152u,
    .user = &encounter_lens_state,
    .activate = encounter_lens_activate,
    .input = encounter_lens_input,
    .update = encounter_lens_update,
    .render = encounter_lens_render,
};

const GBPortExtension* crystal_encounter_lens_extension_get(void) {
    return &encounter_lens_extension;
}
