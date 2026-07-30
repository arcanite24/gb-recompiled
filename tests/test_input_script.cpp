#include "platform_sdl.h"

#include "gbrt.h"

#include <cstdint>
#include <cstdio>
#include <string>

static std::string make_cycle_script(unsigned entry_count) {
    std::string script;
    for (unsigned i = 0; i < entry_count; ++i) {
        if (!script.empty()) {
            script += ',';
        }
        script += "c" + std::to_string(i * 16u) + ":A:4";
    }
    return script;
}

int main() {
    const std::string crystal_sized_script = make_cycle_script(512);
    if (!gb_platform_set_input_script(crystal_sized_script.c_str())) {
        std::fputs("a valid 512-entry cycle script was rejected\n", stderr);
        return 1;
    }

    if (gb_platform_set_input_script("c0:A:4,not-an-input-token,c32:B:4")) {
        std::fputs("a partially malformed input script was accepted\n", stderr);
        return 1;
    }

    if (gb_platform_set_input_script("c0:X:4")) {
        std::fputs("an input script with an unknown button was accepted\n", stderr);
        return 1;
    }

    if (gb_platform_set_input_script("c0:A:0")) {
        std::fputs("an input script with a zero duration was accepted\n", stderr);
        return 1;
    }

    GBConfig config = {};
    GBContext* ctx = gb_context_create(&config);
    if (!ctx) {
        return 2;
    }
    if (!gb_platform_set_input_script("p100-120/10:A:4")) {
        gb_context_destroy(ctx);
        std::fputs("a valid periodic cycle input was rejected\n", stderr);
        return 1;
    }
    ctx->total_cycles = 100;
    gb_platform_set_benchmark_mode(true);
    gb_platform_poll_events(ctx);
    const bool first_periodic_press = (gb_platform_get_joypad() & 0x01u) == 0;
    ctx->total_cycles = 104;
    gb_platform_poll_events(ctx);
    const bool periodic_release = (gb_platform_get_joypad() & 0x01u) != 0;
    ctx->total_cycles = 110;
    gb_platform_poll_events(ctx);
    const bool second_periodic_press = (gb_platform_get_joypad() & 0x01u) == 0;
    ctx->total_cycles = 123;
    gb_platform_poll_events(ctx);
    const bool final_periodic_press_duration = (gb_platform_get_joypad() & 0x01u) == 0;
    ctx->total_cycles = 124;
    gb_platform_poll_events(ctx);
    const bool final_periodic_release = (gb_platform_get_joypad() & 0x01u) != 0;
    if (!first_periodic_press || !periodic_release || !second_periodic_press ||
        !final_periodic_press_duration || !final_periodic_release) {
        gb_context_destroy(ctx);
        std::fputs("periodic cycle input did not release and retrigger\n", stderr);
        return 1;
    }

    ctx->cycles = 32;
    ctx->total_cycles = UINT64_C(4294967328);
    if (!gb_platform_set_input_script("c4294967328:A:16")) {
        gb_context_destroy(ctx);
        return 2;
    }
    gb_platform_poll_events(ctx);
    const bool crossed_wrap = (gb_platform_get_joypad() & 0x01u) == 0;
    gb_context_destroy(ctx);
    if (!crossed_wrap) {
        std::fputs("cycle input did not fire after the 32-bit cycle counter wrapped\n", stderr);
        return 1;
    }

    return 0;
}
