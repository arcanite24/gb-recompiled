#ifndef GBRT_PORT_H
#define GBRT_PORT_H

#include "gbrt_host_configuration.h"
#include "gbrt_semantic.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GB_PORT_ABI_VERSION 3u
#define GB_PORT_EXTENSION_ABI_VERSION 1u
#define GB_PORT_MAX_EXTENSIONS 16u
#define GB_PORT_MAX_DRAW_COMMANDS 128u
#define GB_PORT_DRAW_TEXT_CAPACITY 128u

typedef struct GBContext GBContext;

typedef enum GBPortStatus {
    GB_PORT_OK = 0,
    GB_PORT_INVALID_ARGUMENT,
    GB_PORT_ABI_MISMATCH,
    GB_PORT_ROM_MISMATCH,
    GB_PORT_ALREADY_ATTACHED,
    GB_PORT_MODULE_REJECTED,
    GB_PORT_NOT_ATTACHED,
} GBPortStatus;

typedef enum GBPortLogLevel {
    GB_PORT_LOG_INFO = 0,
    GB_PORT_LOG_WARNING = 1,
    GB_PORT_LOG_ERROR = 2,
} GBPortLogLevel;

typedef enum GBPortInputAction {
    GB_PORT_INPUT_TOGGLE_UI = 0,
    GB_PORT_INPUT_CLOSE_UI = 1,
    GB_PORT_INPUT_UP = 2,
    GB_PORT_INPUT_DOWN = 3,
    GB_PORT_INPUT_LEFT = 4,
    GB_PORT_INPUT_RIGHT = 5,
    GB_PORT_INPUT_ACCEPT = 6,
    GB_PORT_INPUT_BACK = 7,
    GB_PORT_INPUT_OPEN_UI = 8,
    GB_PORT_INPUT_OPEN_PC = 9,
    GB_PORT_INPUT_TOGGLE_ENCOUNTERS = 10,
} GBPortInputAction;

typedef struct GBPortInputEvent {
    GBPortInputAction action;
    bool pressed;
} GBPortInputEvent;

typedef enum GBPortDrawCommandType {
    GB_PORT_DRAW_PANEL = 0,
    GB_PORT_DRAW_TEXT = 1,
} GBPortDrawCommandType;

typedef struct GBPortDrawCommand {
    GBPortDrawCommandType type;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    uint32_t color_rgba;
    char text[GB_PORT_DRAW_TEXT_CAPACITY];
} GBPortDrawCommand;

typedef struct GBPortFrame {
    uint32_t abi_version;
    uint32_t canvas_width;
    uint32_t canvas_height;
    size_t command_count;
    GBPortDrawCommand commands[GB_PORT_MAX_DRAW_COMMANDS];
} GBPortFrame;

typedef struct GBPortMetadata {
    uint32_t abi_version;
    const char* game_id;
    const char* game_title;
    const char* rom_sha256;
    size_t rom_size;
} GBPortMetadata;

typedef void (*GBPortLogFn)(
    void* user,
    GBPortLogLevel level,
    const char* module_id,
    const char* message);

typedef void (*GBPortSubmitFrameFn)(
    void* user,
    const GBPortFrame* frame);

/*
 * A source-built module may stage and validate one exact-ROM semantic edit
 * inside this callback. The transaction pointer is valid only for the
 * duration of the callback and must not be retained. The runtime owns begin,
 * commit, and abort.
 */
typedef GBSemanticStatus (*GBPortSemanticEditFn)(
    GBSemanticTransaction* transaction,
    void* user);

typedef GBSemanticStatus (*GBPortRunSemanticEditFn)(
    void* service_user,
    const char* expected_rom_sha256,
    GBPortSemanticEditFn edit,
    void* edit_user);

typedef GBHostConfigurationStatus (*GBPortApplyHostConfigurationFn)(
    void* service_user,
    const GBHostConfiguration* configuration);

typedef void (*GBPortSetInputCaptureFn)(
    void* service_user,
    bool captured);

typedef struct GBPortHost {
    uint32_t abi_version;
    bool headless;
    void* user;
    GBPortLogFn log;
    GBPortSubmitFrameFn submit_frame;
} GBPortHost;

