#include "audio.h"
#include "gbrt.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_CYCLES 32768u
#define EXPECTED_SAMPLE_COUNT 344u
#define EXPECTED_DOUBLE_SPEED_SAMPLE_COUNT 172u
#define EXPECTED_DIRECT_PCM_HASH UINT64_C(0x7AC4A860E16D9C99)
#define EXPECTED_RUNTIME_PCM_HASH UINT64_C(0xC49D47F4A3C907CD)
#define EXPECTED_DOUBLE_SPEED_PCM_HASH UINT64_C(0x3B8DE42C9BFDB4E1)

typedef struct AudioCapture {
    uint64_t hash;
    uint32_t samples;
} AudioCapture;

typedef struct AudioProfile {
    AudioCapture capture;
    void* state;
    size_t state_size;
} AudioProfile;

typedef struct ObserverProfile {
    AudioProfile audio;
    uint32_t observations;
} ObserverProfile;

static AudioCapture g_capture;

static uint64_t fnv1a_byte(uint64_t hash, uint8_t byte) {
    return (hash ^ byte) * UINT64_C(1099511628211);
}

static void capture_audio_sample(GBContext* ctx, int16_t left, int16_t right) {
    (void)ctx;
    const uint16_t left_bits = (uint16_t)left;
    const uint16_t right_bits = (uint16_t)right;
    g_capture.hash = fnv1a_byte(g_capture.hash, (uint8_t)left_bits);
    g_capture.hash = fnv1a_byte(g_capture.hash, (uint8_t)(left_bits >> 8));
    g_capture.hash = fnv1a_byte(g_capture.hash, (uint8_t)right_bits);
    g_capture.hash = fnv1a_byte(g_capture.hash, (uint8_t)(right_bits >> 8));
    g_capture.samples++;
}

static GBContext* make_audio_context(GBModel model,
                                     int double_speed,
                                     uint8_t system_cycle_remainder) {
    GBConfig config;
    memset(&config, 0, sizeof(config));
    config.model = model;
    config.enable_audio = true;

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

    GBPlatformCallbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.on_audio_sample = capture_audio_sample;
    gb_set_platform_callbacks(ctx, &callbacks);

    /* Begin shortly before a DIV-APU falling edge so the gb_tick profile
     * exercises several 512 Hz frame-sequencer events. */
    ctx->div_counter = 0x0F00;
    ctx->io[0x04] = (uint8_t)(ctx->div_counter >> 8);
    ctx->cgb_double_speed = double_speed ? 1 : 0;
    ctx->cgb_system_cycle_remainder = system_cycle_remainder & 1u;

    /* Drive all four channels with deliberately different periods. */
    gb_audio_write(ctx, 0xFF24, 0x77);
    gb_audio_write(ctx, 0xFF25, 0xFF);

    gb_audio_write(ctx, 0xFF10, 0x16);
    gb_audio_write(ctx, 0xFF11, 0x80);
    gb_audio_write(ctx, 0xFF12, 0xF2);
    gb_audio_write(ctx, 0xFF13, 0xC0);
    gb_audio_write(ctx, 0xFF14, 0x87);

    gb_audio_write(ctx, 0xFF16, 0x40);
    gb_audio_write(ctx, 0xFF17, 0xA3);
    gb_audio_write(ctx, 0xFF18, 0x80);
    gb_audio_write(ctx, 0xFF19, 0x86);

    for (uint16_t offset = 0; offset < 16; ++offset) {
        gb_audio_write(ctx,
                       (uint16_t)(0xFF30 + offset),
                       (uint8_t)(0xF0u ^ (uint8_t)(offset * 0x11u)));
    }
    gb_audio_write(ctx, 0xFF1A, 0x80);
    gb_audio_write(ctx, 0xFF1B, 0x00);
    gb_audio_write(ctx, 0xFF1C, 0x20);
    gb_audio_write(ctx, 0xFF1D, 0x20);
    gb_audio_write(ctx, 0xFF1E, 0x87);

    gb_audio_write(ctx, 0xFF20, 0x00);
    gb_audio_write(ctx, 0xFF21, 0x94);
    gb_audio_write(ctx, 0xFF22, 0x15);
    gb_audio_write(ctx, 0xFF23, 0x80);
    return ctx;
}

static int finish_profile(GBContext* ctx, AudioProfile* profile) {
    profile->capture = g_capture;
    profile->state_size = gb_audio_state_size();
    profile->state = malloc(profile->state_size);
    if (!profile->state ||
        !gb_audio_save_state(ctx->apu, profile->state, profile->state_size)) {
        free(profile->state);
        profile->state = NULL;
        gb_context_destroy(ctx);
        return 1;
    }
    gb_context_destroy(ctx);
    return 0;
}

