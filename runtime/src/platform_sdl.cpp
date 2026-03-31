/**
 * @file platform_sdl.cpp
 * @brief SDL2 platform implementation for GameBoy runtime with ImGui
 */

#include "platform_sdl.h"
#include "gbrt.h"   /* For GBPlatformCallbacks */
#include "ppu.h"
#include "audio_stats.h"
#include "gbrt_debug.h"

#ifdef GB_HAS_SDL2
#include <SDL.h>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>
#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"

namespace fs = std::filesystem;

/* ============================================================================
 * SDL State
 * ========================================================================== */

static SDL_Window* g_window = NULL;
static SDL_Renderer* g_renderer = NULL;
static SDL_Texture* g_texture = NULL;
static int g_scale = 5;
static int g_windowed_width = GB_SCREEN_WIDTH * 5;
static int g_windowed_height = GB_SCREEN_HEIGHT * 5;
static SDL_Rect g_game_viewport = {0, 0, GB_SCREEN_WIDTH * 5, GB_SCREEN_HEIGHT * 5};
static uint32_t g_last_frame_time = 0;
static SDL_AudioDeviceID g_audio_device = 0;
static SDL_GameController* g_controller = NULL;
static bool g_vsync = false;  /* VSync OFF - we pace with wall clock for 59.7 FPS */
static bool g_audio_started = false;
static uint32_t g_audio_start_threshold = 0;

/* Performance timing diagnostics */
static double g_timing_render_total = 0.0;
static double g_timing_vsync_total = 0.0;
static uint32_t g_timing_frame_count = 0;
static GBPlatformTimingInfo g_last_timing = {};

/* Menu State */
static bool g_show_menu = false;
static bool g_show_overlay = false;
static int g_speed_percent = 100;
static int g_palette_idx = 0;
static bool g_smooth_lcd_transitions = true;
static bool g_launcher_return_enabled = false;
static bool g_benchmark_mode = false;
static bool g_fullscreen = false;
static bool g_app_suspended = false;
static bool g_renderer_reset_pending = false;
static GBPlatformExitAction g_exit_action = GB_PLATFORM_EXIT_QUIT;
static const char* g_palette_names[] = { "Original (Green)", "Black & White (Pocket)", "Amber (Plasma)" };
static const char* g_scale_names[] = { "1x (160x144)", "2x (320x288)", "3x (480x432)", "4x (640x576)", "5x (800x720)", "6x (960x864)", "7x (1120x1008)", "8x (1280x1152)" };
typedef enum GBRenderScalingMode {
    GB_RENDER_SCALING_PIXEL_PERFECT = 0,
    GB_RENDER_SCALING_ASPECT_FIT = 1,
    GB_RENDER_SCALING_ASPECT_FILL = 2,
    GB_RENDER_SCALING_STRETCH = 3,
} GBRenderScalingMode;
typedef enum GBRenderFilterMode {
    GB_RENDER_FILTER_NEAREST = 0,
    GB_RENDER_FILTER_LINEAR = 1,
} GBRenderFilterMode;
typedef enum GBControllerLabelProfile {
    GB_CONTROLLER_LABEL_GENERIC = 0,
    GB_CONTROLLER_LABEL_XBOX = 1,
    GB_CONTROLLER_LABEL_PLAYSTATION = 2,
    GB_CONTROLLER_LABEL_NINTENDO = 3,
} GBControllerLabelProfile;
typedef enum GBInputAction {
    GB_INPUT_ACTION_RIGHT = 0,
    GB_INPUT_ACTION_LEFT = 1,
    GB_INPUT_ACTION_UP = 2,
    GB_INPUT_ACTION_DOWN = 3,
    GB_INPUT_ACTION_A = 4,
    GB_INPUT_ACTION_B = 5,
    GB_INPUT_ACTION_SELECT = 6,
    GB_INPUT_ACTION_START = 7,
    GB_INPUT_ACTION_FAST_FORWARD = 8,
    GB_INPUT_ACTION_TOGGLE_MAX_SPEED = 9,
    GB_INPUT_ACTION_SAVE_STATE = 10,
    GB_INPUT_ACTION_LOAD_STATE = 11,
    GB_INPUT_ACTION_PREVIOUS_STATE_SLOT = 12,
    GB_INPUT_ACTION_NEXT_STATE_SLOT = 13,
    GB_INPUT_ACTION_TOGGLE_OVERLAY = 14,
    GB_INPUT_ACTION_TOGGLE_MUTE = 15,
    GB_INPUT_ACTION_TOGGLE_MENU = 16,
    GB_INPUT_ACTION_COUNT = 17,
} GBInputAction;
typedef enum GBInputBindingKind {
    GB_INPUT_BINDING_NONE = 0,
    GB_INPUT_BINDING_KEY = 1,
    GB_INPUT_BINDING_CONTROLLER_BUTTON = 2,
    GB_INPUT_BINDING_CONTROLLER_AXIS_POSITIVE = 3,
    GB_INPUT_BINDING_CONTROLLER_AXIS_NEGATIVE = 4,
} GBInputBindingKind;
typedef enum GBBindingCaptureDevice {
    GB_CAPTURE_DEVICE_NONE = 0,
    GB_CAPTURE_DEVICE_KEYBOARD = 1,
    GB_CAPTURE_DEVICE_CONTROLLER = 2,
} GBBindingCaptureDevice;
typedef struct GBInputBinding {
    GBInputBindingKind kind;
    int16_t code;
} GBInputBinding;
static GBRenderScalingMode g_render_scaling_mode = GB_RENDER_SCALING_PIXEL_PERFECT;
static GBRenderFilterMode g_render_filter_mode = GB_RENDER_FILTER_NEAREST;
static SDL_GameControllerType g_controller_type = SDL_CONTROLLER_TYPE_UNKNOWN;
static GBControllerLabelProfile g_controller_label_profile = GB_CONTROLLER_LABEL_GENERIC;
static std::string g_controller_name;
static bool g_audio_output_enabled = true;
static bool g_audio_muted = false;
static uint32_t g_audio_latency_ms = 80;
static uint32_t g_audio_volume_percent = 100;
static uint32_t g_audio_low_watermark = 0;
static uint32_t g_audio_device_sample_rate = 44100;
static uint32_t g_audio_device_buffer_samples = 0;
static std::vector<std::string> g_audio_output_devices;
static std::string g_audio_target_device_name;
static std::string g_audio_active_device_name;
static constexpr int GB_SAVESTATE_SLOT_COUNT = 10;
static constexpr int GB_JOYPAD_ACTION_COUNT = 8;
static constexpr int GB_FAST_FORWARD_SPEED_PERCENT = 250;
static constexpr int GB_MAX_SHORTCUT_SPEED_PERCENT = 500;
static int g_savestate_slot = 0;
static std::string g_savestate_status;
static const char* g_render_scaling_mode_names[] = {
    "Pixel Perfect",
    "Aspect Fit",
    "Aspect Fill",
    "Stretch",
};
static const char* g_render_filter_names[] = {
    "Nearest",
    "Linear",
};
static const uint32_t g_palettes[][4] = {
    { 0xFFE0F8D0, 0xFF88C070, 0xFF346856, 0xFF081820 }, // Original
    { 0xFFFFFFFF, 0xFFAAAAAA, 0xFF555555, 0xFF000000 }, // B&W
    { 0xFFFFB000, 0xFFCB4F0E, 0xFF800000, 0xFF330000 }  // Amber
};
static uint32_t g_lcd_off_framebuffer[GB_FRAMEBUFFER_SIZE];
static uint32_t g_last_guest_framebuffer[GB_FRAMEBUFFER_SIZE];
static bool g_lcd_off_framebuffer_initialized = false;
static bool g_last_guest_framebuffer_valid = false;
static uint64_t g_present_count = 0;
static GBContext* g_registered_ctx = NULL;
static GBInputBinding g_keyboard_bindings[GB_INPUT_ACTION_COUNT][2] = {};
static GBInputBinding g_controller_bindings[GB_INPUT_ACTION_COUNT][2] = {};
static bool g_keyboard_binding_pressed[GB_INPUT_ACTION_COUNT][2] = {};
static bool g_controller_button_binding_pressed[GB_INPUT_ACTION_COUNT][2] = {};
static bool g_controller_axis_binding_pressed[GB_INPUT_ACTION_COUNT][2] = {};
static bool g_runtime_action_pressed[GB_INPUT_ACTION_COUNT] = {};
static Sint16 g_controller_axis_values[SDL_CONTROLLER_AXIS_MAX] = {};
static bool g_binding_capture_active = false;
static GBBindingCaptureDevice g_binding_capture_device = GB_CAPTURE_DEVICE_NONE;
static GBInputAction g_binding_capture_action = GB_INPUT_ACTION_RIGHT;
static int g_binding_capture_slot = 0;
static bool g_fast_forward_active = false;
static bool g_max_speed_mode = false;
static const char* g_input_action_names[GB_INPUT_ACTION_COUNT] = {
    "Right",
    "Left",
    "Up",
    "Down",
    "Game Boy A",
    "Game Boy B",
    "Select",
    "Start",
    "Fast Forward (Hold)",
    "Toggle Max Speed",
    "Save State",
    "Load State",
    "Previous State Slot",
    "Next State Slot",
    "Toggle Overlay",
    "Toggle Mute",
    "Toggle Menu",
};

static bool has_interpreter_activity(const GBContext* ctx) {
    return ctx != NULL &&
           (ctx->total_dispatch_fallbacks > 0 ||
            ctx->total_interpreter_entries > 0 ||
            ctx->has_unimplemented_interpreter_opcode);
}

static void update_audio_stats_from_ring(void);
static uint32_t current_audio_underruns(void);
static uint32_t current_audio_ring_fill_samples(void);
static uint32_t current_audio_ring_capacity(void);
static uint32_t audio_ring_fill_samples(void);
static void sdl_audio_callback(void* userdata, Uint8* stream, int len);
static void refresh_audio_output_devices(void);
static const char* current_audio_output_device_label(void);
static bool current_audio_output_device_available(void);
static void close_audio_output_device(void);
static bool reopen_audio_output_device(bool preserve_stats);
static void update_controller_axis_binding_state(void);
static void recompute_audio_targets(void);
static void refresh_audio_device_pause_state(void);
static void reset_audio_output_buffer(bool preserve_stats);
static char* trim_ascii(char* text);
static void update_effective_joypad_state(void);
static void save_runtime_preferences(void);
static bool save_savestate_slot(GBContext* ctx, int slot);
static bool load_savestate_slot(GBContext* ctx, int slot);
static bool delete_savestate_slot(GBContext* ctx, int slot);
static bool savestate_slot_exists(const GBContext* ctx, int slot, std::string* out_path);
static float settings_ui_scale_for_size(const ImVec2& display_size);
static const char* controller_menu_hint_text(void);
static bool input_action_is_runtime(GBInputAction action);
static int effective_speed_percent(void);
static void update_runtime_action_state(GBContext* ctx);

/* Joypad state - exported for gbrt.c to access */
uint8_t g_joypad_buttons = 0xFF;  /* Active low: Start, Select, B, A */
uint8_t g_joypad_dpad = 0xFF;     /* Active low: Down, Up, Left, Right */
static uint8_t g_manual_joypad_buttons = 0xFF;
static uint8_t g_manual_joypad_dpad = 0xFF;
static uint8_t g_script_joypad_buttons = 0xFF;
static uint8_t g_script_joypad_dpad = 0xFF;

/* ============================================================================
 * Automation State
 * ========================================================================== */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define MAX_SCRIPT_ENTRIES 100
typedef enum {
    SCRIPT_ANCHOR_FRAME = 0,
    SCRIPT_ANCHOR_CYCLE = 1,
} ScriptAnchor;

typedef struct {
    ScriptAnchor anchor;
    uint64_t start;
    uint64_t duration;
    uint8_t dpad;    /* Active LOW mask to apply (0 = Pressed) */
    uint8_t buttons; /* Active LOW mask to apply (0 = Pressed) */
} ScriptEntry;

static ScriptEntry g_input_script[MAX_SCRIPT_ENTRIES];
static int g_script_count = 0;
static FILE* g_input_record_file = NULL;
static bool g_input_record_exit_handler_registered = false;
static bool g_input_record_wrote_entry = false;
static bool g_input_record_has_segment = false;
static uint64_t g_input_record_start_cycle = 0;
static uint64_t g_input_record_end_cycle = 0;
static uint8_t g_input_record_dpad = 0xFF;
static uint8_t g_input_record_buttons = 0xFF;

#define MAX_DUMP_FRAMES 100
static uint32_t g_dump_frames[MAX_DUMP_FRAMES];
static int g_dump_count = 0;
static uint32_t g_dump_present_frames[MAX_DUMP_FRAMES];
static int g_dump_present_count = 0;
static char g_screenshot_prefix[64] = "screenshot";

static bool env_flag_enabled(const char* name) {
    const char* value = SDL_getenv(name);
    if (!value || !value[0]) {
        return false;
    }
    return strcmp(value, "1") == 0 ||
           strcmp(value, "true") == 0 ||
           strcmp(value, "TRUE") == 0 ||
           strcmp(value, "yes") == 0 ||
           strcmp(value, "YES") == 0 ||
           strcmp(value, "on") == 0 ||
           strcmp(value, "ON") == 0;
}

static bool platform_default_fullscreen(void) {
#if defined(__ANDROID__)
    return true;
#else
    return false;
#endif
}

static bool platform_uses_app_storage_for_relative_paths(void) {
#if defined(__ANDROID__)
    return true;
#else
    return false;
#endif
}

static const char* overlay_menu_hint_text(void) {
#if defined(__ANDROID__)
    return "Press Back, L3, or R3 for Menu";
#else
    return "Press ESC for Menu";
#endif
}

static float settings_ui_scale_for_size(const ImVec2& display_size) {
    float min_dimension = display_size.x < display_size.y ? display_size.x : display_size.y;
    if (min_dimension <= 0.0f) {
        return 1.0f;
    }

#if defined(__ANDROID__)
    float scale = min_dimension / 520.0f;
    if (scale < 1.0f) scale = 1.0f;
    if (scale > 1.65f) scale = 1.65f;
#else
    float scale = min_dimension / 720.0f;
    if (scale < 0.95f) scale = 0.95f;
    if (scale > 1.25f) scale = 1.25f;
#endif

    return scale;
}

static const char* controller_menu_hint_text(void) {
#if defined(__ANDROID__)
    return "Guide, L3, or R3: Settings Menu";
#else
    return "Guide / Home: Settings Menu";
#endif
}

static bool input_action_is_runtime(GBInputAction action) {
    return action >= GB_JOYPAD_ACTION_COUNT && action < GB_INPUT_ACTION_COUNT;
}

static int effective_speed_percent(void) {
    int speed_percent = (g_speed_percent > 0) ? g_speed_percent : 100;
    if (g_max_speed_mode) {
        return GB_MAX_SHORTCUT_SPEED_PERCENT;
    }
    if (g_fast_forward_active && speed_percent < GB_FAST_FORWARD_SPEED_PERCENT) {
        return GB_FAST_FORWARD_SPEED_PERCENT;
    }
    return speed_percent;
}

static const char* overlay_visibility_hint_text(void) {
#if defined(__ANDROID__)
    return "Use the settings menu or a shortcut binding to toggle the overlay";
#else
    return "Use F1 or a shortcut binding for Overlay";
#endif
}

static bool string_contains_case_insensitive(const char* haystack, const char* needle) {
    if (!haystack || !needle || !haystack[0] || !needle[0]) {
        return false;
    }

    std::string text(haystack);
    std::string pattern(needle);
    for (char& ch : text) ch = (char)std::tolower((unsigned char)ch);
    for (char& ch : pattern) ch = (char)std::tolower((unsigned char)ch);
    return text.find(pattern) != std::string::npos;
}

static bool should_ignore_controller_name(const char* name) {
    return string_contains_case_insensitive(name, "qwerty") ||
           string_contains_case_insensitive(name, "keyboard") ||
           string_contains_case_insensitive(name, "keypad");
}

static const char* controller_type_name(SDL_GameControllerType type) {
    switch (type) {
        case SDL_CONTROLLER_TYPE_XBOX360: return "Xbox 360";
        case SDL_CONTROLLER_TYPE_XBOXONE: return "Xbox One";
        case SDL_CONTROLLER_TYPE_PS3: return "PlayStation 3";
        case SDL_CONTROLLER_TYPE_PS4: return "PlayStation 4";
        case SDL_CONTROLLER_TYPE_PS5: return "PlayStation 5";
        case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO: return "Nintendo Switch Pro";
        case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_LEFT: return "Joy-Con Left";
        case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT: return "Joy-Con Right";
        case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: return "Joy-Con Pair";
        case SDL_CONTROLLER_TYPE_GOOGLE_STADIA: return "Stadia";
        case SDL_CONTROLLER_TYPE_AMAZON_LUNA: return "Luna";
        case SDL_CONTROLLER_TYPE_NVIDIA_SHIELD: return "NVIDIA Shield";
        case SDL_CONTROLLER_TYPE_VIRTUAL: return "Virtual";
        case SDL_CONTROLLER_TYPE_UNKNOWN:
        default:
            return "Unknown";
    }
}

