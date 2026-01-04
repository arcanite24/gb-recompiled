/**
 * @file platform_sdl.c
 * @brief SDL2 platform implementation for GameBoy runtime
 */

#include "platform_sdl.h"
#include "ppu.h"
#include "gbrt_debug.h"

#ifdef GB_HAS_SDL2
#include <SDL.h>

/* ============================================================================
 * SDL State
 * ========================================================================== */

static SDL_Window* g_window = NULL;
static SDL_Renderer* g_renderer = NULL;
static SDL_Texture* g_texture = NULL;
static int g_scale = 3;
static uint32_t g_last_frame_time = 0;

/* Joypad state - exported for gbrt.c to access */
uint8_t g_joypad_buttons = 0xFF;  /* Active low: Start, Select, B, A */
uint8_t g_joypad_dpad = 0xFF;     /* Active low: Down, Up, Left, Right */

/* ============================================================================
 * Platform Functions
 * ========================================================================== */

bool gb_platform_init(int scale) {
    g_scale = scale;
    if (g_scale < 1) g_scale = 1;
    if (g_scale > 8) g_scale = 8;
    
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0) {
        return false;
    }
    
    g_window = SDL_CreateWindow(
        "GameBoy Recompiled",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        GB_SCREEN_WIDTH * g_scale,
        GB_SCREEN_HEIGHT * g_scale,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    
    if (!g_window) {
        SDL_Quit();
        return false;
    }
    
    g_renderer = SDL_CreateRenderer(g_window, -1, 
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    
    if (!g_renderer) {
        SDL_DestroyWindow(g_window);
        SDL_Quit();
        return false;
    }
    
    /* Set scaling hint */
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    
    g_texture = SDL_CreateTexture(
        g_renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        GB_SCREEN_WIDTH,
        GB_SCREEN_HEIGHT
    );
    
    if (!g_texture) {
        SDL_DestroyRenderer(g_renderer);
        SDL_DestroyWindow(g_window);
        SDL_Quit();
        return false;
    }
    
    g_last_frame_time = SDL_GetTicks();
    
    return true;
}

void gb_platform_shutdown(void) {
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
    SDL_Quit();
}

bool gb_platform_poll_events(GBContext* ctx) {
    (void)ctx;
    SDL_Event event;
    
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                return false;
                
            case SDL_KEYDOWN:
            case SDL_KEYUP: {
                bool pressed = (event.type == SDL_KEYDOWN);
                
                switch (event.key.keysym.scancode) {
                    /* D-pad */
                    case SDL_SCANCODE_UP:
                    case SDL_SCANCODE_W:
                        if (pressed) g_joypad_dpad &= ~0x04;
                        else g_joypad_dpad |= 0x04;
                        break;
                    case SDL_SCANCODE_DOWN:
                    case SDL_SCANCODE_S:
                        if (pressed) g_joypad_dpad &= ~0x08;
                        else g_joypad_dpad |= 0x08;
                        break;
                    case SDL_SCANCODE_LEFT:
                    case SDL_SCANCODE_A:
                        if (pressed) g_joypad_dpad &= ~0x02;
                        else g_joypad_dpad |= 0x02;
                        break;
                    case SDL_SCANCODE_RIGHT:
                    case SDL_SCANCODE_D:
                        if (pressed) g_joypad_dpad &= ~0x01;
                        else g_joypad_dpad |= 0x01;
                        break;
                    
                    /* Buttons */
                    case SDL_SCANCODE_Z:
                    case SDL_SCANCODE_J:
                        if (pressed) g_joypad_buttons &= ~0x01;  /* A */
                        else g_joypad_buttons |= 0x01;
                        break;
                    case SDL_SCANCODE_X:
                    case SDL_SCANCODE_K:
                        if (pressed) g_joypad_buttons &= ~0x02;  /* B */
                        else g_joypad_buttons |= 0x02;
                        break;
                    case SDL_SCANCODE_RSHIFT:
                    case SDL_SCANCODE_BACKSPACE:
                        if (pressed) g_joypad_buttons &= ~0x04;  /* Select */
                        else g_joypad_buttons |= 0x04;
                        break;
                    case SDL_SCANCODE_RETURN:
                        if (pressed) g_joypad_buttons &= ~0x08;  /* Start */
                        else g_joypad_buttons |= 0x08;
                        break;
                    
                    case SDL_SCANCODE_ESCAPE:
                        return false;
                        
                    default:
                        break;
                }
                break;
            }
            
            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                    /* Handle resize */
                }
                break;
        }
    }
    
    return true;
}

static int g_frame_count = 0;

void gb_platform_render_frame(const uint32_t* framebuffer) {
    if (!g_texture || !g_renderer || !framebuffer) {
        DBG_FRAME("Platform render_frame: SKIPPED (null: texture=%d, renderer=%d, fb=%d)",
                  g_texture == NULL, g_renderer == NULL, framebuffer == NULL);
        return;
    }
    
    g_frame_count++;
    
    /* Debug: check framebuffer content on first few frames */
    if (g_frame_count <= 3) {
        /* Check if framebuffer has any non-white pixels */
        bool has_content = false;
        uint32_t white = 0xFFE0F8D0;  /* DMG palette color 0 */
        for (int i = 0; i < GB_SCREEN_WIDTH * GB_SCREEN_HEIGHT; i++) {
            if (framebuffer[i] != white) {
                has_content = true;
                break;
            }
        }
        DBG_FRAME("Platform frame %d - has_content=%d, first_pixel=0x%08X",
                  g_frame_count, has_content, framebuffer[0]);
    }
    
    if (g_frame_count % 60 == 0) {
        char title[64];
        snprintf(title, sizeof(title), "GameBoy Recompiled - Frame %d", g_frame_count);
        SDL_SetWindowTitle(g_window, title);
    }
    
    /* Update texture */
    SDL_UpdateTexture(g_texture, NULL, framebuffer, GB_SCREEN_WIDTH * sizeof(uint32_t));
    
    /* Clear and render */
    SDL_RenderClear(g_renderer);
    SDL_RenderCopy(g_renderer, g_texture, NULL, NULL);
    SDL_RenderPresent(g_renderer);
}

uint8_t gb_platform_get_joypad(void) {
    /* Return combined state based on P1 register selection */
    /* Caller should AND with the appropriate selection bits */
    return g_joypad_buttons & g_joypad_dpad;
}

void gb_platform_vsync(void) {
    /* Target 59.7 FPS */
    const uint32_t frame_time_ms = 16;  /* ~60 FPS */
    uint32_t current_time = SDL_GetTicks();
    uint32_t elapsed = current_time - g_last_frame_time;
    
    if (elapsed < frame_time_ms) {
        SDL_Delay(frame_time_ms - elapsed);
    }
    
    g_last_frame_time = SDL_GetTicks();
}

void gb_platform_set_title(const char* title) {
    if (g_window) {
        SDL_SetWindowTitle(g_window, title);
    }
}

#else  /* !GB_HAS_SDL2 */

/* Stub implementations when SDL2 is not available */

bool gb_platform_init(int scale) {
    (void)scale;
    return false;
}

void gb_platform_shutdown(void) {}

bool gb_platform_poll_events(GBContext* ctx) {
    (void)ctx;
    return true;
}

void gb_platform_render_frame(const uint32_t* framebuffer) {
    (void)framebuffer;
}

uint8_t gb_platform_get_joypad(void) {
    return 0xFF;
}

void gb_platform_vsync(void) {}

void gb_platform_set_title(const char* title) {
    (void)title;
}

#endif /* GB_HAS_SDL2 */
