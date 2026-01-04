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

typedef struct GBContext GBContext;

/**
 * @brief Initialize SDL2 platform (window, renderer)
 * @param scale Window scale factor (1-4)
 * @return true on success
 */
bool gb_platform_init(int scale);

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
 * @brief Get joypad state
 * @return Joypad byte (active low)
 */
uint8_t gb_platform_get_joypad(void);

/**
 * @brief Wait for vsync / frame timing
 */
void gb_platform_vsync(void);

/**
 * @brief Set window title
 */
void gb_platform_set_title(const char* title);

#ifdef __cplusplus
}
#endif

#endif /* GB_PLATFORM_SDL_H */