static GBControllerLabelProfile detect_controller_label_profile(SDL_GameController* controller) {
    if (controller) {
        const SDL_GameControllerType type = SDL_GameControllerGetType(controller);
        switch (type) {
            case SDL_CONTROLLER_TYPE_XBOX360:
            case SDL_CONTROLLER_TYPE_XBOXONE:
            case SDL_CONTROLLER_TYPE_GOOGLE_STADIA:
            case SDL_CONTROLLER_TYPE_AMAZON_LUNA:
            case SDL_CONTROLLER_TYPE_NVIDIA_SHIELD:
                return GB_CONTROLLER_LABEL_XBOX;

            case SDL_CONTROLLER_TYPE_PS3:
            case SDL_CONTROLLER_TYPE_PS4:
            case SDL_CONTROLLER_TYPE_PS5:
                return GB_CONTROLLER_LABEL_PLAYSTATION;

            case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO:
            case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
            case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
            case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
                return GB_CONTROLLER_LABEL_NINTENDO;

            default:
                break;
        }

        const char* name = SDL_GameControllerName(controller);
        if (string_contains_case_insensitive(name, "xbox") ||
            string_contains_case_insensitive(name, "xinput") ||
            string_contains_case_insensitive(name, "stadia") ||
            string_contains_case_insensitive(name, "luna") ||
            string_contains_case_insensitive(name, "shield") ||
            string_contains_case_insensitive(name, "kishi") ||
            string_contains_case_insensitive(name, "odin") ||
            string_contains_case_insensitive(name, "retroid")) {
            return GB_CONTROLLER_LABEL_XBOX;
        }
        if (string_contains_case_insensitive(name, "playstation") ||
            string_contains_case_insensitive(name, "dualshock") ||
            string_contains_case_insensitive(name, "dualsense") ||
            string_contains_case_insensitive(name, "ps3") ||
            string_contains_case_insensitive(name, "ps4") ||
            string_contains_case_insensitive(name, "ps5")) {
            return GB_CONTROLLER_LABEL_PLAYSTATION;
        }
        if (string_contains_case_insensitive(name, "switch") ||
            string_contains_case_insensitive(name, "joy-con") ||
            string_contains_case_insensitive(name, "joycon") ||
            string_contains_case_insensitive(name, "nintendo")) {
            return GB_CONTROLLER_LABEL_NINTENDO;
        }
    }

#if defined(__ANDROID__)
    return GB_CONTROLLER_LABEL_XBOX;
#else
    return GB_CONTROLLER_LABEL_GENERIC;
#endif
}

static const char* controller_face_south_label(void) {
    switch (g_controller_label_profile) {
        case GB_CONTROLLER_LABEL_XBOX: return "A (Bottom)";
        case GB_CONTROLLER_LABEL_PLAYSTATION: return "Cross (Bottom)";
        case GB_CONTROLLER_LABEL_NINTENDO: return "B (Bottom)";
        case GB_CONTROLLER_LABEL_GENERIC:
        default:
            return "South / Bottom";
    }
}

static const char* controller_face_east_label(void) {
    switch (g_controller_label_profile) {
        case GB_CONTROLLER_LABEL_XBOX: return "B (Right)";
        case GB_CONTROLLER_LABEL_PLAYSTATION: return "Circle (Right)";
        case GB_CONTROLLER_LABEL_NINTENDO: return "A (Right)";
        case GB_CONTROLLER_LABEL_GENERIC:
        default:
            return "East / Right";
    }
}

static const char* controller_left_shoulder_label(void) {
    switch (g_controller_label_profile) {
        case GB_CONTROLLER_LABEL_XBOX: return "LB";
        case GB_CONTROLLER_LABEL_PLAYSTATION: return "L1";
        case GB_CONTROLLER_LABEL_NINTENDO: return "L";
        case GB_CONTROLLER_LABEL_GENERIC:
        default:
            return "Left Shoulder";
    }
}

static const char* controller_right_shoulder_label(void) {
    switch (g_controller_label_profile) {
        case GB_CONTROLLER_LABEL_XBOX: return "RB";
        case GB_CONTROLLER_LABEL_PLAYSTATION: return "R1";
        case GB_CONTROLLER_LABEL_NINTENDO: return "R";
        case GB_CONTROLLER_LABEL_GENERIC:
        default:
            return "Right Shoulder";
    }
}

static const char* controller_back_label(void) {
    switch (g_controller_label_profile) {
        case GB_CONTROLLER_LABEL_XBOX: return "View / Back";
        case GB_CONTROLLER_LABEL_PLAYSTATION: return "Create / Share";
        case GB_CONTROLLER_LABEL_NINTENDO: return "-";
        case GB_CONTROLLER_LABEL_GENERIC:
        default:
            return "Back / Select";
    }
}

static const char* controller_start_label(void) {
    switch (g_controller_label_profile) {
        case GB_CONTROLLER_LABEL_XBOX: return "Menu / Start";
        case GB_CONTROLLER_LABEL_PLAYSTATION: return "Options";
        case GB_CONTROLLER_LABEL_NINTENDO: return "+";
        case GB_CONTROLLER_LABEL_GENERIC:
        default:
            return "Start";
    }
}

static const char* controller_guide_label(void) {
    switch (g_controller_label_profile) {
        case GB_CONTROLLER_LABEL_XBOX: return "Xbox / Guide";
        case GB_CONTROLLER_LABEL_PLAYSTATION: return "PS";
        case GB_CONTROLLER_LABEL_NINTENDO: return "Home";
        case GB_CONTROLLER_LABEL_GENERIC:
        default:
            return "Guide / Home";
    }
}

static const char* controller_button_label(SDL_GameControllerButton button) {
    switch (button) {
        case SDL_CONTROLLER_BUTTON_A: return controller_face_south_label();
        case SDL_CONTROLLER_BUTTON_B: return controller_face_east_label();
        case SDL_CONTROLLER_BUTTON_X: return "West Face";
        case SDL_CONTROLLER_BUTTON_Y: return "North Face";
        case SDL_CONTROLLER_BUTTON_BACK: return controller_back_label();
        case SDL_CONTROLLER_BUTTON_GUIDE: return controller_guide_label();
        case SDL_CONTROLLER_BUTTON_START: return controller_start_label();
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return controller_left_shoulder_label();
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return controller_right_shoulder_label();
        case SDL_CONTROLLER_BUTTON_DPAD_UP: return "D-Pad Up";
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return "D-Pad Down";
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return "D-Pad Left";
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return "D-Pad Right";
        case SDL_CONTROLLER_BUTTON_LEFTSTICK: return "Left Stick Click";
        case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return "Right Stick Click";
        default:
            return "Controller Button";
    }
}

static const char* controller_axis_label(SDL_GameControllerAxis axis, bool positive_direction) {
    switch (axis) {
        case SDL_CONTROLLER_AXIS_LEFTX: return positive_direction ? "Left Stick Right" : "Left Stick Left";
        case SDL_CONTROLLER_AXIS_LEFTY: return positive_direction ? "Left Stick Down" : "Left Stick Up";
        case SDL_CONTROLLER_AXIS_RIGHTX: return positive_direction ? "Right Stick Right" : "Right Stick Left";
        case SDL_CONTROLLER_AXIS_RIGHTY: return positive_direction ? "Right Stick Down" : "Right Stick Up";
        case SDL_CONTROLLER_AXIS_TRIGGERLEFT: return "Left Trigger";
        case SDL_CONTROLLER_AXIS_TRIGGERRIGHT: return "Right Trigger";
        default:
            return positive_direction ? "Axis +" : "Axis -";
    }
}

static std::string binding_display_label(const GBInputBinding& binding) {
    switch (binding.kind) {
        case GB_INPUT_BINDING_KEY: {
            const char* name = SDL_GetScancodeName((SDL_Scancode)binding.code);
            return (name && name[0]) ? std::string(name) : ("Scancode " + std::to_string((int)binding.code));
        }

        case GB_INPUT_BINDING_CONTROLLER_BUTTON:
            return controller_button_label((SDL_GameControllerButton)binding.code);

        case GB_INPUT_BINDING_CONTROLLER_AXIS_POSITIVE:
            return controller_axis_label((SDL_GameControllerAxis)binding.code, true);

        case GB_INPUT_BINDING_CONTROLLER_AXIS_NEGATIVE:
            return controller_axis_label((SDL_GameControllerAxis)binding.code, false);

        case GB_INPUT_BINDING_NONE:
        default:
            return "Unbound";
    }
}

static void start_binding_capture(GBBindingCaptureDevice device, GBInputAction action, int slot) {
    g_binding_capture_active = true;
    g_binding_capture_device = device;
    g_binding_capture_action = action;
    g_binding_capture_slot = slot;
}

static void cancel_binding_capture(void) {
    g_binding_capture_active = false;
    g_binding_capture_device = GB_CAPTURE_DEVICE_NONE;
    g_binding_capture_slot = 0;
}

static void assign_binding_slot(GBBindingCaptureDevice device,
                                GBInputAction action,
                                int slot,
                                const GBInputBinding& binding) {
    if (slot < 0 || slot >= 2) {
        return;
    }

    if (device == GB_CAPTURE_DEVICE_KEYBOARD) {
        g_keyboard_bindings[action][slot] = binding;
        g_keyboard_binding_pressed[action][slot] = false;
    } else if (device == GB_CAPTURE_DEVICE_CONTROLLER) {
        g_controller_bindings[action][slot] = binding;
        g_controller_button_binding_pressed[action][slot] = false;
        g_controller_axis_binding_pressed[action][slot] = false;
        update_controller_axis_binding_state();
    }

    update_effective_joypad_state();
    save_runtime_preferences();
}

static void commit_binding_capture(const GBInputBinding& binding) {
    if (!g_binding_capture_active || g_binding_capture_slot < 0 || g_binding_capture_slot >= 2) {
        cancel_binding_capture();
        return;
    }

    assign_binding_slot(g_binding_capture_device, g_binding_capture_action, g_binding_capture_slot, binding);
    cancel_binding_capture();
}

static void clear_controller_state(void) {
    if (g_controller) {
        SDL_GameControllerClose(g_controller);
        g_controller = NULL;
    }
    memset(g_controller_button_binding_pressed, 0, sizeof(g_controller_button_binding_pressed));
    memset(g_controller_axis_binding_pressed, 0, sizeof(g_controller_axis_binding_pressed));
    memset(g_controller_axis_values, 0, sizeof(g_controller_axis_values));
    g_controller_type = SDL_CONTROLLER_TYPE_UNKNOWN;
    g_controller_label_profile = detect_controller_label_profile(NULL);
    g_controller_name.clear();
    update_effective_joypad_state();
}

static void refresh_controller_profile(void) {
    if (!g_controller) {
        g_controller_type = SDL_CONTROLLER_TYPE_UNKNOWN;
        g_controller_label_profile = detect_controller_label_profile(NULL);
        g_controller_name.clear();
        return;
    }

    g_controller_type = SDL_GameControllerGetType(g_controller);
    g_controller_label_profile = detect_controller_label_profile(g_controller);
    const char* name = SDL_GameControllerName(g_controller);
    g_controller_name = (name && name[0]) ? name : "Controller";
    fprintf(stderr,
            "[SDL] Controller: %s [%s]\n",
            g_controller_name.c_str(),
            controller_type_name(g_controller_type));
}

static bool open_controller_index(int joystick_index) {
    if (joystick_index < 0 || joystick_index >= SDL_NumJoysticks() || !SDL_IsGameController(joystick_index)) {
        return false;
    }

    SDL_GameController* controller = SDL_GameControllerOpen(joystick_index);
    if (!controller) {
        return false;
    }

    if (should_ignore_controller_name(SDL_GameControllerName(controller))) {
        SDL_GameControllerClose(controller);
        return false;
    }

    clear_controller_state();
    g_controller = controller;
    refresh_controller_profile();
    return true;
}

static bool open_first_available_controller(void) {
    if (g_controller) {
        return true;
    }

    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (open_controller_index(i)) {
            return true;
        }
    }
    return false;
}

static SDL_JoystickID active_controller_instance_id(void) {
    if (!g_controller) {
        return -1;
    }
    SDL_Joystick* joystick = SDL_GameControllerGetJoystick(g_controller);
    return joystick ? SDL_JoystickInstanceID(joystick) : -1;
}

static bool ensure_parent_directory(const fs::path& path) {
    if (path.parent_path().empty()) {
        return true;
    }
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    return !ec;
}

static std::string extract_path_leaf(const char* path) {
    if (!path || !path[0]) {
        return "game";
    }
    try {
        fs::path input(path);
        std::string leaf = input.filename().string();
        return leaf.empty() ? std::string(path) : leaf;
    } catch (...) {
        return std::string(path);
    }
}

static std::string make_pref_storage_dir(const char* app_component) {
    const char* safe_component = (app_component && app_component[0]) ? app_component : "runtime";
    char* pref_path = SDL_GetPrefPath("gbrecompiled", safe_component);
    if (!pref_path) {
        return std::string();
    }
    std::string result(pref_path);
    SDL_free(pref_path);
    return result;
}

static std::string resolve_writable_path(const char* requested_path, const char* app_component) {
    if (!requested_path || !requested_path[0]) {
        return std::string();
    }

    fs::path requested(requested_path);
    if (!platform_uses_app_storage_for_relative_paths() || requested.is_absolute()) {
        return requested.lexically_normal().string();
    }

    const std::string pref_dir = make_pref_storage_dir(app_component);
    if (pref_dir.empty()) {
        return requested.lexically_normal().string();
    }

    fs::path resolved = fs::path(pref_dir) / requested;
    ensure_parent_directory(resolved);
    return resolved.lexically_normal().string();
}

static GBInputBinding make_binding(GBInputBindingKind kind, int code) {
    GBInputBinding binding = {};
    binding.kind = kind;
    binding.code = (int16_t)code;
    return binding;
}

static const char* input_action_config_name(GBInputAction action) {
    switch (action) {
        case GB_INPUT_ACTION_RIGHT: return "right";
        case GB_INPUT_ACTION_LEFT: return "left";
        case GB_INPUT_ACTION_UP: return "up";
        case GB_INPUT_ACTION_DOWN: return "down";
        case GB_INPUT_ACTION_A: return "a";
        case GB_INPUT_ACTION_B: return "b";
        case GB_INPUT_ACTION_SELECT: return "select";
        case GB_INPUT_ACTION_START: return "start";
        case GB_INPUT_ACTION_FAST_FORWARD: return "fast_forward";
        case GB_INPUT_ACTION_TOGGLE_MAX_SPEED: return "toggle_max_speed";
        case GB_INPUT_ACTION_SAVE_STATE: return "save_state";
        case GB_INPUT_ACTION_LOAD_STATE: return "load_state";
        case GB_INPUT_ACTION_PREVIOUS_STATE_SLOT: return "previous_state_slot";
        case GB_INPUT_ACTION_NEXT_STATE_SLOT: return "next_state_slot";
        case GB_INPUT_ACTION_TOGGLE_OVERLAY: return "toggle_overlay";
        case GB_INPUT_ACTION_TOGGLE_MUTE: return "toggle_mute";
        case GB_INPUT_ACTION_TOGGLE_MENU: return "toggle_menu";
        case GB_INPUT_ACTION_COUNT:
        default:
            return "unknown";
    }
}

static bool parse_input_action_name(const char* text, GBInputAction* out_action) {
    if (!text || !out_action) {
        return false;
    }

    for (int action = 0; action < GB_INPUT_ACTION_COUNT; action++) {
        if (strcmp(text, input_action_config_name((GBInputAction)action)) == 0) {
            *out_action = (GBInputAction)action;
            return true;
        }
    }

    return false;
}

static void clear_all_binding_pressed_state(void) {
    memset(g_keyboard_binding_pressed, 0, sizeof(g_keyboard_binding_pressed));
    memset(g_controller_button_binding_pressed, 0, sizeof(g_controller_button_binding_pressed));
    memset(g_controller_axis_binding_pressed, 0, sizeof(g_controller_axis_binding_pressed));
    memset(g_runtime_action_pressed, 0, sizeof(g_runtime_action_pressed));
    memset(g_controller_axis_values, 0, sizeof(g_controller_axis_values));
    g_fast_forward_active = false;
}

static bool binding_is_valid(const GBInputBinding& binding) {
    switch (binding.kind) {
        case GB_INPUT_BINDING_KEY:
            return binding.code > SDL_SCANCODE_UNKNOWN && binding.code < SDL_NUM_SCANCODES;

        case GB_INPUT_BINDING_CONTROLLER_BUTTON:
            return binding.code >= 0 && binding.code < SDL_CONTROLLER_BUTTON_MAX;

        case GB_INPUT_BINDING_CONTROLLER_AXIS_POSITIVE:
        case GB_INPUT_BINDING_CONTROLLER_AXIS_NEGATIVE:
            return binding.code >= 0 && binding.code < SDL_CONTROLLER_AXIS_MAX;

        case GB_INPUT_BINDING_NONE:
        default:
            return false;
    }
}

static bool binding_matches_scancode(const GBInputBinding& binding, SDL_Scancode scancode) {
    return binding.kind == GB_INPUT_BINDING_KEY && binding.code == (int)scancode;
}

