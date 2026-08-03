#include "gbrt_port.h"
#include "gbrt.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct ModuleState {
    bool visible;
    uint64_t updates;
    size_t submitted_commands;
    unsigned logs;
    GBSemanticStatus edit_status;
    GBHostConfigurationStatus configuration_status;
} ModuleState;

static GBSemanticStatus validate_staged_byte(
    const GBSemanticReader* reader,
    void* user) {
    uint8_t value = 0;
    const uint8_t expected = *(const uint8_t*)user;
    const GBSemanticStatus status = gbrt_semantic_read(
        reader,
        reader->rom_sha256,
        GB_SEMANTIC_READ_LIVE,
        GB_SEMANTIC_WRAM,
        0,
        0xC123u,
        &value,
        1);
    return status == GB_SEMANTIC_OK && value == expected
        ? GB_SEMANTIC_OK
        : GB_SEMANTIC_INVALID_DATA;
}

static GBSemanticStatus stage_test_edit(
    GBSemanticTransaction* transaction,
    void* user) {
    const uint8_t value = *(const uint8_t*)user;
    GBSemanticStatus status = gbrt_semantic_transaction_write(
        transaction,
        GB_SEMANTIC_WRAM,
        0,
        0xC123u,
        &value,
        1);
    if (status != GB_SEMANTIC_OK) return status;
    return gbrt_semantic_transaction_validate(
        transaction,
        validate_staged_byte,
        user);
}

static bool activate(
    void* user,
    const GBPortServices* services) {
    ModuleState* state = (ModuleState*)user;
    return state != NULL && services != NULL &&
           services->abi_version == GB_PORT_ABI_VERSION &&
           services->headless && services->semantic_reader != NULL &&
           services->semantic_edit_user != NULL &&
           services->run_semantic_edit != NULL &&
           services->host_configuration != NULL &&
           services->host_configuration_contract != NULL &&
           services->host_configuration_user != NULL &&
           services->apply_host_configuration != NULL &&
           services->input_capture_user != NULL &&
           services->set_input_capture != NULL &&
           services->metadata != NULL &&
           strcmp(services->metadata->game_id, "fixture") == 0;
}

static void input(
    void* user,
    const GBPortServices* services,
    const GBPortInputEvent* event) {
    ModuleState* state = (ModuleState*)user;
    if (event->pressed && event->action == GB_PORT_INPUT_TOGGLE_UI) {
        state->visible = !state->visible;
        services->set_input_capture(
            services->input_capture_user, state->visible);
    } else if (event->pressed && event->action == GB_PORT_INPUT_ACCEPT) {
        const uint8_t value = 0x5Au;
        state->edit_status = services->run_semantic_edit(
            services->semantic_edit_user,
            services->metadata->rom_sha256,
            stage_test_edit,
            (void*)&value);
    } else if (event->pressed && event->action == GB_PORT_INPUT_RIGHT) {
        GBHostConfiguration configuration = {
            .abi_version = GB_HOST_CONFIGURATION_ABI_VERSION,
            .present = 1,
            .applied = 1,
            .enabled = 1,
            .schema_version = 1,
            .offset = 2,
            .minimum = 1,
            .maximum = 100,
        };
        snprintf(configuration.schema, sizeof(configuration.schema), "%s", "fixture.config");
        snprintf(configuration.policy_id, sizeof(configuration.policy_id), "%s", "fixture-v1");
        state->configuration_status = services->apply_host_configuration(
            services->host_configuration_user, &configuration);
    }
}

static void update(
    void* user,
    const GBPortServices* services,
    uint64_t frame_index,
    uint32_t guest_cycles) {
    (void)services;
    (void)frame_index;
    (void)guest_cycles;
    ((ModuleState*)user)->updates++;
}

static void render(
    void* user,
    const GBPortServices* services,
    GBPortFrame* frame) {
    (void)services;
    if (!((ModuleState*)user)->visible) return;
    gbrt_port_frame_panel(frame, 10, 10, 100, 80, 0x11223344u);
    gbrt_port_frame_text(frame, 20, 20, 0xffffffffu, "fixture");
}

static void log_message(
    void* user,
    GBPortLogLevel level,
    const char* module_id,
    const char* message) {
    (void)level;
    (void)module_id;
    (void)message;
    ((ModuleState*)user)->logs++;
}

static void submit_frame(void* user, const GBPortFrame* frame) {
    ((ModuleState*)user)->submitted_commands = frame->command_count;
}

