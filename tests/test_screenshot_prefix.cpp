#include "platform_sdl.h"

#include <SDL.h>
#include <array>
#include <cstdio>
#include <filesystem>
#include <string>
#include <unistd.h>

int main() {
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("gbrecomp-long-screenshot-prefix-" + std::to_string((long long)getpid())) /
        "validator-evidence-segment-with-a-deliberately-long-name";
    std::error_code error;
    std::filesystem::remove_all(directory.parent_path(), error);
    std::filesystem::create_directories(directory, error);
    if (error) {
        return 2;
    }

    gb_platform_set_benchmark_mode(true);
    if (!gb_platform_init(1)) {
        std::filesystem::remove_all(directory.parent_path(), error);
        return 2;
    }
    const std::string prefix = (directory / "frame").string();
    gb_platform_set_dump_frames("1");
    gb_platform_set_screenshot_prefix(prefix.c_str());
    std::array<uint32_t, 160 * 144> framebuffer = {};
    gb_platform_render_frame(framebuffer.data());
    gb_platform_shutdown();

    const std::filesystem::path expected = directory / "frame_00001.ppm";
    const bool passed = std::filesystem::is_regular_file(expected);
    std::filesystem::remove_all(directory.parent_path(), error);
    if (!passed) {
        std::fputs("long screenshot prefix was truncated\n", stderr);
        return 1;
    }
    return 0;
}