static bool binding_matches_controller_button(const GBInputBinding& binding, uint8_t button) {
    return binding.kind == GB_INPUT_BINDING_CONTROLLER_BUTTON && binding.code == (int)button;
}

static bool binding_active_for_axis_value(const GBInputBinding& binding, Sint16 value) {
    const Sint16 threshold = 16000;
    if (binding.kind == GB_INPUT_BINDING_CONTROLLER_AXIS_POSITIVE) {
        return value >= threshold;
    }
    if (binding.kind == GB_INPUT_BINDING_CONTROLLER_AXIS_NEGATIVE) {
        return value <= -threshold;
    }
    return false;
}

static void set_default_audio_preferences(void) {
    g_audio_output_enabled = true;
    g_audio_muted = false;
    g_audio_latency_ms = 80;
    g_audio_volume_percent = 100;
    g_audio_target_device_name.clear();
}

static void set_default_input_bindings(void) {
    memset(g_keyboard_bindings, 0, sizeof(g_keyboard_bindings));
    memset(g_controller_bindings, 0, sizeof(g_controller_bindings));

    g_keyboard_bindings[GB_INPUT_ACTION_UP][0] = make_binding(GB_INPUT_BINDING_KEY, SDL_SCANCODE_UP);
    g_keyboard_bindings[GB_INPUT_ACTION_UP][1] = make_binding(GB_INPUT_BINDING_KEY, SDL_SCANCODE_W);
    g_keyboard_bindings[GB_INPUT_ACTION_DOWN][0] = make_binding(GB_INPUT_BINDING_KEY, SDL_SCANCODE_DOWN);
    g_keyboard_bindings[GB_INPUT_ACTION_DOWN][1] = make_binding(GB_INPUT_BINDING_KEY, SDL_SCANCODE_S);
    g_keyboard_bindings[GB_INPUT_ACTION_LEFT][0] = make_binding(GB_INPUT_BINDING_KEY, SDL_SCANCODE_LEFT);
    g_keyboard_bindings[GB_INPUT_ACTION_LEFT][1] = make_binding(GB_INPUT_BINDING_KEY, SDL_SCANCODE_A);
    g_keyboard_bindings[GB_INPUT_ACTION_RIGHT][0] = make_binding(GB_INPUT_BINDING_KEY, SDL_SCANCODE_RIGHT);
    g_keyboard_bindings[GB_INPUT_ACTION_RIGHT][1] = make_binding(GB_INPUT_BINDING_KEY, SDL_SCANCODE_D);
    g_keyboard_bindings[GB_INPUT_ACTION_A][0] = make_binding(GB_INPUT_BINDING_KEY, SDL_SCANCODE_Z);
    g_keyboard_bindings[GB_INPUT_ACTION_A][1] = make_binding(GB_INPUT_BINDING_KEY, SDL_SCANCODE_J);
    g_keyboard_bindings[GB_INPUT_ACTION_B][0] = make_binding(GB_INPUT_BINDING_KEY, SDL_SCANCODE_X);
    g_keyboard_bindings[GB_INPUT_ACTION_B][1] = make_binding(GB_INPUT_BINDING_KEY, SDL_SCANCODE_K);
    g_keyboard_bindings[GB_INPUT_ACTION_SELECT][0] = make_binding(GB_INPUT_BINDING_KEY, SDL_SCANCODE_BACKSPACE);
    g_keyboard_bindings[GB_INPUT_ACTION_SELECT][1] = make_binding(GB_INPUT_BINDING_KEY, SDL_SCANCODE_RSHIFT);
    g_keyboard_bindings[GB_INPUT_ACTION_START][0] = make_binding(GB_INPUT_BINDING_KEY, SDL_SCANCODE_RETURN);
    g_keyboard_bindings[GB_INPUT_ACTION_FAST_FORWARD][0] = make_binding(GB_INPUT_BINDING_KEY, SDL_SCANCODE_TAB);
    g_keyboard_bindings[GB_INPUT_ACTION_TOGGLE_MAX_SPEED][0] = make_binding(GB_INPUT_BINDING_KEY, SDL_SCANCODE_GRAVE);
    g_keyboard_bindings[GB_INPUT_ACTION_SAVE_STATE][0] = make_binding(GB_INPUT_BINDING_KEY, SDL_SCANCODE_F5);
    g_keyboard_bindings[GB_INPUT_ACTION_LOAD_STATE][0] = make_binding(GB_INPUT_BINDING_KEY, SDL_SCANCODE_F8);
    g_keyboard_bindings[GB_INPUT_ACTION_PREVIOUS_STATE_SLOT][0] = make_binding(GB_INPUT_BINDING_KEY, SDL_SCANCODE_F6);
    g_keyboard_bindings[GB_INPUT_ACTION_NEXT_STATE_SLOT][0] = make_binding(GB_INPUT_BINDING_KEY, SDL_SCANCODE_F7);
    g_keyboard_bindings[GB_INPUT_ACTION_TOGGLE_OVERLAY][0] = make_binding(GB_INPUT_BINDING_KEY, SDL_SCANCODE_F1);
    g_keyboard_bindings[GB_INPUT_ACTION_TOGGLE_MUTE][0] = make_binding(GB_INPUT_BINDING_KEY, SDL_SCANCODE_M);
    g_keyboard_bindings[GB_INPUT_ACTION_TOGGLE_MENU][0] = make_binding(GB_INPUT_BINDING_KEY, SDL_SCANCODE_F10);

    g_controller_bindings[GB_INPUT_ACTION_UP][0] = make_binding(GB_INPUT_BINDING_CONTROLLER_BUTTON, SDL_CONTROLLER_BUTTON_DPAD_UP);
    g_controller_bindings[GB_INPUT_ACTION_UP][1] = make_binding(GB_INPUT_BINDING_CONTROLLER_AXIS_NEGATIVE, SDL_CONTROLLER_AXIS_LEFTY);
    g_controller_bindings[GB_INPUT_ACTION_DOWN][0] = make_binding(GB_INPUT_BINDING_CONTROLLER_BUTTON, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    g_controller_bindings[GB_INPUT_ACTION_DOWN][1] = make_binding(GB_INPUT_BINDING_CONTROLLER_AXIS_POSITIVE, SDL_CONTROLLER_AXIS_LEFTY);
    g_controller_bindings[GB_INPUT_ACTION_LEFT][0] = make_binding(GB_INPUT_BINDING_CONTROLLER_BUTTON, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    g_controller_bindings[GB_INPUT_ACTION_LEFT][1] = make_binding(GB_INPUT_BINDING_CONTROLLER_AXIS_NEGATIVE, SDL_CONTROLLER_AXIS_LEFTX);
    g_controller_bindings[GB_INPUT_ACTION_RIGHT][0] = make_binding(GB_INPUT_BINDING_CONTROLLER_BUTTON, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    g_controller_bindings[GB_INPUT_ACTION_RIGHT][1] = make_binding(GB_INPUT_BINDING_CONTROLLER_AXIS_POSITIVE, SDL_CONTROLLER_AXIS_LEFTX);
    g_controller_bindings[GB_INPUT_ACTION_A][0] = make_binding(GB_INPUT_BINDING_CONTROLLER_BUTTON, SDL_CONTROLLER_BUTTON_B);
    g_controller_bindings[GB_INPUT_ACTION_A][1] = make_binding(GB_INPUT_BINDING_CONTROLLER_BUTTON, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    g_controller_bindings[GB_INPUT_ACTION_B][0] = make_binding(GB_INPUT_BINDING_CONTROLLER_BUTTON, SDL_CONTROLLER_BUTTON_A);
    g_controller_bindings[GB_INPUT_ACTION_B][1] = make_binding(GB_INPUT_BINDING_CONTROLLER_BUTTON, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    g_controller_bindings[GB_INPUT_ACTION_SELECT][0] = make_binding(GB_INPUT_BINDING_CONTROLLER_BUTTON, SDL_CONTROLLER_BUTTON_BACK);
    g_controller_bindings[GB_INPUT_ACTION_START][0] = make_binding(GB_INPUT_BINDING_CONTROLLER_BUTTON, SDL_CONTROLLER_BUTTON_START);
    g_controller_bindings[GB_INPUT_ACTION_FAST_FORWARD][0] = make_binding(GB_INPUT_BINDING_CONTROLLER_AXIS_POSITIVE, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
    g_controller_bindings[GB_INPUT_ACTION_TOGGLE_MAX_SPEED][0] = make_binding(GB_INPUT_BINDING_CONTROLLER_AXIS_POSITIVE, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
    g_controller_bindings[GB_INPUT_ACTION_SAVE_STATE][0] = make_binding(GB_INPUT_BINDING_CONTROLLER_BUTTON, SDL_CONTROLLER_BUTTON_X);
    g_controller_bindings[GB_INPUT_ACTION_LOAD_STATE][0] = make_binding(GB_INPUT_BINDING_CONTROLLER_BUTTON, SDL_CONTROLLER_BUTTON_Y);
    g_controller_bindings[GB_INPUT_ACTION_TOGGLE_MENU][0] = make_binding(GB_INPUT_BINDING_CONTROLLER_BUTTON, SDL_CONTROLLER_BUTTON_LEFTSTICK);
    g_controller_bindings[GB_INPUT_ACTION_TOGGLE_MENU][1] = make_binding(GB_INPUT_BINDING_CONTROLLER_BUTTON, SDL_CONTROLLER_BUTTON_RIGHTSTICK);

    clear_all_binding_pressed_state();
}

static std::string runtime_preferences_path(void) {
    const std::string pref_dir = make_pref_storage_dir("runtime");
    if (!pref_dir.empty()) {
        fs::path resolved = fs::path(pref_dir) / "runtime_prefs.ini";
        ensure_parent_directory(resolved);
        return resolved.lexically_normal().string();
    }
    return fs::path("runtime_prefs.ini").lexically_normal().string();
}

static void load_runtime_preferences(void) {
    set_default_audio_preferences();
    set_default_input_bindings();
    g_savestate_slot = 0;
    g_savestate_status.clear();
    g_max_speed_mode = false;

    const std::string path = runtime_preferences_path();
    if (!path.empty()) {
        FILE* file = fopen(path.c_str(), "r");
        if (file) {
            char line[256];
            while (fgets(line, sizeof(line), file)) {
                char* text = trim_ascii(line);
                if (!text || !text[0] || text[0] == '#') {
                    continue;
                }

                char* equals = strchr(text, '=');
                if (!equals) {
                    continue;
                }

                *equals = '\0';
                char* key = trim_ascii(text);
                char* value = trim_ascii(equals + 1);
                if (!key || !value || !key[0]) {
                    continue;
                }

                if (strcmp(key, "audio.enabled") == 0) {
                    g_audio_output_enabled = (strcmp(value, "0") != 0);
                    continue;
                }
                if (strcmp(key, "audio.muted") == 0) {
                    g_audio_muted = (strcmp(value, "0") != 0);
                    continue;
                }
                if (strcmp(key, "audio.latency_ms") == 0) {
                    long parsed = strtol(value, NULL, 10);
                    if (parsed > 0) {
                        g_audio_latency_ms = (uint32_t)parsed;
                    }
                    continue;
                }
                if (strcmp(key, "audio.volume_percent") == 0) {
                    long parsed = strtol(value, NULL, 10);
                    if (parsed >= 0) {
                        if (parsed > 200) parsed = 200;
                        g_audio_volume_percent = (uint32_t)parsed;
                    }
                    continue;
                }
                if (strcmp(key, "audio.device_name") == 0) {
                    g_audio_target_device_name = value;
                    continue;
                }
                if (strcmp(key, "savestate.slot") == 0) {
                    long parsed = strtol(value, NULL, 10);
                    if (parsed >= 0 && parsed < GB_SAVESTATE_SLOT_COUNT) {
                        g_savestate_slot = (int)parsed;
                    }
                    continue;
                }

                bool is_keyboard = strncmp(key, "keyboard.", 9) == 0;
                bool is_controller = strncmp(key, "controller.", 11) == 0;
                if (!is_keyboard && !is_controller) {
                    continue;
                }

                char* section = key + (is_keyboard ? 9 : 11);
                char* dot = strrchr(section, '.');
                if (!dot) {
                    continue;
                }
                *dot = '\0';
                char* slot_text = dot + 1;
                long slot = strtol(slot_text, NULL, 10);
                if (slot < 0 || slot >= 2) {
                    continue;
                }

                GBInputAction action = GB_INPUT_ACTION_RIGHT;
                if (!parse_input_action_name(section, &action)) {
                    continue;
                }

                GBInputBinding binding = {};
                if (strcmp(value, "none") == 0) {
                    binding = make_binding(GB_INPUT_BINDING_NONE, 0);
                } else if (is_keyboard) {
                    if (strncmp(value, "key:", 4) != 0) {
                        continue;
                    }
                    long code = strtol(value + 4, NULL, 10);
                    binding = make_binding(GB_INPUT_BINDING_KEY, (int)code);
                } else if (strncmp(value, "button:", 7) == 0) {
                    long code = strtol(value + 7, NULL, 10);
                    binding = make_binding(GB_INPUT_BINDING_CONTROLLER_BUTTON, (int)code);
                } else if (strncmp(value, "axis:", 5) == 0) {
                    char* value_copy = strdup(value + 5);
                    if (!value_copy) {
                        continue;
                    }
                    char* axis_text = strtok(value_copy, ":");
                    char* direction_text = strtok(NULL, ":");
                    if (axis_text && direction_text) {
                        long axis = strtol(axis_text, NULL, 10);
                        binding = make_binding(
                            (strcmp(direction_text, "+") == 0) ? GB_INPUT_BINDING_CONTROLLER_AXIS_POSITIVE
                                                                 : GB_INPUT_BINDING_CONTROLLER_AXIS_NEGATIVE,
                            (int)axis);
                    }
                    free(value_copy);
                } else {
                    continue;
                }

                if (!binding_is_valid(binding) && binding.kind != GB_INPUT_BINDING_NONE) {
                    continue;
                }

                if (is_keyboard) {
                    g_keyboard_bindings[action][slot] = binding;
                } else {
                    g_controller_bindings[action][slot] = binding;
                }
            }
            fclose(file);
        }
    }
    update_effective_joypad_state();
}

static void binding_to_config_value(const GBInputBinding& binding, char* out, size_t out_size) {
    if (!out || out_size == 0) {
        return;
    }

    switch (binding.kind) {
        case GB_INPUT_BINDING_KEY:
            snprintf(out, out_size, "key:%d", (int)binding.code);
            break;

        case GB_INPUT_BINDING_CONTROLLER_BUTTON:
            snprintf(out, out_size, "button:%d", (int)binding.code);
            break;

        case GB_INPUT_BINDING_CONTROLLER_AXIS_POSITIVE:
            snprintf(out, out_size, "axis:%d:+", (int)binding.code);
            break;

        case GB_INPUT_BINDING_CONTROLLER_AXIS_NEGATIVE:
            snprintf(out, out_size, "axis:%d:-", (int)binding.code);
            break;

        case GB_INPUT_BINDING_NONE:
        default:
            snprintf(out, out_size, "none");
            break;
    }
}

static void save_runtime_preferences(void) {
    const std::string path = runtime_preferences_path();
    if (path.empty()) {
        return;
    }

    FILE* file = fopen(path.c_str(), "w");
    if (!file) {
        fprintf(stderr, "[SDL] Failed to save runtime prefs to %s\n", path.c_str());
        return;
    }

    fprintf(file, "audio.enabled=%d\n", g_audio_output_enabled ? 1 : 0);
    fprintf(file, "audio.muted=%d\n", g_audio_muted ? 1 : 0);
    fprintf(file, "audio.latency_ms=%u\n", g_audio_latency_ms);
    fprintf(file, "audio.volume_percent=%u\n", g_audio_volume_percent);
    fprintf(file, "audio.device_name=%s\n", g_audio_target_device_name.c_str());
    fprintf(file, "savestate.slot=%d\n", g_savestate_slot);

    for (int action = 0; action < GB_INPUT_ACTION_COUNT; action++) {
        for (int slot = 0; slot < 2; slot++) {
            char value[64];
            binding_to_config_value(g_keyboard_bindings[action][slot], value, sizeof(value));
            fprintf(file, "keyboard.%s.%d=%s\n", input_action_config_name((GBInputAction)action), slot, value);
        }
    }

    for (int action = 0; action < GB_INPUT_ACTION_COUNT; action++) {
        for (int slot = 0; slot < 2; slot++) {
            char value[64];
            binding_to_config_value(g_controller_bindings[action][slot], value, sizeof(value));
            fprintf(file, "controller.%s.%d=%s\n", input_action_config_name((GBInputAction)action), slot, value);
        }
    }

    fclose(file);
}

static void set_savestate_status(const char* action, int slot, bool success, const char* detail) {
    char message[512];
    snprintf(message,
             sizeof(message),
             "%s slot %d %s%s%s",
             action ? action : "Savestate",
             slot + 1,
             success ? "succeeded" : "failed",
             (detail && detail[0]) ? ": " : "",
             (detail && detail[0]) ? detail : "");
    g_savestate_status = message;
}

static bool recreate_streaming_texture(void);
static void set_app_suspended(bool suspended);

static bool frame_is_selected_for_dump(const uint32_t* frames, int count, uint32_t frame) {
    for (int i = 0; i < count; i++) {
        if (frames[i] == frame) {
            return true;
        }
    }
    return false;
}

/* Helper to parse button string "U,D,L,R,A,B,S,T" */
static void parse_buttons(const char* btn_str, uint8_t* dpad, uint8_t* buttons) {
    *dpad = 0xFF;
    *buttons = 0xFF;
    // Simple parser: check for existence of characters
    if (strchr(btn_str, 'U')) *dpad &= ~0x04;
    if (strchr(btn_str, 'D')) *dpad &= ~0x08;
    if (strchr(btn_str, 'L')) *dpad &= ~0x02;
    if (strchr(btn_str, 'R')) *dpad &= ~0x01;
    if (strchr(btn_str, 'A')) *buttons &= ~0x01;
    if (strchr(btn_str, 'B')) *buttons &= ~0x02;
    if (strchr(btn_str, 'S')) *buttons &= ~0x08; /* Start */
    if (strchr(btn_str, 'T')) *buttons &= ~0x04; /* Select (T for selecT) */
}

static bool input_action_is_pressed(GBInputAction action) {
    for (int slot = 0; slot < 2; slot++) {
        if (g_keyboard_binding_pressed[action][slot] ||
            g_controller_button_binding_pressed[action][slot] ||
            g_controller_axis_binding_pressed[action][slot]) {
            return true;
        }
    }
    return false;
}

static void update_runtime_action_state(GBContext* ctx) {
    const int previous_effective_speed = effective_speed_percent();

    for (int action = 0; action < GB_INPUT_ACTION_COUNT; action++) {
        if (!input_action_is_runtime((GBInputAction)action)) {
            continue;
        }

        const bool pressed = input_action_is_pressed((GBInputAction)action);
        const bool was_pressed = g_runtime_action_pressed[action];
        if (pressed && !was_pressed) {
            switch ((GBInputAction)action) {
                case GB_INPUT_ACTION_TOGGLE_MAX_SPEED:
                    g_max_speed_mode = !g_max_speed_mode;
                    break;

                case GB_INPUT_ACTION_SAVE_STATE:
                    save_savestate_slot(ctx, g_savestate_slot);
                    break;

                case GB_INPUT_ACTION_LOAD_STATE:
                    load_savestate_slot(ctx, g_savestate_slot);
                    break;

                case GB_INPUT_ACTION_PREVIOUS_STATE_SLOT:
                    g_savestate_slot = (g_savestate_slot + GB_SAVESTATE_SLOT_COUNT - 1) % GB_SAVESTATE_SLOT_COUNT;
                    g_savestate_status = "Selected slot " + std::to_string(g_savestate_slot + 1);
                    save_runtime_preferences();
                    break;

                case GB_INPUT_ACTION_NEXT_STATE_SLOT:
                    g_savestate_slot = (g_savestate_slot + 1) % GB_SAVESTATE_SLOT_COUNT;
                    g_savestate_status = "Selected slot " + std::to_string(g_savestate_slot + 1);
                    save_runtime_preferences();
                    break;

                case GB_INPUT_ACTION_TOGGLE_OVERLAY:
                    g_show_overlay = !g_show_overlay;
                    break;

                case GB_INPUT_ACTION_TOGGLE_MUTE:
                    g_audio_muted = !g_audio_muted;
                    save_runtime_preferences();
                    break;

                case GB_INPUT_ACTION_TOGGLE_MENU:
                    g_show_menu = !g_show_menu;
                    break;

                case GB_INPUT_ACTION_FAST_FORWARD:
                case GB_INPUT_ACTION_RIGHT:
                case GB_INPUT_ACTION_LEFT:
                case GB_INPUT_ACTION_UP:
                case GB_INPUT_ACTION_DOWN:
                case GB_INPUT_ACTION_A:
                case GB_INPUT_ACTION_B:
                case GB_INPUT_ACTION_SELECT:
                case GB_INPUT_ACTION_START:
                case GB_INPUT_ACTION_COUNT:
                default:
                    break;
            }
        }

        g_runtime_action_pressed[action] = pressed;
    }

    g_fast_forward_active = g_runtime_action_pressed[GB_INPUT_ACTION_FAST_FORWARD];

    if (effective_speed_percent() != previous_effective_speed) {
        reset_audio_output_buffer(true);
    }
}

static void rebuild_manual_joypad_state_from_bindings(void) {
    g_manual_joypad_dpad = 0xFF;
    g_manual_joypad_buttons = 0xFF;

    if (input_action_is_pressed(GB_INPUT_ACTION_RIGHT)) g_manual_joypad_dpad &= (uint8_t)~0x01;
    if (input_action_is_pressed(GB_INPUT_ACTION_LEFT)) g_manual_joypad_dpad &= (uint8_t)~0x02;
    if (input_action_is_pressed(GB_INPUT_ACTION_UP)) g_manual_joypad_dpad &= (uint8_t)~0x04;
    if (input_action_is_pressed(GB_INPUT_ACTION_DOWN)) g_manual_joypad_dpad &= (uint8_t)~0x08;
    if (input_action_is_pressed(GB_INPUT_ACTION_A)) g_manual_joypad_buttons &= (uint8_t)~0x01;
    if (input_action_is_pressed(GB_INPUT_ACTION_B)) g_manual_joypad_buttons &= (uint8_t)~0x02;
    if (input_action_is_pressed(GB_INPUT_ACTION_SELECT)) g_manual_joypad_buttons &= (uint8_t)~0x04;
    if (input_action_is_pressed(GB_INPUT_ACTION_START)) g_manual_joypad_buttons &= (uint8_t)~0x08;
}

static void update_effective_joypad_state(void) {
    rebuild_manual_joypad_state_from_bindings();
    g_joypad_dpad = g_manual_joypad_dpad & g_script_joypad_dpad;
    g_joypad_buttons = g_manual_joypad_buttons & g_script_joypad_buttons;
}

static void request_joypad_interrupt(GBContext* ctx) {
    if (!ctx) return;
    ctx->io[0x0F] |= 0x10;
    if (ctx->halted) ctx->halted = 0;
}

static bool input_state_has_press(uint8_t dpad, uint8_t buttons) {
    return dpad != 0xFF || buttons != 0xFF;
}

static void write_buttons(FILE* file, uint8_t dpad, uint8_t buttons) {
    if (!(dpad & 0x04)) fputc('U', file);
    if (!(dpad & 0x08)) fputc('D', file);
    if (!(dpad & 0x02)) fputc('L', file);
    if (!(dpad & 0x01)) fputc('R', file);
    if (!(buttons & 0x01)) fputc('A', file);
    if (!(buttons & 0x02)) fputc('B', file);
    if (!(buttons & 0x08)) fputc('S', file);
    if (!(buttons & 0x04)) fputc('T', file);
}

static char* trim_ascii(char* text) {
    while (text && *text && isspace((unsigned char)*text)) {
        text++;
    }
    if (!text || !*text) {
        return text;
    }

    char* end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = '\0';
    return text;
}

static bool parse_script_token(char* token, ScriptEntry* entry, char* button_buf, size_t button_buf_size) {
    char* start_text = trim_ascii(token);
    if (!start_text || !*start_text) {
        return false;
    }

    char* first_colon = strchr(start_text, ':');
    if (!first_colon) {
        return false;
    }
    *first_colon = '\0';

    char* buttons_text = trim_ascii(first_colon + 1);
    char* second_colon = strchr(buttons_text, ':');
    if (!second_colon) {
        return false;
    }
    *second_colon = '\0';

    char* duration_text = trim_ascii(second_colon + 1);
    start_text = trim_ascii(start_text);
    buttons_text = trim_ascii(buttons_text);
    duration_text = trim_ascii(duration_text);
    if (!*start_text || !*duration_text) {
        return false;
    }

    entry->anchor = SCRIPT_ANCHOR_FRAME;
    if (*start_text == 'c' || *start_text == 'C') {
        entry->anchor = SCRIPT_ANCHOR_CYCLE;
        start_text++;
    } else if (*start_text == 'f' || *start_text == 'F') {
        start_text++;
    }
    start_text = trim_ascii(start_text);
    if (!*start_text) {
        return false;
    }

    char* start_end = NULL;
    char* duration_end = NULL;
    entry->start = strtoull(start_text, &start_end, 10);
    entry->duration = strtoull(duration_text, &duration_end, 10);
    if ((start_end && *trim_ascii(start_end)) || (duration_end && *trim_ascii(duration_end))) {
        return false;
    }
    if (entry->duration == 0) {
        return false;
    }

    snprintf(button_buf, button_buf_size, "%s", buttons_text);
    return true;
}

static void flush_input_record_segment(void) {
    if (!g_input_record_file || !g_input_record_has_segment) return;

    uint64_t duration_cycles = 0;
    if (g_input_record_end_cycle > g_input_record_start_cycle) {
        duration_cycles = g_input_record_end_cycle - g_input_record_start_cycle;
    }

    if (duration_cycles > 0 && input_state_has_press(g_input_record_dpad, g_input_record_buttons)) {
        if (g_input_record_wrote_entry) {
            fputc(',', g_input_record_file);
        }
        fprintf(g_input_record_file, "c%llu:", (unsigned long long)g_input_record_start_cycle);
        write_buttons(g_input_record_file, g_input_record_dpad, g_input_record_buttons);
        fprintf(g_input_record_file, ":%llu", (unsigned long long)duration_cycles);
        fflush(g_input_record_file);
        g_input_record_wrote_entry = true;
    }

    g_input_record_has_segment = false;
    g_input_record_start_cycle = 0;
    g_input_record_end_cycle = 0;
    g_input_record_dpad = 0xFF;
    g_input_record_buttons = 0xFF;
}

static void close_input_record_file(void) {
    if (!g_input_record_file) return;
    flush_input_record_segment();
    fputc('\n', g_input_record_file);
    fclose(g_input_record_file);
    g_input_record_file = NULL;
    g_input_record_wrote_entry = false;
}

static void record_manual_input_state(uint64_t cycle_count) {
    if (!g_input_record_file) return;

    if (!g_input_record_has_segment) {
        g_input_record_has_segment = true;
        g_input_record_start_cycle = cycle_count;
        g_input_record_end_cycle = cycle_count;
        g_input_record_dpad = g_manual_joypad_dpad;
        g_input_record_buttons = g_manual_joypad_buttons;
        return;
    }

    if (g_input_record_dpad == g_manual_joypad_dpad && g_input_record_buttons == g_manual_joypad_buttons) {
        g_input_record_end_cycle = cycle_count;
        return;
    }

    g_input_record_end_cycle = cycle_count;
    flush_input_record_segment();
    g_input_record_has_segment = true;
    g_input_record_start_cycle = cycle_count;
    g_input_record_end_cycle = cycle_count;
    g_input_record_dpad = g_manual_joypad_dpad;
    g_input_record_buttons = g_manual_joypad_buttons;
}

void gb_platform_set_input_script(const char* script) {
    // Formats: frame:buttons:duration,... or ccycle:buttons:duration,...
    g_script_count = 0;
    g_script_joypad_dpad = 0xFF;
    g_script_joypad_buttons = 0xFF;
    update_effective_joypad_state();

    if (!script) return;
    
    char* copy = strdup(script);
    char* token = strtok(copy, ",");
    
    while (token && g_script_count < MAX_SCRIPT_ENTRIES) {
        char btn_buf[16] = {0};
        ScriptEntry parsed = {};

        if (parse_script_token(token, &parsed, btn_buf, sizeof(btn_buf))) {
            ScriptEntry* e = &g_input_script[g_script_count++];
            *e = parsed;
            parse_buttons(btn_buf, &e->dpad, &e->buttons);
            printf("[AUTO] Added input: %s %llu, Btns '%s', Dur %llu\n",
                   e->anchor == SCRIPT_ANCHOR_CYCLE ? "Cycle" : "Frame",
                   (unsigned long long)e->start,
                   btn_buf,
                   (unsigned long long)e->duration);
        } else {
            fprintf(stderr, "[AUTO] Ignoring invalid input token '%s'\n", token);
        }
        token = strtok(NULL, ",");
    }
    free(copy);
}

void gb_platform_set_input_record_file(const char* path) {
    if (!g_input_record_exit_handler_registered) {
        atexit(close_input_record_file);
        g_input_record_exit_handler_registered = true;
    }

    close_input_record_file();
    g_input_record_has_segment = false;
    g_input_record_start_cycle = 0;
    g_input_record_end_cycle = 0;
    g_input_record_dpad = 0xFF;
    g_input_record_buttons = 0xFF;

    if (!path || !path[0]) return;

    const std::string resolved_path = resolve_writable_path(path, "artifacts");
    g_input_record_file = fopen(resolved_path.c_str(), "w");
    if (!g_input_record_file) {
        fprintf(stderr, "[AUTO] Failed to open input record file '%s'\n", resolved_path.c_str());
        return;
    }

    fprintf(stderr, "[AUTO] Recording live input to %s (cycle anchored)\n", resolved_path.c_str());
}

void gb_platform_set_dump_frames(const char* frames) {
    if (!frames) return;
    char* copy = strdup(frames);
    char* token = strtok(copy, ",");
    g_dump_count = 0;
    while (token && g_dump_count < MAX_DUMP_FRAMES) {
        g_dump_frames[g_dump_count++] = (uint32_t)strtoul(token, NULL, 10);
        token = strtok(NULL, ",");
    }
    free(copy);
}

void gb_platform_set_dump_present_frames(const char* frames) {
    if (!frames) return;
    char* copy = strdup(frames);
    char* token = strtok(copy, ",");
    g_dump_present_count = 0;
    while (token && g_dump_present_count < MAX_DUMP_FRAMES) {
        g_dump_present_frames[g_dump_present_count++] = (uint32_t)strtoul(token, NULL, 10);
        token = strtok(NULL, ",");
    }
    free(copy);
}

void gb_platform_set_screenshot_prefix(const char* prefix) {
    if (prefix) snprintf(g_screenshot_prefix, sizeof(g_screenshot_prefix), "%s", prefix);
}

static void save_ppm(const char* filename, const uint32_t* fb, int width, int height, int frame_count) {
    const std::string resolved_filename = resolve_writable_path(filename, "artifacts");
    // Calculate simple hash
    uint32_t hash = 0;
    for (int k = 0; k < width * height; k++) {
        hash = (hash * 33) ^ fb[k];
    }
    printf("[AUTO] Frame %d hash: %08X\n", frame_count, hash);

    FILE* f = fopen(resolved_filename.c_str(), "wb");
    if (!f) return;
    
    fprintf(f, "P6\n%d %d\n255\n", width, height);
    
    uint8_t* row = (uint8_t*)malloc(width * 3);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint32_t p = fb[y * width + x];
            row[x*3+0] = (p >> 16) & 0xFF; // R
            row[x*3+1] = (p >> 8) & 0xFF;  // G
            row[x*3+2] = (p >> 0) & 0xFF;  // B
        }
        fwrite(row, 1, width * 3, f);
    }
    
    free(row);
    fclose(f);
    printf("[AUTO] Saved screenshot: %s\n", resolved_filename.c_str());
}


static int g_frame_count = 0;

static double sdl_now_ms(void) {
    uint64_t ticks = SDL_GetPerformanceCounter();
    uint64_t freq = SDL_GetPerformanceFrequency();
    return freq ? ((double)ticks * 1000.0) / (double)freq : 0.0;
}

static int round_to_int(double value) {
    return (int)(value + 0.5);
}

static void update_render_filter(void) {
    if (!g_renderer) {
        return;
    }

#if SDL_VERSION_ATLEAST(2, 0, 12)
    if (g_texture) {
        SDL_SetTextureScaleMode(g_texture,
                                g_render_filter_mode == GB_RENDER_FILTER_LINEAR
                                    ? SDL_ScaleModeLinear
                                    : SDL_ScaleModeNearest);
    }
#else
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,
                g_render_filter_mode == GB_RENDER_FILTER_LINEAR ? "linear" : "nearest");
#endif
}

static void update_game_viewport(void) {
    if (!g_window) {
        g_game_viewport.x = 0;
        g_game_viewport.y = 0;
        g_game_viewport.w = GB_SCREEN_WIDTH * g_scale;
        g_game_viewport.h = GB_SCREEN_HEIGHT * g_scale;
        return;
    }

    int window_w = 0;
    int window_h = 0;
    SDL_GetWindowSize(g_window, &window_w, &window_h);
    if (window_w <= 0) window_w = GB_SCREEN_WIDTH;
    if (window_h <= 0) window_h = GB_SCREEN_HEIGHT;

    int viewport_w = window_w;
    int viewport_h = window_h;

    switch (g_render_scaling_mode) {
        case GB_RENDER_SCALING_PIXEL_PERFECT: {
            int scale_x = window_w / GB_SCREEN_WIDTH;
            int scale_y = window_h / GB_SCREEN_HEIGHT;
            int integer_scale = (scale_x < scale_y) ? scale_x : scale_y;
            if (integer_scale < 1) {
                integer_scale = 1;
            }
            viewport_w = GB_SCREEN_WIDTH * integer_scale;
            viewport_h = GB_SCREEN_HEIGHT * integer_scale;
            break;
        }
        case GB_RENDER_SCALING_ASPECT_FIT: {
            double scale_x = (double)window_w / (double)GB_SCREEN_WIDTH;
            double scale_y = (double)window_h / (double)GB_SCREEN_HEIGHT;
            double scale = (scale_x < scale_y) ? scale_x : scale_y;
            if (scale <= 0.0) {
                scale = 1.0;
            }
            viewport_w = round_to_int((double)GB_SCREEN_WIDTH * scale);
            viewport_h = round_to_int((double)GB_SCREEN_HEIGHT * scale);
            break;
        }
        case GB_RENDER_SCALING_ASPECT_FILL: {
            double scale_x = (double)window_w / (double)GB_SCREEN_WIDTH;
            double scale_y = (double)window_h / (double)GB_SCREEN_HEIGHT;
            double scale = (scale_x > scale_y) ? scale_x : scale_y;
            if (scale <= 0.0) {
                scale = 1.0;
            }
            viewport_w = round_to_int((double)GB_SCREEN_WIDTH * scale);
            viewport_h = round_to_int((double)GB_SCREEN_HEIGHT * scale);
            break;
        }
        case GB_RENDER_SCALING_STRETCH:
        default:
            viewport_w = window_w;
            viewport_h = window_h;
            break;
    }

    if (viewport_w < 1) viewport_w = 1;
    if (viewport_h < 1) viewport_h = 1;

    g_game_viewport.w = viewport_w;
    g_game_viewport.h = viewport_h;
    g_game_viewport.x = (window_w - viewport_w) / 2;
    g_game_viewport.y = (window_h - viewport_h) / 2;
}

static void apply_window_scale_preset(void) {
    g_windowed_width = GB_SCREEN_WIDTH * g_scale;
    g_windowed_height = GB_SCREEN_HEIGHT * g_scale;

    if (g_window && !g_fullscreen) {
        SDL_SetWindowSize(g_window, g_windowed_width, g_windowed_height);
        SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
    update_game_viewport();
}

static void set_fullscreen_enabled(bool enabled) {
    if (!g_window || g_fullscreen == enabled) {
        return;
    }

    if (enabled) {
        SDL_GetWindowSize(g_window, &g_windowed_width, &g_windowed_height);
        if (SDL_SetWindowFullscreen(g_window, SDL_WINDOW_FULLSCREEN_DESKTOP) == 0) {
            g_fullscreen = true;
        }
    } else {
        if (SDL_SetWindowFullscreen(g_window, 0) == 0) {
            g_fullscreen = false;
            SDL_SetWindowSize(g_window, g_windowed_width, g_windowed_height);
            SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        }
    }

    update_game_viewport();
}

static bool recreate_streaming_texture(void) {
    if (!g_renderer || g_benchmark_mode) {
        return true;
    }

    if (g_texture) {
        SDL_DestroyTexture(g_texture);
        g_texture = NULL;
    }

    g_texture = SDL_CreateTexture(
        g_renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        GB_SCREEN_WIDTH,
        GB_SCREEN_HEIGHT
    );
    if (!g_texture) {
        fprintf(stderr, "[SDL] Failed to recreate texture: %s\n", SDL_GetError());
        return false;
    }

    update_render_filter();
    g_renderer_reset_pending = false;
    return true;
}

static void set_app_suspended(bool suspended) {
    if (g_app_suspended == suspended) {
        return;
    }

    g_app_suspended = suspended;
    refresh_audio_device_pause_state();

    if (!suspended) {
        g_last_frame_time = SDL_GetTicks();
        if (g_window) {
            update_game_viewport();
        }
        g_renderer_reset_pending = true;
    }
}

static void reset_runtime_display_defaults(void) {
    g_scale = 5;
    g_speed_percent = 100;
    g_palette_idx = 0;
    g_smooth_lcd_transitions = true;
    g_vsync = false;
    g_show_overlay = false;
    g_max_speed_mode = false;
    g_render_scaling_mode = GB_RENDER_SCALING_PIXEL_PERFECT;
    g_render_filter_mode = GB_RENDER_FILTER_NEAREST;
    update_render_filter();

    if (g_renderer) {
        SDL_RenderSetVSync(g_renderer, 0);
    }

    const bool want_fullscreen = platform_default_fullscreen();
    if (g_window) {
        if (g_fullscreen != want_fullscreen) {
            set_fullscreen_enabled(want_fullscreen);
        }
        if (!g_fullscreen) {
            apply_window_scale_preset();
        } else {
            update_game_viewport();
        }
    } else {
        g_fullscreen = want_fullscreen;
    }

    reset_audio_output_buffer(true);
}

static void reset_runtime_audio_defaults(void) {
    set_default_audio_preferences();
    refresh_audio_output_devices();
    reopen_audio_output_device(true);
    save_runtime_preferences();
}

static void reset_runtime_control_defaults(void) {
    set_default_input_bindings();
    update_effective_joypad_state();
    save_runtime_preferences();
}

static void update_controller_axis_binding_state(void) {
    for (int action = 0; action < GB_INPUT_ACTION_COUNT; action++) {
        for (int slot = 0; slot < 2; slot++) {
            const GBInputBinding& binding = g_controller_bindings[action][slot];
            if (binding.kind == GB_INPUT_BINDING_CONTROLLER_AXIS_POSITIVE ||
                binding.kind == GB_INPUT_BINDING_CONTROLLER_AXIS_NEGATIVE) {
                Sint16 value = 0;
                if (binding.code >= 0 && binding.code < SDL_CONTROLLER_AXIS_MAX) {
                    value = g_controller_axis_values[binding.code];
                }
                g_controller_axis_binding_pressed[action][slot] = binding_active_for_axis_value(binding, value);
            } else {
                g_controller_axis_binding_pressed[action][slot] = false;
            }
        }
    }
}

static bool input_transition_creates_press(uint8_t previous_dpad,
                                           uint8_t previous_buttons,
                                           uint8_t next_dpad,
                                           uint8_t next_buttons,
                                           bool dpad_selected,
                                           bool buttons_selected) {
    if (dpad_selected && ((previous_dpad & ~next_dpad) != 0)) {
        return true;
    }
    if (buttons_selected && ((previous_buttons & ~next_buttons) != 0)) {
        return true;
    }
    return false;
}

static void render_binding_editor(const char* section_label,
                                  GBBindingCaptureDevice device,
                                  GBInputBinding bindings[GB_INPUT_ACTION_COUNT][2],
                                  GBInputAction action_begin,
                                  GBInputAction action_end) {
    ImGui::TextDisabled("%s", section_label);
    const ImGuiStyle& style = ImGui::GetStyle();
    const float content_width = ImGui::GetContentRegionAvail().x;
    const bool compact_layout = content_width < 720.0f;

    for (int action = (int)action_begin; action < (int)action_end; action++) {
        ImGui::PushID(section_label);
        ImGui::PushID(action);
        ImGui::Text("%s", g_input_action_names[action]);

        const float clear_width = ImGui::CalcTextSize("Clear").x + style.FramePadding.x * 2.0f + 12.0f;
        if (!compact_layout) {
            const float action_column_width = content_width * 0.26f;
            float binding_width =
                (content_width - action_column_width - clear_width * 2.0f - style.ItemSpacing.x * 4.0f) / 2.0f;
            if (binding_width < 120.0f) {
                binding_width = 120.0f;
            }
            ImGui::SameLine(action_column_width);

            for (int slot = 0; slot < 2; slot++) {
                std::string label;
                if (g_binding_capture_active &&
                    g_binding_capture_device == device &&
                    g_binding_capture_action == (GBInputAction)action &&
                    g_binding_capture_slot == slot) {
                    label = "Press Input...";
                } else {
                    label = binding_display_label(bindings[action][slot]);
                }

                ImGui::PushID(slot);
                if (ImGui::Button(label.c_str(), ImVec2(binding_width, 0.0f))) {
                    start_binding_capture(device, (GBInputAction)action, slot);
                }
                ImGui::SameLine();
                if (ImGui::Button("Clear", ImVec2(clear_width, 0.0f))) {
                    assign_binding_slot(device, (GBInputAction)action, slot, make_binding(GB_INPUT_BINDING_NONE, 0));
                }
                ImGui::PopID();
                if (slot == 0) {
                    ImGui::SameLine();
                }
            }
        } else {
            float compact_button_width = content_width - clear_width - style.ItemSpacing.x;
            if (compact_button_width < 120.0f) {
                compact_button_width = 120.0f;
            }
            for (int slot = 0; slot < 2; slot++) {
                std::string label;
                if (g_binding_capture_active &&
                    g_binding_capture_device == device &&
                    g_binding_capture_action == (GBInputAction)action &&
                    g_binding_capture_slot == slot) {
                    label = "Press Input...";
                } else {
                    label = binding_display_label(bindings[action][slot]);
                }

                ImGui::PushID(slot);
                if (ImGui::Button(label.c_str(), ImVec2(compact_button_width, 0.0f))) {
                    start_binding_capture(device, (GBInputAction)action, slot);
                }
                ImGui::SameLine();
                if (ImGui::Button("Clear", ImVec2(clear_width, 0.0f))) {
                    assign_binding_slot(device, (GBInputAction)action, slot, make_binding(GB_INPUT_BINDING_NONE, 0));
                }
                ImGui::PopID();
            }
        }
        ImGui::PopID();
        ImGui::PopID();
    }
}

static void ensure_lcd_off_framebuffer(void) {
    if (g_lcd_off_framebuffer_initialized) {
        return;
    }

    for (int i = 0; i < GB_FRAMEBUFFER_SIZE; i++) {
        g_lcd_off_framebuffer[i] = 0xFFE0F8D0;
    }

    g_lcd_off_framebuffer_initialized = true;
}

static void render_frame_internal(const uint32_t* framebuffer, bool count_guest_frame) {
    if (!framebuffer) {
        DBG_FRAME("Platform render_frame: SKIPPED (null: texture=%d, renderer=%d, fb=%d)",
                  g_texture == NULL, g_renderer == NULL, framebuffer == NULL);
        return;
    }
    g_present_count++;
    if (count_guest_frame) {
        g_frame_count++;
        memcpy(g_last_guest_framebuffer, framebuffer, sizeof(g_last_guest_framebuffer));
        g_last_guest_framebuffer_valid = true;
    }

    if (count_guest_frame) {
        /* Handle Screenshot Dumping */
        if (frame_is_selected_for_dump(g_dump_frames, g_dump_count, (uint32_t)g_frame_count)) {
            char filename[128];
            snprintf(filename, sizeof(filename), "%s_%05d.ppm", g_screenshot_prefix, g_frame_count);
            save_ppm(filename, framebuffer, GB_SCREEN_WIDTH, GB_SCREEN_HEIGHT, g_frame_count);
        }
    }

    if (frame_is_selected_for_dump(g_dump_present_frames, g_dump_present_count, (uint32_t)g_frame_count)) {
        char filename[160];
        snprintf(filename,
                 sizeof(filename),
                 "%s_guest_%05d_present_%06llu.ppm",
                 g_screenshot_prefix,
                 g_frame_count,
                 (unsigned long long)g_present_count);
        save_ppm(filename, framebuffer, GB_SCREEN_WIDTH, GB_SCREEN_HEIGHT, g_frame_count);
    }

    if (g_benchmark_mode || g_app_suspended || !g_renderer) {
        DBG_FRAME("Platform render_frame: host render skipped (benchmark=%d texture=%d renderer=%d)",
                  g_benchmark_mode ? 1 : 0,
                  g_texture == NULL,
                  g_renderer == NULL);
        g_last_timing.upload_ms = 0.0;
        g_last_timing.compose_ms = 0.0;
        g_last_timing.present_ms = 0.0;
        g_last_timing.total_render_ms = 0.0;
        return;
    }

    if ((!g_texture || g_renderer_reset_pending) && !recreate_streaming_texture()) {
        g_last_timing.upload_ms = 0.0;
        g_last_timing.compose_ms = 0.0;
        g_last_timing.present_ms = 0.0;
        g_last_timing.total_render_ms = 0.0;
        return;
    }

    double total_render_start_ms = sdl_now_ms();

    /* Debug: check framebuffer content on first few guest frames */
    if (count_guest_frame && g_frame_count <= 3) {
        bool has_content = false;
        uint32_t white = 0xFFE0F8D0;
        for (int i = 0; i < GB_SCREEN_WIDTH * GB_SCREEN_HEIGHT; i++) {
            if (framebuffer[i] != white) {
                has_content = true;
                break;
            }
        }
        DBG_FRAME("Platform frame %d - has_content=%d, first_pixel=0x%08X",
                  g_frame_count, has_content, framebuffer[0]);
    }

    if (count_guest_frame && (g_frame_count % 60) == 0) {
        char title[64];
        snprintf(title, sizeof(title), "GameBoy Recompiled - Frame %d", g_frame_count);
        SDL_SetWindowTitle(g_window, title);
    } else if (!count_guest_frame && (g_present_count % 30) == 0) {
        char title[96];
        if (g_frame_count == 0) {
            snprintf(title, sizeof(title), "GameBoy Recompiled - Starting... (%llu)",
                     (unsigned long long)(g_present_count / 30));
        } else {
            snprintf(title, sizeof(title), "GameBoy Recompiled - Frame %d (Working...)",
                     g_frame_count);
        }
        SDL_SetWindowTitle(g_window, title);
    }

    /* Update texture */
    double upload_start_ms = sdl_now_ms();
    void* pixels;
    int pitch;
    SDL_LockTexture(g_texture, NULL, &pixels, &pitch);

    const uint32_t* src = framebuffer;
    uint32_t* dst = (uint32_t*)pixels;

    if (g_palette_idx == 0) {
        memcpy(dst, src, GB_SCREEN_WIDTH * GB_SCREEN_HEIGHT * sizeof(uint32_t));
    } else {
        uint32_t original_palette[4] = { 0xFFE0F8D0, 0xFF88C070, 0xFF346856, 0xFF081820 };

        for (int i = 0; i < GB_SCREEN_WIDTH * GB_SCREEN_HEIGHT; i++) {
            uint32_t c = src[i];
            int color_idx = -1;
            if (c == original_palette[0]) color_idx = 0;
            else if (c == original_palette[1]) color_idx = 1;
            else if (c == original_palette[2]) color_idx = 2;
            else if (c == original_palette[3]) color_idx = 3;

            if (color_idx >= 0) {
                dst[i] = g_palettes[g_palette_idx][color_idx];
            } else {
                dst[i] = c;
            }
        }
    }

    SDL_UnlockTexture(g_texture);
    g_last_timing.upload_ms = sdl_now_ms() - upload_start_ms;

    /* Clear and render */
    double compose_start_ms = sdl_now_ms();
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_renderer);
    SDL_RenderCopy(g_renderer, g_texture, NULL, &g_game_viewport);

    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    ImGuiIO& imgui_io = ImGui::GetIO();
    imgui_io.FontGlobalScale = settings_ui_scale_for_size(imgui_io.DisplaySize);

    if (g_show_menu) {
        const float ui_scale = imgui_io.FontGlobalScale;
        const float footer_height = ImGui::GetFrameHeightWithSpacing() *
                                    (g_launcher_return_enabled ? 3.8f : 3.0f);

        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(imgui_io.DisplaySize, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.96f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f * ui_scale, 16.0f * ui_scale));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.0f * ui_scale, 10.0f * ui_scale));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f * ui_scale, 10.0f * ui_scale));
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 18.0f * ui_scale);
        ImGui::Begin("GameBoy Recompiled",
                     &g_show_menu,
                     ImGuiWindowFlags_NoDecoration |
                         ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoCollapse);
        ImGui::Text("GameBoy Recompiled");
        ImGui::TextDisabled("Settings");
        ImGui::SameLine();
        if (ImGui::Button("Resume")) {
            g_show_menu = false;
        }
        ImGui::Separator();
        ImGui::TextWrapped("Toggle menu: %s", controller_menu_hint_text());