static int run_profile(AudioProfile* profile,
                       uint32_t chunk,
                       int use_runtime_tick,
                       int double_speed,
                       int eager_audio) {
    GBContext* ctx = make_audio_context(double_speed ? GB_MODEL_CGB : GB_MODEL_DMG,
                                        double_speed,
                                        double_speed ? 1u : 0u);
    if (!ctx) {
        return 1;
    }

    g_capture.hash = UINT64_C(14695981039346656037);
    g_capture.samples = 0;
    gbrt_force_eager_audio = eager_audio != 0;
    uint32_t remaining = TEST_CYCLES;
    while (remaining > 0) {
        const uint32_t step = remaining < chunk ? remaining : chunk;
        if (use_runtime_tick) {
            gb_tick(ctx, step);
        } else {
            gb_audio_step(ctx, step);
        }
        remaining -= step;
    }
    if (use_runtime_tick) {
        gbrt_audio_sync(ctx);
    }
    gbrt_force_eager_audio = false;
    return finish_profile(ctx, profile);
}

static int compare_profiles(const char* label,
                            const AudioProfile* coarse,
                            const AudioProfile* fine,
                            uint32_t expected_samples,
                            uint64_t expected_hash) {
    if (coarse->capture.samples != expected_samples ||
        fine->capture.samples != expected_samples) {
        fprintf(stderr,
                "%s sample count mismatch: coarse=%u fine=%u expected=%u\n",
                label,
                coarse->capture.samples,
                fine->capture.samples,
                expected_samples);
        return 1;
    }
    if (coarse->capture.hash != fine->capture.hash) {
        fprintf(stderr,
                "%s PCM hash depends on chunk size: coarse=%016" PRIX64
                " fine=%016" PRIX64 "\n",
                label,
                coarse->capture.hash,
                fine->capture.hash);
        return 1;
    }
    if (coarse->capture.hash != expected_hash) {
        fprintf(stderr,
                "%s PCM hash changed: actual=%016" PRIX64
                " expected=%016" PRIX64 "\n",
                label,
                coarse->capture.hash,
                expected_hash);
        return 1;
    }
    if (coarse->state_size != fine->state_size ||
        memcmp(coarse->state, fine->state, coarse->state_size) != 0) {
        fprintf(stderr, "%s final APU state depends on chunk size\n", label);
        return 1;
    }
    return 0;
}

static int div_reset_clocks_one_edge(void) {
    GBContext* reset = make_audio_context(GB_MODEL_DMG, 0, 0);
    GBContext* natural = make_audio_context(GB_MODEL_DMG, 0, 0);
    if (!reset || !natural) {
        gb_context_destroy(reset);
        gb_context_destroy(natural);
        return 1;
    }

    const size_t state_size = gb_audio_state_size();
    void* reset_state = malloc(state_size);
    void* natural_state = malloc(state_size);
    if (!reset_state || !natural_state) {
        free(reset_state);
        free(natural_state);
        gb_context_destroy(reset);
        gb_context_destroy(natural);
        return 1;
    }

    /* Pan Docs: clearing DIV while bit 4 is high creates one DIV-APU
     * falling edge, not all of the natural edges between old DIV and zero. */
    gb_audio_div_reset(reset->apu, 0x1000, false);
    gb_audio_div_tick(natural->apu, 0x1000, 0x2000, false);
    const int failed =
        !gb_audio_save_state(reset->apu, reset_state, state_size) ||
        !gb_audio_save_state(natural->apu, natural_state, state_size) ||
        memcmp(reset_state, natural_state, state_size) != 0;

    free(reset_state);
    free(natural_state);
    gb_context_destroy(reset);
    gb_context_destroy(natural);
    if (failed) {
        fputs("DIV reset did not clock exactly one DIV-APU edge\n", stderr);
    }
    return failed;
}

static int sample_clock_is_exact(void) {
    GBContext* ctx = make_audio_context(GB_MODEL_DMG, 0, 0);
    if (!ctx) return 1;

    g_capture.hash = UINT64_C(14695981039346656037);
    g_capture.samples = 0;
    gb_audio_step(ctx, UINT32_C(4194304));
    gb_context_destroy(ctx);
    if (g_capture.samples != 44100u) {
        fprintf(stderr,
                "one APU clock second emitted %u samples instead of 44100\n",
                g_capture.samples);
        return 1;
    }
    return 0;
}

