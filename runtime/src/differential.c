#include "gbrt.h"
#include "audio.h"
#include "ppu.h"

#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define DIFF_WRAM_SIZE (0x1000u * 8u)
#define DIFF_VRAM_TOTAL_SIZE (VRAM_SIZE * 2u)
#define DIFF_OAM_SIZE OAM_SIZE
#define DIFF_HRAM_SIZE 0x7Fu
#define DIFF_IO_TOTAL_SIZE 0x81u
#define DIFF_MAX_SCRIPT_ENTRIES 2048

typedef enum {
    GB_DIFF_INPUT_FRAME = 0,
    GB_DIFF_INPUT_CYCLE = 1,
} GBDiffInputAnchor;

typedef struct {
    GBDiffInputAnchor anchor;
    uint64_t start;
    uint64_t duration;
    uint8_t dpad;
    uint8_t buttons;
} GBDiffScriptEntry;

typedef struct {
    GBDiffScriptEntry entries[DIFF_MAX_SCRIPT_ENTRIES];
    size_t count;
    uint64_t frame_index;
} GBDiffInputScript;

static uint8_t gb_diff_pack_flags(const GBContext* ctx) {
    return (ctx->f_z ? 0x80 : 0) |
           (ctx->f_n ? 0x40 : 0) |
           (ctx->f_h ? 0x20 : 0) |
           (ctx->f_c ? 0x10 : 0);
}

static uint16_t gb_diff_current_bank(const GBContext* ctx) {
    return gb_resolve_rom_bank(ctx, ctx->pc);
}

static void gb_diff_read_opcode_bytes(const GBContext* ctx, uint8_t bytes[3]) {
    GBContext* mutable_ctx = (GBContext*)ctx;
    bytes[0] = gb_read8(mutable_ctx, ctx->pc);
    bytes[1] = gb_read8(mutable_ctx, (uint16_t)(ctx->pc + 1));
    bytes[2] = gb_read8(mutable_ctx, (uint16_t)(ctx->pc + 2));
}

static void gb_diff_print_state(FILE* stream, const char* label, const GBContext* ctx) {
    fprintf(stream,
            "[DIFF] %s PC=%03X:%04X SP=%04X "
            "A=%02X B=%02X C=%02X D=%02X E=%02X H=%02X L=%02X F=%02X "
            "IME=%u IME_PENDING=%u HALT=%u STOP=%u STOP_MODE=%u HALT_BUG=%u DS=%u DS_REM=%u "
            "ROM=%03X RAM=%02X WRAM=%u VRAM=%u DMA=%u/%u HDMA=%04X->%04X/%u "
            "CYC=%u FRAME=%u DIV=%04X\n",
            label,
            gb_diff_current_bank(ctx),
            ctx->pc,
            ctx->sp,
            ctx->a,
            ctx->b,
            ctx->c,
            ctx->d,
            ctx->e,
            ctx->h,
            ctx->l,
            gb_diff_pack_flags(ctx),
            ctx->ime,
            ctx->ime_pending,
            ctx->halted,
            ctx->stopped,
            ctx->stop_mode_active,
            ctx->halt_bug,
            ctx->cgb_double_speed,
            ctx->cgb_system_cycle_remainder,
            ctx->rom_bank,
            ctx->ram_bank,
            ctx->wram_bank,
            ctx->vram_bank,
            ctx->dma.progress,
            ctx->dma.cycles_remaining,
            ctx->hdma.source,
            ctx->hdma.dest,
            ctx->hdma.blocks_remaining,
            ctx->cycles,
            ctx->frame_cycles,
            ctx->div_counter);
}

static void gb_diff_print_code_bytes(FILE* stream,
                                     const char* label,
                                     const GBContext* ctx,
                                     uint16_t addr,
                                     size_t count) {
    GBContext* mutable_ctx = (GBContext*)ctx;

    fprintf(stream, "[DIFF] %s bytes at %03X:%04X:", label, gb_diff_current_bank(ctx), addr);
    for (size_t i = 0; i < count; i++) {
        fprintf(stream, " %02X", gb_read8(mutable_ctx, (uint16_t)(addr + i)));
    }
    fputc('\n', stream);
}