#if defined(__ANDROID__)
        ImGui::TextWrapped("Android Back / Escape also opens or closes the menu.");
#else
        ImGui::TextWrapped("Escape also opens or closes the menu.");
#endif
        ImGui::Separator();
        ImGui::BeginChild("SettingsScroll", ImVec2(0.0f, -footer_height), false);
        ImGui::Text("Performance: %.1f FPS", ImGui::GetIO().Framerate);

        int window_w = 0;
        int window_h = 0;
        SDL_GetWindowSize(g_window, &window_w, &window_h);

        ImGui::Separator();
        ImGui::TextDisabled("Graphics");
        ImGui::Text("Window: %d x %d", window_w, window_h);
        ImGui::Text("Viewport: %d x %d (%.2fx)",
                    g_game_viewport.w,
                    g_game_viewport.h,
                    (double)g_game_viewport.w / (double)GB_SCREEN_WIDTH);

        bool fullscreen = g_fullscreen;
        if (ImGui::Checkbox("Fullscreen", &fullscreen)) {
            set_fullscreen_enabled(fullscreen);
        }

        int scaling_mode = (int)g_render_scaling_mode;
        if (ImGui::Combo("Scaling Mode",
                         &scaling_mode,
                         g_render_scaling_mode_names,
                         IM_ARRAYSIZE(g_render_scaling_mode_names))) {
            g_render_scaling_mode = (GBRenderScalingMode)scaling_mode;
            update_game_viewport();
        }

        int filter_mode = (int)g_render_filter_mode;
        if (ImGui::Combo("Scale Filter",
                         &filter_mode,
                         g_render_filter_names,
                         IM_ARRAYSIZE(g_render_filter_names))) {
            g_render_filter_mode = (GBRenderFilterMode)filter_mode;
            update_render_filter();
        }

        if (!g_fullscreen) {
            int scale_idx = g_scale - 1;
            if (ImGui::Combo("Window Size",
                             &scale_idx,
                             g_scale_names,
                             IM_ARRAYSIZE(g_scale_names))) {
                g_scale = scale_idx + 1;
                apply_window_scale_preset();
            }
        } else {
            ImGui::TextDisabled("Window Size is disabled while fullscreen is active.");
        }

        ImGui::Separator();
        ImGui::TextDisabled("Runtime");
        if (ImGui::Checkbox("V-Sync", &g_vsync)) {
            SDL_RenderSetVSync(g_renderer, g_vsync ? 1 : 0);
        }

        ImGui::Checkbox("Show Overlay", &g_show_overlay);
        ImGui::Checkbox("Smooth Slow Frames", &g_smooth_lcd_transitions);
        if (ImGui::SliderInt("Speed %", &g_speed_percent, 10, 500)) {
            reset_audio_output_buffer(true);
        }
        if (ImGui::Button("Reset Speed")) {
            g_speed_percent = 100;
            g_max_speed_mode = false;
            reset_audio_output_buffer(true);
        }
        ImGui::Text("Effective Speed: %d%%", effective_speed_percent());
        if (g_fast_forward_active) {
            ImGui::TextDisabled("Fast forward shortcut is active.");
        } else if (g_max_speed_mode) {
            ImGui::TextDisabled("Max speed shortcut is active.");
        }
        ImGui::Combo("Palette", &g_palette_idx, g_palette_names, IM_ARRAYSIZE(g_palette_names));

        ImGui::Separator();
        ImGui::TextDisabled("Audio");
        if (ImGui::Checkbox("Enable Audio Output", &g_audio_output_enabled)) {
            reset_audio_output_buffer(true);
            save_runtime_preferences();
        }
        if (ImGui::Checkbox("Mute", &g_audio_muted)) {
            save_runtime_preferences();
        }
        int audio_volume_percent = (int)g_audio_volume_percent;
        if (ImGui::SliderInt("Master Volume (%)", &audio_volume_percent, 0, 200)) {
            g_audio_volume_percent = (uint32_t)audio_volume_percent;
            save_runtime_preferences();
        }
        if (ImGui::BeginCombo("Output Device", current_audio_output_device_label())) {
            bool selected_default = g_audio_target_device_name.empty();
            if (ImGui::Selectable("System Default", selected_default)) {
                g_audio_target_device_name.clear();
                save_runtime_preferences();
                reopen_audio_output_device(true);
            }
            if (selected_default) {
                ImGui::SetItemDefaultFocus();
            }

            for (size_t i = 0; i < g_audio_output_devices.size(); i++) {
                const bool selected = g_audio_output_devices[i] == g_audio_target_device_name;
                if (ImGui::Selectable(g_audio_output_devices[i].c_str(), selected)) {
                    g_audio_target_device_name = g_audio_output_devices[i];
                    save_runtime_preferences();
                    reopen_audio_output_device(true);
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        int audio_latency_ms = (int)g_audio_latency_ms;
        if (ImGui::SliderInt("Target Audio Latency (ms)", &audio_latency_ms, 20, 250)) {
            g_audio_latency_ms = (uint32_t)audio_latency_ms;
            recompute_audio_targets();
            reset_audio_output_buffer(true);
            save_runtime_preferences();
        }
        if (ImGui::Button("Refresh Devices")) {
            refresh_audio_output_devices();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reconnect Audio")) {
            reopen_audio_output_device(true);
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear Audio Buffer")) {
            reset_audio_output_buffer(true);
        }
        ImGui::Text("Requested Output: %s", current_audio_output_device_label());
        ImGui::Text("Active Output: %s", g_audio_active_device_name.empty() ? "Unavailable" : g_audio_active_device_name.c_str());
        if (!current_audio_output_device_available()) {
            ImGui::TextDisabled("Requested device is unavailable. Using the default output when possible.");
        }
        ImGui::Text("Ring Buffer: %u / %u samples", current_audio_ring_fill_samples(), current_audio_ring_capacity());
        ImGui::Text("Device: %u Hz, device buffer %u, start %u, low-water %u",
                    g_audio_device_sample_rate,
                    g_audio_device_buffer_samples,
                    g_audio_start_threshold,
                    g_audio_low_watermark);
        if (effective_speed_percent() != 100) {
            ImGui::TextDisabled("Audio output pauses while Speed %% is not 100.");
        }
        if (ImGui::Button("Reset Audio")) {
            reset_runtime_audio_defaults();
        }

        ImGui::Separator();
        ImGui::TextDisabled("Savestates");
        int savestate_slot = g_savestate_slot + 1;
        if (ImGui::SliderInt("Active Slot", &savestate_slot, 1, GB_SAVESTATE_SLOT_COUNT)) {
            g_savestate_slot = savestate_slot - 1;
            save_runtime_preferences();
        }
        if (ImGui::Button("Save State (F5)")) {
            save_savestate_slot(g_registered_ctx, g_savestate_slot);
        }
        ImGui::SameLine();
        if (ImGui::Button("Load State (F8)")) {
            load_savestate_slot(g_registered_ctx, g_savestate_slot);
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete Slot")) {
            delete_savestate_slot(g_registered_ctx, g_savestate_slot);
        }
        {
            std::string savestate_path;
            const bool slot_exists = savestate_slot_exists(g_registered_ctx, g_savestate_slot, &savestate_path);
            ImGui::Text("Slot File: %s", savestate_path.empty() ? "(unavailable)" : savestate_path.c_str());
            ImGui::Text("Slot Status: %s", slot_exists ? "Present" : "Empty");
        }
        if (!g_savestate_status.empty()) {
            ImGui::TextWrapped("%s", g_savestate_status.c_str());
        }

        ImGui::Separator();
        ImGui::TextDisabled("Controls");
        if (!g_controller_name.empty()) {
            ImGui::Text("Detected Controller: %s", g_controller_name.c_str());
            ImGui::TextDisabled("Profile: %s", controller_type_name(g_controller_type));
        } else {
#if defined(__ANDROID__)
            ImGui::TextDisabled("No controller detected. Controller mappings will apply when one is connected.");
#else
            ImGui::TextDisabled("No controller detected.");
#endif
        }
        render_binding_editor("Keyboard Gameplay",
                              GB_CAPTURE_DEVICE_KEYBOARD,
                              g_keyboard_bindings,
                              GB_INPUT_ACTION_RIGHT,
                              GB_INPUT_ACTION_FAST_FORWARD);
        ImGui::Spacing();
        render_binding_editor("Controller Gameplay",
                              GB_CAPTURE_DEVICE_CONTROLLER,
                              g_controller_bindings,
                              GB_INPUT_ACTION_RIGHT,
                              GB_INPUT_ACTION_FAST_FORWARD);
        ImGui::Spacing();
        render_binding_editor("Keyboard Shortcuts",
                              GB_CAPTURE_DEVICE_KEYBOARD,
                              g_keyboard_bindings,
                              GB_INPUT_ACTION_FAST_FORWARD,
                              GB_INPUT_ACTION_COUNT);
        ImGui::Spacing();
        render_binding_editor("Controller Shortcuts",
                              GB_CAPTURE_DEVICE_CONTROLLER,
                              g_controller_bindings,
                              GB_INPUT_ACTION_FAST_FORWARD,
                              GB_INPUT_ACTION_COUNT);
        if (g_binding_capture_active) {
            ImGui::Separator();
            ImGui::TextDisabled("Waiting for %s input for %s slot %d",
                                g_binding_capture_device == GB_CAPTURE_DEVICE_KEYBOARD ? "keyboard" : "controller",
                                g_input_action_names[g_binding_capture_action],
                                g_binding_capture_slot + 1);
            if (ImGui::Button("Cancel Capture")) {
                cancel_binding_capture();
            }
        }
        if (ImGui::Button("Reset Controls")) {
            reset_runtime_control_defaults();
        }
        ImGui::TextDisabled("Shortcut bindings can also control fast forward, max speed, savestates, overlay, mute, and the menu.");
        ImGui::BulletText("%s", controller_menu_hint_text());
#if defined(__ANDROID__)
        ImGui::BulletText("Android Back / Escape: Settings Menu");
#else
        ImGui::BulletText("Escape: Settings Menu");
#endif

        if (has_interpreter_activity(g_registered_ctx)) {
            const GBInterpreterHotspot* hotspot = &g_registered_ctx->interpreter_hotspots[0];
            ImGui::Separator();
            ImGui::TextDisabled("Interpreter Fallback");
            ImGui::Text("Frame Fallbacks: %u", g_registered_ctx->frame_dispatch_fallbacks);
            ImGui::Text("Interp Entries: %llu",
                        (unsigned long long)g_registered_ctx->total_interpreter_entries);
            ImGui::Text("Total Fallbacks: %llu",
                        (unsigned long long)g_registered_ctx->total_dispatch_fallbacks);
            ImGui::Text("Interp Instr: frame %llu total %llu",
                        (unsigned long long)g_registered_ctx->frame_interpreter_instructions,
                        (unsigned long long)g_registered_ctx->total_interpreter_instructions);
            ImGui::Text("Interp Cycles: frame %llu total %llu",
                        (unsigned long long)g_registered_ctx->frame_interpreter_cycles,
                        (unsigned long long)g_registered_ctx->total_interpreter_cycles);
            if (hotspot->valid && hotspot->entries > 0) {
                ImGui::Text("Top Hotspot: %03X:%04X", hotspot->bank, hotspot->addr);
                ImGui::Text("Hits %llu Instr %llu Cycles %llu",
                            (unsigned long long)hotspot->entries,
                            (unsigned long long)hotspot->instructions,
                            (unsigned long long)hotspot->cycles);
            }
            if (g_registered_ctx->has_unimplemented_interpreter_opcode) {
                ImGui::Text("Coverage Gap: %02X at %03X:%04X",
                            g_registered_ctx->last_unimplemented_opcode,
                            g_registered_ctx->last_unimplemented_bank,
                            g_registered_ctx->last_unimplemented_addr);
            }
        }

        ImGui::EndChild();
        ImGui::Separator();

        const int footer_button_count = g_launcher_return_enabled ? 3 : 2;
        const float footer_button_width =
            (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * (footer_button_count - 1)) /
            (float)footer_button_count;
        if (ImGui::Button("Reset Display", ImVec2(footer_button_width, 0.0f))) {
            reset_runtime_display_defaults();
        }

        if (g_launcher_return_enabled) {
            ImGui::SameLine();
            if (ImGui::Button("Return to Launcher", ImVec2(footer_button_width, 0.0f))) {
                g_exit_action = GB_PLATFORM_EXIT_RETURN_TO_LAUNCHER;
                SDL_Event quit_event;
                quit_event.type = SDL_QUIT;
                SDL_PushEvent(&quit_event);
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Quit", ImVec2(footer_button_width, 0.0f))) {
            g_exit_action = GB_PLATFORM_EXIT_QUIT;
            SDL_Event quit_event;
            quit_event.type = SDL_QUIT;
            SDL_PushEvent(&quit_event);
        }
        ImGui::End();
        ImGui::PopStyleVar(6);
    } else if (g_show_overlay) {
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.35f);
        if (ImGui::Begin("Overlay", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav)) {
            update_audio_stats_from_ring();
            ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
            ImGui::Text("%s", overlay_menu_hint_text());
            ImGui::Text("%s", overlay_visibility_hint_text());
            ImGui::Text("Viewport: %d x %d (%s)",
                        g_game_viewport.w,
                        g_game_viewport.h,
                        g_render_scaling_mode_names[(int)g_render_scaling_mode]);
            if (g_timing_frame_count > 0) {
                float avg_render = (float)(g_timing_render_total / g_timing_frame_count);
                float avg_vsync = (float)(g_timing_vsync_total / g_timing_frame_count);
                ImGui::Text("Render: %.1fms, VSync: %.1fms", avg_render, avg_vsync);
                ImGui::Text("Upload: %.2f Compose: %.2f Present: %.2f",
                            (float)g_last_timing.upload_ms,
                            (float)g_last_timing.compose_ms,
                            (float)g_last_timing.present_ms);
                ImGui::Text("AudioBuf: %u/%u, Underruns:%u",
                            current_audio_ring_fill_samples(),
                            current_audio_ring_capacity(),
                            current_audio_underruns());
                ImGui::Text("Smooth Slow Frames: %s", g_smooth_lcd_transitions ? "On" : "Off");
                ImGui::TextUnformatted(audio_stats_get_summary());
                if (has_interpreter_activity(g_registered_ctx)) {
                    const GBInterpreterHotspot* hotspot = &g_registered_ctx->interpreter_hotspots[0];
                    ImGui::Separator();
                    ImGui::Text("Interp: frame %u total %llu entries %llu",
                                g_registered_ctx->frame_dispatch_fallbacks,
                                (unsigned long long)g_registered_ctx->total_dispatch_fallbacks,
                                (unsigned long long)g_registered_ctx->total_interpreter_entries);
                    if (hotspot->valid && hotspot->entries > 0) {
                        ImGui::Text("Hotspot: %03X:%04X (%llu hits)",
                                    hotspot->bank,
                                    hotspot->addr,
                                    (unsigned long long)hotspot->entries);
                    }
                }
            }
            ImGui::End();
        }
    }

    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), g_renderer);
    g_last_timing.compose_ms = sdl_now_ms() - compose_start_ms;

    double present_start_ms = sdl_now_ms();
    SDL_RenderPresent(g_renderer);
    g_last_timing.present_ms = sdl_now_ms() - present_start_ms;
    g_last_timing.total_render_ms = sdl_now_ms() - total_render_start_ms;
    g_timing_render_total += g_last_timing.total_render_ms;
}

