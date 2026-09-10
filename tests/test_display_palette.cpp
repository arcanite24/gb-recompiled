#include "gbrt.h"
#include "platform_sdl.h"
#include "ppu.h"

#include <array>
#include <cstdio>

int main() {
    GBConfig config = {};
    config.model = GB_MODEL_DMG;
    GBContext* ctx = gb_context_create(&config);
    if (!ctx) return 2;
    GBPPU* ppu = (GBPPU*)ctx->ppu;
    // Tile pixels repeat all four shades, with an identity BGP mapping.
    ppu->bgp = 0xE4;
    for (int row = 0; row < 8; ++row) {
        ctx->vram[row * 2] = 0x55;
        ctx->vram[row * 2 + 1] = 0x33;
    }
    ppu_tick(ppu, ctx, 70224 * 2);
    const uint32_t* src = ppu_get_framebuffer(ppu);
    constexpr int stride = GB_SCREEN_WIDTH + 7;
    std::array<uint32_t, stride * GB_SCREEN_HEIGHT> dst;
    const uint32_t expected[3][4] = {
        {src[0], src[1], src[2], src[3]},
        {0xFFFFFFFF, 0xFFAAAAAA, 0xFF555555, 0xFF000000},
        {0xFFFFB000, 0xFFCB4F0E, 0xFF800000, 0xFF330000},
    };
    for (int i = 1; i < 4; ++i) {
        if (src[i] == src[i - 1]) {
            std::fputs("fixture did not render four distinct PPU shades\n", stderr);
            return 2;
        }
    }
    for (int mode = 0; mode < 3; ++mode) {
        ctx->config.model = mode ? GB_MODEL_CGB : GB_MODEL_DMG;
        ctx->config.cgb_compatibility_mode = mode == 2;
        for (int palette = 0; palette < 3; ++palette) {
            dst.fill(0x12345678);
            gb_platform_test_copy_display_frame(dst.data(), stride * 4, src, palette, ctx);
            for (int y = 0; y < GB_SCREEN_HEIGHT; ++y) {
                for (int x = 0; x < stride; ++x) {
                    uint32_t color = x >= GB_SCREEN_WIDTH ? 0x12345678 :
                        (mode ? src[y * GB_SCREEN_WIDTH + x] : expected[palette][x % 4]);
                    if (dst[y * stride + x] != color) {
                        std::fprintf(stderr, "palette=%d mode=%d at %d,%d: %08X != %08X\n",
                            palette, mode, x, y, dst[y * stride + x], color);
                        return 1;
                    }
                }
            }
        }
    }
    // LCD-off/startup colors and unrelated host pixels share the upload path.
    std::array<uint32_t, GB_FRAMEBUFFER_SIZE> startup;
    startup.fill(0xFFE0F8D0);
    startup[1] = 0xFF123456;
    ctx->config.model = GB_MODEL_DMG;
    ctx->config.cgb_compatibility_mode = false;
    gb_platform_test_copy_display_frame(dst.data(), stride * 4, startup.data(), 2, ctx);
    if (dst[0] != 0xFFFFB000 || dst[1] != startup[1]) {
        std::fputs("startup or unrelated display colors changed incorrectly\n", stderr);
        return 1;
    }
    gb_context_destroy(ctx);
    return 0;
}