typedef struct GBPortServices {
    uint32_t abi_version;
    bool headless;
    const GBPortMetadata* metadata;
    const GBSemanticReader* semantic_reader;
    void* semantic_edit_user;
    GBPortRunSemanticEditFn run_semantic_edit;
    const GBHostConfiguration* host_configuration;
    const GBHostConfigurationContract* host_configuration_contract;
    void* host_configuration_user;
    GBPortApplyHostConfigurationFn apply_host_configuration;
    void* input_capture_user;
    GBPortSetInputCaptureFn set_input_capture;
    void* host_user;
    GBPortLogFn log;
} GBPortServices;

typedef bool (*GBPortActivateFn)(
    void* module_user,
    const GBPortServices* services);
typedef void (*GBPortDeactivateFn)(
    void* module_user,
    const GBPortServices* services);
typedef void (*GBPortInputFn)(
    void* module_user,
    const GBPortServices* services,
    const GBPortInputEvent* event);
typedef void (*GBPortUpdateFn)(
    void* module_user,
    const GBPortServices* services,
    uint64_t frame_index,
    uint32_t guest_cycles);
typedef void (*GBPortRenderFn)(
    void* module_user,
    const GBPortServices* services,
    GBPortFrame* frame);

/*
 * Source-built extensions add independently versioned host behavior to one
 * exact-ROM port module. The runtime owns deterministic lifecycle dispatch;
 * extensions receive a reduced read/input/draw service view with the
 * semantic-edit callback cleared, plus the bounded draw-command frame.
 */
typedef struct GBPortExtension {
    uint32_t abi_version;
    const char* extension_id;
    uint32_t extension_version;
    uint32_t priority;
    const char* rom_sha256;
    size_t rom_size;
    void* user;
    GBPortActivateFn activate;
    GBPortDeactivateFn deactivate;
    GBPortInputFn input;
    GBPortUpdateFn update;
    GBPortRenderFn render;
} GBPortExtension;

typedef const GBPortExtension* (*GBPortExtensionGetFn)(void);

typedef struct GBPortExtensionRegistration {
    GBPortExtensionGetFn get;
    const char* expected_id;
    uint32_t expected_version;
    uint32_t expected_priority;
} GBPortExtensionRegistration;

typedef struct GBPortExtensionSet {
    uint32_t abi_version;
    size_t count;
    const GBPortExtensionRegistration* registrations;
} GBPortExtensionSet;

typedef struct GBPortModule {
    uint32_t abi_version;
    const char* module_id;
    uint32_t module_version;
    const char* rom_sha256;
    size_t rom_size;
    void* user;
    GBPortActivateFn activate;
    GBPortDeactivateFn deactivate;
    GBPortInputFn input;
    GBPortUpdateFn update;
    GBPortRenderFn render;
} GBPortModule;

typedef struct GBPortSnapshot {
    bool active;
    bool headless;
    uint64_t input_events;
    uint64_t updates;
    uint64_t renders;
    size_t last_command_count;
    size_t extension_count;
    bool input_captured;
} GBPortSnapshot;

GBPortStatus gbrt_port_attach(
    GBContext* context,
    const GBPortModule* module,
    const GBPortMetadata* metadata,
    const GBPortHost* host);
void gbrt_port_detach(GBContext* context);
GBPortStatus gbrt_port_input(
    GBContext* context,
    const GBPortInputEvent* event);
GBPortStatus gbrt_port_update(
    GBContext* context,
    uint64_t frame_index,
    uint32_t guest_cycles);
GBPortStatus gbrt_port_render(
    GBContext* context,
    uint32_t canvas_width,
    uint32_t canvas_height);
GBPortSnapshot gbrt_port_snapshot(const GBContext* context);
bool gbrt_port_write_state_json(const GBContext* context, const char* path);

bool gbrt_port_frame_panel(
    GBPortFrame* frame,
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height,
    uint32_t color_rgba);
bool gbrt_port_frame_text(
    GBPortFrame* frame,
    int32_t x,
    int32_t y,
    uint32_t color_rgba,
    const char* text);

#ifdef GBRT_ENABLE_PORT_MODULE
const GBPortModule* gb_port_module_get(void);
#endif

#ifdef GBRT_ENABLE_PORT_EXTENSIONS
const GBPortExtensionSet* gb_port_extension_set_get(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