/* ============================================================================
 * Platform Functions
 * ========================================================================== */

void gb_platform_shutdown(void) {
    close_input_record_file();
    close_audio_output_device();
    g_audio_output_devices.clear();

    clear_controller_state();
    g_binding_capture_active = false;
    g_binding_capture_device = GB_CAPTURE_DEVICE_NONE;
    
    if (ImGui::GetCurrentContext() != NULL) {
        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
    }

    if (g_texture) {
        SDL_DestroyTexture(g_texture);
        g_texture = NULL;
    }
    if (g_renderer) {
        SDL_DestroyRenderer(g_renderer);
        g_renderer = NULL;
    }
    if (g_window) {
        SDL_DestroyWindow(g_window);
        g_window = NULL;
    }
    g_registered_ctx = NULL;
    g_app_suspended = false;
    g_renderer_reset_pending = false;
    SDL_Quit();
}

void gb_platform_set_benchmark_mode(bool enabled) {
    g_benchmark_mode = enabled;
}

void gb_platform_set_launcher_return_enabled(bool enabled) {
    g_launcher_return_enabled = enabled;
}

GBPlatformExitAction gb_platform_get_exit_action(void) {
    return g_exit_action;
}

/* ============================================================================
 * Audio - Simple Push Mode with SDL_QueueAudio
 * ========================================================================== */

