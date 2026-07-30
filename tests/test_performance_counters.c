#include "gbrt.h"
#include "ppu.h"

#include <stdio.h>

int main(void) {
    GBConfig config = {
        .model = GB_MODEL_DMG,
        .enable_audio = false,
        .enable_serial = false,
        .speed_percent = 100,
    };
    GBContext* ctx = gb_context_create(&config);
    if (!ctx) {
        fprintf(stderr, "failed to create context\n");
        return 1;
    }

    gbrt_reset_performance_counters(ctx);
    ctx->stopped = 0;
    if (gbrt_generated_safepoint(ctx)) {
        fprintf(stderr, "clear stopped state changed at generated safepoint\n");
        gb_context_destroy(ctx);
        return 1;
    }
    gbrt_note_generated_direct_transition(ctx);
    gbrt_note_generated_indirect_dispatch(ctx);
    gbrt_note_generated_generic_read(ctx);
    gbrt_note_generated_specialized_read(ctx);
    gbrt_note_generated_generic_write(ctx);
    gbrt_note_generated_specialized_write(ctx);
    gbrt_note_ppu_tick(ctx, 4);
    gbrt_note_ppu_draw_dot(ctx, false);
    gbrt_note_ppu_draw_dot(ctx, true);
    gbrt_note_ppu_draw_span(ctx, 4);
    gbrt_note_ppu_stable_span(ctx, 8);
    gbrt_note_audio_sample(ctx);
    gbrt_note_dispatch_fallback(ctx, 1, 0x4000);
    gb_tick(ctx, 4);

    ctx->stopped = 1;
    if (!gbrt_generated_safepoint(ctx)) {
        fprintf(stderr, "set stopped state changed at generated safepoint\n");
        gb_context_destroy(ctx);
        return 1;
    }

#ifdef GBRT_ENABLE_PERFORMANCE_COUNTERS
    if (!gbrt_performance_counters_available() ||
        ctx->performance_counters.tick_commits != 1 ||
        ctx->performance_counters.tick_cycles != 4 ||
        ctx->performance_counters.generated_safepoints != 2 ||
        ctx->performance_counters.generated_direct_transitions != 1 ||
        ctx->performance_counters.generated_indirect_dispatches != 1 ||
        ctx->performance_counters.generated_generic_reads != 1 ||
        ctx->performance_counters.generated_specialized_reads != 1 ||
        ctx->performance_counters.generated_generic_writes != 1 ||
        ctx->performance_counters.generated_specialized_writes != 1 ||
        ctx->performance_counters.interpreter_fallbacks != 1 ||
        ctx->performance_counters.ppu_tick_calls != 2 ||
        ctx->performance_counters.ppu_dots != 8 ||
        ctx->performance_counters.ppu_draw_dots != 6 ||
        ctx->performance_counters.ppu_rendered_pixels != 5 ||
        ctx->performance_counters.ppu_stable_spans != 1 ||
        ctx->performance_counters.ppu_stable_span_dots != 8 ||
        ctx->performance_counters.audio_samples != 1 ||
        ctx->performance_counters.tick_cycle_histogram[3] != 1 ||
        ctx->performance_counters.timer_tick_calls != 1 ||
        ctx->performance_counters.timer_tick_cycles != 4 ||
        ctx->performance_counters.interrupt_checks != 1) {
        fprintf(stderr, "enabled performance counters recorded incorrect values\n");
        gbrt_report_performance_counters(ctx);
        gb_context_destroy(ctx);
        return 1;
    }
#else
    const GBPerformanceCounters zero = {0};
    const GBPerformanceCounters* counters = &ctx->performance_counters;
    if (gbrt_performance_counters_available() ||
        counters->tick_commits != zero.tick_commits ||
        counters->tick_cycles != zero.tick_cycles ||
        counters->generated_safepoints != zero.generated_safepoints ||
        counters->generated_direct_transitions != zero.generated_direct_transitions ||
        counters->generated_indirect_dispatches != zero.generated_indirect_dispatches ||
        counters->generated_generic_reads != zero.generated_generic_reads ||
        counters->generated_specialized_reads != zero.generated_specialized_reads ||
        counters->generated_generic_writes != zero.generated_generic_writes ||
        counters->generated_specialized_writes != zero.generated_specialized_writes ||
        counters->interpreter_fallbacks != zero.interpreter_fallbacks ||
        counters->ppu_tick_calls != zero.ppu_tick_calls ||
        counters->ppu_dots != zero.ppu_dots ||
        counters->ppu_draw_dots != zero.ppu_draw_dots ||
        counters->ppu_rendered_pixels != zero.ppu_rendered_pixels ||
        counters->ppu_stable_spans != zero.ppu_stable_spans ||
        counters->ppu_stable_span_dots != zero.ppu_stable_span_dots ||
        counters->audio_samples != zero.audio_samples) {
        fprintf(stderr, "disabled performance counters changed diagnostic state\n");
        gb_context_destroy(ctx);
        return 1;
    }
#endif

    gbrt_reset_performance_counters(ctx);
#ifdef GBRT_ENABLE_PERFORMANCE_COUNTERS
    gbrt_visibility_estimator_enabled = true;
    ctx->stopped = 0;
    ctx->frame_done = 0;
    ctx->ime_pending = 0;
    ctx->halted = 0;
    ctx->halt_bug = 0;
    ctx->stop_mode_active = 0;
    ctx->io[0x07] = 0;
    ctx->last_sync_cycles = ctx->cycles;
    ((GBPPU*)ctx->ppu)->lcdc = 0;

    gbrt_note_generated_direct_transition(ctx);
    gb_tick(ctx, 4);
    (void)gbrt_generated_safepoint(ctx);
    gb_tick(ctx, 4);
    (void)gbrt_generated_safepoint(ctx);
    gbrt_note_generated_generic_read(ctx);
    gb_tick(ctx, 4);
    (void)gbrt_generated_safepoint(ctx);

    if (ctx->performance_counters.region_candidate_units != 1 ||
        ctx->performance_counters.region_estimated_removable_tick_commits != 1 ||
        ctx->performance_counters.region_estimated_removable_safepoints != 1 ||
        ctx->performance_counters.region_reject_visibility != 1 ||
        ctx->performance_counters.visibility_unit_histogram[0] != 1 ||
        ctx->performance_counters.visibility_unit_histogram[2] != 1) {
        fprintf(stderr, "visibility-aware region estimate recorded incorrect values\n");
        gbrt_report_performance_counters(ctx);
        gb_context_destroy(ctx);
        return 1;
    }
    gbrt_visibility_estimator_enabled = false;
#endif
    gb_context_destroy(ctx);
    return 0;
}
