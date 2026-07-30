#include "platform_sdl.h"

#include <cinttypes>
#include <cstdlib>
#include <cstring>
#include <cstdio>

int main(int argc, char** argv) {
    uint32_t frames = 500000;
    bool require_batched = true;
    if (argc >= 2) {
        const unsigned long parsed = std::strtoul(argv[1], nullptr, 10);
        if (parsed == 0 || parsed > UINT32_MAX) {
            std::fputs("invalid frame count\n", stderr);
            return 2;
        }
        frames = (uint32_t)parsed;
    }
    if (argc >= 3 && std::strcmp(argv[2], "--allow-unbatched") == 0) {
        require_batched = false;
    }

    GBAudioStressResult result = {};
    if (!gb_platform_test_audio_concurrency(frames, &result)) {
        std::fputs("audio callback concurrency stress failed\n", stderr);
        return 1;
    }
    if (result.frames_enqueued == 0 || result.write_publications == 0 ||
        (require_batched && result.write_publications * 4 >= result.frames_enqueued)) {
        std::fprintf(stderr,
                     "audio writes were not batched: frames=%" PRIu64
                     " publications=%" PRIu64 "\n",
                     result.frames_enqueued,
                     result.write_publications);
        return 1;
    }

    std::printf("frames=%" PRIu64 " publications=%" PRIu64
                " underruns=%" PRIu64 "\n",
                result.frames_enqueued,
                result.write_publications,
                result.underruns);
    return 0;
}
