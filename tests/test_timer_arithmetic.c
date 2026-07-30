#include "gbrt.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t rng_state = UINT32_C(0x6D2B79F5);

static uint32_t next_random(void) {
    uint32_t value = rng_state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    rng_state = value;
    return value;
}

static GBContext* make_context(GBModel model, int double_speed) {
    GBConfig config;
    memset(&config, 0, sizeof(config));
    config.model = model;

    GBContext* ctx = gb_context_create(&config);
    if (!ctx) {
        return NULL;
    }

    uint8_t rom[32u * 1024u];
    memset(rom, 0, sizeof(rom));
    if (!gb_context_load_rom(ctx, rom, sizeof(rom))) {
        gb_context_destroy(ctx);
        return NULL;
    }
    gb_context_reset(ctx, true);
    gb_write8(ctx, 0xFF40, 0x00);
    ctx->cgb_double_speed = double_speed ? 1 : 0;
    ctx->cgb_system_cycle_remainder = double_speed ? 1u : 0u;
    return ctx;
}

static int compare_timer_state(const char* label,
                               unsigned step,
                               const GBContext* scalar,
                               const GBContext* arithmetic) {
    if (scalar->div_counter == arithmetic->div_counter &&
        scalar->io[0x04] == arithmetic->io[0x04] &&
        scalar->io[0x05] == arithmetic->io[0x05] &&
        scalar->io[0x06] == arithmetic->io[0x06] &&
        scalar->io[0x07] == arithmetic->io[0x07] &&
        scalar->io[0x0F] == arithmetic->io[0x0F] &&
        scalar->tima_reload_pending == arithmetic->tima_reload_pending &&
        scalar->cycles == arithmetic->cycles &&
        scalar->cgb_system_cycle_remainder ==
            arithmetic->cgb_system_cycle_remainder) {
        return 0;
    }

    fprintf(stderr,
            "%s diverged at step %u:\n"
            "  scalar:     cycles=%" PRIu32 " DIV=%04X/IO%02X TIMA=%02X "
            "TMA=%02X TAC=%02X IF=%02X reload=%02X\n"
            "  arithmetic: cycles=%" PRIu32 " DIV=%04X/IO%02X TIMA=%02X "
            "TMA=%02X TAC=%02X IF=%02X reload=%02X\n",
            label,
            step,
            scalar->cycles,
            scalar->div_counter,
            scalar->io[0x04],
            scalar->io[0x05],
            scalar->io[0x06],
            scalar->io[0x07],
            scalar->io[0x0F],
            scalar->tima_reload_pending,
            arithmetic->cycles,
            arithmetic->div_counter,
            arithmetic->io[0x04],
            arithmetic->io[0x05],
            arithmetic->io[0x06],
            arithmetic->io[0x07],
            arithmetic->io[0x0F],
            arithmetic->tima_reload_pending);
    return 1;
}

static void tick_pair(GBContext* scalar,
                      GBContext* arithmetic,
                      uint32_t cycles) {
    gbrt_force_scalar_timer = true;
    gb_tick(scalar, cycles);
    gbrt_force_scalar_timer = false;
    gb_tick(arithmetic, cycles);
}

static void write_pair(GBContext* scalar,
                       GBContext* arithmetic,
                       uint16_t address,
                       uint8_t value) {
    gb_write8(scalar, address, value);
    gb_write8(arithmetic, address, value);
}

static void set_timer_state(GBContext* ctx,
                            uint16_t div,
                            uint8_t tima,
                            uint8_t tma,
                            uint8_t tac,
                            uint8_t interrupt_flags,
                            uint8_t reload_state) {
    ctx->div_counter = div;
    ctx->io[0x04] = (uint8_t)(div >> 8);
    ctx->io[0x05] = tima;
    ctx->io[0x06] = tma;
    ctx->io[0x07] = tac;
    ctx->io[0x0F] = interrupt_flags;
    ctx->tima_reload_pending = reload_state;
}

static int randomized_spans(GBContext* scalar, GBContext* arithmetic) {
    static const uint8_t reload_states[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x81, 0x82, 0x83, 0x84,
    };

    for (unsigned step = 0; step < 600; ++step) {
        const uint16_t div = (uint16_t)next_random();
        const uint8_t tima = (uint8_t)next_random();
        const uint8_t tma = (uint8_t)next_random();
        const uint8_t tac = (uint8_t)(next_random() & 0x07u);
        const uint8_t interrupt_flags = (uint8_t)(next_random() & 0x1Fu);
        const uint8_t reload_state =
            reload_states[next_random() %
                          (sizeof(reload_states) / sizeof(reload_states[0]))];
        const uint32_t cycles = next_random() % 100001u;

        set_timer_state(scalar,
                        div,
                        tima,
                        tma,
                        tac,
                        interrupt_flags,
                        reload_state);
        set_timer_state(arithmetic,
                        div,
                        tima,
                        tma,
                        tac,
                        interrupt_flags,
                        reload_state);
        tick_pair(scalar, arithmetic, cycles);
        if (compare_timer_state("random span", step, scalar, arithmetic)) {
            return 1;
        }
    }
    return 0;
}

static int interleaved_writes(GBContext* scalar, GBContext* arithmetic) {
    set_timer_state(scalar, 0xFFF0, 0xFC, 0x42, 0x05, 0x00, 0x00);
    set_timer_state(arithmetic, 0xFFF0, 0xFC, 0x42, 0x05, 0x00, 0x00);

    for (unsigned step = 0; step < 12000; ++step) {
        const uint32_t operation = next_random() % 10u;
        if (operation < 6u) {
            tick_pair(scalar, arithmetic, next_random() % 2049u);
        } else {
            static const uint16_t timer_addresses[] = {
                0xFF04, 0xFF05, 0xFF06, 0xFF07,
            };
            const uint16_t address = timer_addresses[operation - 6u];
            write_pair(scalar,
                       arithmetic,
                       address,
                       (uint8_t)next_random());
        }

        if (compare_timer_state("interleaved timer write",
                                step,
                                scalar,
                                arithmetic)) {
            return 1;
        }
    }
    return 0;
}

static int run_model(GBModel model, int double_speed) {
    GBContext* scalar = make_context(model, double_speed);
    GBContext* arithmetic = make_context(model, double_speed);
    if (!scalar || !arithmetic) {
        gb_context_destroy(scalar);
        gb_context_destroy(arithmetic);
        return 2;
    }

    int result = randomized_spans(scalar, arithmetic);
    if (!result) {
        result = interleaved_writes(scalar, arithmetic);
    }

    gbrt_force_scalar_timer = false;
    gb_context_destroy(scalar);
    gb_context_destroy(arithmetic);
    return result;
}

int main(void) {
    int result = run_model(GB_MODEL_DMG, 0);
    if (!result) {
        result = run_model(GB_MODEL_CGB, 1);
    }
    return result;
}