#define AUDIO_SAMPLE_RATE 44100

/* 
 * Audio: Simple circular buffer with SDL callback
 * The callback pulls samples at exactly 44100 Hz.
 * The emulator pushes samples as they're generated.
 * A large buffer provides tolerance for timing variations.
 */
#define AUDIO_RING_SIZE 16384  /* ~370ms buffer - plenty of headroom */
static int16_t g_audio_ring[AUDIO_RING_SIZE * 2];  /* Stereo */
static std::atomic<uint32_t> g_audio_write_pos{0};
static std::atomic<uint32_t> g_audio_read_pos{0};

/* Debug counters */
static uint32_t g_audio_samples_written = 0;
static uint32_t g_audio_underruns = 0;

static bool audio_subsystem_available(void) {
    return (SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) != 0;
}

static void refresh_audio_output_devices(void) {
    g_audio_output_devices.clear();
    if (!audio_subsystem_available()) {
        return;
    }

    const int count = SDL_GetNumAudioDevices(0);
    for (int i = 0; i < count; i++) {
        const char* name = SDL_GetAudioDeviceName(i, 0);
        if (name && name[0]) {
            g_audio_output_devices.emplace_back(name);
        }
    }
}

static int current_audio_output_device_index(void) {
    if (g_audio_target_device_name.empty()) {
        return 0;
    }

    for (size_t i = 0; i < g_audio_output_devices.size(); i++) {
        if (g_audio_output_devices[i] == g_audio_target_device_name) {
            return (int)i + 1;
        }
    }

    return 0;
}

static const char* current_audio_output_device_label(void) {
    if (!g_audio_target_device_name.empty()) {
        return g_audio_target_device_name.c_str();
    }
    return "System Default";
}

static bool current_audio_output_device_available(void) {
    return g_audio_target_device_name.empty() || current_audio_output_device_index() != 0;
}