static void gb_diff_parse_buttons(const char* buttons, uint8_t* dpad, uint8_t* joypad_buttons) {
    *dpad = 0xFF;
    *joypad_buttons = 0xFF;

    if (buttons == NULL) {
        return;
    }

    if (strchr(buttons, 'U')) *dpad &= (uint8_t)~0x04;
    if (strchr(buttons, 'D')) *dpad &= (uint8_t)~0x08;
    if (strchr(buttons, 'L')) *dpad &= (uint8_t)~0x02;
    if (strchr(buttons, 'R')) *dpad &= (uint8_t)~0x01;
    if (strchr(buttons, 'A')) *joypad_buttons &= (uint8_t)~0x01;
    if (strchr(buttons, 'B')) *joypad_buttons &= (uint8_t)~0x02;
    if (strchr(buttons, 'T')) *joypad_buttons &= (uint8_t)~0x04;
    if (strchr(buttons, 'S')) *joypad_buttons &= (uint8_t)~0x08;
}

static char* gb_diff_trim_ascii(char* text) {
    while (text && *text && isspace((unsigned char)*text)) {
        text++;
    }
    if (!text || !*text) {
        return text;
    }

    char* end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = '\0';
    return text;
}

static bool gb_diff_parse_input_token(char* token,
                                      GBDiffScriptEntry* entry,
                                      char* buttons,
                                      size_t buttons_size) {
    char* start_text = gb_diff_trim_ascii(token);
    if (!start_text || !*start_text) {
        return false;
    }

    char* first_colon = strchr(start_text, ':');
    if (!first_colon) {
        return false;
    }
    *first_colon = '\0';

    char* buttons_text = gb_diff_trim_ascii(first_colon + 1);
    char* second_colon = strchr(buttons_text, ':');
    if (!second_colon) {
        return false;
    }
    *second_colon = '\0';

    char* duration_text = gb_diff_trim_ascii(second_colon + 1);
    start_text = gb_diff_trim_ascii(start_text);
    buttons_text = gb_diff_trim_ascii(buttons_text);
    duration_text = gb_diff_trim_ascii(duration_text);
    if (!*start_text || !*duration_text) {
        return false;
    }

    entry->anchor = GB_DIFF_INPUT_FRAME;
    if (*start_text == 'c' || *start_text == 'C') {
        entry->anchor = GB_DIFF_INPUT_CYCLE;
        start_text++;
    } else if (*start_text == 'f' || *start_text == 'F') {
        start_text++;
    }
    start_text = gb_diff_trim_ascii(start_text);
    if (!*start_text) {
        return false;
    }

    char* start_end = NULL;
    char* duration_end = NULL;
    entry->start = strtoull(start_text, &start_end, 10);
    entry->duration = strtoull(duration_text, &duration_end, 10);
    if ((start_end && *gb_diff_trim_ascii(start_end)) || (duration_end && *gb_diff_trim_ascii(duration_end))) {
        return false;
    }
    if (entry->duration == 0) {
        return false;
    }

    snprintf(buttons, buttons_size, "%s", buttons_text);
    return true;
}

