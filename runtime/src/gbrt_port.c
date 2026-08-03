#include "gbrt_port.h"

#include "gbrt.h"
#include "gbrt_hash.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct GBPortState {
    GBContext* context;
    const GBPortModule* module;
    GBPortMetadata metadata;
    GBPortHost host;
    GBSemanticReader semantic_reader;
    GBPortServices services;
    GBPortServices extension_services;
    GBPortSnapshot snapshot;
    GBPortFrame last_frame;
    const GBPortExtensionSet* extension_set;
    const GBPortExtension*
        extensions[GB_PORT_MAX_EXTENSIONS];
    size_t active_extension_count;
} GBPortState;

static void default_log(
    void* user,
    GBPortLogLevel level,
    const char* module_id,
    const char* message) {
    (void)user;
    static const char* names[] = {"info", "warning", "error"};
    const unsigned index =
        (unsigned)level < sizeof(names) / sizeof(names[0])
            ? (unsigned)level
            : (unsigned)GB_PORT_LOG_ERROR;
    fprintf(
        stderr,
        "[GBRT][port:%s][%s] %s\n",
        module_id != NULL ? module_id : "unknown",
        names[index],
        message != NULL ? message : "");
}

static GBPortState* port_state(GBContext* context) {
    return context != NULL ? (GBPortState*)context->port_state : NULL;
}

static const GBPortState* const_port_state(const GBContext* context) {
    return context != NULL
        ? (const GBPortState*)context->port_state
        : NULL;
}

static bool valid_extension_id(const char* value) {
    if (value == NULL || value[0] < 'a' || value[0] > 'z') return false;
    bool separator = false;
    for (const unsigned char* cursor = (const unsigned char*)value;
         *cursor != '\0';
         ++cursor) {
        const bool lower = *cursor >= 'a' && *cursor <= 'z';
        const bool digit = *cursor >= '0' && *cursor <= '9';
        const bool current_separator = *cursor == '.' || *cursor == '-';
        if ((!lower && !digit && !current_separator) ||
            (current_separator && separator)) {
            return false;
        }
        separator = current_separator;
    }
    return !separator;
}

static bool load_extensions(
    GBPortState* state,
    const GBPortExtensionSet* set) {
    if (state == NULL) return false;
    if (set == NULL) return true;
    if (set->abi_version != GB_PORT_EXTENSION_ABI_VERSION ||
        set->count == 0 || set->count > GB_PORT_MAX_EXTENSIONS ||
        set->registrations == NULL) {
        return false;
    }
    const GBPortExtension* previous = NULL;
    for (size_t index = 0; index < set->count; ++index) {
        const GBPortExtensionRegistration* registration =
            &set->registrations[index];
        const GBPortExtension* extension =
            registration->get != NULL ? registration->get() : NULL;
        if (extension == NULL ||
            extension->abi_version != GB_PORT_EXTENSION_ABI_VERSION ||
            !valid_extension_id(extension->extension_id) ||
            extension->rom_sha256 == NULL ||
            registration->expected_id == NULL ||
            strcmp(extension->extension_id, registration->expected_id) != 0 ||
            extension->extension_version != registration->expected_version ||
            extension->priority != registration->expected_priority ||
            extension->rom_size != state->metadata.rom_size ||
            strcmp(extension->rom_sha256, state->metadata.rom_sha256) != 0) {
            return false;
        }
        if (previous != NULL &&
            (previous->priority > extension->priority ||
             (previous->priority == extension->priority &&
              strcmp(previous->extension_id, extension->extension_id) >= 0))) {
            return false;
        }
        state->extensions[index] = extension;
        previous = extension;
    }
    state->extension_set = set;
    state->snapshot.extension_count = set->count;
    return true;
}

static void deactivate_extensions(GBPortState* state) {
    if (state == NULL) return;
    while (state->active_extension_count > 0) {
        const GBPortExtension* extension =
            state->extensions[--state->active_extension_count];
        if (extension->deactivate != NULL) {
            extension->deactivate(
                extension->user, &state->extension_services);
        }
        state->host.log(
            state->host.user,
            GB_PORT_LOG_INFO,
            extension->extension_id,
            "extension deactivated");
    }
}