static void close_audio_output_device(void) {
    if (g_audio_device) {
        SDL_CloseAudioDevice(g_audio_device);
        g_audio_device = 0;
    }
    g_audio_started = false;
    g_audio_start_threshold = 0;
    g_audio_low_watermark = 0;
    g_audio_device_sample_rate = AUDIO_SAMPLE_RATE;
    g_audio_device_buffer_samples = 0;
    g_audio_active_device_name.clear();
}

static bool open_audio_output_device(const char* device_name, bool preserve_stats) {
    SDL_AudioSpec want, have;
    memset(&want, 0, sizeof(want));
    want.freq = AUDIO_SAMPLE_RATE;
    want.format = AUDIO_S16LSB;
    want.channels = 2;
    want.samples = 2048;
    want.callback = sdl_audio_callback;
    want.userdata = NULL;

    g_audio_device = SDL_OpenAudioDevice(device_name, 0, &want, &have, 0);
    if (g_audio_device == 0) {
        return false;
    }

    g_audio_device_sample_rate = (uint32_t)have.freq;
    g_audio_device_buffer_samples = (uint32_t)have.samples;
    g_audio_active_device_name = (device_name && device_name[0]) ? device_name : "System Default";
    recompute_audio_targets();
    reset_audio_output_buffer(preserve_stats);
    fprintf(stderr,
            "[SDL] Audio initialized: %d Hz, %d channels, device '%s', buffer %d samples, target latency %u ms\n",
            have.freq,
            have.channels,
            g_audio_active_device_name.c_str(),
            have.samples,
            g_audio_latency_ms);
    return true;
}

static bool reopen_audio_output_device(bool preserve_stats) {
    if (!audio_subsystem_available()) {
        return false;
    }

    const std::string requested_device = g_audio_target_device_name;
    close_audio_output_device();

    if (!requested_device.empty() && open_audio_output_device(requested_device.c_str(), preserve_stats)) {
        return true;
    }

    if (!requested_device.empty()) {
        fprintf(stderr,
                "[SDL] Failed to open requested audio device '%s': %s. Falling back to default output.\n",
                requested_device.c_str(),
                SDL_GetError());
    }

    if (open_audio_output_device(NULL, preserve_stats)) {
        return true;
    }

    fprintf(stderr, "[SDL] Failed to open audio: %s\n", SDL_GetError());
    return false;
}

static uint32_t current_audio_underruns(void) {
    return g_audio_underruns;
}

static uint32_t current_audio_ring_fill_samples(void) {
    return audio_ring_fill_samples();
}

static uint32_t current_audio_ring_capacity(void) {
    return AUDIO_RING_SIZE;
}

static bool audio_output_should_run(void) {
    return g_audio_device != 0 &&
           g_audio_output_enabled &&
           !g_benchmark_mode &&
           !g_app_suspended &&
           effective_speed_percent() == 100;
}

static uint32_t audio_ring_fill_samples(void) {
    uint32_t write_pos = g_audio_write_pos.load(std::memory_order_acquire);
    uint32_t read_pos = g_audio_read_pos.load(std::memory_order_acquire);
    return (write_pos >= read_pos) ? (write_pos - read_pos) : (AUDIO_RING_SIZE - read_pos + write_pos);
}

static void update_audio_stats_from_ring(void) {
    audio_stats_update_buffer(audio_ring_fill_samples(), AUDIO_RING_SIZE, g_audio_device_sample_rate);
}

static void recompute_audio_targets(void) {
    const uint32_t sample_rate = g_audio_device_sample_rate ? g_audio_device_sample_rate : AUDIO_SAMPLE_RATE;
    const uint32_t device_buffer = g_audio_device_buffer_samples ? g_audio_device_buffer_samples : 512u;
    uint32_t target = (uint32_t)(((uint64_t)sample_rate * (uint64_t)g_audio_latency_ms + 999ull) / 1000ull);

    if (target < device_buffer) {
        target = device_buffer;
    }
    if (target == 0) {
        target = 1;
    }
    if (target > (AUDIO_RING_SIZE / 2)) {
        target = AUDIO_RING_SIZE / 2;
    }

    g_audio_start_threshold = target;
    g_audio_low_watermark = target / 2;
    if (g_audio_low_watermark == 0) {
        g_audio_low_watermark = 1;
    }
}

static void clear_audio_ring_buffer_locked(void) {
    g_audio_write_pos.store(0, std::memory_order_relaxed);
    g_audio_read_pos.store(0, std::memory_order_relaxed);
    memset(g_audio_ring, 0, sizeof(g_audio_ring));
    g_audio_started = false;
    update_audio_stats_from_ring();
}

static void refresh_audio_device_pause_state(void) {
    if (!g_audio_device) {
        return;
    }

    const bool paused = !audio_output_should_run() || !g_audio_started;
    SDL_PauseAudioDevice(g_audio_device, paused ? 1 : 0);
}

static void reset_audio_output_buffer(bool preserve_stats) {
    if (g_audio_device) {
        SDL_LockAudioDevice(g_audio_device);
        clear_audio_ring_buffer_locked();
        refresh_audio_device_pause_state();
        SDL_UnlockAudioDevice(g_audio_device);
    } else {
        clear_audio_ring_buffer_locked();
    }

    if (!preserve_stats) {
        g_audio_underruns = 0;
        g_audio_samples_written = 0;
    }
}

/* SDL callback - pulls samples from ring buffer */
static void sdl_audio_callback(void* userdata, Uint8* stream, int len) {
    (void)userdata;
    int16_t* out = (int16_t*)stream;
    int samples_needed = len / 4;  /* Stereo 16-bit = 4 bytes per sample */
    
    uint32_t write_pos = g_audio_write_pos.load(std::memory_order_acquire);
    uint32_t read_pos = g_audio_read_pos.load(std::memory_order_relaxed);
    
    for (int i = 0; i < samples_needed; i++) {
        if (read_pos != write_pos) {
            /* Have data - copy it */
            int32_t left = g_audio_ring[read_pos * 2];
            int32_t right = g_audio_ring[read_pos * 2 + 1];
            if (g_audio_muted || g_audio_volume_percent == 0) {
                left = 0;
                right = 0;
            } else if (g_audio_volume_percent != 100) {
                left = (left * (int32_t)g_audio_volume_percent) / 100;
                right = (right * (int32_t)g_audio_volume_percent) / 100;
                if (left < -32768) left = -32768;
                if (left > 32767) left = 32767;
                if (right < -32768) right = -32768;
                if (right > 32767) right = 32767;
            }
            out[i * 2] = (int16_t)left;
            out[i * 2 + 1] = (int16_t)right;
            read_pos = (read_pos + 1) % AUDIO_RING_SIZE;
        } else {
            /* Underrun - output silence */
            out[i * 2] = 0;
            out[i * 2 + 1] = 0;
            g_audio_underruns++;
            audio_stats_underrun();
        }
    }
    
    g_audio_read_pos.store(read_pos, std::memory_order_release);
}

static void on_audio_sample(GBContext* ctx, int16_t left, int16_t right) {
    (void)ctx;
    if (!audio_output_should_run()) return;
    
    uint32_t write_pos = g_audio_write_pos.load(std::memory_order_relaxed);
    uint32_t next_write = (write_pos + 1) % AUDIO_RING_SIZE;
    
    /* If buffer is full, drop this sample (prevents blocking) */
    if (next_write == g_audio_read_pos.load(std::memory_order_acquire)) {
        audio_stats_samples_dropped(1);
        return;  /* Drop sample */
    }
    
    g_audio_ring[write_pos * 2] = left;
    g_audio_ring[write_pos * 2 + 1] = right;
    g_audio_write_pos.store(next_write, std::memory_order_release);
    g_audio_samples_written++;
    audio_stats_samples_queued(1);

    if (!g_audio_started && audio_ring_fill_samples() >= g_audio_start_threshold) {
        g_audio_started = true;
        refresh_audio_device_pause_state();
    }
}