static int run_observer_profile(ObserverProfile* profile, int eager_audio) {
    GBContext* ctx = make_audio_context(GB_MODEL_CGB, 0, 0);
    if (!ctx) return 1;

    g_capture.hash = UINT64_C(14695981039346656037);
    g_capture.samples = 0;
    gbrt_force_eager_audio = eager_audio != 0;

    gb_tick(ctx, 37u);
    if (!eager_audio && ctx->audio_pending_cpu_cycles == 0u) {
        fputs("lazy APU did not accumulate sub-sample time\n", stderr);
        gb_context_destroy(ctx);
        gbrt_force_eager_audio = false;
        return 1;
    }
    profile->observations = gb_read8(ctx, 0xFF76);
    if (ctx->audio_pending_cpu_cycles != 0u) {
        fputs("PCM read did not publish pending APU time\n", stderr);
        gb_context_destroy(ctx);
        gbrt_force_eager_audio = false;
        return 1;
    }

    gb_tick(ctx, 41u);
    gb_write8(ctx, 0xFF12, 0xE2);
    if (ctx->audio_pending_cpu_cycles != 0u) {
        fputs("APU register write did not publish pending time\n", stderr);
        gb_context_destroy(ctx);
        gbrt_force_eager_audio = false;
        return 1;
    }

    gb_tick(ctx, 211u);
    gb_write8(ctx, 0xFF04, 0x00);
    if (ctx->audio_pending_cpu_cycles != 0u) {
        fputs("DIV reset did not publish pending APU time\n", stderr);
        gb_context_destroy(ctx);
        gbrt_force_eager_audio = false;
        return 1;
    }

    gb_tick(ctx, 127u);
    profile->observations =
        (profile->observations << 8) | gb_read8(ctx, 0xFF26);
    gbrt_audio_sync(ctx);
    gbrt_force_eager_audio = false;
    return finish_profile(ctx, &profile->audio);
}

static int observers_match_eager_oracle(void) {
    ObserverProfile lazy = {0};
    ObserverProfile eager = {0};
    if (run_observer_profile(&lazy, 0) || run_observer_profile(&eager, 1)) {
        free(lazy.audio.state);
        free(eager.audio.state);
        return 1;
    }

    const int failed =
        lazy.observations != eager.observations ||
        lazy.audio.capture.samples != eager.audio.capture.samples ||
        lazy.audio.capture.hash != eager.audio.capture.hash ||
        lazy.audio.state_size != eager.audio.state_size ||
        memcmp(lazy.audio.state,
               eager.audio.state,
               lazy.audio.state_size) != 0;
    if (failed) {
        fputs("lazy APU observers diverged from eager scheduling\n", stderr);
    }
    free(lazy.audio.state);
    free(eager.audio.state);
    return failed;
}

int main(void) {
    AudioProfile direct_coarse = {0};
    AudioProfile direct_fine = {0};
    AudioProfile halt_coarse = {0};
    AudioProfile halt_fine = {0};
    AudioProfile halt_eager = {0};
    AudioProfile double_coarse = {0};
    AudioProfile double_fine = {0};
    AudioProfile double_eager = {0};
    int result = 0;

    if (run_profile(&direct_coarse, 456, 0, 0, 0) ||
        run_profile(&direct_fine, 1, 0, 0, 0) ||
        run_profile(&halt_coarse, 456, 1, 0, 0) ||
        run_profile(&halt_fine, 4, 1, 0, 0) ||
        run_profile(&halt_eager, 4, 1, 0, 1) ||
        run_profile(&double_coarse, 455, 1, 1, 0) ||
        run_profile(&double_fine, 1, 1, 1, 0) ||
        run_profile(&double_eager, 1, 1, 1, 1)) {
        fputs("failed to construct audio chunk-invariance profile\n", stderr);
        result = 2;
        goto cleanup;
    }

    result |= compare_profiles("direct APU stepping",
                               &direct_coarse,
                               &direct_fine,
                               EXPECTED_SAMPLE_COUNT,
                               EXPECTED_DIRECT_PCM_HASH);
    result |= compare_profiles("HALT-sized runtime stepping",
                               &halt_coarse,
                               &halt_fine,
                               EXPECTED_SAMPLE_COUNT,
                               EXPECTED_RUNTIME_PCM_HASH);
    result |= compare_profiles("lazy versus eager runtime stepping",
                               &halt_fine,
                               &halt_eager,
                               EXPECTED_SAMPLE_COUNT,
                               EXPECTED_RUNTIME_PCM_HASH);
    result |= compare_profiles("CGB double-speed runtime stepping",
                               &double_coarse,
                               &double_fine,
                               EXPECTED_DOUBLE_SPEED_SAMPLE_COUNT,
                               EXPECTED_DOUBLE_SPEED_PCM_HASH);
    result |= compare_profiles("lazy versus eager CGB double-speed stepping",
                               &double_fine,
                               &double_eager,
                               EXPECTED_DOUBLE_SPEED_SAMPLE_COUNT,
                               EXPECTED_DOUBLE_SPEED_PCM_HASH);
    result |= div_reset_clocks_one_edge();
    result |= sample_clock_is_exact();
    result |= observers_match_eager_oracle();

cleanup:
    free(direct_coarse.state);
    free(direct_fine.state);
    free(halt_coarse.state);
    free(halt_fine.state);
    free(halt_eager.state);
    free(double_coarse.state);
    free(double_fine.state);
    free(double_eager.state);
    return result;
}