static GBSemanticStatus run_semantic_edit(
    void* service_user,
    const char* expected_rom_sha256,
    GBPortSemanticEditFn edit,
    void* edit_user) {
    GBPortState* state = (GBPortState*)service_user;
    if (state == NULL || state->context == NULL ||
        expected_rom_sha256 == NULL || edit == NULL ||
        state->metadata.rom_sha256 == NULL) {
        return GB_SEMANTIC_INVALID_ARGUMENT;
    }
    if (strcmp(
            expected_rom_sha256,
            state->metadata.rom_sha256) != 0) {
        return GB_SEMANTIC_ROM_MISMATCH;
    }

    GBSemanticTransaction transaction = {0};
    GBSemanticStatus status = gbrt_semantic_transaction_begin(
        &transaction,
        state->context,
        state->metadata.rom_sha256,
        expected_rom_sha256);
    if (status != GB_SEMANTIC_OK) return status;

    status = edit(&transaction, edit_user);
    if (status != GB_SEMANTIC_OK || !transaction.active ||
        transaction.context != state->context) {
        if (transaction.active) {
            gbrt_semantic_transaction_abort(&transaction);
        }
        return status != GB_SEMANTIC_OK
            ? status
            : GB_SEMANTIC_INVALID_DATA;
    }
    return gbrt_semantic_transaction_commit(&transaction);
}

static GBHostConfigurationStatus apply_host_configuration(
    void* service_user,
    const GBHostConfiguration* configuration) {
    GBPortState* state = (GBPortState*)service_user;
    char canonical[GB_HOST_CONFIGURATION_CANONICAL_CAPACITY];
    size_t canonical_size = 0;
    GBHostConfiguration validated = {0};
    GBHostConfigurationStatus status;
    if (state == NULL || state->context == NULL || configuration == NULL ||
        state->context->host_configuration_path == NULL ||
        state->context->host_configuration_path[0] == '\0' ||
        state->context->host_configuration_contract.schema == NULL ||
        state->context->host_configuration_contract.policy_id == NULL) {
        return GB_HOST_CONFIGURATION_WRITE_ERROR;
    }
    status = gbrt_host_configuration_serialize(
        configuration, canonical, sizeof(canonical), &canonical_size);
    if (status != GB_HOST_CONFIGURATION_OK) return status;
    status = gbrt_host_configuration_parse(
        (const uint8_t*)canonical,
        canonical_size,
        &state->context->host_configuration_contract,
        &validated);
    if (status != GB_HOST_CONFIGURATION_OK) return status;
    if (!validated.applied) return GB_HOST_CONFIGURATION_MALFORMED;
    status = gbrt_host_configuration_write_file(
        state->context->host_configuration_path, &validated);
    if (status != GB_HOST_CONFIGURATION_OK) return status;
    state->context->config.host_configuration = validated;
    return GB_HOST_CONFIGURATION_OK;
}

static void set_input_capture(void* service_user, bool captured) {
    GBPortState* state = (GBPortState*)service_user;
    if (state != NULL) state->snapshot.input_captured = captured;
}