static bool gb_diff_parse_input_script(const char* script,
                                       GBDiffInputScript* parsed,
                                       char* message,
                                       size_t message_size) {
    memset(parsed, 0, sizeof(*parsed));

    if (script == NULL || *script == '\0') {
        return true;
    }

    char* copy = strdup(script);
    if (copy == NULL) {
        snprintf(message, message_size, "failed to allocate input script copy");
        return false;
    }

    char* token = strtok(copy, ",");
    while (token != NULL) {
        if (parsed->count >= DIFF_MAX_SCRIPT_ENTRIES) {
            snprintf(message, message_size, "input script exceeds %u entries", DIFF_MAX_SCRIPT_ENTRIES);
            free(copy);
            return false;
        }

        char buttons[32] = {0};

        GBDiffScriptEntry* entry = &parsed->entries[parsed->count];
        if (!gb_diff_parse_input_token(token, entry, buttons, sizeof(buttons))) {
            snprintf(message, message_size, "invalid input script token: %s", token);
            free(copy);
            return false;
        }

        parsed->count++;
        gb_diff_parse_buttons(buttons, &entry->dpad, &entry->buttons);

        token = strtok(NULL, ",");
    }

    free(copy);
    return true;
}

static void gb_diff_request_joypad_interrupt(GBContext* ctx,
                                             const GBJoypadState* previous,
                                             const GBJoypadState* current) {
    if (ctx == NULL || previous == NULL || current == NULL) {
        return;
    }

    uint8_t joyp = ctx->io[0x00];
    bool dpad_selected = (joyp & 0x10) == 0;
    bool buttons_selected = (joyp & 0x20) == 0;

    bool pressed = false;
    if (dpad_selected) {
        uint8_t falling = (previous->dpad ^ current->dpad) & previous->dpad & (uint8_t)~current->dpad;
        pressed |= (falling & 0x0F) != 0;
    }
    if (buttons_selected) {
        uint8_t falling = (previous->buttons ^ current->buttons) & previous->buttons & (uint8_t)~current->buttons;
        pressed |= (falling & 0x0F) != 0;
    }

    if (pressed) {
        ctx->io[0x0F] |= 0x10;
        if (ctx->halted) {
            ctx->halted = 0;
        }
    }
}

static void gb_diff_apply_input_state(const GBDiffInputScript* script,
                                      GBContext* ctx,
                                      GBJoypadState* state) {
    GBJoypadState next = {.dpad = 0xFF, .buttons = 0xFF};
    uint64_t current_cycles = ctx ? ctx->total_cycles : 0;

    if (script != NULL) {
        for (size_t i = 0; i < script->count; i++) {
            const GBDiffScriptEntry* entry = &script->entries[i];
            bool active = false;
            if (entry->anchor == GB_DIFF_INPUT_CYCLE) {
                active = current_cycles >= entry->start &&
                         current_cycles < (entry->start + entry->duration);
            } else {
                active = script->frame_index >= entry->start &&
                         script->frame_index < (entry->start + entry->duration);
            }
            if (active) {
                next.dpad &= entry->dpad;
                next.buttons &= entry->buttons;
            }
        }
    }

    gb_diff_request_joypad_interrupt(ctx, state, &next);
    *state = next;
    if (ctx != NULL) {
        ctx->last_joypad = (uint8_t)(state->dpad & state->buttons);
    }
}

static bool gb_diff_compare_region(const char* name,
                                   const uint8_t* generated,
                                   const uint8_t* interpreted,
                                   size_t size,
                                   char* message,
                                   size_t message_size) {
    if (generated == NULL || interpreted == NULL) {
        if (generated != interpreted) {
            snprintf(message, message_size, "%s presence mismatch", name);
            return false;
        }
        return true;
    }

    for (size_t i = 0; i < size; i++) {
        if (generated[i] != interpreted[i]) {
            snprintf(message, message_size,
                     "%s mismatch at offset 0x%zX: %02X != %02X",
                     name, i, generated[i], interpreted[i]);
            return false;
        }
    }

    return true;
}

