/**
 * @file platform_sdl.h
 * @brief SDL2 platform layer for GameBoy runtime
 */

#ifndef GB_PLATFORM_SDL_H
#define GB_PLATFORM_SDL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Joypad state variables
extern uint8_t g_joypad_buttons;
extern uint8_t g_joypad_dpad;

typedef struct GBContext GBContext;
typedef struct GBPortFrame GBPortFrame;

typedef struct GBPlatformTimingInfo {
    double upload_ms;
    double compose_ms;
    double present_ms;
    double total_render_ms;
    double pacing_ms;
    uint32_t pacing_cycles;
} GBPlatformTimingInfo;

typedef enum GBPlatformExitAction {
    GB_PLATFORM_EXIT_QUIT = 0,
    GB_PLATFORM_EXIT_RETURN_TO_LAUNCHER = 1,
} GBPlatformExitAction;

enum {
    GB_PLATFORM_RETURN_TO_LAUNCHER_EXIT_CODE = 64,
};

/**
 * @brief Initialize SDL2 platform (window, renderer)
 * @param scale Initial window scale preset (1-8)
 * @return true on success
 */
bool gb_platform_init(int scale);

/**
 * @brief Register context with platform (sets up callbacks)
 */
void gb_platform_register_context(GBContext* ctx);

/**
 * @brief Submit renderer-independent native port commands for the next present.
 */
void gb_platform_submit_port_frame(void* user, const GBPortFrame* frame);

/**
 * @brief Enable a headless benchmark mode with no host pacing or UI work.
 */
void gb_platform_set_benchmark_mode(bool enabled);

/**
 * @brief Enable or disable the launcher return action in the runtime menu.
 */
void gb_platform_set_launcher_return_enabled(bool enabled);

/**
 * @brief Report how the SDL runtime requested to exit the current game.
 */
GBPlatformExitAction gb_platform_get_exit_action(void);

/**
 * @brief Shutdown SDL2 platform
 */
void gb_platform_shutdown(void);

/**
 * @brief Process SDL events
 * @return false if quit requested
 */
bool gb_platform_poll_events(GBContext* ctx);

/**
 * @brief Render frame to screen
 */
void gb_platform_render_frame(const uint32_t* framebuffer);

/**
 * @brief Present a framebuffer without advancing guest-frame counters
 */
void gb_platform_present_framebuffer(const uint32_t* framebuffer);

/**
 * @brief Render a blank LCD-off presentation frame without advancing guest frame counters
 */
void gb_platform_render_lcd_off_frame(void);

/**
 * @brief Set input automation script.
 *
 * Legacy entries use "frame:buttons:duration". Cycle-anchored entries use
 * "c<cycle>:buttons:<duration_cycles>".
 */
/**
 * @return true when the complete script is valid and installed. A malformed
 * script is rejected atomically and leaves no scripted input installed.
 */
bool gb_platform_set_input_script(const char* script);

/**
 * @brief Override the directory used for battery, RTC, and save-state files.
 * @return true when path is an existing directory. NULL or empty clears it.
 */
bool gb_platform_set_persistence_dir(const char* path);

/**
 * @brief Deterministic persistence failure modes for repository tests.
 *
 * Production callers should leave this disabled. The selected fault is
 * consumed by the next matching battery/RTC transaction.
 */
typedef enum GBPersistenceTestFault {
    GB_PERSISTENCE_TEST_FAULT_NONE = 0,
    GB_PERSISTENCE_TEST_FAULT_SHORT_WRITE = 1,
    GB_PERSISTENCE_TEST_FAULT_FULL_DISK = 2,
    GB_PERSISTENCE_TEST_FAULT_INTERRUPTION = 3,
    GB_PERSISTENCE_TEST_FAULT_TRUNCATION = 4,
} GBPersistenceTestFault;

typedef enum GBPersistenceTestTarget {
    GB_PERSISTENCE_TEST_TARGET_BATTERY = 0,
    GB_PERSISTENCE_TEST_TARGET_RTC = 1,
} GBPersistenceTestTarget;

void gb_platform_test_inject_persistence_fault(
    GBPersistenceTestTarget target,
    GBPersistenceTestFault fault);

/**
 * @brief Record live keyboard/controller input to a replayable script file
 */
void gb_platform_set_input_record_file(const char* path);

/**
 * @brief Set frames to dump screenshots (format: "frame1,frame2,...")
 */
void gb_platform_set_dump_frames(const char* frames);

/**
 * @brief Dump every host present that occurs while one of the selected guest
 * frames is current (format: "frame1,frame2,...")
 */
void gb_platform_set_dump_present_frames(const char* frames);

/**
 * @brief Set filename prefix for screenshots
 */
void gb_platform_set_screenshot_prefix(const char* prefix);

/**
 * @brief Get timing data captured during the most recent render/pacing pass
 */
void gb_platform_get_timing_info(GBPlatformTimingInfo* out);

/**
 * @brief Get joypad state
 * @return Joypad byte (active low)
 */
uint8_t gb_platform_get_joypad(void);

/**
 * @brief Wait for vsync / frame timing
 */
void gb_platform_vsync(uint32_t frame_cycles);

/**
 * @brief Query whether slow-frame presentation smoothing is enabled
 */
bool gb_platform_get_smooth_lcd_transitions(void);

/**
 * @brief Override whether slow-frame presentation smoothing is enabled
 */
void gb_platform_set_smooth_lcd_transitions(bool enabled);

/**
 * @brief Set window title
 */
void gb_platform_set_title(const char* title);

#ifdef GBRT_ENABLE_TEST_HOOKS
typedef struct GBAudioStressResult {
    uint64_t frames_enqueued;
    uint64_t write_publications;
    uint64_t underruns;
} GBAudioStressResult;

/**
 * @brief Exercise the SDL audio producer/callback boundary without a device.
 */
bool gb_platform_test_audio_concurrency(uint32_t frames,
                                        GBAudioStressResult* out_result);
#endif

#ifdef __cplusplus
}
#endif

#endif /* GB_PLATFORM_SDL_H */