GBPortStatus gbrt_port_attach(
    GBContext* context,
    const GBPortModule* module,
    const GBPortMetadata* metadata,
    const GBPortHost* host) {
    if (context == NULL || module == NULL || metadata == NULL ||
        host == NULL || module->module_id == NULL ||
        module->rom_sha256 == NULL || metadata->game_id == NULL ||
        metadata->game_title == NULL || metadata->rom_sha256 == NULL ||
        context->rom == NULL) {
        return GB_PORT_INVALID_ARGUMENT;
    }
    if (context->port_state != NULL) {
        return GB_PORT_ALREADY_ATTACHED;
    }
    if (module->abi_version != GB_PORT_ABI_VERSION ||
        metadata->abi_version != GB_PORT_ABI_VERSION ||
        host->abi_version != GB_PORT_ABI_VERSION) {
        return GB_PORT_ABI_MISMATCH;
    }
    if (module->rom_size != metadata->rom_size ||
        context->rom_size != metadata->rom_size ||
        strcmp(module->rom_sha256, metadata->rom_sha256) != 0 ||
        !gbrt_sha256_matches_hex(
            context->rom, context->rom_size, module->rom_sha256)) {
        return GB_PORT_ROM_MISMATCH;
    }

    GBPortState* state = (GBPortState*)calloc(1, sizeof(*state));
    if (state == NULL) {
        return GB_PORT_MODULE_REJECTED;
    }
    state->context = context;
    state->module = module;
    state->metadata = *metadata;
    state->host = *host;
    if (state->host.log == NULL) {
        state->host.log = default_log;
    }
    if (gbrt_semantic_reader_init_live(
            &state->semantic_reader,
            context,
            metadata->rom_sha256) != GB_SEMANTIC_OK) {
        free(state);
        return GB_PORT_MODULE_REJECTED;
    }
    state->services = (GBPortServices){
        .abi_version = GB_PORT_ABI_VERSION,
        .headless = host->headless,
        .metadata = &state->metadata,
        .semantic_reader = &state->semantic_reader,
        .semantic_edit_user = state,
        .run_semantic_edit = run_semantic_edit,
        .host_configuration = &context->config.host_configuration,
        .host_configuration_contract =
            &context->host_configuration_contract,
        .host_configuration_user = state,
        .apply_host_configuration = apply_host_configuration,
        .input_capture_user = state,
        .set_input_capture = set_input_capture,
        .host_user = state->host.user,
        .log = state->host.log,
    };
    state->extension_services = state->services;
    state->extension_services.semantic_edit_user = NULL;
    state->extension_services.run_semantic_edit = NULL;
    state->extension_services.host_configuration = NULL;
    state->extension_services.host_configuration_contract = NULL;
    state->extension_services.host_configuration_user = NULL;
    state->extension_services.apply_host_configuration = NULL;
    state->extension_services.input_capture_user = NULL;
    state->extension_services.set_input_capture = NULL;
#ifdef GBRT_ENABLE_PORT_EXTENSIONS
    if (!load_extensions(state, gb_port_extension_set_get())) {
        free(state);
        return GB_PORT_MODULE_REJECTED;
    }
#else
    if (!load_extensions(state, NULL)) {
        free(state);
        return GB_PORT_MODULE_REJECTED;
    }
#endif
    state->snapshot.active = true;
    state->snapshot.headless = host->headless;
    context->port_state = state;
    if (module->activate != NULL &&
        !module->activate(module->user, &state->services)) {
        context->port_state = NULL;
        free(state);
        return GB_PORT_MODULE_REJECTED;
    }
    for (size_t index = 0;
         index < state->snapshot.extension_count;
         ++index) {
        const GBPortExtension* extension = state->extensions[index];
        if (extension->activate != NULL &&
            !extension->activate(
                extension->user, &state->extension_services)) {
            deactivate_extensions(state);
            if (module->deactivate != NULL) {
                module->deactivate(module->user, &state->services);
            }
            context->port_state = NULL;
            free(state);
            return GB_PORT_MODULE_REJECTED;
        }
        state->active_extension_count++;
        state->host.log(
            state->host.user,
            GB_PORT_LOG_INFO,
            extension->extension_id,
            "extension activated");
    }
    state->host.log(
        state->host.user,
        GB_PORT_LOG_INFO,
        module->module_id,
        "module activated");
    return GB_PORT_OK;
}

void gbrt_port_detach(GBContext* context) {
    GBPortState* state = port_state(context);
    if (state == NULL) return;
    deactivate_extensions(state);
    if (state->module->deactivate != NULL) {
        state->module->deactivate(
            state->module->user, &state->services);
    }
    state->host.log(
        state->host.user,
        GB_PORT_LOG_INFO,
        state->module->module_id,
        "module deactivated");
    context->port_state = NULL;
    free(state);
}