bool gb_platform_init(int scale) {
    g_benchmark_mode = g_benchmark_mode || env_flag_enabled("GBRECOMP_BENCHMARK");
    g_scale = scale;
    if (g_scale < 1) g_scale = 1;
    if (g_scale > 8) g_scale = 8;
    g_windowed_width = GB_SCREEN_WIDTH * g_scale;
    g_windowed_height = GB_SCREEN_HEIGHT * g_scale;
    g_game_viewport = {0, 0, g_windowed_width, g_windowed_height};
    g_exit_action = GB_PLATFORM_EXIT_QUIT;
    g_frame_count = 0;
    g_manual_joypad_buttons = 0xFF;
    g_manual_joypad_dpad = 0xFF;
    g_script_joypad_buttons = 0xFF;
    g_script_joypad_dpad = 0xFF;
    g_fullscreen = platform_default_fullscreen();
    g_app_suspended = false;
    g_renderer_reset_pending = false;
    g_show_overlay = false;
    g_max_speed_mode = false;
    g_binding_capture_active = false;
    g_binding_capture_device = GB_CAPTURE_DEVICE_NONE;
    update_effective_joypad_state();
    g_last_guest_framebuffer_valid = false;
    g_present_count = 0;
    g_last_timing = {};

    if (g_benchmark_mode) {
        if (!SDL_getenv("SDL_VIDEODRIVER")) {
            SDL_setenv("SDL_VIDEODRIVER", "dummy", 0);
        }
        if (!SDL_getenv("SDL_AUDIODRIVER")) {
            SDL_setenv("SDL_AUDIODRIVER", "dummy", 0);
        }
        if (SDL_Init(SDL_INIT_TIMER) < 0) {
            fprintf(stderr, "[SDL] SDL_Init failed in benchmark mode: %s\n", SDL_GetError());
            return false;
        }
        load_runtime_preferences();
        g_last_frame_time = SDL_GetTicks();
        return true;
    }
    
    fprintf(stderr, "[SDL] Initializing SDL...\n");
#if defined(__ANDROID__)
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "0");
#endif
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0) {
        fprintf(stderr, "[SDL] SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    load_runtime_preferences();
    fprintf(stderr, "[SDL] SDL initialized.\n");

#if defined(__ANDROID__)
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
#endif

    clear_controller_state();
    open_first_available_controller();
    refresh_audio_output_devices();
    
    reopen_audio_output_device(false);
    
    fprintf(stderr, "[SDL] Creating window...\n");
    g_window = SDL_CreateWindow(
        "GameBoy Recompiled",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        g_windowed_width,
        g_windowed_height,
        SDL_WINDOW_SHOWN |
        (g_fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : SDL_WINDOW_RESIZABLE)
    );
    
    if (!g_window) {
        fprintf(stderr, "[SDL] SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }
    fprintf(stderr, "[SDL] Window created.\n");
    SDL_SetWindowMinimumSize(g_window, GB_SCREEN_WIDTH, GB_SCREEN_HEIGHT);
    
    fprintf(stderr, "[SDL] Creating renderer...\n");
    /* 
     * NO VSync - we use wall-clock timing to run at exactly 59.7 FPS.
     * This is essential for non-60Hz monitors (like 100Hz).
     */
    g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED);
        
    if (!g_renderer) {
        fprintf(stderr, "[SDL] Hardware renderer failed, trying software fallback...\n");
        g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_SOFTWARE);
    }
        
    if (!g_renderer) {
        fprintf(stderr, "[SDL] SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(g_window);
        SDL_Quit();
        return false;
    }
    
    update_render_filter();

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplSDL2_InitForSDLRenderer(g_window, g_renderer);
    ImGui_ImplSDLRenderer2_Init(g_renderer);

    if (!recreate_streaming_texture()) {
        SDL_DestroyRenderer(g_renderer);
        SDL_DestroyWindow(g_window);
        SDL_Quit();
        return false;
    }
    update_game_viewport();
    
    g_last_frame_time = SDL_GetTicks();
    
    return true;
}

static bool handle_binding_capture_event(const SDL_Event* event) {
    if (!g_binding_capture_active || !event) {
        return false;
    }

    switch (event->type) {
        case SDL_KEYDOWN:
            if (g_binding_capture_device != GB_CAPTURE_DEVICE_KEYBOARD || event->key.repeat != 0) {
                return true;
            }
            if (event->key.keysym.scancode == SDL_SCANCODE_ESCAPE ||
                event->key.keysym.scancode == SDL_SCANCODE_AC_BACK) {
                cancel_binding_capture();
                return true;
            }
            commit_binding_capture(make_binding(GB_INPUT_BINDING_KEY, event->key.keysym.scancode));
            return true;

        case SDL_CONTROLLERBUTTONDOWN:
            if (g_binding_capture_device != GB_CAPTURE_DEVICE_CONTROLLER) {
                return true;
            }
            if (event->cbutton.button == SDL_CONTROLLER_BUTTON_GUIDE) {
                cancel_binding_capture();
                return true;
            }
            commit_binding_capture(make_binding(GB_INPUT_BINDING_CONTROLLER_BUTTON, event->cbutton.button));
            return true;

        case SDL_CONTROLLERAXISMOTION:
            if (g_binding_capture_device != GB_CAPTURE_DEVICE_CONTROLLER) {
                return true;
            }
            if (event->caxis.value >= 16000) {
                commit_binding_capture(make_binding(GB_INPUT_BINDING_CONTROLLER_AXIS_POSITIVE, event->caxis.axis));
            } else if (event->caxis.value <= -16000) {
                commit_binding_capture(make_binding(GB_INPUT_BINDING_CONTROLLER_AXIS_NEGATIVE, event->caxis.axis));
            }
            return true;

        case SDL_KEYUP:
        case SDL_CONTROLLERBUTTONUP:
            return true;

        default:
            return false;
    }
}

static bool handle_runtime_event(const SDL_Event* event, GBContext* ctx) {
    if (!event) {
        return true;
    }

    const uint8_t joyp = ctx ? ctx->io[0x00] : 0xFF;
    const bool dpad_selected = !(joyp & 0x10);
    const bool buttons_selected = !(joyp & 0x20);

    if (event->type == SDL_QUIT) {
        return false;
    }
    if (event->type == SDL_WINDOWEVENT &&
        event->window.event == SDL_WINDOWEVENT_CLOSE &&
        (!g_window || event->window.windowID == SDL_GetWindowID(g_window))) {
        return false;
    }
    if (handle_binding_capture_event(event)) {
        return true;
    }

    switch (event->type) {
        case SDL_APP_WILLENTERBACKGROUND:
            set_app_suspended(true);
            break;

        case SDL_APP_DIDENTERFOREGROUND:
            set_app_suspended(false);
            break;

        case SDL_RENDER_TARGETS_RESET:
        case SDL_RENDER_DEVICE_RESET:
            g_renderer_reset_pending = true;
            recreate_streaming_texture();
            break;

        case SDL_CONTROLLERDEVICEADDED:
            if (!g_controller) {
                open_controller_index(event->cdevice.which);
            }
            break;

        case SDL_CONTROLLERDEVICEREMOVED:
            if (g_controller && active_controller_instance_id() == event->cdevice.which) {
                clear_controller_state();
                open_first_available_controller();
            }
            break;

        case SDL_CONTROLLERDEVICEREMAPPED:
            if (g_controller && active_controller_instance_id() == event->cdevice.which) {
                refresh_controller_profile();
            }
            break;

        case SDL_AUDIODEVICEADDED:
            if (!event->adevice.iscapture) {
                refresh_audio_output_devices();
            }
            break;

        case SDL_AUDIODEVICEREMOVED:
            if (!event->adevice.iscapture) {
                refresh_audio_output_devices();
                if (g_audio_device != 0 && event->adevice.which == g_audio_device) {
                    reopen_audio_output_device(true);
                }
            }
            break;

        case SDL_CONTROLLERAXISMOTION: {
            uint8_t previous_dpad = g_joypad_dpad;
            uint8_t previous_buttons = g_joypad_buttons;

            if (event->caxis.axis >= 0 && event->caxis.axis < SDL_CONTROLLER_AXIS_MAX) {
                g_controller_axis_values[event->caxis.axis] = event->caxis.value;
            }
            update_controller_axis_binding_state();
            update_effective_joypad_state();
            update_runtime_action_state(ctx);
            if (ctx &&
                input_transition_creates_press(previous_dpad,
                                              previous_buttons,
                                              g_joypad_dpad,
                                              g_joypad_buttons,
                                              dpad_selected,
                                              buttons_selected)) {
                request_joypad_interrupt(ctx);
            }
            break;
        }

        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP: {
            const bool pressed = (event->type == SDL_CONTROLLERBUTTONDOWN);
            uint8_t previous_dpad = g_joypad_dpad;
            uint8_t previous_buttons = g_joypad_buttons;

            if (event->cbutton.button == SDL_CONTROLLER_BUTTON_GUIDE) {
                if (pressed) {
                    g_show_menu = !g_show_menu;
                }
                return true;
            }

            for (int action = 0; action < GB_INPUT_ACTION_COUNT; action++) {
                for (int slot = 0; slot < 2; slot++) {
                    if (binding_matches_controller_button(g_controller_bindings[action][slot], event->cbutton.button)) {
                        g_controller_button_binding_pressed[action][slot] = pressed;
                    }
                }
            }

            update_effective_joypad_state();
            update_runtime_action_state(ctx);
            if (ctx &&
                pressed &&
                input_transition_creates_press(previous_dpad,
                                              previous_buttons,
                                              g_joypad_dpad,
                                              g_joypad_buttons,
                                              dpad_selected,
                                              buttons_selected)) {
                request_joypad_interrupt(ctx);
            }
            break;
        }

        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            const bool pressed = (event->type == SDL_KEYDOWN);
            uint8_t previous_dpad = g_joypad_dpad;
            uint8_t previous_buttons = g_joypad_buttons;

            switch (event->key.keysym.scancode) {
                case SDL_SCANCODE_ESCAPE:
                case SDL_SCANCODE_AC_BACK:
                    if (pressed && event->key.repeat == 0) {
                        g_show_menu = !g_show_menu;
                    }
                    return true;

                default:
                    break;
            }

            for (int action = 0; action < GB_INPUT_ACTION_COUNT; action++) {
                for (int slot = 0; slot < 2; slot++) {
                    if (binding_matches_scancode(g_keyboard_bindings[action][slot], event->key.keysym.scancode)) {
                        g_keyboard_binding_pressed[action][slot] = pressed;
                    }
                }
            }

            update_effective_joypad_state();
            update_runtime_action_state(ctx);
            if (ctx &&
                pressed &&
                event->key.repeat == 0 &&
                input_transition_creates_press(previous_dpad,
                                              previous_buttons,
                                              g_joypad_dpad,
                                              g_joypad_buttons,
                                              dpad_selected,
                                              buttons_selected)) {
                request_joypad_interrupt(ctx);
            }
            break;
        }

        case SDL_WINDOWEVENT:
            if (event->window.event == SDL_WINDOWEVENT_RESIZED ||
                event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                if (!g_fullscreen) {
                    g_windowed_width = event->window.data1;
                    g_windowed_height = event->window.data2;
                }
                update_game_viewport();
            }
            break;

        default:
            break;
    }

    return true;
}

bool gb_platform_poll_events(GBContext* ctx) {
    SDL_Event event;

    if (!g_benchmark_mode) {
        while (SDL_PollEvent(&event)) {
            if (ImGui::GetCurrentContext() != NULL) {
                ImGui_ImplSDL2_ProcessEvent(&event);
            }
            if (!handle_runtime_event(&event, ctx)) {
                return false;
            }
        }

        while (g_app_suspended) {
            if (!SDL_WaitEvent(&event)) {
                continue;
            }
            if (ImGui::GetCurrentContext() != NULL) {
                ImGui_ImplSDL2_ProcessEvent(&event);
            }
            if (!handle_runtime_event(&event, ctx)) {
                return false;
            }
        }
    }
    
    /* Handle Automation Inputs */
    uint8_t previous_script_dpad = g_script_joypad_dpad;
    uint8_t previous_script_buttons = g_script_joypad_buttons;
    g_script_joypad_dpad = 0xFF;
    g_script_joypad_buttons = 0xFF;

    uint64_t current_cycles = ctx ? ctx->cycles : 0;
    for (int i = 0; i < g_script_count; i++) {
        ScriptEntry* e = &g_input_script[i];
        bool active = false;
        if (e->anchor == SCRIPT_ANCHOR_CYCLE) {
            active = current_cycles >= e->start &&
                     current_cycles < (e->start + e->duration);
        } else {
            uint64_t current_frame = (uint64_t)g_frame_count;
            active = current_frame >= e->start &&
                     current_frame < (e->start + e->duration);
        }
        if (active) {
            g_script_joypad_dpad &= e->dpad;
            g_script_joypad_buttons &= e->buttons;
        }
    }

    uint8_t new_script_dpad = (uint8_t)(previous_script_dpad & (uint8_t)(~g_script_joypad_dpad) & 0x0F);
    uint8_t new_script_buttons = (uint8_t)(previous_script_buttons & (uint8_t)(~g_script_joypad_buttons) & 0x0F);
    uint8_t joyp = ctx ? ctx->io[0x00] : 0xFF;
    bool dpad_selected = !(joyp & 0x10);
    bool buttons_selected = !(joyp & 0x20);
    if (ctx && ((new_script_dpad && dpad_selected) || (new_script_buttons && buttons_selected))) {
        request_joypad_interrupt(ctx);
    }

    update_effective_joypad_state();
    record_manual_input_state(current_cycles);

    return true;
}



void gb_platform_render_frame(const uint32_t* framebuffer) {
    render_frame_internal(framebuffer, true);
}

void gb_platform_present_framebuffer(const uint32_t* framebuffer) {
    const uint32_t* stable_framebuffer = g_last_guest_framebuffer_valid ? g_last_guest_framebuffer : framebuffer;
    render_frame_internal(stable_framebuffer, false);
}

void gb_platform_render_lcd_off_frame(void) {
    ensure_lcd_off_framebuffer();
    const uint32_t* stable_framebuffer = g_last_guest_framebuffer_valid ? g_last_guest_framebuffer : g_lcd_off_framebuffer;
    render_frame_internal(stable_framebuffer, false);
}

void gb_platform_get_timing_info(GBPlatformTimingInfo* out) {
    if (!out) return;
    *out = g_last_timing;
}

uint8_t gb_platform_get_joypad(void) {
    /* Return combined state based on P1 register selection */
    /* Caller should AND with the appropriate selection bits */
    return g_joypad_buttons & g_joypad_dpad;
}

void gb_platform_vsync(uint32_t frame_cycles) {
    if (g_benchmark_mode || g_app_suspended) {
        g_last_timing.pacing_cycles = (frame_cycles > 0) ? frame_cycles : 70224u;
        g_last_timing.pacing_ms = 0.0;
        return;
    }
    /* 
     * Frame pacing: Run at the DMG frame cadence derived from 70224 cycles
     * at 4194304 Hz, and ease off sleeping when audio fill is too low.
     *
     * Each call accounts for the frame that just completed. Keep an
     * accumulated wall-clock target and advance it before waiting so the
     * current frame's cycle count is what determines the current sleep.
     */
    static uint64_t next_frame_time = 0;
    static uint64_t frame_remainder = 0;
    const uint64_t gb_frame_cycles = (frame_cycles > 0) ? (uint64_t)frame_cycles : 70224ull;
    const uint64_t gb_cpu_hz = 4194304;
    uint64_t freq = SDL_GetPerformanceFrequency();
    uint64_t now = SDL_GetPerformanceCounter();
    uint32_t speed_percent = (uint32_t)effective_speed_percent();
    uint64_t frame_ticks_num = (freq * gb_frame_cycles * 100ull) + frame_remainder;
    uint64_t frame_ticks_den = gb_cpu_hz * (uint64_t)speed_percent;
    uint64_t frame_ticks = frame_ticks_num / frame_ticks_den;
    double pacing_start_ms = sdl_now_ms();

    if (frame_ticks == 0) {
        frame_ticks = 1;
    }

    if (next_frame_time == 0) {
        next_frame_time = now;
    }

    frame_remainder = frame_ticks_num % frame_ticks_den;
    next_frame_time += frame_ticks;
    uint64_t target_frame_time = next_frame_time;

    uint32_t audio_fill = audio_ring_fill_samples();
    bool audio_starved = audio_output_should_run() && g_audio_started && audio_fill < g_audio_low_watermark;

    if (!audio_starved && now < target_frame_time) {
        for (;;) {
            uint64_t wait_ticks = target_frame_time - now;
            uint32_t wait_us = (uint32_t)((wait_ticks * 1000000) / freq);

            /*
             * SDL_Delay() can oversleep by several milliseconds on desktop OSes.
             * Sleep in small chunks, then busy-wait the tail for stable pacing.
             */
            if (wait_us > 4000) {
                SDL_Delay((wait_us / 1000) - 2);
            } else if (wait_us > 1500) {
                SDL_Delay(1);
            } else {
                break;
            }

            now = SDL_GetPerformanceCounter();
            if (now >= target_frame_time) {
                break;
            }
        }

        while (SDL_GetPerformanceCounter() < target_frame_time) {
            /* spin */
        }
        now = SDL_GetPerformanceCounter();
    }

    /* If we fell behind by more than 3 frames, reset (don't try to catch up) */
    uint64_t max_frame_lag = frame_ticks * 3;
    if (now > target_frame_time + max_frame_lag) {
        next_frame_time = now;
        frame_remainder = 0;
    }
    
    update_audio_stats_from_ring();
    audio_stats_tick(SDL_GetTicks64());
    g_last_timing.pacing_cycles = (uint32_t)gb_frame_cycles;
    g_last_timing.pacing_ms = sdl_now_ms() - pacing_start_ms;
    g_timing_vsync_total += g_last_timing.pacing_ms;
    g_timing_frame_count++;
    g_last_frame_time = SDL_GetTicks();
}

bool gb_platform_get_smooth_lcd_transitions(void) {
    return g_smooth_lcd_transitions;
}

void gb_platform_set_smooth_lcd_transitions(bool enabled) {
    g_smooth_lcd_transitions = enabled;
}

void gb_platform_set_title(const char* title) {
    if (g_window && !g_benchmark_mode) {
        SDL_SetWindowTitle(g_window, title);
    }
}

/* ============================================================================
 * Save Data
 * ========================================================================== */

static void sdl_get_persistent_path(char* buffer, size_t size, const char* rom_name, const char* extension) {
    const std::string base_name = extract_path_leaf(rom_name);
    const std::string suffix = (extension && extension[0]) ? extension : ".sav";
    const std::string filename = base_name + suffix;

#if defined(__ANDROID__)
    const std::string resolved = resolve_writable_path(filename.c_str(), base_name.c_str());
    snprintf(buffer, size, "%s", resolved.c_str());
#else
    char* base_path = SDL_GetBasePath();
    if (base_path) {
        fs::path resolved = fs::path(base_path) / filename;
        SDL_free(base_path);
        snprintf(buffer, size, "%s", resolved.lexically_normal().string().c_str());
    } else {
        const std::string resolved = resolve_writable_path(filename.c_str(), base_name.c_str());
        snprintf(buffer, size, "%s", resolved.c_str());
    }
#endif
}

static void sdl_get_save_path(char* buffer, size_t size, const char* rom_name) {
    sdl_get_persistent_path(buffer, size, rom_name, ".sav");
}

static void sdl_get_rtc_path(char* buffer, size_t size, const char* rom_name) {
    sdl_get_persistent_path(buffer, size, rom_name, ".rtc");
}

static void context_storage_name(const GBContext* ctx, char* buffer, size_t size) {
    if (!buffer || size == 0) {
        return;
    }

    buffer[0] = '\0';
    if (ctx && ctx->save_id[0]) {
        snprintf(buffer, size, "%s", ctx->save_id);
        return;
    }

    if (ctx && ctx->rom && ctx->rom_size > 0x143) {
        char title[17];
        memset(title, 0, sizeof(title));
        memcpy(title, &ctx->rom[0x134], 16);
        for (int i = 0; i < 16; i++) {
            if (title[i] == 0 || title[i] < 32 || title[i] > 126) {
                title[i] = 0;
            }
        }
        if (title[0]) {
            snprintf(buffer, size, "%s", title);
            return;
        }
    }

    snprintf(buffer, size, "game");
}

static void sdl_get_savestate_path(char* buffer, size_t size, const GBContext* ctx, int slot) {
    char storage_name[64];
    char extension[32];
    context_storage_name(ctx, storage_name, sizeof(storage_name));
    if (slot < 0) {
        slot = 0;
    }
    if (slot >= GB_SAVESTATE_SLOT_COUNT) {
        slot = GB_SAVESTATE_SLOT_COUNT - 1;
    }
    snprintf(extension, sizeof(extension), ".state%d", slot + 1);
    sdl_get_persistent_path(buffer, size, storage_name, extension);
}

static bool savestate_slot_exists(const GBContext* ctx, int slot, std::string* out_path) {
    char filename[512];
    sdl_get_savestate_path(filename, sizeof(filename), ctx, slot);
    if (out_path) {
        *out_path = filename;
    }

    std::error_code ec;
    return fs::exists(fs::path(filename), ec) && !ec;
}

static bool save_savestate_slot(GBContext* ctx, int slot) {
    if (!ctx) {
        set_savestate_status("Save", slot, false, "No active game context");
        return false;
    }

    char filename[512];
    sdl_get_savestate_path(filename, sizeof(filename), ctx, slot);
    const bool success = gb_context_save_state_file(ctx, filename);
    set_savestate_status("Save", slot, success, filename);
    return success;
}

static bool load_savestate_slot(GBContext* ctx, int slot) {
    if (!ctx) {
        set_savestate_status("Load", slot, false, "No active game context");
        return false;
    }

    char filename[512];
    sdl_get_savestate_path(filename, sizeof(filename), ctx, slot);
    const bool success = gb_context_load_state_file(ctx, filename);
    if (success) {
        reset_audio_output_buffer(true);
        g_last_guest_framebuffer_valid = false;
        g_present_count = ctx->completed_frames;
        g_last_frame_time = SDL_GetTicks();
    }
    set_savestate_status("Load", slot, success, filename);
    return success;
}

static bool delete_savestate_slot(GBContext* ctx, int slot) {
    if (!ctx) {
        set_savestate_status("Delete", slot, false, "No active game context");
        return false;
    }

    char filename[512];
    sdl_get_savestate_path(filename, sizeof(filename), ctx, slot);
    std::error_code ec;
    const bool removed = fs::remove(fs::path(filename), ec);
    if (!removed && !ec) {
        set_savestate_status("Delete", slot, false, "Slot file does not exist");
        return false;
    }
    set_savestate_status("Delete", slot, removed && !ec, filename);
    return removed && !ec;
}

static bool sdl_load_battery_ram(GBContext* ctx, const char* rom_name, void* data, size_t size) {
    (void)ctx;
    char filename[512];
    sdl_get_save_path(filename, sizeof(filename), rom_name);
    
    FILE* f = fopen(filename, "rb");
    if (!f) return false;
    
    size_t read = fread(data, 1, size, f);
    fclose(f);
    
    return read == size;
}

static bool sdl_save_battery_ram(GBContext* ctx, const char* rom_name, const void* data, size_t size) {
    (void)ctx;
    char filename[512];
    sdl_get_save_path(filename, sizeof(filename), rom_name);
    
    FILE* f = fopen(filename, "wb");
    if (!f) return false;
    
    size_t written = fwrite(data, 1, size, f);
    fclose(f);
    
    return written == size;
}

static bool sdl_load_rtc_data(GBContext* ctx, const char* rom_name, void* data, size_t size) {
    (void)ctx;
    char filename[512];
    sdl_get_rtc_path(filename, sizeof(filename), rom_name);

    FILE* f = fopen(filename, "rb");
    if (!f) return false;

    size_t read = fread(data, 1, size, f);
    fclose(f);

    return read == size;
}

static bool sdl_save_rtc_data(GBContext* ctx, const char* rom_name, const void* data, size_t size) {
    (void)ctx;
    char filename[512];
    sdl_get_rtc_path(filename, sizeof(filename), rom_name);

    FILE* f = fopen(filename, "wb");
    if (!f) return false;

    size_t written = fwrite(data, 1, size, f);
    fclose(f);

    return written == size;
}

void gb_platform_register_context(GBContext* ctx) {
    g_registered_ctx = ctx;
    GBPlatformCallbacks callbacks = {
        .on_audio_sample = on_audio_sample,
        .load_battery_ram = sdl_load_battery_ram,
        .save_battery_ram = sdl_save_battery_ram,
        .load_rtc_data = sdl_load_rtc_data,
        .save_rtc_data = sdl_save_rtc_data
    };
    gb_set_platform_callbacks(ctx, &callbacks);
}

#else  /* !GB_HAS_SDL2 */

/* Stub implementations when SDL2 is not available */

bool gb_platform_init(int scale) {
    (void)scale;
    return false;
}

void gb_platform_shutdown(void) {}

void gb_platform_set_launcher_return_enabled(bool enabled) {
    (void)enabled;
}

GBPlatformExitAction gb_platform_get_exit_action(void) {
    return GB_PLATFORM_EXIT_QUIT;
}

bool gb_platform_poll_events(GBContext* ctx) {
    (void)ctx;
    return true;
}

void gb_platform_set_input_script(const char* script) { (void)script; }

void gb_platform_set_input_record_file(const char* path) { (void)path; }
void gb_platform_set_benchmark_mode(bool enabled) { (void)enabled; }

void gb_platform_render_frame(const uint32_t* framebuffer) {
    (void)framebuffer;
}

void gb_platform_present_framebuffer(const uint32_t* framebuffer) {
    (void)framebuffer;
}

void gb_platform_render_lcd_off_frame(void) {}

void gb_platform_get_timing_info(GBPlatformTimingInfo* out) {
    if (!out) return;
    *out = GBPlatformTimingInfo{};
}

uint8_t gb_platform_get_joypad(void) {
    return 0xFF;
}

void gb_platform_vsync(uint32_t frame_cycles) { (void)frame_cycles; }

bool gb_platform_get_smooth_lcd_transitions(void) { return false; }

void gb_platform_set_smooth_lcd_transitions(bool enabled) { (void)enabled; }

void gb_platform_set_title(const char* title) {
    (void)title;
}

void gb_platform_set_dump_frames(const char* frames) { (void)frames; }

void gb_platform_set_screenshot_prefix(const char* prefix) { (void)prefix; }

void gb_platform_register_context(GBContext* ctx) { (void)ctx; }

#endif /* GB_HAS_SDL2 */