static bool gb_diff_compare_ppu(const GBContext* generated_ctx,
                                const GBContext* interpreted_ctx,
                                bool compare_memory,
                                char* message,
                                size_t message_size) {
    const GBPPU* generated = (const GBPPU*)generated_ctx->ppu;
    const GBPPU* interpreted = (const GBPPU*)interpreted_ctx->ppu;

    if (generated == NULL || interpreted == NULL) {
        if (generated != interpreted) {
            snprintf(message, message_size, "PPU presence mismatch");
            return false;
        }
        return true;
    }

#define DIFF_PPU_FIELD(field, fmt) \
    do { \
        if (generated->field != interpreted->field) { \
            snprintf(message, message_size, \
                     "PPU field %s mismatch: " fmt " != " fmt, \
                     #field, generated->field, interpreted->field); \
            return false; \
        } \
    } while (0)

    DIFF_PPU_FIELD(lcdc, "%u");
    DIFF_PPU_FIELD(stat, "%u");
    DIFF_PPU_FIELD(scy, "%u");
    DIFF_PPU_FIELD(scx, "%u");
    DIFF_PPU_FIELD(ly, "%u");
    DIFF_PPU_FIELD(lyc, "%u");
    DIFF_PPU_FIELD(dma, "%u");
    DIFF_PPU_FIELD(bgp, "%u");
    DIFF_PPU_FIELD(obp0, "%u");
    DIFF_PPU_FIELD(obp1, "%u");
    DIFF_PPU_FIELD(wy, "%u");
    DIFF_PPU_FIELD(wx, "%u");
    DIFF_PPU_FIELD(bgpi, "%u");
    DIFF_PPU_FIELD(obpi, "%u");
    DIFF_PPU_FIELD(opri, "%u");
    DIFF_PPU_FIELD(stat_irq_state, "%u");
    DIFF_PPU_FIELD(mode, "%u");
    DIFF_PPU_FIELD(mode_cycles, "%u");
    DIFF_PPU_FIELD(window_line, "%u");
    DIFF_PPU_FIELD(window_triggered, "%u");
    DIFF_PPU_FIELD(frame_ready, "%u");

#undef DIFF_PPU_FIELD

    if (!compare_memory) {
        return true;
    }

    if (!gb_diff_compare_region("PPU framebuffer",
                                generated->framebuffer,
                                interpreted->framebuffer,
                                sizeof(generated->framebuffer),
                                message,
                                message_size)) {
        return false;
    }

    if (!gb_diff_compare_region("PPU color framebuffer",
                                (const uint8_t*)generated->color_framebuffer,
                                (const uint8_t*)interpreted->color_framebuffer,
                                sizeof(generated->color_framebuffer),
                                message,
                                message_size)) {
        return false;
    }

    if (!gb_diff_compare_region("PPU BG palette RAM",
                                generated->bg_palette_ram,
                                interpreted->bg_palette_ram,
                                sizeof(generated->bg_palette_ram),
                                message,
                                message_size)) {
        return false;
    }

    if (!gb_diff_compare_region("PPU OBJ palette RAM",
                                generated->obj_palette_ram,
                                interpreted->obj_palette_ram,
                                sizeof(generated->obj_palette_ram),
                                message,
                                message_size)) {
        return false;
    }

    return gb_diff_compare_region("PPU RGB framebuffer",
                                  (const uint8_t*)generated->rgb_framebuffer,
                                  (const uint8_t*)interpreted->rgb_framebuffer,
                                  sizeof(generated->rgb_framebuffer),
                                  message,
                                  message_size);
}

static bool gb_diff_compare_contexts(const GBContext* generated,
                                     const GBContext* interpreted,
                                     bool compare_memory,
                                     char* message,
                                     size_t message_size) {
#define DIFF_FIELD(field, fmt) \
    do { \
        if (generated->field != interpreted->field) { \
            snprintf(message, message_size, \
                     "%s mismatch: " fmt " != " fmt, \
                     #field, generated->field, interpreted->field); \
            return false; \
        } \
    } while (0)

    DIFF_FIELD(a, "%u");
    DIFF_FIELD(b, "%u");
    DIFF_FIELD(c, "%u");
    DIFF_FIELD(d, "%u");
    DIFF_FIELD(e, "%u");
    DIFF_FIELD(h, "%u");
    DIFF_FIELD(l, "%u");
    DIFF_FIELD(sp, "%u");
    DIFF_FIELD(pc, "%u");
    DIFF_FIELD(f_z, "%u");
    DIFF_FIELD(f_n, "%u");
    DIFF_FIELD(f_h, "%u");
    DIFF_FIELD(f_c, "%u");
    DIFF_FIELD(ime, "%u");
    DIFF_FIELD(ime_pending, "%u");
    DIFF_FIELD(halted, "%u");
    DIFF_FIELD(stopped, "%u");
    DIFF_FIELD(stop_mode_active, "%u");
    DIFF_FIELD(halt_bug, "%u");
    DIFF_FIELD(cgb_double_speed, "%u");
    DIFF_FIELD(cgb_system_cycle_remainder, "%u");
    DIFF_FIELD(rom_bank, "%u");
    DIFF_FIELD(ram_bank, "%u");
    DIFF_FIELD(wram_bank, "%u");
    DIFF_FIELD(vram_bank, "%u");
    DIFF_FIELD(config.model, "%u");
    DIFF_FIELD(config.cgb_compatibility_mode, "%u");
    DIFF_FIELD(mbc_type, "%u");
    DIFF_FIELD(ram_enabled, "%u");
    DIFF_FIELD(mbc_mode, "%u");
    DIFF_FIELD(rom_bank_upper, "%u");
    DIFF_FIELD(rtc_mode, "%u");
    DIFF_FIELD(rtc_reg, "%u");
    DIFF_FIELD(cycles, "%u");
    DIFF_FIELD(frame_cycles, "%u");
    DIFF_FIELD(last_sync_cycles, "%u");
    DIFF_FIELD(frame_done, "%u");
    DIFF_FIELD(div_counter, "%u");
    DIFF_FIELD(last_joypad, "%u");
    DIFF_FIELD(dma.active, "%u");
    DIFF_FIELD(dma.source_high, "%u");
    DIFF_FIELD(dma.active_source_high, "%u");
    DIFF_FIELD(dma.progress, "%u");
    DIFF_FIELD(dma.cycles_remaining, "%u");
    DIFF_FIELD(hdma.source, "%u");
    DIFF_FIELD(hdma.dest, "%u");
    DIFF_FIELD(hdma.blocks_remaining, "%u");
    DIFF_FIELD(hdma.active, "%u");
    DIFF_FIELD(hdma.hblank_mode, "%u");
    DIFF_FIELD(rtc.s, "%u");
    DIFF_FIELD(rtc.m, "%u");
    DIFF_FIELD(rtc.h, "%u");
    DIFF_FIELD(rtc.dl, "%u");
    DIFF_FIELD(rtc.dh, "%u");
    DIFF_FIELD(rtc.latched_s, "%u");
    DIFF_FIELD(rtc.latched_m, "%u");
    DIFF_FIELD(rtc.latched_h, "%u");
    DIFF_FIELD(rtc.latched_dl, "%u");
    DIFF_FIELD(rtc.latched_dh, "%u");
    DIFF_FIELD(rtc.latch_state, "%u");
    DIFF_FIELD(rtc.last_time, "%" PRIu64);
    DIFF_FIELD(rtc.active, "%u");

#undef DIFF_FIELD

    if (gb_diff_pack_flags(generated) != gb_diff_pack_flags(interpreted)) {
        snprintf(message, message_size,
                 "flags mismatch: %02X != %02X",
                 gb_diff_pack_flags(generated),
                 gb_diff_pack_flags(interpreted));
        return false;
    }

    if (!gb_diff_compare_ppu(generated, interpreted, compare_memory, message, message_size)) {
        return false;
    }

    if (generated->apu == NULL || interpreted->apu == NULL) {
        if (generated->apu != interpreted->apu) {
            snprintf(message, message_size, "APU presence mismatch");
            return false;
        }
    } else {
        int16_t generated_left = 0;
        int16_t generated_right = 0;
        int16_t interpreted_left = 0;
        int16_t interpreted_right = 0;

        gb_audio_get_samples(generated->apu, &generated_left, &generated_right);
        gb_audio_get_samples(interpreted->apu, &interpreted_left, &interpreted_right);

        if (generated_left != interpreted_left || generated_right != interpreted_right) {
            snprintf(message, message_size,
                     "audio sample mismatch: (%d,%d) != (%d,%d)",
                     generated_left, generated_right,
                     interpreted_left, interpreted_right);
            return false;
        }
    }

    if (!compare_memory) {
        return true;
    }

    if (generated->eram_size != interpreted->eram_size) {
        snprintf(message, message_size,
                 "eram_size mismatch: %zu != %zu",
                 generated->eram_size, interpreted->eram_size);
        return false;
    }

    if (!gb_diff_compare_region("WRAM",
                                generated->wram,
                                interpreted->wram,
                                DIFF_WRAM_SIZE,
                                message,
                                message_size) ||
        !gb_diff_compare_region("VRAM",
                                generated->vram,
                                interpreted->vram,
                                DIFF_VRAM_TOTAL_SIZE,
                                message,
                                message_size) ||
        !gb_diff_compare_region("OAM",
                                generated->oam,
                                interpreted->oam,
                                DIFF_OAM_SIZE,
                                message,
                                message_size) ||
        !gb_diff_compare_region("HRAM",
                                generated->hram,
                                interpreted->hram,
                                DIFF_HRAM_SIZE,
                                message,
                                message_size) ||
        !gb_diff_compare_region("IO",
                                generated->io,
                                interpreted->io,
                                DIFF_IO_TOTAL_SIZE,
                                message,
                                message_size)) {
        return false;
    }

    if (generated->eram_size > 0 &&
        !gb_diff_compare_region("ERAM",
                                generated->eram,
                                interpreted->eram,
                                generated->eram_size,
                                message,
                                message_size)) {
        return false;
    }

    return true;
}

bool gb_run_differential(GBContext* generated_ctx,
                         GBContext* interpreted_ctx,
                         const GBDifferentialOptions* options,
                         GBDifferentialResult* result) {
    GBDifferentialOptions effective = {
        .max_steps = 10000,
        .max_frames = 0,
        .log_interval = 1000,
        .compare_memory = true,
        .log_fallbacks = false,
        .fail_on_fallback = false,
        .inject_mismatch_step = 0,
        .input_script = NULL,
    };

    if (options != NULL) {
        effective = *options;
        if (effective.max_steps == 0 && effective.max_frames == 0) effective.max_steps = 10000;
    }

    if (result != NULL) {
        memset(result, 0, sizeof(*result));
        result->matched = false;
    }

    GBDiffInputScript input_script;
    char setup_message[256] = {0};
    if (!gb_diff_parse_input_script(effective.input_script,
                                    &input_script,
                                    setup_message,
                                    sizeof(setup_message))) {
        if (result != NULL) {
            snprintf(result->message, sizeof(result->message), "%s", setup_message);
        }
        fprintf(stderr, "[DIFF] %s\n", setup_message);
        return false;
    }

    uint64_t saved_instruction_count = gbrt_instruction_count;
    uint64_t saved_instruction_limit = gbrt_instruction_limit;
    void* saved_generated_joypad = generated_ctx->joypad;
    void* saved_interpreted_joypad = interpreted_ctx->joypad;
    gbrt_instruction_count = 0;
    gbrt_instruction_limit = 0;

    GBJoypadState generated_joypad = {.dpad = 0xFF, .buttons = 0xFF};
    GBJoypadState interpreted_joypad = {.dpad = 0xFF, .buttons = 0xFF};
    if (effective.input_script != NULL && *effective.input_script != '\0') {
        generated_ctx->joypad = &generated_joypad;
        interpreted_ctx->joypad = &interpreted_joypad;
        gb_diff_apply_input_state(&input_script, generated_ctx, &generated_joypad);
        gb_diff_apply_input_state(&input_script, interpreted_ctx, &interpreted_joypad);
    }

    uint64_t frames_completed = 0;
    uint64_t executed_steps = 0;
    uint16_t last_logged_fallback_bank = UINT16_MAX;
    uint16_t last_logged_fallback_addr = 0xFFFF;
    bool unlimited_steps = (effective.max_steps == 0);
    uint64_t step_limit = effective.max_steps;
    for (uint64_t step = 0; unlimited_steps || step < step_limit; step++) {
        if (effective.input_script != NULL && *effective.input_script != '\0') {
            gb_diff_apply_input_state(&input_script, generated_ctx, &generated_joypad);
            gb_diff_apply_input_state(&input_script, interpreted_ctx, &interpreted_joypad);
        }

        executed_steps = step + 1;
        uint16_t start_pc = generated_ctx->pc;
        uint16_t start_bank = gb_diff_current_bank(generated_ctx);
        uint8_t opcode_bytes[3] = {0};
        gb_diff_read_opcode_bytes(generated_ctx, opcode_bytes);

        gb_debug_step(generated_ctx, GB_EXECUTION_GENERATED);
        gb_debug_step(interpreted_ctx, GB_EXECUTION_INTERPRETER);

        if (effective.inject_mismatch_step == step + 1) {
            interpreted_ctx->a ^= 0x01;
            fprintf(stderr, "[DIFF] Injected mismatch at step %" PRIu64 "\n", step + 1);
        }

        if (generated_ctx->used_dispatch_fallback) {
            if (effective.log_fallbacks &&
                (generated_ctx->dispatch_fallback_bank != last_logged_fallback_bank ||
                 generated_ctx->dispatch_fallback_addr != last_logged_fallback_addr)) {
                fprintf(stderr,
                        "[DIFF] Generated fallback to interpreter at %03X:%04X\n",
                        generated_ctx->dispatch_fallback_bank,
                        generated_ctx->dispatch_fallback_addr);
                if (generated_ctx->dispatch_fallback_addr >= 0x8000) {
                    gb_diff_print_code_bytes(stderr,
                                             "Generated fallback",
                                             generated_ctx,
                                             generated_ctx->dispatch_fallback_addr,
                                             8);
                }
                last_logged_fallback_bank = generated_ctx->dispatch_fallback_bank;
                last_logged_fallback_addr = generated_ctx->dispatch_fallback_addr;
            }
            if (effective.fail_on_fallback) {
                snprintf(setup_message, sizeof(setup_message),
                         "generated fallback to interpreter at %03X:%04X",
                         generated_ctx->dispatch_fallback_bank,
                         generated_ctx->dispatch_fallback_addr);
                if (result != NULL) {
                    result->matched = false;
                    result->steps_completed = step;
                    result->frames_completed = frames_completed;
                    result->mismatch_step = step + 1;
                    result->pc = generated_ctx->dispatch_fallback_addr;
                    result->bank = generated_ctx->dispatch_fallback_bank;
                    snprintf(result->message, sizeof(result->message), "%s", setup_message);
                }
                fprintf(stderr, "[DIFF] %s\n", setup_message);
                generated_ctx->joypad = saved_generated_joypad;
                interpreted_ctx->joypad = saved_interpreted_joypad;
                gbrt_instruction_count = saved_instruction_count;
                gbrt_instruction_limit = saved_instruction_limit;
                return false;
            }
        }

        char message[256];
        if (!gb_diff_compare_contexts(generated_ctx,
                                      interpreted_ctx,
                                      effective.compare_memory,
                                      message,
                                      sizeof(message))) {
            fprintf(stderr,
                    "[DIFF] Mismatch at step %" PRIu64 " "
                    "before %03X:%04X opcode=%02X %02X %02X\n",
                    step + 1,
                    start_bank,
                    start_pc,
                    opcode_bytes[0],
                    opcode_bytes[1],
                    opcode_bytes[2]);
            fprintf(stderr, "[DIFF] %s\n", message);
            gb_diff_print_state(stderr, "Generated", generated_ctx);
            gb_diff_print_state(stderr, "Interpreter", interpreted_ctx);
            if (start_pc >= 0x8000) {
                gb_diff_print_code_bytes(stderr, "Generated", generated_ctx, start_pc, 8);
                gb_diff_print_code_bytes(stderr, "Interpreter", interpreted_ctx, start_pc, 8);
            }

            if (result != NULL) {
                result->matched = false;
                result->steps_completed = step;
                result->frames_completed = frames_completed;
                result->mismatch_step = step + 1;
                result->pc = start_pc;
                result->bank = start_bank;
                snprintf(result->message, sizeof(result->message), "%s", message);
            }

            generated_ctx->joypad = saved_generated_joypad;
            interpreted_ctx->joypad = saved_interpreted_joypad;
            gbrt_instruction_count = saved_instruction_count;
            gbrt_instruction_limit = saved_instruction_limit;
            return false;
        }

        if (generated_ctx->frame_done && interpreted_ctx->frame_done) {
            frames_completed++;
            gb_reset_frame(generated_ctx);
            gb_reset_frame(interpreted_ctx);

            if (effective.input_script != NULL && *effective.input_script != '\0') {
                input_script.frame_index++;
                gb_diff_apply_input_state(&input_script, generated_ctx, &generated_joypad);
                gb_diff_apply_input_state(&input_script, interpreted_ctx, &interpreted_joypad);
            }

            if (effective.max_frames > 0 && frames_completed >= effective.max_frames) {
                if (result != NULL) {
                    result->matched = true;
                    result->steps_completed = step + 1;
                    result->frames_completed = frames_completed;
                    result->mismatch_step = 0;
                    result->pc = generated_ctx->pc;
                    result->bank = gb_diff_current_bank(generated_ctx);
                    snprintf(result->message, sizeof(result->message), "matched");
                }
                fprintf(stderr,
                        "[DIFF] Matched generated and interpreter execution for %" PRIu64 " steps / %" PRIu64 " frames\n",
                        step + 1,
                        frames_completed);
                generated_ctx->joypad = saved_generated_joypad;
                interpreted_ctx->joypad = saved_interpreted_joypad;
                gbrt_instruction_count = saved_instruction_count;
                gbrt_instruction_limit = saved_instruction_limit;
                return true;
            }
        }

        if (effective.log_interval > 0 && ((step + 1) % effective.log_interval) == 0) {
            if (unlimited_steps) {
                fprintf(stderr,
                        "[DIFF] Matched %" PRIu64 " steps at %03X:%04X (frames=%" PRIu64 ")\n",
                        step + 1,
                        gb_diff_current_bank(generated_ctx),
                        generated_ctx->pc,
                        frames_completed);
            } else {
                fprintf(stderr,
                        "[DIFF] Matched %" PRIu64 "/%" PRIu64 " steps at %03X:%04X (frames=%" PRIu64 ")\n",
                        step + 1,
                        step_limit,
                        gb_diff_current_bank(generated_ctx),
                        generated_ctx->pc,
                        frames_completed);
            }
        }
    }

    fprintf(stderr,
            "[DIFF] Matched generated and interpreter execution for %" PRIu64 " steps / %" PRIu64 " frames\n",
            executed_steps,
            frames_completed);

    if (result != NULL) {
        result->matched = true;
        result->steps_completed = executed_steps;
        result->frames_completed = frames_completed;
        result->mismatch_step = 0;
        result->pc = generated_ctx->pc;
        result->bank = gb_diff_current_bank(generated_ctx);
        snprintf(result->message, sizeof(result->message), "matched");
    }

    generated_ctx->joypad = saved_generated_joypad;
    interpreted_ctx->joypad = saved_interpreted_joypad;
    gbrt_instruction_count = saved_instruction_count;
    gbrt_instruction_limit = saved_instruction_limit;
    return true;
}