GBPortStatus gbrt_port_input(
    GBContext* context,
    const GBPortInputEvent* event) {
    GBPortState* state = port_state(context);
    if (state == NULL) return GB_PORT_NOT_ATTACHED;
    if (event == NULL || event->action < GB_PORT_INPUT_TOGGLE_UI ||
        event->action > GB_PORT_INPUT_TOGGLE_ENCOUNTERS) {
        return GB_PORT_INVALID_ARGUMENT;
    }
    state->snapshot.input_events++;
    if (state->module->input != NULL) {
        state->module->input(
            state->module->user, &state->services, event);
    }
    for (size_t index = 0;
         index < state->active_extension_count;
         ++index) {
        const GBPortExtension* extension = state->extensions[index];
        if (extension->input != NULL) {
            extension->input(
                extension->user, &state->extension_services, event);
        }
    }
    return GB_PORT_OK;
}

GBPortStatus gbrt_port_update(
    GBContext* context,
    uint64_t frame_index,
    uint32_t guest_cycles) {
    GBPortState* state = port_state(context);
    if (state == NULL) return GB_PORT_NOT_ATTACHED;
    state->snapshot.updates++;
    if (state->module->update != NULL) {
        state->module->update(
            state->module->user,
            &state->services,
            frame_index,
            guest_cycles);
    }
    for (size_t index = 0;
         index < state->active_extension_count;
         ++index) {
        const GBPortExtension* extension = state->extensions[index];
        if (extension->update != NULL) {
            extension->update(
                extension->user,
                &state->extension_services,
                frame_index,
                guest_cycles);
        }
    }
    return GB_PORT_OK;
}

GBPortStatus gbrt_port_render(
    GBContext* context,
    uint32_t canvas_width,
    uint32_t canvas_height) {
    GBPortState* state = port_state(context);
    if (state == NULL) return GB_PORT_NOT_ATTACHED;
    if (canvas_width == 0 || canvas_height == 0) {
        return GB_PORT_INVALID_ARGUMENT;
    }
    GBPortFrame frame = {
        .abi_version = GB_PORT_ABI_VERSION,
        .canvas_width = canvas_width,
        .canvas_height = canvas_height,
    };
    if (state->module->render != NULL) {
        state->module->render(
            state->module->user, &state->services, &frame);
    }
    for (size_t index = 0;
         index < state->active_extension_count;
         ++index) {
        const GBPortExtension* extension = state->extensions[index];
        if (extension->render != NULL) {
            extension->render(
                extension->user, &state->extension_services, &frame);
        }
    }
    state->snapshot.renders++;
    state->snapshot.last_command_count = frame.command_count;
    state->last_frame = frame;
    if (state->host.submit_frame != NULL) {
        state->host.submit_frame(state->host.user, &frame);
    }
    return GB_PORT_OK;
}

GBPortSnapshot gbrt_port_snapshot(const GBContext* context) {
    const GBPortState* state = const_port_state(context);
    return state != NULL ? state->snapshot : (GBPortSnapshot){0};
}