int main(void) {
    static const char* hash =
        "9f64a747e1b97f131fabb6b447296c9b6f0201e79fb3c5356e6c77e89b6a806a";
    uint8_t rom[4] = {1, 2, 3, 4};
    uint8_t wram[0x8000] = {0};
    uint8_t before[sizeof(wram)];
    memcpy(before, wram, sizeof(wram));
    GBContext context = {0};
    context.rom = rom;
    context.rom_size = sizeof(rom);
    context.wram = wram;
    const GBHostConfigurationContract configuration_contract = {
        GB_HOST_CONFIGURATION_ABI_VERSION,
        "fixture.config",
        1,
        "fixture-v1",
        -3,
        3,
        1,
        100,
    };
    gb_context_set_host_configuration_service(
        &context,
        &configuration_contract,
        "test_port_host_configuration.json");
    ModuleState state = {0};
    GBPortModule module = {
        .abi_version = GB_PORT_ABI_VERSION,
        .module_id = "fixture-module",
        .module_version = 1,
        .rom_sha256 = hash,
        .rom_size = sizeof(rom),
        .user = &state,
        .activate = activate,
        .input = input,
        .update = update,
        .render = render,
    };
    GBPortMetadata metadata = {
        .abi_version = GB_PORT_ABI_VERSION,
        .game_id = "fixture",
        .game_title = "Fixture",
        .rom_sha256 = hash,
        .rom_size = sizeof(rom),
    };
    GBPortHost host = {
        .abi_version = GB_PORT_ABI_VERSION,
        .headless = true,
        .user = &state,
        .log = log_message,
        .submit_frame = submit_frame,
    };

    module.abi_version++;
    if (gbrt_port_attach(&context, &module, &metadata, &host) !=
        GB_PORT_ABI_MISMATCH) {
        return 1;
    }
    module.abi_version = GB_PORT_ABI_VERSION;
    rom[0] = 5;
    if (gbrt_port_attach(&context, &module, &metadata, &host) !=
        GB_PORT_ROM_MISMATCH) {
        return 2;
    }
    rom[0] = 1;
    if (gbrt_port_attach(&context, &module, &metadata, &host) != GB_PORT_OK ||
        gbrt_port_attach(&context, &module, &metadata, &host) !=
            GB_PORT_ALREADY_ATTACHED) {
        return 3;
    }
    const GBPortInputEvent toggle = {GB_PORT_INPUT_TOGGLE_UI, true};
    if (gbrt_port_input(&context, &toggle) != GB_PORT_OK ||
        gbrt_port_update(&context, 1, 70224) != GB_PORT_OK ||
        gbrt_port_render(&context, 1280, 720) != GB_PORT_OK) {
        return 4;
    }
    const GBPortInputEvent accept = {GB_PORT_INPUT_ACCEPT, true};
    if (gbrt_port_input(&context, &accept) != GB_PORT_OK ||
        state.edit_status != GB_SEMANTIC_OK ||
        wram[0x123] != 0x5Au) {
        return 4;
    }
    const GBPortInputEvent right = {GB_PORT_INPUT_RIGHT, true};
    if (gbrt_port_input(&context, &right) != GB_PORT_OK ||
        state.configuration_status != GB_HOST_CONFIGURATION_OK ||
        !context.config.host_configuration.present ||
        !context.config.host_configuration.applied ||
        !context.config.host_configuration.enabled ||
        context.config.host_configuration.offset != 2) {
        return 4;
    }
    FILE* configuration_file = fopen(
        context.host_configuration_path, "rb");
    if (configuration_file == NULL) return 4;
    fclose(configuration_file);
    remove(context.host_configuration_path);
    const GBPortSnapshot snapshot = gbrt_port_snapshot(&context);
    if (!state.visible || state.updates != 1 || state.submitted_commands != 2 ||
        state.logs != 1 || !snapshot.active || !snapshot.headless ||
        snapshot.input_events != 3 || snapshot.updates != 1 ||
        snapshot.renders != 1 || snapshot.last_command_count != 2 ||
        snapshot.extension_count != 0 || !snapshot.input_captured ||
        memcmp(before, wram, 0x123) != 0 ||
        memcmp(before + 0x124, wram + 0x124, sizeof(wram) - 0x124) != 0) {
        return 5;
    }
    const char* path = "test_port_extension_state.json";
    if (!gbrt_port_write_state_json(&context, path)) return 6;
    FILE* file = fopen(path, "rb");
    if (file == NULL) return 6;
    char json[512] = {0};
    const size_t read = fread(json, 1, sizeof(json) - 1, file);
    fclose(file);
    remove(path);
    if (read == 0 || strstr(json, "\"headless\": true") == NULL ||
        strstr(json, "\"last_command_count\": 2") == NULL ||
        strstr(json, "\"input_captured\": true") == NULL) {
        return 6;
    }
    gbrt_port_detach(&context);
    if (gbrt_port_snapshot(&context).active ||
        gbrt_port_update(&context, 2, 70224) != GB_PORT_NOT_ATTACHED ||
        state.logs != 2) {
        return 7;
    }
    return 0;
}