bool gbrt_port_write_state_json(const GBContext* context, const char* path) {
    const GBPortState* state = const_port_state(context);
    if (state == NULL || path == NULL || path[0] == '\0') return false;
    FILE* file = fopen(path, "wb");
    if (file == NULL) return false;
    int result = fprintf(
        file,
        "{\n"
        "  \"schema\": \"gbrecompiled.port-state\",\n"
        "  \"version\": 3,\n"
        "  \"module_id\": \"%s\",\n"
        "  \"module_version\": %u,\n"
        "  \"active\": %s,\n"
        "  \"headless\": %s,\n"
        "  \"input_events\": %llu,\n"
        "  \"updates\": %llu,\n"
        "  \"renders\": %llu,\n"
        "  \"last_command_count\": %zu,\n"
        "  \"input_captured\": %s,\n"
        "  \"extensions\": [",
        state->module->module_id,
        state->module->module_version,
        state->snapshot.active ? "true" : "false",
        state->snapshot.headless ? "true" : "false",
        (unsigned long long)state->snapshot.input_events,
        (unsigned long long)state->snapshot.updates,
        (unsigned long long)state->snapshot.renders,
        state->snapshot.last_command_count,
        state->snapshot.input_captured ? "true" : "false");
    bool wrote = result > 0;
    for (size_t index = 0;
         wrote && index < state->active_extension_count;
         ++index) {
        const GBPortExtension* extension = state->extensions[index];
        result = fprintf(
            file,
            "%s{\"id\":\"%s\",\"version\":%u,\"priority\":%u}",
            index == 0 ? "" : ",",
            extension->extension_id,
            extension->extension_version,
            extension->priority);
        wrote = result > 0;
    }
    result = wrote
        ? fprintf(
              file,
              "],\n"
              "  \"frame\": {\n"
              "    \"canvas_width\": %u,\n"
              "    \"canvas_height\": %u,\n"
              "    \"commands\": [\n",
        state->last_frame.canvas_width,
              state->last_frame.canvas_height)
        : -1;
    wrote = result > 0;
    for (size_t index = 0;
         wrote && index < state->last_frame.command_count;
         ++index) {
        const GBPortDrawCommand* command =
            &state->last_frame.commands[index];
        result = fprintf(
            file,
            "      {\"type\":\"%s\",\"x\":%d,\"y\":%d,"
            "\"width\":%d,\"height\":%d,\"color_rgba\":%u",
            command->type == GB_PORT_DRAW_PANEL ? "panel" : "text",
            command->x,
            command->y,
            command->width,
            command->height,
            command->color_rgba);
        wrote = result > 0;
        if (wrote && command->type == GB_PORT_DRAW_TEXT) {
            wrote = fputs(",\"text\":\"", file) >= 0;
            for (const unsigned char* cursor =
                     (const unsigned char*)command->text;
                 wrote && *cursor != '\0';
                 ++cursor) {
                if (*cursor == '"' || *cursor == '\\') {
                    wrote = fputc('\\', file) != EOF &&
                            fputc(*cursor, file) != EOF;
                } else if (*cursor >= 0x20u) {
                    wrote = fputc(*cursor, file) != EOF;
                }
            }
            wrote = wrote && fputc('"', file) != EOF;
        }
        wrote = wrote &&
                fprintf(
                    file,
                    "}%s\n",
                    index + 1u < state->last_frame.command_count
                        ? ","
                        : "") > 0;
    }
    wrote = wrote &&
            fputs(
                "    ]\n"
                "  }\n"
                "}\n",
                file) >= 0;
    const bool closed = fclose(file) == 0;
    return wrote && closed;
}

bool gbrt_port_frame_panel(
    GBPortFrame* frame,
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height,
    uint32_t color_rgba) {
    if (frame == NULL || frame->abi_version != GB_PORT_ABI_VERSION ||
        width <= 0 || height <= 0 ||
        frame->command_count >= GB_PORT_MAX_DRAW_COMMANDS) {
        return false;
    }
    frame->commands[frame->command_count++] = (GBPortDrawCommand){
        .type = GB_PORT_DRAW_PANEL,
        .x = x,
        .y = y,
        .width = width,
        .height = height,
        .color_rgba = color_rgba,
    };
    return true;
}

bool gbrt_port_frame_text(
    GBPortFrame* frame,
    int32_t x,
    int32_t y,
    uint32_t color_rgba,
    const char* text) {
    if (frame == NULL || frame->abi_version != GB_PORT_ABI_VERSION ||
        text == NULL || frame->command_count >= GB_PORT_MAX_DRAW_COMMANDS) {
        return false;
    }
    GBPortDrawCommand* command =
        &frame->commands[frame->command_count++];
    *command = (GBPortDrawCommand){
        .type = GB_PORT_DRAW_TEXT,
        .x = x,
        .y = y,
        .color_rgba = color_rgba,
    };
    snprintf(command->text, sizeof(command->text), "%s", text);
    return true;
}
