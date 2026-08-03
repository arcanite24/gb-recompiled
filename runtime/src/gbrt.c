#include "gbrt.h"
#include "gbrt_data_mod.h"
#include "gbrt_port.h"
#include "ppu.h"
#include "audio.h"
#include "audio_stats.h"
#include "platform_sdl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "gbrt_debug.h"
#ifdef GBRT_ENABLE_NATIVE_PATCHES
#include "gbrt_native_patch_internal.h"
#endif

/* ============================================================================
 * Definitions
 * ========================================================================== */

#define WRAM_BANK_SIZE 0x1000
#define VRAM_SIZE      0x2000
#define OAM_SIZE       0xA0
#define IO_SIZE        0x80
#define HRAM_SIZE      0x7F

/* ============================================================================
 * Globals
 * ========================================================================== */

bool gbrt_trace_enabled = false;
bool gbrt_log_lcd_transitions = false;
bool gbrt_dispatch_fallback_tracking_enabled = false;
bool gbrt_rgb_framebuffer_enabled = true;
bool gbrt_benchmark_fast_tick_enabled = false;
bool gbrt_force_scalar_timer = false;
bool gbrt_force_eager_audio = false;
bool gbrt_visibility_estimator_enabled = false;
bool gbrt_test_breakpoint_enabled = false;
uint64_t gbrt_instruction_count = 0;
uint64_t gbrt_instruction_limit = 0;
void (*gbrt_instruction_limit_callback)(void) = NULL;

static char* gbrt_trace_filename = NULL;
static bool gbrt_ppu_trace_config_loaded = false;
static char* gbrt_ppu_trace_filename = NULL;
static uint64_t gbrt_ppu_trace_start_frame = 0;
static uint64_t gbrt_ppu_trace_end_frame = 0;

static inline void gb_sync(GBContext* ctx);
static void gbrt_trigger_oam_bug_write(GBContext* ctx, uint16_t address);
static void gbrt_trigger_oam_bug_read(GBContext* ctx, uint16_t address);
static uint32_t gb_halt_fast_forward_cycles(GBContext* ctx, uint32_t run_start, uint32_t max_cycles);
static bool gb_context_try_load_battery_ram(GBContext* ctx);
static bool gb_context_try_load_rtc(GBContext* ctx);
static uint64_t gb_context_compute_rom_hash(const GBContext* ctx);
static bool gbrt_write_exact(FILE* file, const void* data, size_t size);
static bool gbrt_read_exact(FILE* file, void* data, size_t size);

typedef struct {
    uint64_t saved_unix_time;
    uint64_t cycle_remainder;
    uint8_t s, m, h, dl, dh;
    uint8_t latched_s, latched_m, latched_h, latched_dl, latched_dh;
    uint8_t latch_state;
} GBRTCPersistedState;

#define GBRTC_PERSIST_MAGIC 0x47525443u /* 'GRTC' */
#define GBRTC_PERSIST_VERSION 2u
#define GBRTC_PERSIST_LEGACY_VERSION 1u
#define GBRTC_PERSIST_SIZE 40u

static uint32_t gbrt_read_le32(const uint8_t* data) {
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static uint64_t gbrt_read_le64(const uint8_t* data) {
    uint64_t value = 0;
    for (unsigned shift = 0; shift < 64; shift += 8) {
        value |= (uint64_t)data[shift / 8] << shift;
    }
    return value;
}

static void gbrt_write_le32(uint8_t* data, uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        data[shift / 8] = (uint8_t)(value >> shift);
    }
}

static void gbrt_write_le64(uint8_t* data, uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) {
        data[shift / 8] = (uint8_t)(value >> shift);
    }
}

static bool gbrt_decode_rtc_persistence(
    const uint8_t serialized[GBRTC_PERSIST_SIZE],
    GBRTCPersistedState* persisted,
    uint32_t* version_out) {
    const uint32_t magic = gbrt_read_le32(serialized);
    const uint32_t version = gbrt_read_le32(serialized + 4);
    if (magic != GBRTC_PERSIST_MAGIC ||
        (version != GBRTC_PERSIST_LEGACY_VERSION &&
         version != GBRTC_PERSIST_VERSION)) {
        return false;
    }
    memset(persisted, 0, sizeof(*persisted));
    persisted->saved_unix_time = gbrt_read_le64(serialized + 8);
    persisted->cycle_remainder = gbrt_read_le64(serialized + 16);
    persisted->s = serialized[24];
    persisted->m = serialized[25];
    persisted->h = serialized[26];
    persisted->dl = serialized[27];
    persisted->dh = serialized[28];
    persisted->latched_s = serialized[29];
    persisted->latched_m = serialized[30];
    persisted->latched_h = serialized[31];
    persisted->latched_dl = serialized[32];
    persisted->latched_dh = serialized[33];
    persisted->latch_state = serialized[34];
    if (version_out) {
        *version_out = version;
    }
    return true;
}

static void gbrt_encode_rtc_persistence(
    uint8_t serialized[GBRTC_PERSIST_SIZE],
    const GBRTCPersistedState* persisted) {
    memset(serialized, 0, GBRTC_PERSIST_SIZE);
    gbrt_write_le32(serialized, GBRTC_PERSIST_MAGIC);
    gbrt_write_le32(serialized + 4, GBRTC_PERSIST_VERSION);
    gbrt_write_le64(serialized + 8, persisted->saved_unix_time);
    gbrt_write_le64(serialized + 16, persisted->cycle_remainder);
    serialized[24] = persisted->s;
    serialized[25] = persisted->m;
    serialized[26] = persisted->h;
    serialized[27] = persisted->dl;
    serialized[28] = persisted->dh;
    serialized[29] = persisted->latched_s;
    serialized[30] = persisted->latched_m;
    serialized[31] = persisted->latched_h;
    serialized[32] = persisted->latched_dl;
    serialized[33] = persisted->latched_dh;
    serialized[34] = persisted->latch_state;
}

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint64_t rom_hash;
    uint32_t rom_size;
    uint32_t eram_size;
    uint32_t wram_size;
    uint32_t vram_size;
    uint32_t oam_size;
    uint32_t hram_size;
    uint32_t io_size;
    uint32_t ppu_size;
    uint32_t apu_size;
} GBSavestateFileHeader;

typedef struct {
    uint16_t af;
    uint16_t bc;
    uint16_t de;
    uint16_t hl;
    uint16_t sp;
    uint16_t pc;
    uint8_t f_z;
    uint8_t f_n;
    uint8_t f_h;
    uint8_t f_c;
    uint8_t ime;
    uint8_t ime_pending;
    uint8_t halted;
    uint8_t stopped;
    uint8_t stop_mode_active;
    uint8_t halt_bug;
    uint8_t single_step_mode;
    uint8_t cgb_double_speed;
    uint8_t cgb_system_cycle_remainder;
    GBConfig config;
    char save_id[64];
    uint16_t rom_bank;
    uint8_t ram_bank;
    uint8_t wram_bank;
    uint8_t vram_bank;
    uint8_t mbc_type;
    uint8_t ram_enabled;
    uint8_t mbc_mode;
    uint8_t rom_bank_low;
    uint8_t rom_bank_upper;
    uint8_t mbc1_multicart;
    uint8_t rtc_mode;
    uint8_t rtc_reg;
    uint8_t last_joypad;
    uint8_t used_dispatch_fallback;
    uint16_t dispatch_fallback_bank;
    uint8_t has_unimplemented_interpreter_opcode;
    uint8_t last_unimplemented_opcode;
    uint16_t last_unimplemented_bank;
    uint16_t frame_first_fallback_bank;
    uint16_t frame_last_fallback_bank;
    uint16_t dispatch_fallback_addr;
    uint16_t frame_first_fallback_addr;
    uint16_t frame_last_fallback_addr;
    uint16_t div_counter;
    uint8_t tima_reload_pending;
    struct {
        uint8_t active;
        uint8_t pending;
        uint8_t source_high;
        uint8_t active_source_high;
        uint8_t progress;
        uint16_t cycles_remaining;
        uint8_t startup_delay;
    } dma;
    struct {
        uint16_t source;
        uint16_t dest;
        uint8_t blocks_remaining;
        uint8_t active;
        uint8_t hblank_mode;
        uint16_t cpu_stall_cycles;
        uint8_t processing_stall;
    } hdma;
    struct {
        uint8_t active;
        uint8_t fast_clock;
        uint32_t cycles_remaining;
    } serial_transfer;
    struct {
        uint8_t s, m, h, dl, dh;
        uint8_t latched_s, latched_m, latched_h, latched_dl, latched_dh;
        uint8_t latch_state;
        uint64_t last_time;
        bool active;
    } rtc;
    uint32_t cycles;
    uint64_t total_cycles;
    uint32_t frame_cycles;
    uint32_t last_sync_cycles;
    uint32_t run_cycle_budget;
    uint32_t run_cycle_budget_start;
    uint8_t frame_done;
    uint8_t lcd_off_active;
    uint32_t lcd_off_start_cycles;
    uint32_t lcd_off_start_frame_cycles;
    uint32_t frame_lcd_off_cycles;
    uint32_t frame_lcd_transition_count;
    uint32_t frame_lcd_off_span_count;
    uint32_t last_lcd_off_span_cycles;
    uint64_t total_lcd_off_cycles;
    uint64_t total_lcd_transition_count;
    uint64_t total_lcd_off_span_count;
    uint32_t frame_dispatch_fallbacks;
    uint64_t total_dispatch_fallbacks;
    uint64_t total_interpreter_entries;
    uint64_t total_interpreter_instructions;
    uint64_t total_interpreter_cycles;
    uint64_t frame_interpreter_instructions;
    uint64_t frame_interpreter_cycles;
    uint16_t last_unimplemented_addr;
    GBInterpreterHotspot interpreter_hotspots[GBRT_INTERPRETER_HOTSPOT_CAPACITY];
    uint64_t completed_frames;
} GBSavestateCoreState;

#define GBSAVESTATE_MAGIC 0x56534247u /* 'GBSV' */
#define GBSAVESTATE_VERSION 9u

static int gbrt_compare_hotspots_desc(const GBInterpreterHotspot* lhs,
                                      const GBInterpreterHotspot* rhs) {
    if (lhs->valid != rhs->valid) {
        return lhs->valid ? -1 : 1;
    }
    if (!lhs->valid && !rhs->valid) {
        return 0;
    }
    if (lhs->cycles != rhs->cycles) {
        return (lhs->cycles > rhs->cycles) ? -1 : 1;
    }
    if (lhs->entries != rhs->entries) {
        return (lhs->entries > rhs->entries) ? -1 : 1;
    }
    if (lhs->instructions != rhs->instructions) {
        return (lhs->instructions > rhs->instructions) ? -1 : 1;
    }
    if (lhs->bank != rhs->bank) {
        return (lhs->bank < rhs->bank) ? -1 : 1;
    }
    if (lhs->addr != rhs->addr) {
        return (lhs->addr < rhs->addr) ? -1 : 1;
    }
    return 0;
}

static void gbrt_sort_interpreter_hotspots(GBContext* ctx) {
    if (!ctx) {
        return;
    }

    for (size_t i = 1; i < GBRT_INTERPRETER_HOTSPOT_CAPACITY; i++) {
        GBInterpreterHotspot candidate = ctx->interpreter_hotspots[i];
        size_t j = i;
        while (j > 0 &&
               gbrt_compare_hotspots_desc(&candidate, &ctx->interpreter_hotspots[j - 1]) < 0) {
            ctx->interpreter_hotspots[j] = ctx->interpreter_hotspots[j - 1];
            j--;
        }
        ctx->interpreter_hotspots[j] = candidate;
    }
}

static size_t gbrt_find_or_allocate_interpreter_hotspot(GBContext* ctx,
                                                        uint16_t bank,
                                                        uint16_t addr) {
    size_t replacement = 0;

    for (size_t i = 0; i < GBRT_INTERPRETER_HOTSPOT_CAPACITY; i++) {
        GBInterpreterHotspot* hotspot = &ctx->interpreter_hotspots[i];
        if (hotspot->valid && hotspot->bank == bank && hotspot->addr == addr) {
            return i;
        }
        if (!hotspot->valid) {
            hotspot->valid = 1;
            hotspot->bank = bank;
            hotspot->addr = addr;
            hotspot->entries = 0;
            hotspot->instructions = 0;
            hotspot->cycles = 0;
            hotspot->last_frame = 0;
            return i;
        }
        if (gbrt_compare_hotspots_desc(hotspot, &ctx->interpreter_hotspots[replacement]) > 0) {
            replacement = i;
        }
    }

    memset(&ctx->interpreter_hotspots[replacement], 0, sizeof(ctx->interpreter_hotspots[replacement]));
    ctx->interpreter_hotspots[replacement].valid = 1;
    ctx->interpreter_hotspots[replacement].bank = bank;
    ctx->interpreter_hotspots[replacement].addr = addr;
    return replacement;
}

static void gbrt_load_ppu_trace_config(void) {
    if (gbrt_ppu_trace_config_loaded) {
        return;
    }

    gbrt_ppu_trace_config_loaded = true;

    const char* trace_path = getenv("GBRT_PPU_TRACE");
    if (trace_path && trace_path[0] != '\0') {
        gbrt_ppu_trace_filename = strdup(trace_path);
    }

    const char* frame_spec = getenv("GBRT_PPU_TRACE_FRAMES");
    if (!frame_spec || frame_spec[0] == '\0') {
        return;
    }

    char* spec_copy = strdup(frame_spec);
    if (!spec_copy) {
        return;
    }

    char* dash = strchr(spec_copy, '-');
    if (dash) {
        *dash = '\0';
        gbrt_ppu_trace_start_frame = strtoull(spec_copy, NULL, 10);
        gbrt_ppu_trace_end_frame = strtoull(dash + 1, NULL, 10);
    } else {
        gbrt_ppu_trace_start_frame = strtoull(spec_copy, NULL, 10);
        gbrt_ppu_trace_end_frame = gbrt_ppu_trace_start_frame;
    }

    free(spec_copy);
}

static bool gbrt_ppu_trace_enabled_for_frame(const GBContext* ctx, uint64_t frame_index) {
    if (!ctx || !ctx->ppu_trace_file || !gbrt_ppu_trace_filename) {
        return false;
    }

    if (gbrt_ppu_trace_start_frame == 0 && gbrt_ppu_trace_end_frame == 0) {
        return true;
    }

    return frame_index >= gbrt_ppu_trace_start_frame && frame_index <= gbrt_ppu_trace_end_frame;
}

static void gbrt_log_oam_write(GBContext* ctx,
                               uint16_t addr,
                               uint8_t value,
                               uint8_t accepted,
                               const char* reason) {
    uint64_t frame_index = ctx ? (ctx->completed_frames + 1) : 0;
    if (!gbrt_ppu_trace_enabled_for_frame(ctx, frame_index)) {
        return;
    }

    fprintf((FILE*)ctx->ppu_trace_file,
            "[OAM-WRITE] frame=%llu cyc=%u pc=%04X bank=%u ly=%u mode=%u addr=%04X val=%02X accepted=%u reason=%s\n",
            (unsigned long long)frame_index,
            ctx->frame_cycles,
            ctx->pc,
            (unsigned)gb_resolve_rom_bank(ctx, ctx->pc),
            ctx->io[0x44],
            ctx->io[0x41] & 0x03,
            addr,
            value,
            accepted,
            reason ? reason : "-");
}

static void gbrt_log_dma_start(GBContext* ctx, uint8_t source_high) {
    uint64_t frame_index = ctx ? (ctx->completed_frames + 1) : 0;
    if (!gbrt_ppu_trace_enabled_for_frame(ctx, frame_index)) {
        return;
    }

    fprintf((FILE*)ctx->ppu_trace_file,
            "[DMA-START] frame=%llu cyc=%u pc=%04X bank=%u ly=%u mode=%u src=%02X00\n",
            (unsigned long long)frame_index,
            ctx->frame_cycles,
            ctx->pc,
            (unsigned)gb_resolve_rom_bank(ctx, ctx->pc),
            ctx->io[0x44],
            ctx->io[0x41] & 0x03,
            source_high);
}

static void gbrt_log_vram_write(GBContext* ctx,
                                uint16_t addr,
                                uint8_t value,
                                uint8_t accepted,
                                const char* reason) {
    uint64_t frame_index = ctx ? (ctx->completed_frames + 1) : 0;
    if (!gbrt_ppu_trace_enabled_for_frame(ctx, frame_index)) {
        return;
    }

    fprintf((FILE*)ctx->ppu_trace_file,
            "[VRAM-WRITE] frame=%llu cyc=%u pc=%04X bank=%u ly=%u mode=%u addr=%04X val=%02X accepted=%u reason=%s\n",
            (unsigned long long)frame_index,
            ctx->frame_cycles,
            ctx->pc,
            (unsigned)gb_resolve_rom_bank(ctx, ctx->pc),
            ctx->io[0x44],
            ctx->io[0x41] & 0x03,
            addr,
            value,
            accepted,
            reason ? reason : "-");
}


/* ============================================================================
 * Context Management
 * ========================================================================== */

static bool gb_cart_type_has_battery(uint8_t type) {
    switch (type) {
        case 0x03: /* MBC1+RAM+BATTERY */
        case 0x06: /* MBC2+BATTERY */
        case 0x09: /* ROM+RAM+BATTERY */
        case 0x0D: /* MMM01+RAM+BATTERY */
        case 0x0F: /* MBC3+TIMER+BATTERY */
        case 0x10: /* MBC3+TIMER+RAM+BATTERY */
        case 0x13: /* MBC3+RAM+BATTERY */
        case 0x1B: /* MBC5+RAM+BATTERY */
        case 0x1E: /* MBC5+RUMBLE+RAM+BATTERY */
        case 0x22: /* MBC7+SENSOR+RUMBLE+RAM+BATTERY */
        case 0xFF: /* HuC1+RAM+BATTERY */
            return true;
    }
    return false;
}

static bool gb_cart_type_has_rtc(uint8_t type) {
    switch (type) {
        case 0x0F: /* MBC3+TIMER+BATTERY */
        case 0x10: /* MBC3+TIMER+RAM+BATTERY */
            return true;
    }
    return false;
}

static void gb_context_get_rom_title(const GBContext* ctx, char title[17]) {
    memset(title, 0, 17);
    if (!ctx || !ctx->rom || ctx->rom_size <= 0x143) {
        strcpy(title, "UNKNOWN_GAME");
        return;
    }

    memcpy(title, &ctx->rom[0x134], 16);
    for (int i = 0; i < 16; i++) {
        if (title[i] == 0 || title[i] < 32 || title[i] > 126) {
            title[i] = 0;
        }
    }
    if (title[0] == 0) {
        strcpy(title, "UNKNOWN_GAME");
    }
}

static void gb_context_get_save_id(const GBContext* ctx, char save_id[64]) {
    if (!save_id) {
        return;
    }

    memset(save_id, 0, 64);
    if (ctx && ctx->save_id[0]) {
        snprintf(save_id, 64, "%s", ctx->save_id);
        return;
    }

    char title[17];
    gb_context_get_rom_title(ctx, title);
    snprintf(save_id, 64, "%s", title);
}

static bool gb_save_id_differs_from_legacy_title(const GBContext* ctx, const char* save_id) {
    char legacy_title[17];
    gb_context_get_rom_title(ctx, legacy_title);
    return strcmp(save_id, legacy_title) != 0;
}

static void gb_rtc_reset(GBContext* ctx) {
    if (!ctx) {
        return;
    }
    memset(&ctx->rtc, 0, sizeof(ctx->rtc));
    ctx->rtc.active = true;
}

static void gb_rtc_refresh_latch(GBContext* ctx) {
    if (!ctx) {
        return;
    }

    ctx->rtc.latched_s = ctx->rtc.s;
    ctx->rtc.latched_m = ctx->rtc.m;
    ctx->rtc.latched_h = ctx->rtc.h;
    ctx->rtc.latched_dl = ctx->rtc.dl;
    ctx->rtc.latched_dh = ctx->rtc.dh;
}

static uint64_t gb_context_current_unix_time(const GBContext* ctx) {
    if (ctx && ctx->config.rtc_unix_time_override_enabled) {
        return ctx->config.rtc_unix_time_override;
    }

    time_t now = time(NULL);
    return (now == (time_t)-1 || now < 0) ? 0u : (uint64_t)now;
}

static void gb_rtc_advance_seconds(GBContext* ctx, uint64_t elapsed_seconds) {
    if (!ctx || elapsed_seconds == 0) {
        return;
    }

    uint64_t total = (uint64_t)ctx->rtc.s + elapsed_seconds;
    ctx->rtc.s = (uint8_t)(total % 60u);
    total = (uint64_t)ctx->rtc.m + (total / 60u);
    ctx->rtc.m = (uint8_t)(total % 60u);
    total = (uint64_t)ctx->rtc.h + (total / 60u);
    ctx->rtc.h = (uint8_t)(total % 24u);

    uint64_t days = (uint64_t)(ctx->rtc.dl | ((ctx->rtc.dh & 0x01u) << 8)) + (total / 24u);
    uint8_t dh = (uint8_t)(ctx->rtc.dh & 0x40u);
    if ((ctx->rtc.dh & 0x80u) || days > 0x1FFu) {
        dh |= 0x80u;
    }
    days &= 0x1FFu;
    ctx->rtc.dl = (uint8_t)(days & 0xFFu);
    ctx->rtc.dh = (uint8_t)(dh | ((days >> 8) & 0x01u));
    ctx->rtc.active = (ctx->rtc.dh & 0x40u) == 0;
    gb_rtc_refresh_latch(ctx);
}

static bool gb_context_try_load_battery_ram(GBContext* ctx) {
    if (!ctx || !ctx->rom || ctx->rom_size <= 0x149 || !ctx->eram || !ctx->eram_size) {
        return false;
    }
    if (!ctx->callbacks.load_battery_ram) {
        return false;
    }
    if (!gb_cart_type_has_battery(ctx->rom[0x147])) {
        return false;
    }

    char save_id[64];
    gb_context_get_save_id(ctx, save_id);
    if (ctx->callbacks.load_battery_ram(ctx, save_id, ctx->eram, ctx->eram_size)) {
        printf("[GBRT] Loaded battery RAM for '%s'\n", save_id);
        return true;
    }

    if (ctx->save_id[0] && gb_save_id_differs_from_legacy_title(ctx, save_id)) {
        char legacy_title[17];
        gb_context_get_rom_title(ctx, legacy_title);
        if (ctx->callbacks.load_battery_ram(ctx, legacy_title, ctx->eram, ctx->eram_size)) {
            printf("[GBRT] Loaded battery RAM for '%s' via legacy title fallback\n", legacy_title);
            return true;
        }
    }

    return false;
}

static bool gb_context_try_load_rtc(GBContext* ctx) {
    if (!ctx || !ctx->rom || ctx->rom_size <= 0x149 || !ctx->callbacks.load_rtc_data) {
        return false;
    }
    if (!gb_cart_type_has_rtc(ctx->rom[0x147])) {
        return false;
    }
    if (ctx->config.ignore_rtc_persistence) {
        printf("[GBRT] RTC persistence load explicitly ignored\n");
        return false;
    }

    uint8_t serialized[GBRTC_PERSIST_SIZE];
    memset(serialized, 0, sizeof(serialized));

    char save_id[64];
    gb_context_get_save_id(ctx, save_id);
    const char* loaded_id = save_id;
    bool loaded = ctx->callbacks.load_rtc_data(
        ctx, save_id, serialized, sizeof(serialized));
    if (!loaded && ctx->save_id[0] && gb_save_id_differs_from_legacy_title(ctx, save_id)) {
        char legacy_title[17];
        gb_context_get_rom_title(ctx, legacy_title);
        loaded = ctx->callbacks.load_rtc_data(
            ctx, legacy_title, serialized, sizeof(serialized));
        if (loaded) {
            loaded_id = legacy_title;
        }
    }

    if (!loaded) {
        return false;
    }
    GBRTCPersistedState persisted;
    uint32_t persisted_version = 0;
    if (!gbrt_decode_rtc_persistence(
            serialized, &persisted, &persisted_version)) {
        ctx->persistence_load_failed = true;
        fprintf(stderr,
                "[GBRT] Rejected RTC data for '%s': unsupported magic or version; "
                "automatic persistence overwrite suppressed\n",
                loaded_id);
        return false;
    }

    ctx->rtc.s = persisted.s;
    ctx->rtc.m = persisted.m;
    ctx->rtc.h = persisted.h;
    ctx->rtc.dl = persisted.dl;
    ctx->rtc.dh = persisted.dh;
    ctx->rtc.latched_s = persisted.latched_s;
    ctx->rtc.latched_m = persisted.latched_m;
    ctx->rtc.latched_h = persisted.latched_h;
    ctx->rtc.latched_dl = persisted.latched_dl;
    ctx->rtc.latched_dh = persisted.latched_dh;
    ctx->rtc.latch_state = persisted.latch_state;
    ctx->rtc.last_time = persisted.cycle_remainder % 4194304u;
    ctx->rtc.active = (ctx->rtc.dh & 0x40u) == 0;

    uint64_t now_u64 = gb_context_current_unix_time(ctx);
    if (ctx->rtc.active && persisted.saved_unix_time > 0 && now_u64 > 0) {
        if (now_u64 > persisted.saved_unix_time) {
            gb_rtc_advance_seconds(ctx, now_u64 - persisted.saved_unix_time);
        }
    }

    if (loaded_id != save_id) {
        printf("[GBRT] Loaded RTC data for '%s' via legacy title fallback\n", loaded_id);
    } else {
        printf(
            "[GBRT] Loaded RTC data for '%s' (serialization v%u)\n",
            loaded_id,
            persisted_version);
    }
    return true;
}

static bool gb_context_save_rtc(GBContext* ctx) {
    if (!ctx || !ctx->rom || ctx->rom_size <= 0x149 || !ctx->callbacks.save_rtc_data) {
        return false;
    }
    if (!gb_cart_type_has_rtc(ctx->rom[0x147])) {
        return false;
    }

    GBRTCPersistedState persisted;
    memset(&persisted, 0, sizeof(persisted));
    persisted.saved_unix_time = gb_context_current_unix_time(ctx);
    persisted.cycle_remainder = ctx->rtc.last_time;
    persisted.s = ctx->rtc.s;
    persisted.m = ctx->rtc.m;
    persisted.h = ctx->rtc.h;
    persisted.dl = ctx->rtc.dl;
    persisted.dh = ctx->rtc.dh;
    persisted.latched_s = ctx->rtc.latched_s;
    persisted.latched_m = ctx->rtc.latched_m;
    persisted.latched_h = ctx->rtc.latched_h;
    persisted.latched_dl = ctx->rtc.latched_dl;
    persisted.latched_dh = ctx->rtc.latched_dh;
    persisted.latch_state = ctx->rtc.latch_state;
    uint8_t serialized[GBRTC_PERSIST_SIZE];
    gbrt_encode_rtc_persistence(serialized, &persisted);

    char save_id[64];
    gb_context_get_save_id(ctx, save_id);
    bool result = ctx->callbacks.save_rtc_data(
        ctx, save_id, serialized, sizeof(serialized));
    if (result) {
        printf(
            "[GBRT] Saved RTC data for '%s' (serialization v%u)\n",
            save_id,
            GBRTC_PERSIST_VERSION);
    } else {
        printf("[GBRT] Failed to save RTC data for '%s'\n", save_id);
    }
    return result;
}

static GBConfig gb_default_config(void) {
    GBConfig config;
    memset(&config, 0, sizeof(config));
    config.model = GB_MODEL_DMG;
    config.enable_audio = true;
    config.enable_serial = true;
    config.speed_percent = 100;
    return config;
}

static bool gb_is_cgb_hardware(const GBContext* ctx) {
    return ctx && ctx->config.model == GB_MODEL_CGB;
}

static bool gb_is_cgb_mode(const GBContext* ctx) {
    return gb_is_cgb_hardware(ctx) && !ctx->config.cgb_compatibility_mode;
}

static bool gb_is_cgb_compat_mode(const GBContext* ctx) {
    return gb_is_cgb_hardware(ctx) && ctx->config.cgb_compatibility_mode;
}

static bool gb_cartridge_uses_nintendo_license(const GBContext* ctx) {
    if (!ctx || !ctx->rom || ctx->rom_size <= 0x145) {
        return false;
    }

    if (ctx->rom[0x14B] == 0x01) {
        return true;
    }

    return ctx->rom[0x14B] == 0x33 &&
           ctx->rom[0x144] == '0' &&
           ctx->rom[0x145] == '1';
}

static uint8_t gb_compute_title_checksum(const GBContext* ctx) {
    uint8_t checksum = 0;

    if (!ctx || !ctx->rom || ctx->rom_size <= 0x143) {
        return 0;
    }

    for (size_t i = 0; i < 16; i++) {
        checksum = (uint8_t)(checksum + ctx->rom[0x134 + i]);
    }

    return checksum;
}

static uint8_t gb_compute_cgb_compat_b(const GBContext* ctx) {
    if (!gb_cartridge_uses_nintendo_license(ctx)) {
        return 0;
    }

    return gb_compute_title_checksum(ctx);
}

static uint64_t gb_context_compute_rom_hash(const GBContext* ctx) {
    if (!ctx || !ctx->rom || ctx->rom_size == 0) {
        return 0;
    }

    uint64_t hash = 1469598103934665603ull;
    for (size_t i = 0; i < ctx->rom_size; i++) {
        hash ^= (uint64_t)ctx->rom[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

static bool gb_detect_mbc1_multicart(const uint8_t* rom, size_t rom_size) {
    static const uint8_t nintendo_logo[48] = {
        0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B,
        0x03, 0x73, 0x00, 0x83, 0x00, 0x0C, 0x00, 0x0D,
        0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E,
        0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99,
        0xBB, 0xBB, 0x67, 0x63, 0x6E, 0x0E, 0xEC, 0xCC,
        0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E,
    };
    const size_t secondary_logo = (0x10u * 0x4000u) + 0x0104u;
    return rom &&
           rom_size >= secondary_logo + sizeof(nintendo_logo) &&
           memcmp(rom + secondary_logo,
                  nintendo_logo,
                  sizeof(nintendo_logo)) == 0;
}

static uint16_t gb_wrap_rom_bank(const GBContext* ctx, uint32_t bank) {
    if (!ctx || ctx->rom_size < 0x4000u) {
        return 0;
    }
    const uint32_t bank_count = (uint32_t)(ctx->rom_size / 0x4000u);
    return bank_count ? (uint16_t)(bank % bank_count) : 0;
}

uint16_t gb_resolve_rom_bank(const GBContext* ctx, uint16_t addr) {
    if (!ctx || addr >= 0x8000) {
        return 0;
    }

    if (ctx->mbc_type >= 0x01 && ctx->mbc_type <= 0x03) {
        const uint8_t raw_low = ctx->rom_bank_low & 0x1F;
        const uint8_t shift = ctx->mbc1_multicart ? 4 : 5;
        const uint16_t high = (uint16_t)(ctx->rom_bank_upper & 0x03) << shift;
        if (addr < 0x4000) {
            return gb_wrap_rom_bank(ctx, ctx->mbc_mode ? high : 0);
        }

        uint16_t low = ctx->mbc1_multicart ? (raw_low & 0x0F) : raw_low;
        if (raw_low == 0) {
            low = 1;
        }
        return gb_wrap_rom_bank(ctx, high | low);
    }

    if (addr < 0x4000) {
        return 0;
    }

    if (ctx->mbc_type >= 0x05 && ctx->mbc_type <= 0x06) {
        uint16_t bank = ctx->rom_bank_low & 0x0F;
        return gb_wrap_rom_bank(ctx, bank == 0 ? 1 : bank);
    }
    if (ctx->mbc_type >= 0x0F && ctx->mbc_type <= 0x13) {
        uint16_t bank = ctx->rom_bank_low & 0x7F;
        return gb_wrap_rom_bank(ctx, bank == 0 ? 1 : bank);
    }
    if (ctx->mbc_type >= 0x19 && ctx->mbc_type <= 0x1E) {
        return gb_wrap_rom_bank(
            ctx,
            ((uint16_t)(ctx->rom_bank_upper & 0x01) << 8) |
                ctx->rom_bank_low);
    }
    if (ctx->mbc_type == 0x00 ||
        ctx->mbc_type == 0x08 ||
        ctx->mbc_type == 0x09) {
        return gb_wrap_rom_bank(ctx, 1);
    }
    return gb_wrap_rom_bank(ctx, ctx->rom_bank);
}

static bool gb_resolve_rom_offset(const GBContext* ctx,
                                  uint16_t addr,
                                  size_t* out_offset) {
    if (!ctx || !out_offset || addr >= 0x8000) {
        return false;
    }
    const uint16_t bank = gb_resolve_rom_bank(ctx, addr);
    const size_t offset =
        ((size_t)bank * 0x4000u) + (size_t)(addr & 0x3FFFu);
    if (offset >= ctx->rom_size) {
        return false;
    }
    *out_offset = offset;
    return true;
}

static bool gbrt_write_exact(FILE* file, const void* data, size_t size) {
    return size == 0 || (file && data && fwrite(data, 1, size, file) == size);
}

static bool gbrt_read_exact(FILE* file, void* data, size_t size) {
    return size == 0 || (file && data && fread(data, 1, size, file) == size);
}

static void gbrt_capture_core_state(const GBContext* ctx, GBSavestateCoreState* state) {
    if (!ctx || !state) {
        return;
    }

    memset(state, 0, sizeof(*state));
    state->af = ctx->af;
    state->bc = ctx->bc;
    state->de = ctx->de;
    state->hl = ctx->hl;
    state->sp = ctx->sp;
    state->pc = ctx->pc;
    state->f_z = ctx->f_z;
    state->f_n = ctx->f_n;
    state->f_h = ctx->f_h;
    state->f_c = ctx->f_c;
    state->ime = ctx->ime;
    state->ime_pending = ctx->ime_pending;
    state->halted = ctx->halted;
    state->stopped = ctx->stopped;
    state->stop_mode_active = ctx->stop_mode_active;
    state->halt_bug = ctx->halt_bug;
    state->single_step_mode = ctx->single_step_mode;
    state->cgb_double_speed = ctx->cgb_double_speed;
    state->cgb_system_cycle_remainder = ctx->cgb_system_cycle_remainder;
    state->config = ctx->config;
    memcpy(state->save_id, ctx->save_id, sizeof(state->save_id));
    state->rom_bank = ctx->rom_bank;
    state->ram_bank = ctx->ram_bank;
    state->wram_bank = ctx->wram_bank;
    state->vram_bank = ctx->vram_bank;
    state->mbc_type = ctx->mbc_type;
    state->ram_enabled = ctx->ram_enabled;
    state->mbc_mode = ctx->mbc_mode;
    state->rom_bank_low = ctx->rom_bank_low;
    state->rom_bank_upper = ctx->rom_bank_upper;
    state->mbc1_multicart = ctx->mbc1_multicart;
    state->rtc_mode = ctx->rtc_mode;
    state->rtc_reg = ctx->rtc_reg;
    state->last_joypad = ctx->last_joypad;
    state->used_dispatch_fallback = ctx->used_dispatch_fallback;
    state->dispatch_fallback_bank = ctx->dispatch_fallback_bank;
    state->has_unimplemented_interpreter_opcode = ctx->has_unimplemented_interpreter_opcode;
    state->last_unimplemented_opcode = ctx->last_unimplemented_opcode;
    state->last_unimplemented_bank = ctx->last_unimplemented_bank;
    state->frame_first_fallback_bank = ctx->frame_first_fallback_bank;
    state->frame_last_fallback_bank = ctx->frame_last_fallback_bank;
    state->dispatch_fallback_addr = ctx->dispatch_fallback_addr;
    state->frame_first_fallback_addr = ctx->frame_first_fallback_addr;
    state->frame_last_fallback_addr = ctx->frame_last_fallback_addr;
    state->div_counter = ctx->div_counter;
    state->tima_reload_pending = ctx->tima_reload_pending;
    memcpy(&state->dma, &ctx->dma, sizeof(state->dma));
    memcpy(&state->hdma, &ctx->hdma, sizeof(state->hdma));
    memcpy(&state->serial_transfer, &ctx->serial_transfer, sizeof(state->serial_transfer));
    memcpy(&state->rtc, &ctx->rtc, sizeof(state->rtc));
    state->cycles = ctx->cycles;
    state->total_cycles = ctx->total_cycles;
    state->frame_cycles = ctx->frame_cycles;
    state->last_sync_cycles = ctx->last_sync_cycles;
    state->run_cycle_budget = ctx->run_cycle_budget;
    state->run_cycle_budget_start = ctx->run_cycle_budget_start;
    state->frame_done = ctx->frame_done;
    state->lcd_off_active = ctx->lcd_off_active;
    state->lcd_off_start_cycles = ctx->lcd_off_start_cycles;
    state->lcd_off_start_frame_cycles = ctx->lcd_off_start_frame_cycles;
    state->frame_lcd_off_cycles = ctx->frame_lcd_off_cycles;
    state->frame_lcd_transition_count = ctx->frame_lcd_transition_count;
    state->frame_lcd_off_span_count = ctx->frame_lcd_off_span_count;
    state->last_lcd_off_span_cycles = ctx->last_lcd_off_span_cycles;
    state->total_lcd_off_cycles = ctx->total_lcd_off_cycles;
    state->total_lcd_transition_count = ctx->total_lcd_transition_count;
    state->total_lcd_off_span_count = ctx->total_lcd_off_span_count;
    state->frame_dispatch_fallbacks = ctx->frame_dispatch_fallbacks;
    state->total_dispatch_fallbacks = ctx->total_dispatch_fallbacks;
    state->total_interpreter_entries = ctx->total_interpreter_entries;
    state->total_interpreter_instructions = ctx->total_interpreter_instructions;
    state->total_interpreter_cycles = ctx->total_interpreter_cycles;
    state->frame_interpreter_instructions = ctx->frame_interpreter_instructions;
    state->frame_interpreter_cycles = ctx->frame_interpreter_cycles;
    state->last_unimplemented_addr = ctx->last_unimplemented_addr;
    memcpy(state->interpreter_hotspots,
           ctx->interpreter_hotspots,
           sizeof(state->interpreter_hotspots));
    state->completed_frames = ctx->completed_frames;
}

static void gbrt_restore_core_state(GBContext* ctx, const GBSavestateCoreState* state) {
    if (!ctx || !state) {
        return;
    }

    ctx->af = state->af;
    ctx->bc = state->bc;
    ctx->de = state->de;
    ctx->hl = state->hl;
    ctx->sp = state->sp;
    ctx->pc = state->pc;
    ctx->f_z = state->f_z;
    ctx->f_n = state->f_n;
    ctx->f_h = state->f_h;
    ctx->f_c = state->f_c;
    ctx->ime = state->ime;
    ctx->ime_pending = state->ime_pending;
    ctx->halted = state->halted;
    ctx->stopped = state->stopped;
    ctx->stop_mode_active = state->stop_mode_active;
    ctx->halt_bug = state->halt_bug;
    ctx->single_step_mode = state->single_step_mode;
    ctx->cgb_double_speed = state->cgb_double_speed;
    ctx->cgb_system_cycle_remainder = state->cgb_system_cycle_remainder;
    ctx->config = state->config;
    memcpy(ctx->save_id, state->save_id, sizeof(ctx->save_id));
    ctx->rom_bank = state->rom_bank;
    ctx->ram_bank = state->ram_bank;
    ctx->wram_bank = state->wram_bank;
    ctx->vram_bank = state->vram_bank;
    ctx->mbc_type = state->mbc_type;
    ctx->ram_enabled = state->ram_enabled;
    ctx->mbc_mode = state->mbc_mode;
    ctx->rom_bank_low = state->rom_bank_low;
    ctx->rom_bank_upper = state->rom_bank_upper;
    ctx->mbc1_multicart = state->mbc1_multicart;
    ctx->rtc_mode = state->rtc_mode;
    ctx->rtc_reg = state->rtc_reg;
    ctx->last_joypad = state->last_joypad;
    ctx->used_dispatch_fallback = state->used_dispatch_fallback;
    ctx->dispatch_fallback_bank = state->dispatch_fallback_bank;
    ctx->has_unimplemented_interpreter_opcode = state->has_unimplemented_interpreter_opcode;
    ctx->last_unimplemented_opcode = state->last_unimplemented_opcode;
    ctx->last_unimplemented_bank = state->last_unimplemented_bank;
    ctx->frame_first_fallback_bank = state->frame_first_fallback_bank;
    ctx->frame_last_fallback_bank = state->frame_last_fallback_bank;
    ctx->dispatch_fallback_addr = state->dispatch_fallback_addr;
    ctx->frame_first_fallback_addr = state->frame_first_fallback_addr;
    ctx->frame_last_fallback_addr = state->frame_last_fallback_addr;
    ctx->div_counter = state->div_counter;
    ctx->tima_reload_pending = state->tima_reload_pending;
    memcpy(&ctx->dma, &state->dma, sizeof(ctx->dma));
    memcpy(&ctx->hdma, &state->hdma, sizeof(ctx->hdma));
    memcpy(&ctx->serial_transfer, &state->serial_transfer, sizeof(ctx->serial_transfer));
    memcpy(&ctx->rtc, &state->rtc, sizeof(ctx->rtc));
    ctx->cycles = state->cycles;
    ctx->total_cycles = state->total_cycles;
    ctx->frame_cycles = state->frame_cycles;
    ctx->last_sync_cycles = state->last_sync_cycles;
    ctx->run_cycle_budget = state->run_cycle_budget;
    ctx->run_cycle_budget_start = state->run_cycle_budget_start;
    ctx->frame_done = state->frame_done;
    ctx->lcd_off_active = state->lcd_off_active;
    ctx->lcd_off_start_cycles = state->lcd_off_start_cycles;
    ctx->lcd_off_start_frame_cycles = state->lcd_off_start_frame_cycles;
    ctx->frame_lcd_off_cycles = state->frame_lcd_off_cycles;
    ctx->frame_lcd_transition_count = state->frame_lcd_transition_count;
    ctx->frame_lcd_off_span_count = state->frame_lcd_off_span_count;
    ctx->last_lcd_off_span_cycles = state->last_lcd_off_span_cycles;
    ctx->total_lcd_off_cycles = state->total_lcd_off_cycles;
    ctx->total_lcd_transition_count = state->total_lcd_transition_count;
    ctx->total_lcd_off_span_count = state->total_lcd_off_span_count;
    ctx->frame_dispatch_fallbacks = state->frame_dispatch_fallbacks;
    ctx->total_dispatch_fallbacks = state->total_dispatch_fallbacks;
    ctx->total_interpreter_entries = state->total_interpreter_entries;
    ctx->total_interpreter_instructions = state->total_interpreter_instructions;
    ctx->total_interpreter_cycles = state->total_interpreter_cycles;
    ctx->frame_interpreter_instructions = state->frame_interpreter_instructions;
    ctx->frame_interpreter_cycles = state->frame_interpreter_cycles;
    ctx->last_unimplemented_addr = state->last_unimplemented_addr;
    memcpy(ctx->interpreter_hotspots,
           state->interpreter_hotspots,
           sizeof(ctx->interpreter_hotspots));
    ctx->completed_frames = state->completed_frames;
}

GBContext* gb_context_create(const GBConfig* config) {
    gbrt_load_ppu_trace_config();

    GBContext* ctx = (GBContext*)calloc(1, sizeof(GBContext));
    if (!ctx) return NULL;

    ctx->config = config ? *config : gb_default_config();
    if (!gb_is_cgb_hardware(ctx)) {
        ctx->config.cgb_compatibility_mode = false;
    }

    ctx->wram = (uint8_t*)calloc(1, WRAM_BANK_SIZE * 8);
    ctx->vram = (uint8_t*)calloc(1, VRAM_SIZE * 2);
    ctx->oam = (uint8_t*)calloc(1, OAM_SIZE);
    ctx->hram = (uint8_t*)calloc(1, HRAM_SIZE);
    ctx->io = (uint8_t*)calloc(1, IO_SIZE + 1);

    if (!ctx->wram || !ctx->vram || !ctx->oam || !ctx->hram || !ctx->io) {
        gb_context_destroy(ctx);
        return NULL;
    }

    GBPPU* ppu = (GBPPU*)calloc(1, sizeof(GBPPU));
    if (ppu) {
        ppu_init(ppu);
        ctx->ppu = ppu;
    }

    if (ctx->config.enable_audio) {
        ctx->apu = gb_audio_create();
        audio_stats_init();
    }
    gb_rtc_reset(ctx);
    gb_context_reset(ctx, true);

    if (gbrt_trace_filename) {
        ctx->trace_file = fopen(gbrt_trace_filename, "w");
        if (ctx->trace_file) {
            ctx->trace_entries_enabled = true;
            fprintf(stderr, "[GBRT] Tracing entry points to %s\n", gbrt_trace_filename);
        }
    }

    if (gbrt_ppu_trace_filename) {
        ctx->ppu_trace_file = fopen(gbrt_ppu_trace_filename, "w");
        if (ctx->ppu_trace_file) {
            fprintf(stderr,
                    "[GBRT] Tracing PPU state to %s (frames %llu-%llu)\n",
                    gbrt_ppu_trace_filename,
                    (unsigned long long)gbrt_ppu_trace_start_frame,
                    (unsigned long long)gbrt_ppu_trace_end_frame);
        }
    }

    return ctx;
}

void gb_context_set_host_configuration_service(
    GBContext* ctx,
    const GBHostConfigurationContract* contract,
    const char* path) {
    if (ctx == NULL) return;
    ctx->host_configuration_contract = contract != NULL
        ? *contract
        : (GBHostConfigurationContract){0};
    ctx->host_configuration_path =
        path != NULL && path[0] != '\0' ? path : NULL;
}

void gb_context_destroy(GBContext* ctx) {
    if (!ctx) return;
    gbrt_port_detach(ctx);
    gbrt_data_mod_unload(ctx);
    
    /* Save RAM before destroying if available */
    if (ctx->eram && ctx->callbacks.save_battery_ram && !ctx->persistence_load_failed) {
        gb_context_save_ram(ctx);
    } else if (ctx->persistence_load_failed) {
        fprintf(stderr,
                "[GBRT] Automatic persistence save suppressed because persisted "
                "data was rejected during load\n");
    }

    if (ctx->trace_file) fclose((FILE*)ctx->trace_file);
    if (ctx->ppu_trace_file) fclose((FILE*)ctx->ppu_trace_file);
    free(ctx->wram);
    free(ctx->vram);
    free(ctx->oam);
    free(ctx->hram);
    free(ctx->io);
    
    if (ctx->eram) free(ctx->eram);
    
    if (ctx->ppu) free(ctx->ppu);
    if (ctx->apu) gb_audio_destroy(ctx->apu);
    if (ctx->rom) free(ctx->rom);
#ifdef GBRT_ENABLE_NATIVE_PATCHES
    gbrt_native_patch_destroy(ctx);
#endif
    free(ctx);
}

void gb_context_reset(GBContext* ctx, bool skip_bootrom) {
    if (!ctx) {
        return;
    }

#ifdef GBRT_ENABLE_NATIVE_PATCHES
    gbrt_native_patch_reset(ctx);
#endif

    if (ctx->apu) {
        gb_audio_reset(ctx->apu);
    }

    if (!gb_is_cgb_hardware(ctx)) {
        ctx->config.cgb_compatibility_mode = false;
    }

    /* Reset DMA state */
    ctx->dma.active = 0;
    ctx->dma.pending = 0;
    ctx->dma.source_high = 0;
    ctx->dma.active_source_high = 0;
    ctx->dma.progress = 0;
    ctx->dma.cycles_remaining = 0;
    ctx->dma.startup_delay = 0;
    memset(&ctx->hdma, 0, sizeof(ctx->hdma));
    
    /* Reset HALT bug state */
    ctx->halt_bug = 0;
    
    /* Reset interrupt state */
    ctx->ime = 0;
    ctx->ime_pending = 0;
    ctx->halted = 0;
    ctx->stopped = 0;
    ctx->stop_mode_active = 0;
    ctx->single_step_mode = 0;
    ctx->cgb_double_speed = 0;
    ctx->cgb_system_cycle_remainder = 0;
    ctx->audio_pending_cpu_cycles = 0u;
    ctx->audio_cycles_until_event = 0u;
    ctx->audio_pending_old_div = 0u;
    ctx->audio_pending_system_cycle_remainder = 0u;
    ctx->audio_pending_double_speed = 0u;
    memset(&ctx->serial_transfer, 0, sizeof(ctx->serial_transfer));
    ctx->last_joypad = 0xFF;
    ctx->used_dispatch_fallback = 0;
    ctx->dispatch_fallback_bank = 0;
    ctx->dispatch_fallback_addr = 0;
    ctx->completed_frames = 0;
    ctx->frame_dispatch_fallbacks = 0;
    ctx->total_dispatch_fallbacks = 0;
    ctx->frame_first_fallback_bank = 0;
    ctx->frame_first_fallback_addr = 0;
    ctx->frame_last_fallback_bank = 0;
    ctx->frame_last_fallback_addr = 0;
    ctx->total_interpreter_entries = 0;
    ctx->total_interpreter_instructions = 0;
    ctx->total_interpreter_cycles = 0;
    ctx->frame_interpreter_instructions = 0;
    ctx->frame_interpreter_cycles = 0;
    ctx->has_unimplemented_interpreter_opcode = 0;
    ctx->last_unimplemented_opcode = 0;
    ctx->last_unimplemented_bank = 0;
    ctx->last_unimplemented_addr = 0;
    memset(ctx->interpreter_hotspots, 0, sizeof(ctx->interpreter_hotspots));
    memset(ctx->dispatch_fallback_sites, 0, sizeof(ctx->dispatch_fallback_sites));
    ctx->dispatch_fallback_site_count = 0;
    ctx->dispatch_fallback_sites_dropped = 0;
    ctx->lcd_off_active = 0;
    ctx->lcd_off_start_cycles = 0;
    ctx->lcd_off_start_frame_cycles = 0;
    ctx->frame_lcd_off_cycles = 0;
    ctx->frame_lcd_transition_count = 0;
    ctx->frame_lcd_off_span_count = 0;
    ctx->last_lcd_off_span_cycles = 0;
    ctx->total_lcd_off_cycles = 0;
    ctx->total_lcd_transition_count = 0;
    ctx->total_lcd_off_span_count = 0;
    gbrt_reset_performance_counters(ctx);
    
    /* Reset MBC state */
    ctx->rtc_mode = 0;
    ctx->rtc_reg = 0;
    ctx->ram_enabled = 0;
    ctx->mbc_mode = 0;
    ctx->rom_bank_low = 1;
    ctx->rom_bank_upper = 0;
    ctx->rom_bank = 1;
    ctx->ram_bank = 0;
    ctx->wram_bank = 1;
    ctx->vram_bank = 0;

    memset(ctx->io, 0, IO_SIZE + 1);
    
    if (skip_bootrom) {
        ctx->pc = 0x0100;
        ctx->sp = 0xFFFE;

        if (gb_is_cgb_mode(ctx)) {
            ctx->af = 0x1180;
            ctx->bc = 0x0000;
            ctx->de = 0xFF56;
            ctx->hl = 0x000D;
            ctx->div_counter = 0x0000;
        } else if (gb_is_cgb_compat_mode(ctx)) {
            uint8_t compat_b = gb_compute_cgb_compat_b(ctx);
            ctx->af = 0x1180;
            ctx->bc = compat_b;
            ctx->de = 0x0008;
            ctx->hl = (compat_b == 0x43 || compat_b == 0x58) ? 0x991A : 0x007C;
            ctx->div_counter = 0x0000;
        } else {
            ctx->af = 0x01B0;
            ctx->bc = 0x0013;
            ctx->de = 0x00D8;
            ctx->hl = 0x014D;
            ctx->div_counter = 0xABCC; /* Post-bootrom DIV internal counter value */
        }
        gb_unpack_flags(ctx);

        ctx->io[0x00] = 0xCF; /* JOYP */
        ctx->io[0x01] = 0x00; /* SB */
        ctx->io[0x02] = gb_is_cgb_hardware(ctx) ? 0x7F : 0x7E; /* SC */
        ctx->io[0x04] = (uint8_t)(ctx->div_counter >> 8); /* DIV */
        ctx->io[0x05] = 0x00; /* TIMA */
        ctx->io[0x06] = 0x00; /* TMA */
        ctx->io[0x07] = 0xF8; /* TAC */
        ctx->io[0x0F] = 0xE1; /* IF */
        ctx->io[0x10] = 0x80; /* NR10 */
        ctx->io[0x11] = 0xBF; /* NR11 */
        ctx->io[0x12] = 0xF3; /* NR12 */
        ctx->io[0x13] = 0xFF; /* NR13 */
        ctx->io[0x14] = 0xBF; /* NR14 */
        ctx->io[0x16] = 0x3F; /* NR21 */
        ctx->io[0x17] = 0x00; /* NR22 */
        ctx->io[0x18] = 0xFF; /* NR23 */
        ctx->io[0x19] = 0xBF; /* NR24 */
        ctx->io[0x1A] = 0x7F; /* NR30 */
        ctx->io[0x1B] = 0xFF; /* NR31 */
        ctx->io[0x1C] = 0x9F; /* NR32 */
        ctx->io[0x1D] = 0xFF; /* NR33 */
        ctx->io[0x1E] = 0xBF; /* NR34 */
        ctx->io[0x20] = 0xFF; /* NR41 */
        ctx->io[0x21] = 0x00; /* NR42 */
        ctx->io[0x22] = 0x00; /* NR43 */
        ctx->io[0x23] = 0xBF; /* NR44 */
        ctx->io[0x24] = 0x77; /* NR50 */
        ctx->io[0x25] = 0xF3; /* NR51 */
        ctx->io[0x26] = 0xF1; /* NR52 */
        ctx->io[0x46] = gb_is_cgb_hardware(ctx) ? 0x00 : 0xFF; /* DMA */
        if (gb_is_cgb_hardware(ctx)) {
            ctx->io[0x4D] = 0x7E; /* KEY1 */
            ctx->io[0x4F] = 0xFE; /* VBK */
            ctx->io[0x51] = 0xFF; /* HDMA1 */
            ctx->io[0x52] = 0xFF; /* HDMA2 */
            ctx->io[0x53] = 0xFF; /* HDMA3 */
            ctx->io[0x54] = 0xFF; /* HDMA4 */
            ctx->io[0x55] = 0xFF; /* HDMA5 */
            ctx->io[0x56] = 0x3E; /* RP */
            ctx->io[0x68] = 0xC0; /* BGPI */
            ctx->io[0x6A] = 0xC0; /* OBPI */
            ctx->io[0x70] = 0xF8; /* SVBK */
        }
        ctx->io[0x80] = 0x00; /* IE */
    }

    if (ctx->ppu) {
        ppu_reset((GBPPU*)ctx->ppu, ctx);
    }
}

bool gb_context_load_rom(GBContext* ctx, const uint8_t* data, size_t size) {
    gbrt_data_mod_unload(ctx);
    if (ctx->rom) free(ctx->rom);
    ctx->rom = (uint8_t*)malloc(size);
    if (!ctx->rom) return false;
    memcpy(ctx->rom, data, size);
    ctx->rom_size = size;
    /* A new cartridge starts from a clean RTC before any persisted state is
     * loaded. Console/CPU resets preserve the cartridge clock. */
    gb_rtc_reset(ctx);
    
    /* Parse Header for RAM/Battery info */
    if (size > 0x149) {
        uint8_t type = ctx->rom[0x147];
        uint8_t ram_size_code = ctx->rom[0x149];

        ctx->mbc_type = type;
        ctx->mbc1_multicart =
            (type >= 0x01 && type <= 0x03) &&
            gb_detect_mbc1_multicart(ctx->rom, ctx->rom_size);

        bool has_battery = gb_cart_type_has_battery(type);

        /* Calculate RAM size */
        size_t ram_bytes = 0;

        /* MBC2 has fixed 512x4 bits (256 bytes effective, usually 512 allocated) */
        if (type == 0x05 || type == 0x06) {
            ram_bytes = 512;
            ram_size_code = 0; /* Override */
        } else {
            switch (ram_size_code) {
                case 0x00: ram_bytes = 0; break;
                case 0x01: ram_bytes = 2 * 1024; break; /* 2KB */
                case 0x02: ram_bytes = 8 * 1024; break; /* 8KB */
                case 0x03: ram_bytes = 32 * 1024; break; /* 32KB (4 banks) */
                case 0x04: ram_bytes = 128 * 1024; break; /* 128KB (16 banks) */
                case 0x05: ram_bytes = 64 * 1024; break; /* 64KB (8 banks) */
                default: ram_bytes = 0; break;
            }
        }
        
        /* Allocate RAM */
        if (ctx->eram) free(ctx->eram);
        ctx->eram = NULL;
        ctx->eram_size = 0;
        
        if (ram_bytes > 0) {
            ctx->eram = (uint8_t*)calloc(1, ram_bytes);
            if (ctx->eram) {
                ctx->eram_size = ram_bytes;
                printf("[GBRT] Allocated %zu bytes for External RAM\n", ram_bytes);
                
                /* Load Save Data if Battery Present */
                if (has_battery) {
                    gb_context_try_load_battery_ram(ctx);
                    gb_context_try_load_rtc(ctx);
                }
            }
        }
    }
    
    return true;
}

bool gb_context_save_battery_snapshot(
    GBContext* ctx,
    const uint8_t* data,
    size_t size) {
    if (!ctx || !ctx->eram || !ctx->eram_size ||
        !ctx->callbacks.save_battery_ram || !data || size != ctx->eram_size) {
        return false;
    }
    if (ctx->persistence_load_failed) {
        fprintf(stderr,
                "[GBRT] Refusing to overwrite persistence after a rejected load\n");
        return false;
    }
    
    char save_id[64];
    gb_context_get_save_id(ctx, save_id);
    
    bool ram_result =
        ctx->callbacks.save_battery_ram(ctx, save_id, data, size);
    if (ram_result) {
        printf("[GBRT] Saved battery RAM for '%s'\n", save_id);
    } else {
        printf("[GBRT] Failed to save battery RAM for '%s'\n", save_id);
    }

    return ram_result;
}

bool gb_context_save_ram(GBContext* ctx) {
    if (!ctx || !ctx->eram || !ctx->eram_size) {
        return false;
    }
    bool ram_result =
        gb_context_save_battery_snapshot(ctx, ctx->eram, ctx->eram_size);

    bool rtc_result = true;
    if (ctx->rom && ctx->rom_size > 0x147u &&
        gb_cart_type_has_rtc(ctx->rom[0x147])) {
        rtc_result = gb_context_save_rtc(ctx);
    }
    return ram_result && rtc_result;
}

static const char* gbrt_semantic_transaction_outcome_name(
    GBSemanticTransactionOutcome outcome) {
    switch (outcome) {
        case GB_SEMANTIC_TRANSACTION_COMMITTED: return "committed";
        case GB_SEMANTIC_TRANSACTION_ABORTED: return "aborted";
        case GB_SEMANTIC_TRANSACTION_VALIDATION_FAILED:
            return "validation_failed";
        case GB_SEMANTIC_TRANSACTION_COMMIT_FAILED: return "commit_failed";
        default: return "none";
    }
}

bool gb_context_write_state_json(const GBContext* ctx, const char* path) {
    if (!ctx || !path || !path[0]) {
        return false;
    }
    FILE* file = fopen(path, "wb");
    if (!file) {
        return false;
    }

    const GBPPU* ppu = (const GBPPU*)ctx->ppu;
    const uint8_t flags = (uint8_t)((ctx->f_z ? 0x80 : 0) |
                                    (ctx->f_n ? 0x40 : 0) |
                                    (ctx->f_h ? 0x20 : 0) |
                                    (ctx->f_c ? 0x10 : 0));
    const int written = fprintf(
        file,
        "{\n"
        "  \"a\": %u,\n"
        "  \"f\": %u,\n"
        "  \"b\": %u,\n"
        "  \"c\": %u,\n"
        "  \"d\": %u,\n"
        "  \"e\": %u,\n"
        "  \"h\": %u,\n"
        "  \"l\": %u,\n"
        "  \"sp\": %u,\n"
        "  \"pc\": %u,\n"
        "  \"cycles\": %u,\n"
        "  \"total_cycles\": %llu,\n"
        "  \"completed_frames\": %llu,\n"
        "  \"ly\": %u,\n"
        "  \"ppu_mode\": %u,\n"
        "  \"rtc\": {\"seconds\": %u, \"minutes\": %u, \"hours\": %u, "
        "\"day_low\": %u, \"day_high\": %u, \"cycle_remainder\": %llu, "
        "\"active\": %s},\n"
        "  \"hram_ff80_ff90\": [",
        ctx->a,
        flags,
        ctx->b,
        ctx->c,
        ctx->d,
        ctx->e,
        ctx->h,
        ctx->l,
        ctx->sp,
        ctx->pc,
        ctx->cycles,
        (unsigned long long)ctx->total_cycles,
        (unsigned long long)ctx->completed_frames,
        ppu ? ppu->ly : 0,
        ppu ? (unsigned)ppu->mode : 0,
        ctx->rtc.s,
        ctx->rtc.m,
        ctx->rtc.h,
        ctx->rtc.dl,
        ctx->rtc.dh,
        (unsigned long long)ctx->rtc.last_time,
        ctx->rtc.active ? "true" : "false");
    bool success = written > 0;
    for (size_t i = 0; success && i < 17; ++i) {
        success = fprintf(file, "%s%u", i ? ", " : "", ctx->hram[i]) > 0;
    }
    if (success) {
        success = fprintf(file, "],\n  \"hram_ff80_fffe\": [") > 0;
    }
    for (size_t i = 0; success && i < HRAM_SIZE; ++i) {
        success = fprintf(file, "%s%u", i ? ", " : "", ctx->hram[i]) > 0;
    }
    if (success) {
        success = fprintf(file, "],\n  \"wram_bank_0_c000_cfff\": [") > 0;
    }
    for (size_t i = 0; success && i < WRAM_BANK_SIZE; ++i) {
        success = fprintf(file, "%s%u", i ? ", " : "", ctx->wram[i]) > 0;
    }
    if (success) {
        success = fprintf(file, "],\n  \"wram_bank_1_d000_dfff\": [") > 0;
    }
    for (size_t i = 0; success && i < WRAM_BANK_SIZE; ++i) {
        success = fprintf(file,
                          "%s%u",
                          i ? ", " : "",
                          ctx->wram[WRAM_BANK_SIZE + i]) > 0;
    }
    if (success) {
        success = fprintf(file, "],\n  \"eram_a000_a0ff\": [") > 0;
    }
    const size_t eram_prefix_size =
        ctx->eram_size < 0x100u ? ctx->eram_size : 0x100u;
    for (size_t i = 0; success && i < eram_prefix_size; ++i) {
        success = fprintf(file, "%s%u", i ? ", " : "", ctx->eram[i]) > 0;
    }
    if (success) {
        success = fprintf(
                      file,
                      "],\n"
                      "  \"host_configuration\": {"
                      "\"present\": %s, \"applied\": %s, "
                      "\"enabled\": %s, \"policy_id\": \"%s\", "
                      "\"sha256\": \"%s\"},\n"
                      "  \"semantic_transaction\": {"
                      "\"sequence\": %llu, \"outcome\": \"%s\", "
                      "\"dirty_ranges\": [",
                      ctx->config.host_configuration.present ? "true" : "false",
                      ctx->config.host_configuration.applied ? "true" : "false",
                      ctx->config.host_configuration.enabled ? "true" : "false",
                      ctx->config.host_configuration.present
                          ? ctx->config.host_configuration.policy_id
                          : "",
                      ctx->config.host_configuration.present
                          ? ctx->config.host_configuration.sha256
                          : "",
                      (unsigned long long)ctx->semantic_transaction_sequence,
                      gbrt_semantic_transaction_outcome_name(
                          ctx->semantic_transaction_outcome)) > 0;
    }
    for (size_t index = 0;
         success && index < ctx->semantic_transaction_dirty_count;
         ++index) {
        const GBSemanticTransactionRangeMetadata* range =
            &ctx->semantic_transaction_dirty[index];
        success = fprintf(
                      file,
                      "%s{\"space\": %u, \"bank\": %u, "
                      "\"address\": %u, \"width\": %u}",
                      index ? ", " : "",
                      range->space,
                      range->bank,
                      range->address,
                      range->width) > 0;
    }
    if (success) {
        success = fprintf(
                      file,
                      "]},\n  \"dispatch_fallbacks\": %llu\n}\n",
                      (unsigned long long)ctx->total_dispatch_fallbacks) > 0;
    }
    success = success && fflush(file) == 0 && ferror(file) == 0;
    if (fclose(file) != 0) {
        return false;
    }
    if (!success) {
        remove(path);
    }
    return success;
}

bool gb_context_save_state_file(GBContext* ctx, const char* path) {
    if (!ctx || !path || !path[0] || !ctx->rom || ctx->rom_size == 0 ||
        !ctx->wram || !ctx->vram || !ctx->oam || !ctx->hram || !ctx->io) {
        return false;
    }

    gbrt_audio_sync(ctx);

    FILE* file = fopen(path, "wb");
    if (!file) {
        fprintf(stderr, "[GBRT] Failed to open savestate for writing: %s\n", path);
        return false;
    }

    const size_t apu_state_size = ctx->apu ? gb_audio_state_size() : 0;
    void* apu_state = NULL;
    bool success = true;

    GBSavestateFileHeader header;
    memset(&header, 0, sizeof(header));
    header.magic = GBSAVESTATE_MAGIC;
    header.version = GBSAVESTATE_VERSION;
    header.rom_hash = gb_context_compute_rom_hash(ctx);
    header.rom_size = (uint32_t)ctx->rom_size;
    header.eram_size = (uint32_t)ctx->eram_size;
    header.wram_size = WRAM_BANK_SIZE * 8u;
    header.vram_size = VRAM_SIZE * 2u;
    header.oam_size = OAM_SIZE;
    header.hram_size = HRAM_SIZE;
    header.io_size = IO_SIZE + 1u;
    header.ppu_size = ctx->ppu ? (uint32_t)sizeof(GBPPU) : 0u;
    header.apu_size = (uint32_t)apu_state_size;

    GBSavestateCoreState core_state;
    gbrt_capture_core_state(ctx, &core_state);

    if (apu_state_size > 0) {
        apu_state = malloc(apu_state_size);
        if (!apu_state || !gb_audio_save_state(ctx->apu, apu_state, apu_state_size)) {
            fprintf(stderr, "[GBRT] Failed to serialize APU state for savestate: %s\n", path);
            success = false;
        }
    }

    if (success) success = gbrt_write_exact(file, &header, sizeof(header));
    if (success) success = gbrt_write_exact(file, &core_state, sizeof(core_state));
    if (success) success = gbrt_write_exact(file, ctx->eram, ctx->eram_size);
    if (success) success = gbrt_write_exact(file, ctx->wram, WRAM_BANK_SIZE * 8u);
    if (success) success = gbrt_write_exact(file, ctx->vram, VRAM_SIZE * 2u);
    if (success) success = gbrt_write_exact(file, ctx->oam, OAM_SIZE);
    if (success) success = gbrt_write_exact(file, ctx->hram, HRAM_SIZE);
    if (success) success = gbrt_write_exact(file, ctx->io, IO_SIZE + 1u);
    if (success && header.ppu_size > 0) success = gbrt_write_exact(file, ctx->ppu, sizeof(GBPPU));
    if (success && apu_state_size > 0) success = gbrt_write_exact(file, apu_state, apu_state_size);

    if (!success) {
        fprintf(stderr, "[GBRT] Failed to write savestate: %s\n", path);
    }

    fclose(file);
    if (!success) {
        remove(path);
    } else {
        printf("[GBRT] Saved state to %s\n", path);
    }
    free(apu_state);
    return success;
}

bool gb_context_load_state_file(GBContext* ctx, const char* path) {
    if (!ctx || !path || !path[0] || !ctx->rom || ctx->rom_size == 0 ||
        !ctx->wram || !ctx->vram || !ctx->oam || !ctx->hram || !ctx->io) {
        return false;
    }

    FILE* file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "[GBRT] Failed to open savestate for reading: %s\n", path);
        return false;
    }

    bool success = true;
    GBSavestateFileHeader header;
    memset(&header, 0, sizeof(header));

    GBSavestateCoreState core_state;
    memset(&core_state, 0, sizeof(core_state));

    void* eram_data = NULL;
    void* wram_data = NULL;
    void* vram_data = NULL;
    void* oam_data = NULL;
    void* hram_data = NULL;
    void* io_data = NULL;
    void* ppu_data = NULL;
    void* apu_data = NULL;

    if (!gbrt_read_exact(file, &header, sizeof(header))) {
        fprintf(stderr, "[GBRT] Failed to read savestate header: %s\n", path);
        success = false;
    }

    const uint64_t expected_rom_hash = gb_context_compute_rom_hash(ctx);
    const size_t expected_apu_size = ctx->apu ? gb_audio_state_size() : 0;
    if (success && header.magic != GBSAVESTATE_MAGIC) {
        fprintf(stderr, "[GBRT] Savestate has invalid magic: %s\n", path);
        success = false;
    }
    if (success && header.version != GBSAVESTATE_VERSION) {
        fprintf(stderr, "[GBRT] Savestate version mismatch for %s (got %u, expected %u)\n",
                path,
                header.version,
                GBSAVESTATE_VERSION);
        success = false;
    }
    if (success &&
        (header.rom_size != ctx->rom_size || header.rom_hash != expected_rom_hash)) {
        fprintf(stderr, "[GBRT] Savestate ROM mismatch for %s\n", path);
        success = false;
    }
    if (success &&
        (header.eram_size != ctx->eram_size ||
         header.wram_size != WRAM_BANK_SIZE * 8u ||
         header.vram_size != VRAM_SIZE * 2u ||
         header.oam_size != OAM_SIZE ||
         header.hram_size != HRAM_SIZE ||
         header.io_size != IO_SIZE + 1u)) {
        fprintf(stderr, "[GBRT] Savestate memory layout mismatch for %s\n", path);
        success = false;
    }
    if (success && header.ppu_size != (ctx->ppu ? (uint32_t)sizeof(GBPPU) : 0u)) {
        fprintf(stderr, "[GBRT] Savestate PPU layout mismatch for %s\n", path);
        success = false;
    }
    if (success && header.apu_size != expected_apu_size) {
        fprintf(stderr, "[GBRT] Savestate APU layout mismatch for %s\n", path);
        success = false;
    }

    if (success) success = gbrt_read_exact(file, &core_state, sizeof(core_state));
    if (!success) {
        fclose(file);
        return false;
    }

    if (ctx->eram_size > 0) {
        eram_data = malloc(ctx->eram_size);
        success = eram_data != NULL && gbrt_read_exact(file, eram_data, ctx->eram_size);
    }
    if (success) {
        wram_data = malloc(WRAM_BANK_SIZE * 8u);
        success = wram_data != NULL && gbrt_read_exact(file, wram_data, WRAM_BANK_SIZE * 8u);
    }
    if (success) {
        vram_data = malloc(VRAM_SIZE * 2u);
        success = vram_data != NULL && gbrt_read_exact(file, vram_data, VRAM_SIZE * 2u);
    }
    if (success) {
        oam_data = malloc(OAM_SIZE);
        success = oam_data != NULL && gbrt_read_exact(file, oam_data, OAM_SIZE);
    }
    if (success) {
        hram_data = malloc(HRAM_SIZE);
        success = hram_data != NULL && gbrt_read_exact(file, hram_data, HRAM_SIZE);
    }
    if (success) {
        io_data = malloc(IO_SIZE + 1u);
        success = io_data != NULL && gbrt_read_exact(file, io_data, IO_SIZE + 1u);
    }
    if (success && header.ppu_size > 0) {
        ppu_data = malloc(header.ppu_size);
        success = ppu_data != NULL && gbrt_read_exact(file, ppu_data, header.ppu_size);
    }
    if (success && header.apu_size > 0) {
        apu_data = malloc(header.apu_size);
        success = apu_data != NULL && gbrt_read_exact(file, apu_data, header.apu_size);
    }

    fclose(file);

    if (!success) {
        fprintf(stderr, "[GBRT] Failed to load savestate data: %s\n", path);
        free(eram_data);
        free(wram_data);
        free(vram_data);
        free(oam_data);
        free(hram_data);
        free(io_data);
        free(ppu_data);
        free(apu_data);
        return false;
    }

    if (ctx->eram_size > 0) memcpy(ctx->eram, eram_data, ctx->eram_size);
    memcpy(ctx->wram, wram_data, WRAM_BANK_SIZE * 8u);
    memcpy(ctx->vram, vram_data, VRAM_SIZE * 2u);
    memcpy(ctx->oam, oam_data, OAM_SIZE);
    memcpy(ctx->hram, hram_data, HRAM_SIZE);
    memcpy(ctx->io, io_data, IO_SIZE + 1u);
    if (header.ppu_size > 0 && ctx->ppu) memcpy(ctx->ppu, ppu_data, header.ppu_size);
    if (header.apu_size > 0 && ctx->apu && !gb_audio_load_state(ctx->apu, apu_data, header.apu_size)) {
        fprintf(stderr, "[GBRT] Failed to restore APU state from savestate: %s\n", path);
        free(eram_data);
        free(wram_data);
        free(vram_data);
        free(oam_data);
        free(hram_data);
        free(io_data);
        free(ppu_data);
        free(apu_data);
        return false;
    }
    gbrt_restore_core_state(ctx, &core_state);
    ctx->audio_pending_cpu_cycles = 0u;
    ctx->audio_cycles_until_event = 0u;
    ctx->audio_pending_old_div = ctx->div_counter;
    ctx->audio_pending_system_cycle_remainder =
        ctx->cgb_system_cycle_remainder & 1u;
    ctx->audio_pending_double_speed = ctx->cgb_double_speed ? 1u : 0u;

    free(eram_data);
    free(wram_data);
    free(vram_data);
    free(oam_data);
    free(hram_data);
    free(io_data);
    free(ppu_data);
    free(apu_data);

    printf("[GBRT] Loaded state from %s\n", path);
    return true;
}

static uint8_t gb_direct_read_dma_source(GBContext* ctx, uint16_t addr) {
    if (addr < 0x8000) {
        size_t rom_offset = 0;
        return gb_resolve_rom_offset(ctx, addr, &rom_offset)
            ? gbrt_data_mod_read_rom(ctx, rom_offset, false)
            : 0xFF;
    }

    if (addr < 0xA000) {
        uint32_t vram_addr =
            ((uint32_t)ctx->vram_bank * VRAM_SIZE) + (addr - 0x8000u);
        return ctx->vram ? ctx->vram[vram_addr] : 0xFF;
    }

    if (addr >= 0xA000 && addr < 0xC000) {
        if (!ctx->eram || !ctx->ram_enabled) {
            return 0xFF;
        }
        uint32_t eram_addr = ((uint32_t)ctx->ram_bank * 0x2000u) + (uint32_t)(addr - 0xA000);
        return (eram_addr < ctx->eram_size) ? ctx->eram[eram_addr] : 0xFF;
    }

    if (addr >= 0xC000 && addr < 0xD000) {
        return ctx->wram[addr - 0xC000];
    }

    if (addr >= 0xD000 && addr < 0xE000) {
        return ctx->wram[(ctx->wram_bank * WRAM_BANK_SIZE) + (addr - 0xD000)];
    }

    if (addr >= 0xE000 && addr < 0xFE00) {
        return gb_direct_read_dma_source(ctx, (uint16_t)(addr - 0x2000));
    }

    return 0xFF;
}

static uint8_t gb_direct_read_oam_dma_source(GBContext* ctx, uint16_t addr) {
    if (addr >= 0xE000) {
        /* DMG exposes only the lower 13 WRAM address lines to OAM DMA, so
         * E000-FFFF mirror C000-DFFF even across the normal FE00 echo cutoff.
         * CGB-family hardware instead returns open-bus FF for these sources.
         */
        if (gb_is_cgb_hardware(ctx)) {
            return 0xFF;
        }
        addr = (uint16_t)(addr & ~0x2000u);
    }
    return gb_direct_read_dma_source(ctx, addr);
}

static void gb_hdma_refresh_registers(GBContext* ctx);

static void gb_hdma_copy_block(GBContext* ctx) {
    if (!ctx || !gb_is_cgb_mode(ctx) || !ctx->hdma.blocks_remaining) {
        return;
    }

    for (uint16_t offset = 0; offset < 0x10; offset++) {
        uint16_t src = (uint16_t)(ctx->hdma.source + offset);
        uint16_t dest = (uint16_t)(ctx->hdma.dest + offset);
        if (dest >= 0x8000 && dest < 0xA000) {
            ctx->vram[(ctx->vram_bank * VRAM_SIZE) + (dest - 0x8000)] =
                gb_direct_read_dma_source(ctx, src);
        }
    }

    ctx->hdma.source = (uint16_t)(ctx->hdma.source + 0x10);
    ctx->hdma.dest = (uint16_t)(ctx->hdma.dest + 0x10);

    if (ctx->hdma.blocks_remaining > 0) {
        ctx->hdma.blocks_remaining--;
    }

    if (ctx->hdma.blocks_remaining == 0 || ctx->hdma.dest >= 0xA000) {
        ctx->hdma.active = 0;
        ctx->hdma.hblank_mode = 0;
        ctx->hdma.blocks_remaining = 0;
    } else if (ctx->hdma.hblank_mode) {
        ctx->hdma.active = 0;
    }

    gb_hdma_refresh_registers(ctx);
}

void gbrt_hdma_hblank(GBContext* ctx) {
    if (!ctx || !gb_is_cgb_mode(ctx)) {
        return;
    }
    if (!ctx->hdma.hblank_mode || ctx->hdma.active || ctx->halted || ctx->stop_mode_active) {
        return;
    }

    ctx->hdma.active = 1;
    gb_hdma_copy_block(ctx);
    ctx->hdma.cpu_stall_cycles = (uint16_t)(
        ctx->hdma.cpu_stall_cycles + (ctx->cgb_double_speed ? 64u : 32u));
}

static uint8_t gb_hdma_status_read(const GBContext* ctx) {
    if (!ctx || !gb_is_cgb_mode(ctx)) {
        return 0xFF;
    }

    if (!ctx->hdma.active && !ctx->hdma.hblank_mode && ctx->hdma.blocks_remaining == 0) {
        return 0xFF;
    }

    return (uint8_t)(((ctx->hdma.active || ctx->hdma.hblank_mode) ? 0x00 : 0x80) |
                     ((ctx->hdma.blocks_remaining - 1) & 0x7F));
}

static void gb_hdma_refresh_registers(GBContext* ctx) {
    ctx->io[0x51] = (uint8_t)(ctx->hdma.source >> 8);
    ctx->io[0x52] = (uint8_t)(ctx->hdma.source & 0xF0);
    ctx->io[0x53] = (uint8_t)(((ctx->hdma.dest - 0x8000) >> 8) & 0x1F);
    ctx->io[0x54] = (uint8_t)(ctx->hdma.dest & 0xF0);
    ctx->io[0x55] = gb_hdma_status_read(ctx);
}

static void gb_hdma_start(GBContext* ctx, uint8_t value) {
    if (!ctx || !gb_is_cgb_mode(ctx)) {
        return;
    }

    if (ctx->hdma.hblank_mode && (value & 0x80) == 0) {
        ctx->hdma.active = 0;
        ctx->hdma.hblank_mode = 0;
        gb_hdma_refresh_registers(ctx);
        return;
    }

    ctx->hdma.blocks_remaining = (uint8_t)((value & 0x7F) + 1);
    ctx->hdma.source = (uint16_t)(((uint16_t)ctx->io[0x51] << 8) | (ctx->io[0x52] & 0xF0));
    ctx->hdma.dest = (uint16_t)(0x8000 | (((uint16_t)ctx->io[0x53] & 0x1F) << 8) | (ctx->io[0x54] & 0xF0));
    ctx->hdma.hblank_mode = (value & 0x80) != 0;
    ctx->hdma.active = 1;

    if (!ctx->hdma.hblank_mode) {
        while (ctx->hdma.active || ctx->hdma.blocks_remaining > 0) {
            gb_hdma_copy_block(ctx);
            /* The CPU is blocked, but every other clocked device advances. */
            gb_tick(ctx, ctx->cgb_double_speed ? 64u : 32u);
        }
    } else {
        ctx->hdma.active = 0;
    }

    gb_hdma_refresh_registers(ctx);
}

/* ============================================================================
 * Memory Access
 * ========================================================================== */

typedef enum {
    GB_DMA_BUS_NONE,
    GB_DMA_BUS_CARTRIDGE,
    GB_DMA_BUS_VRAM,
    GB_DMA_BUS_WRAM,
} GBDMABus;

static GBDMABus gb_oam_dma_bus_for_addr(uint16_t addr) {
    if (addr < 0x8000 || (addr >= 0xA000 && addr < 0xC000)) {
        return GB_DMA_BUS_CARTRIDGE;
    }
    if (addr < 0xA000) {
        return GB_DMA_BUS_VRAM;
    }
    if (addr >= 0xC000) {
        return GB_DMA_BUS_WRAM;
    }
    return GB_DMA_BUS_NONE;
}

static bool gb_oam_dma_blocks_cpu_addr(const GBContext* ctx, uint16_t addr) {
    if (!ctx) {
        return false;
    }
    const bool startup_bus_block =
        ctx->dma.pending && ctx->dma.startup_delay <= 4;
    if (!ctx->dma.active && !startup_bus_block) {
        return false;
    }
    if (addr >= 0xFF80 && addr <= 0xFFFE) {
        return false;
    }
    /* FF46 remains writable so an in-flight transfer can be restarted. */
    if (addr == 0xFF46) {
        return false;
    }
    if (!gb_is_cgb_hardware(ctx)) {
        if (addr >= 0xFE00) {
            return true;
        }

        /* DMG has a dedicated VRAM bus, while cartridge and WRAM share the
         * main bus. OAM DMA only steals the source bus, so a VRAM-source
         * transfer still permits ROM/WRAM access and vice versa. */
        const GBDMABus source_bus = gb_oam_dma_bus_for_addr(
            (uint16_t)(ctx->dma.active
                ? ((uint16_t)ctx->dma.active_source_high << 8)
                : ((uint16_t)ctx->dma.source_high << 8)));
        const GBDMABus address_bus = gb_oam_dma_bus_for_addr(addr);
        if (source_bus == GB_DMA_BUS_VRAM) {
            return address_bus == GB_DMA_BUS_VRAM;
        }
        return address_bus == GB_DMA_BUS_CARTRIDGE ||
               address_bus == GB_DMA_BUS_WRAM;
    }
    if (addr >= 0xFE00) {
        return false;
    }

    const uint8_t source_high = ctx->dma.active
        ? ctx->dma.active_source_high
        : ctx->dma.source_high;
    const uint16_t source = (uint16_t)source_high << 8;
    return gb_oam_dma_bus_for_addr(addr) == gb_oam_dma_bus_for_addr(source);
}

#define GB_TIMA_RELOAD_CYCLE_FLAG 0x80u

static uint16_t gb_timer_mask(uint8_t tac) {
    static const uint16_t masks[] = {
        1u << 9,
        1u << 3,
        1u << 5,
        1u << 7,
    };
    return masks[tac & 0x03u];
}

static bool gb_timer_input(const GBContext* ctx, uint8_t tac) {
    return (tac & 0x04u) != 0 &&
           (ctx->div_counter & gb_timer_mask(tac)) != 0;
}

static void gb_timer_increment(GBContext* ctx) {
    if (ctx->io[0x05] == 0xFF) {
        ctx->io[0x05] = 0x00;
        ctx->tima_reload_pending = 4;
    } else {
        ctx->io[0x05]++;
    }
}

static void gb_timer_advance_reload_state(GBContext* ctx) {
    uint8_t state = ctx->tima_reload_pending;
    if (state == 0) {
        return;
    }

    if (state & GB_TIMA_RELOAD_CYCLE_FLAG) {
        uint8_t remaining = state & (uint8_t)~GB_TIMA_RELOAD_CYCLE_FLAG;
        ctx->tima_reload_pending = remaining > 1
            ? (uint8_t)(GB_TIMA_RELOAD_CYCLE_FLAG | (remaining - 1u))
            : 0;
        return;
    }

    state--;
    if (state == 0) {
        ctx->io[0x05] = ctx->io[0x06];
        ctx->io[0x0F] |= 0x04;
        ctx->tima_reload_pending = GB_TIMA_RELOAD_CYCLE_FLAG | 4u;
    } else {
        ctx->tima_reload_pending = state;
    }
}

static void gb_timer_tick_scalar(GBContext* ctx, uint32_t cpu_cycles) {
    for (uint32_t cycle = 0; cycle < cpu_cycles; ++cycle) {
        gb_timer_advance_reload_state(ctx);
        const bool old_input = gb_timer_input(ctx, ctx->io[0x07]);
        ctx->div_counter++;
        const bool new_input = gb_timer_input(ctx, ctx->io[0x07]);
        if (old_input && !new_input) {
            gb_timer_increment(ctx);
        }
    }
    ctx->io[0x04] = (uint8_t)(ctx->div_counter >> 8);
}

static void gb_timer_tick_arithmetic(GBContext* ctx, uint32_t cpu_cycles) {
    uint32_t remaining = cpu_cycles;

    /* Overflow and reload expose one-T-cycle write/interrupt windows. Keep
     * those short intervals on the scalar oracle; the state lasts at most
     * eight cycles before the common uninterrupted path resumes. */
    while (remaining > 0u && ctx->tima_reload_pending != 0u) {
        gb_timer_tick_scalar(ctx, 1u);
        remaining--;
    }

    while (remaining > 0u) {
        const uint8_t tac = ctx->io[0x07];
        if ((tac & 0x04u) == 0u) {
            ctx->div_counter = (uint16_t)(ctx->div_counter + remaining);
            remaining = 0u;
            break;
        }

        const uint32_t period = (uint32_t)gb_timer_mask(tac) << 1u;
        const uint32_t phase = ctx->div_counter & (period - 1u);
        const uint32_t cycles_to_first_edge = period - phase;
        if (remaining < cycles_to_first_edge) {
            ctx->div_counter = (uint16_t)(ctx->div_counter + remaining);
            remaining = 0u;
            break;
        }

        const uint32_t edge_count =
            1u + (remaining - cycles_to_first_edge) / period;
        const uint32_t edges_to_overflow = 0x100u - ctx->io[0x05];
        if (edge_count < edges_to_overflow) {
            ctx->io[0x05] = (uint8_t)(ctx->io[0x05] + edge_count);
            ctx->div_counter = (uint16_t)(ctx->div_counter + remaining);
            remaining = 0u;
            break;
        }

        const uint32_t cycles_to_overflow =
            cycles_to_first_edge + (edges_to_overflow - 1u) * period;
        ctx->div_counter =
            (uint16_t)(ctx->div_counter + cycles_to_overflow);
        remaining -= cycles_to_overflow;
        ctx->io[0x05] = 0u;
        ctx->tima_reload_pending = 4u;

        while (remaining > 0u && ctx->tima_reload_pending != 0u) {
            gb_timer_tick_scalar(ctx, 1u);
            remaining--;
        }
    }

    ctx->io[0x04] = (uint8_t)(ctx->div_counter >> 8);
}

static void gb_timer_tick(GBContext* ctx, uint32_t cpu_cycles) {
    if (gbrt_force_scalar_timer) {
        gb_timer_tick_scalar(ctx, cpu_cycles);
        return;
    }
    gb_timer_tick_arithmetic(ctx, cpu_cycles);
}

static uint32_t gbrt_min_deadline(uint32_t current, uint32_t candidate) {
    return candidate < current ? candidate : current;
}

uint32_t gbrt_cycles_until_next_event(const GBContext* ctx) {
    if (!ctx || ctx->stopped || ctx->frame_done || ctx->single_step_mode ||
        ctx->halted || ctx->halt_bug || ctx->stop_mode_active ||
        ctx->ime_pending || ctx->cgb_double_speed ||
        gbrt_benchmark_fast_tick_enabled ||
        ctx->last_sync_cycles != ctx->cycles ||
        gbrt_instruction_limit > 0 || gbrt_trace_enabled ||
        ctx->trace_file || ctx->ppu_trace_file) {
        return 0u;
    }

    if (ctx->ime && (ctx->io[0x0F] & ctx->io[0x80] & 0x1Fu)) {
        return 0u;
    }

    uint32_t deadline = UINT32_MAX;

    if (ctx->run_cycle_budget > 0u) {
        const uint32_t elapsed =
            ctx->cycles - ctx->run_cycle_budget_start;
        if (elapsed >= ctx->run_cycle_budget) {
            return 0u;
        }
        deadline = ctx->run_cycle_budget - elapsed;
    }

    if (ctx->tima_reload_pending) {
        deadline = gbrt_min_deadline(deadline, 1u);
    } else if (ctx->io[0x07] & 0x04u) {
        const uint32_t period = (uint32_t)gb_timer_mask(ctx->io[0x07]) << 1u;
        const uint32_t phase = ctx->div_counter & (period - 1u);
        deadline = gbrt_min_deadline(deadline, period - phase);
    }

    if (ctx->ppu) {
        deadline = gbrt_min_deadline(
            deadline,
            ppu_cycles_until_next_event((const GBPPU*)ctx->ppu, ctx));
    }

    if (ctx->hdma.cpu_stall_cycles > 0u) {
        return 0u;
    }

    if (ctx->dma.pending) {
        if (ctx->dma.startup_delay == 0u) {
            return 0u;
        }
        deadline = gbrt_min_deadline(deadline, ctx->dma.startup_delay);
    }
    if (ctx->dma.active) {
        if (ctx->dma.cycles_remaining == 0u) {
            return 0u;
        }
        uint32_t until_byte = ctx->dma.cycles_remaining % 4u;
        if (until_byte == 0u) {
            until_byte = 4u;
        }
        deadline = gbrt_min_deadline(deadline, until_byte);
    }

    if (ctx->serial_transfer.active) {
        if (ctx->serial_transfer.cycles_remaining == 0u) {
            return 0u;
        }
        deadline = gbrt_min_deadline(
            deadline, ctx->serial_transfer.cycles_remaining);
    }

    return deadline;
}

#ifdef GBRT_ENABLE_PERFORMANCE_COUNTERS
static size_t gbrt_profile_tick_cycle_bucket(uint32_t cycles) {
    if (cycles <= 4u) return cycles == 0u ? 0u : (size_t)(cycles - 1u);
    if (cycles <= 7u) return 4u;
    if (cycles == 8u) return 5u;
    if (cycles <= 11u) return 6u;
    if (cycles == 12u) return 7u;
    if (cycles <= 15u) return 8u;
    if (cycles == 16u) return 9u;
    return cycles <= 31u ? 10u : 11u;
}

static size_t gbrt_profile_deadline_bucket(uint32_t deadline) {
    if (deadline == UINT32_MAX) return 9u;
    if (deadline == 0u) return 0u;
    if (deadline == 1u) return 1u;
    if (deadline <= 3u) return 2u;
    if (deadline <= 7u) return 3u;
    if (deadline <= 15u) return 4u;
    if (deadline <= 31u) return 5u;
    if (deadline <= 63u) return 6u;
    if (deadline <= 255u) return 7u;
    return 8u;
}

static size_t gbrt_profile_group_size_bucket(uint64_t units) {
    if (units <= 2u) return units == 1u ? 0u : 1u;
    if (units <= 4u) return 2u;
    if (units <= 8u) return 3u;
    if (units <= 16u) return 4u;
    return 5u;
}

static void gbrt_profile_flush_group(GBPerformanceCounters* counters) {
    if (counters->profile_group_units == 0u) {
        return;
    }
    counters->region_groups++;
    counters->region_grouped_units += counters->profile_group_units;
    counters->region_grouped_tick_commits +=
        counters->profile_group_tick_commits;
    counters->region_group_size_histogram[
        gbrt_profile_group_size_bucket(counters->profile_group_units)]++;
    counters->profile_group_units = 0u;
    counters->profile_group_tick_commits = 0u;
    counters->profile_group_cycles = 0u;
    counters->profile_group_deadline_remaining = 0u;
}

static void gbrt_profile_start_group(GBPerformanceCounters* counters,
                                     uint64_t unit_ticks,
                                     uint64_t unit_cycles,
                                     uint32_t deadline) {
    counters->profile_group_units = 1u;
    counters->profile_group_tick_commits = unit_ticks;
    counters->profile_group_cycles = unit_cycles;
    counters->profile_group_deadline_remaining = deadline;
}

void gbrt_profile_generated_safepoint(GBContext* ctx) {
    GBPerformanceCounters* counters = &ctx->performance_counters;
    if (ctx->stopped) {
        counters->profile_unit_visibility_mask |=
            GBRT_PROFILE_VISIBILITY_STOPPED;
    }

    const uint64_t unit_ticks = counters->profile_unit_tick_commits;
    const uint64_t unit_cycles = counters->profile_unit_tick_cycles;
    const uint32_t visibility = counters->profile_unit_visibility_mask;
    const bool eligible = visibility == 0u && unit_ticks > 0u;
    const uint32_t deadline = gbrt_cycles_until_next_event(ctx);
    counters->deadline_histogram[gbrt_profile_deadline_bucket(deadline)]++;

    if (visibility & GBRT_PROFILE_VISIBILITY_GENERIC_READ) {
        counters->visibility_unit_histogram[2]++;
    }
    if (visibility & GBRT_PROFILE_VISIBILITY_GENERIC_WRITE) {
        counters->visibility_unit_histogram[3]++;
    }
    if (visibility & GBRT_PROFILE_VISIBILITY_TRANSITION) {
        counters->visibility_unit_histogram[4]++;
    }
    if (visibility & GBRT_PROFILE_VISIBILITY_FALLBACK) {
        counters->visibility_unit_histogram[5]++;
    }
    if (visibility & GBRT_PROFILE_VISIBILITY_STOPPED) {
        counters->visibility_unit_histogram[6]++;
    }
    if (eligible) {
        counters->region_candidate_units++;
        counters->visibility_unit_histogram[
            counters->profile_unit_safe_memory ? 1u : 0u]++;
    }

    if (unit_ticks > 0u) {
        bool merged = false;
        if (counters->profile_group_units > 0u && eligible) {
            const uint32_t remaining =
                counters->profile_group_deadline_remaining;
            if (remaining == UINT32_MAX || unit_cycles <= remaining) {
                counters->profile_group_units++;
                counters->profile_group_tick_commits += unit_ticks;
                counters->profile_group_cycles += unit_cycles;
                counters->region_estimated_removable_tick_commits +=
                    unit_ticks;
                counters->region_estimated_removable_safepoints++;
                if (remaining != UINT32_MAX) {
                    counters->profile_group_deadline_remaining =
                        (uint32_t)(remaining - unit_cycles);
                }
                if (deadline < counters->profile_group_deadline_remaining) {
                    counters->profile_group_deadline_remaining = deadline;
                }
                merged = true;
            }
        }

        if (!merged) {
            if (counters->profile_group_units > 0u) {
                if (!eligible) {
                    counters->region_reject_visibility++;
                } else {
                    counters->region_reject_deadline++;
                }
                gbrt_profile_flush_group(counters);
            }
            gbrt_profile_start_group(counters, unit_ticks, unit_cycles, deadline);
        }
    }

    if (ctx->stopped || deadline == 0u) {
        gbrt_profile_flush_group(counters);
    }

    counters->profile_unit_tick_commits = 0u;
    counters->profile_unit_tick_cycles = 0u;
    counters->profile_unit_visibility_mask = 0u;
    counters->profile_unit_safe_memory = 0u;
}

static void gbrt_profile_note_tick(GBContext* ctx, uint32_t cycles) {
    GBPerformanceCounters* counters = &ctx->performance_counters;
    counters->tick_commits++;
    counters->tick_cycles += cycles;
    if (gbrt_visibility_estimator_enabled) {
        counters->profile_unit_tick_commits++;
        counters->profile_unit_tick_cycles += cycles;
    }
    counters->tick_cycle_histogram[gbrt_profile_tick_cycle_bucket(cycles)]++;

    size_t ppu_mode = 0u;
    if (ctx->ppu) {
        const GBPPU* ppu = (const GBPPU*)ctx->ppu;
        if (ppu->lcdc & LCDC_LCD_ENABLE) {
            switch (ppu->mode) {
                case PPU_MODE_OAM: ppu_mode = 1u; break;
                case PPU_MODE_DRAW: ppu_mode = 2u; break;
                case PPU_MODE_HBLANK: ppu_mode = 3u; break;
                case PPU_MODE_VBLANK: ppu_mode = 4u; break;
                default: break;
            }
        }
    }
    counters->ppu_mode_tick_commits[ppu_mode]++;
    counters->ppu_mode_tick_cycles[ppu_mode] += cycles;

    const size_t timer_state = ctx->tima_reload_pending
        ? 2u
        : ((ctx->io[0x07] & 0x04u) ? 1u : 0u);
    counters->timer_state_tick_commits[timer_state]++;
    counters->timer_state_tick_cycles[timer_state] += cycles;
}
#endif

static void gb_timer_write_div(GBContext* ctx) {
    gbrt_audio_sync(ctx);
    const uint16_t old_div = ctx->div_counter;
    const bool old_input = gb_timer_input(ctx, ctx->io[0x07]);
    ctx->div_counter = 0;
    ctx->io[0x04] = 0;
    if (ctx->apu) {
        gb_audio_div_reset(ctx->apu, old_div, ctx->cgb_double_speed != 0);
    }
    if (old_input) {
        gb_timer_increment(ctx);
    }
}

void gbrt_audio_sync(GBContext* ctx) {
    if (!ctx || !ctx->apu || ctx->audio_pending_cpu_cycles == 0u) {
        return;
    }

#ifdef GBRT_ENABLE_PERFORMANCE_COUNTERS
    ctx->performance_counters.audio_step_calls++;
    ctx->performance_counters.audio_step_cycles +=
        ctx->audio_pending_cpu_cycles;
#endif
    gb_audio_step_timed(ctx,
                        ctx->audio_pending_old_div,
                        ctx->audio_pending_cpu_cycles,
                        ctx->audio_pending_double_speed != 0,
                        ctx->audio_pending_system_cycle_remainder);
    ctx->audio_pending_cpu_cycles = 0u;
    ctx->audio_cycles_until_event = 0u;
}

static void gbrt_audio_schedule(GBContext* ctx,
                                uint16_t old_div,
                                uint32_t cpu_cycles,
                                uint32_t system_cycles,
                                bool double_speed,
                                uint8_t system_cycle_remainder) {
    if (!ctx || !ctx->apu || cpu_cycles == 0u) {
        return;
    }

    if (gbrt_force_eager_audio) {
        gbrt_audio_sync(ctx);
#ifdef GBRT_ENABLE_PERFORMANCE_COUNTERS
        ctx->performance_counters.audio_step_calls++;
        ctx->performance_counters.audio_step_cycles += cpu_cycles;
#endif
        gb_audio_step_timed(ctx,
                            old_div,
                            cpu_cycles,
                            double_speed,
                            system_cycle_remainder);
        return;
    }

    if (ctx->audio_pending_cpu_cycles == 0u) {
        const uint32_t deadline = gb_audio_cycles_until_sample(ctx->apu);
        if (deadline == UINT32_MAX) {
            return;
        }
        ctx->audio_pending_old_div = old_div;
        ctx->audio_pending_system_cycle_remainder =
            system_cycle_remainder & 1u;
        ctx->audio_pending_double_speed = double_speed ? 1u : 0u;
        ctx->audio_cycles_until_event = deadline;
    } else if (ctx->audio_pending_double_speed != (double_speed ? 1u : 0u)) {
        /* Speed changes are synchronized by gb_stop(); retain a defensive
         * boundary for direct host-side manipulation of the context. */
        gbrt_audio_sync(ctx);
        gbrt_audio_schedule(ctx,
                            old_div,
                            cpu_cycles,
                            system_cycles,
                            double_speed,
                            system_cycle_remainder);
        return;
    }

    ctx->audio_pending_cpu_cycles += cpu_cycles;
    if (system_cycles >= ctx->audio_cycles_until_event) {
        gbrt_audio_sync(ctx);
    } else {
        ctx->audio_cycles_until_event -= system_cycles;
    }
}

static void gb_timer_write_tac(GBContext* ctx, uint8_t value) {
    const uint8_t old_tac = ctx->io[0x07];
    const bool old_input = gb_timer_input(ctx, old_tac);
    const bool new_input = gb_timer_input(ctx, value);
    const uint16_t new_mask = gb_timer_mask(value);
    const bool enable_on_counter_fall =
        (old_tac & 0x04u) == 0 &&
        (value & 0x04u) != 0 &&
        (((uint16_t)(ctx->div_counter - 1u) & new_mask) != 0) &&
        ((ctx->div_counter & new_mask) == 0);
    const bool cgb_disable_edge = gb_is_cgb_hardware(ctx) &&
                                  (old_tac & 0x04u) != 0 &&
                                  (value & 0x04u) == 0;
    if ((old_input && !new_input && !cgb_disable_edge) ||
        enable_on_counter_fall) {
        gb_timer_increment(ctx);
    }
    ctx->io[0x07] = value;
}

static bool gb_ppu_blocks_cpu_oam_read(const GBContext* ctx) {
    const GBPPU* ppu = ctx ? (const GBPPU*)ctx->ppu : NULL;
    if (!ppu) {
        const uint8_t stat_mode = ctx ? (ctx->io[0x41] & 3u) : 0u;
        return stat_mode == PPU_MODE_OAM || stat_mode == PPU_MODE_DRAW;
    }

    return ppu->mode == PPU_MODE_OAM ||
           ppu->visible_mode == PPU_MODE_OAM ||
           ppu->visible_mode == PPU_MODE_DRAW;
}

static bool gb_ppu_blocks_cpu_oam_write(const GBContext* ctx) {
    const GBPPU* ppu = ctx ? (const GBPPU*)ctx->ppu : NULL;
    if (!ppu) {
        const uint8_t stat_mode = ctx ? (ctx->io[0x41] & 3u) : 0u;
        return stat_mode == PPU_MODE_OAM || stat_mode == PPU_MODE_DRAW;
    }

    /* Writes begin blocking with visible mode 2, one M-cycle after reads.
     * At the other edge both regain access until visible mode 3 begins. */
    return ppu->visible_mode == PPU_MODE_DRAW ||
           (ppu->visible_mode == PPU_MODE_OAM &&
            ppu->mode == PPU_MODE_OAM);
}

static bool gb_ppu_blocks_cpu_vram_read(const GBContext* ctx) {
    const GBPPU* ppu = ctx ? (const GBPPU*)ctx->ppu : NULL;
    if (!ppu) {
        return ctx && (ctx->io[0x41] & 3u) == PPU_MODE_DRAW;
    }
    return ppu->mode == PPU_MODE_DRAW ||
           ppu->visible_mode == PPU_MODE_DRAW;
}

static uint8_t gbrt_read8_impl(GBContext* ctx,
                               uint16_t addr,
                               bool trigger_oam_bug) {
    if (gb_oam_dma_blocks_cpu_addr(ctx, addr)) {
        /* On DMG-family hardware, cartridge/VRAM/WRAM share the CPU data bus.
         * A blocked read below OAM therefore observes the byte most recently
         * driven by the DMA source, not a hard-wired FF. OAM itself remains
         * unreadable during the transfer. CGB has separate buses and retains
         * the existing bus-specific FF behavior. */
        if (!gb_is_cgb_hardware(ctx) &&
            ctx->dma.active &&
            ctx->dma.progress > 0 &&
            addr < 0xFE00) {
            const uint16_t source_addr =
                (uint16_t)(((uint16_t)ctx->dma.active_source_high << 8) |
                           (uint16_t)(ctx->dma.progress - 1u));
            return gb_direct_read_oam_dma_source(ctx, source_addr);
        }
        return 0xFF;
    }

    /* ROM (0x0000-0x7FFF), resolved through the mapper for both windows. */
    if (addr < 0x8000) {
        size_t rom_offset = 0;
        return gb_resolve_rom_offset(ctx, addr, &rom_offset)
            ? gbrt_data_mod_read_rom(ctx, rom_offset, false)
            : 0xFF;
    }

    /* VRAM (0x8000-0x9FFF) */
    if (addr < 0xA000) {
        gb_sync(ctx);
        if (gb_ppu_blocks_cpu_vram_read(ctx)) return 0xFF;
        return ctx->vram[(ctx->vram_bank * VRAM_SIZE) + (addr - 0x8000)];
    }

    /* External RAM / RTC (0xA000-0xBFFF) */
    if (addr < 0xC000) {
        if (!ctx->ram_enabled) return 0xFF;

        /* MBC3 RTC mode */
        if (ctx->rtc_mode) {
            switch (ctx->rtc_reg) {
                case 0x08: return ctx->rtc.latched_s;
                case 0x09: return ctx->rtc.latched_m;
                case 0x0A: return ctx->rtc.latched_h;
                case 0x0B: return ctx->rtc.latched_dl;
                case 0x0C: return ctx->rtc.latched_dh;
                default: return 0xFF;
            }
        }

        /* MBC2: 512x4 bit internal RAM (upper 4 bits always high) */
        if (ctx->mbc_type >= 0x05 && ctx->mbc_type <= 0x06) {
            /* MBC2 RAM is only 512 bytes, echoed throughout 0xA000-0xBFFF */
            if (ctx->eram) {
                return ctx->eram[(addr - 0xA000) & 0x1FF] | 0xF0;
            }
            return 0xFF;
        }

        /* Standard external RAM */
        if (ctx->eram) {
            uint32_t eram_addr = ((uint32_t)ctx->ram_bank * 0x2000) + (addr - 0xA000);
            if (eram_addr < ctx->eram_size) {
                return ctx->eram[eram_addr];
            }
        }
        return 0xFF;
    }
    if (addr < 0xD000) return ctx->wram[addr - 0xC000];
    if (addr < 0xE000) return ctx->wram[(ctx->wram_bank * WRAM_BANK_SIZE) + (addr - 0xD000)];
    if (addr < 0xFE00) {
        return gbrt_read8_impl(ctx,
                               (uint16_t)(addr - 0x2000),
                               trigger_oam_bug);
    }
    if (addr < 0xFF00) {
        gb_sync(ctx);
        if (trigger_oam_bug) {
            gbrt_trigger_oam_bug_read(ctx, addr);
        }
        if (addr >= 0xFEA0) {
            return 0xFF;
        }
        if (gb_ppu_blocks_cpu_oam_read(ctx)) return 0xFF;
        return ctx->oam[addr - 0xFE00];
    }
    if (addr < 0xFF80) {
        if (addr == 0xFF00) {
            const GBJoypadState* joypad = (const GBJoypadState*)ctx->joypad;
            uint8_t joyp = ctx->io[0x00];
            uint8_t dpad = joypad ? joypad->dpad : g_joypad_dpad;
            uint8_t buttons = joypad ? joypad->buttons : g_joypad_buttons;
            uint8_t res = 0xC0 | (joyp & 0x30) | 0x0F;
            if (!(joyp & 0x10)) res &= dpad;
            if (!(joyp & 0x20)) res &= buttons;
            return res;
        }
        if (addr == 0xFF04) return (uint8_t)(ctx->div_counter >> 8);
        if (addr == 0xFF0F) return (uint8_t)(0xE0 | (ctx->io[0x0F] & 0x1F));
        if (addr == 0xFF4D) {
            if (!gb_is_cgb_mode(ctx)) return 0xFF;
            return (uint8_t)((ctx->io[0x4D] & 0x01) | (ctx->cgb_double_speed ? 0xFE : 0x7E));
        }
        if (addr == 0xFF4F) {
            if (!gb_is_cgb_mode(ctx)) return 0xFF;
            return (uint8_t)(ctx->vram_bank | 0xFE);
        }
        if (addr >= 0xFF51 && addr <= 0xFF54) {
            if (!gb_is_cgb_mode(ctx)) return 0xFF;
            return ctx->io[addr - 0xFF00];
        }
        if (addr == 0xFF55) {
            return gb_hdma_status_read(ctx);
        }
        if (addr == 0xFF56) {
            if (!gb_is_cgb_mode(ctx)) return 0xFF;
            return 0x3E;
        }
        if (addr == 0xFF6C) {
            if (!gb_is_cgb_mode(ctx)) return 0xFF;
            return (uint8_t)(0xFE | (((GBPPU*)ctx->ppu)->opri & 0x01));
        }
        if (addr == 0xFF72 || addr == 0xFF73) {
            if (!gb_is_cgb_hardware(ctx)) return 0xFF;
            return ctx->io[addr - 0xFF00];
        }
        if (addr == 0xFF74) {
            if (!gb_is_cgb_mode(ctx)) return 0xFF;
            return ctx->io[0x74];
        }
        if (addr == 0xFF75) {
            if (!gb_is_cgb_hardware(ctx)) return 0xFF;
            return (uint8_t)(ctx->io[0x75] | 0x8F);
        }
        if (addr == 0xFF76) {
            if (!gb_is_cgb_hardware(ctx)) return 0xFF;
            if (!ctx->apu) return 0x00;
            gbrt_audio_sync(ctx);
            return gb_audio_read_pcm12(ctx->apu);
        }
        if (addr == 0xFF77) {
            if (!gb_is_cgb_hardware(ctx)) return 0xFF;
            if (!ctx->apu) return 0x00;
            gbrt_audio_sync(ctx);
            return gb_audio_read_pcm34(ctx->apu);
        }
        if (addr == 0xFF70) {
            if (!gb_is_cgb_mode(ctx)) return 0xFF;
            return ctx->io[0x70];
        }
        if (addr >= 0xFF40 && addr <= 0xFF4B) {
            gb_sync(ctx);
            return ppu_read_register((GBPPU*)ctx->ppu, addr);
        }
        if (addr >= 0xFF68 && addr <= 0xFF6B) {
            gb_sync(ctx);
            return ppu_read_register((GBPPU*)ctx->ppu, addr);
        }
        if (addr >= 0xFF10 && addr <= 0xFF3F) {
            gbrt_audio_sync(ctx);
            return gb_audio_read(ctx, addr);
        }
        return ctx->io[addr - 0xFF00];
    }
    if (addr < 0xFFFF) return ctx->hram[addr - 0xFF80];
    if (addr == 0xFFFF) return ctx->io[0x80];
    return 0xFF;
}

uint8_t gb_read8(GBContext* ctx, uint16_t addr) {
    return gbrt_read8_impl(ctx, addr, true);
}

static void gbrt_write8_impl(GBContext* ctx,
                             uint16_t addr,
                             uint8_t value,
                             bool trigger_oam_bug) {
    if (gb_oam_dma_blocks_cpu_addr(ctx, addr)) {
        return;  /* Bus conflict - write ignored */
    }
    
    /* MBC Write Handling */
    if (addr < 0x8000) {
        /* ================================================================
         * MBC1 (Cartridge types 0x01, 0x02, 0x03)
         * ================================================================ */
        if (ctx->mbc_type >= 0x01 && ctx->mbc_type <= 0x03) {
            if (addr < 0x2000) {
                /* 0x0000-0x1FFF: RAM Enable */
                ctx->ram_enabled = ((value & 0x0F) == 0x0A);
            } else if (addr < 0x4000) {
                /* 0x2000-0x3FFF: ROM Bank Number (lower 5 bits) */
                ctx->rom_bank_low = value & 0x1F;
            } else if (addr < 0x6000) {
                /* 0x4000-0x5FFF: RAM Bank / Upper ROM Bank bits */
                ctx->rom_bank_upper = value & 0x03;
                if (ctx->mbc_mode != 0) {
                    /* Mode 1: Used as RAM bank */
                    ctx->ram_bank = ctx->rom_bank_upper;
                }
            } else {
                /* 0x6000-0x7FFF: Banking Mode Select */
                ctx->mbc_mode = value & 0x01;
                if (ctx->mbc_mode == 0) {
                    /* Mode 0: RAM bank fixed to 0. */
                    ctx->ram_bank = 0;
                } else {
                    /* Mode 1: RAM bank and lower ROM window use upper bits. */
                    ctx->ram_bank = ctx->rom_bank_upper;
                }
            }
            ctx->rom_bank = gb_resolve_rom_bank(ctx, 0x4000);
        }
        /* ================================================================
         * MBC2 (Cartridge types 0x05, 0x06)
         * ================================================================ */
        else if (ctx->mbc_type >= 0x05 && ctx->mbc_type <= 0x06) {
            if (addr < 0x4000) {
                /* MBC2: Bit 8 of addr determines RAM enable vs ROM bank */
                if (addr & 0x0100) {
                    /* 0x2100-0x3FFF: ROM Bank Number (lower 4 bits) */
                    ctx->rom_bank_low = value & 0x0F;
                    ctx->rom_bank = gb_resolve_rom_bank(ctx, 0x4000);
                } else {
                    /* 0x0000-0x1FFF: RAM Enable (if bit 8 is 0) */
                    ctx->ram_enabled = ((value & 0x0F) == 0x0A);
                }
            }
            /* 0x4000-0x7FFF: Unused for MBC2 */
        }
        /* ================================================================
         * MBC3 (Cartridge types 0x0F, 0x10, 0x11, 0x12, 0x13)
         * ================================================================ */
        else if (ctx->mbc_type >= 0x0F && ctx->mbc_type <= 0x13) {
            if (addr < 0x2000) {
                /* RAM/RTC Enable */
                ctx->ram_enabled = ((value & 0x0F) == 0x0A);
            } else if (addr < 0x4000) {
                /* ROM Bank Number (1-127) */
                ctx->rom_bank_low = value & 0x7F;
                ctx->rom_bank = gb_resolve_rom_bank(ctx, 0x4000);
            } else if (addr < 0x6000) {
                /* RAM Bank Number or RTC Register Select */
                if (value <= 0x03) {
                    ctx->rtc_mode = 0;
                    ctx->ram_bank = value;
                } else if (value >= 0x08 && value <= 0x0C) {
                    ctx->rtc_mode = 1;
                    ctx->rtc_reg = value;
                }
            } else {
                /* Latch Clock Data */
                if (ctx->rtc.latch_state == 0 && value == 0) {
                    ctx->rtc.latch_state = 1;
                } else if (ctx->rtc.latch_state == 1 && value == 1) {
                    ctx->rtc.latch_state = 0;
                    /* Latch current time */
                    ctx->rtc.latched_s = ctx->rtc.s;
                    ctx->rtc.latched_m = ctx->rtc.m;
                    ctx->rtc.latched_h = ctx->rtc.h;
                    ctx->rtc.latched_dl = ctx->rtc.dl;
                    ctx->rtc.latched_dh = ctx->rtc.dh;
                } else {
                    ctx->rtc.latch_state = 0;
                }
            }
        }
        /* ================================================================
         * MBC5 (Cartridge types 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E)
         * ================================================================ */
        else if (ctx->mbc_type >= 0x19 && ctx->mbc_type <= 0x1E) {
            if (addr < 0x2000) {
                /* RAM Enable */
                ctx->ram_enabled = ((value & 0x0F) == 0x0A);
            } else if (addr < 0x3000) {
                /* ROM Bank Number (lower 8 bits) */
                ctx->rom_bank_low = value;
                ctx->rom_bank = gb_resolve_rom_bank(ctx, 0x4000);
                /* MBC5 allows bank 0 - no fixup needed */
            } else if (addr < 0x4000) {
                /* ROM Bank Number (9th bit) */
                ctx->rom_bank_upper = value & 0x01;
                ctx->rom_bank = gb_resolve_rom_bank(ctx, 0x4000);
            } else if (addr < 0x6000) {
                /* RAM Bank Number (0-15) */
                ctx->ram_bank = value & 0x0F;
            }
            /* 0x6000-0x7FFF: Unused for MBC5 */
        }
        /* ================================================================
         * No MBC / ROM Only (type 0x00) or Unknown
         * ================================================================ */
        else {
            /* Simple fallback: just ROM bank register */
            if (addr >= 0x2000 && addr < 0x4000) {
                ctx->rom_bank_low = value & 0x1F;
                ctx->rom_bank = value & 0x1F;
                if (ctx->rom_bank == 0) ctx->rom_bank = 1;
            }
        }
        return;
    }
    if (addr < 0xA000) {
        gb_sync(ctx);
        /* VRAM is not CPU-accessible during mode 3. */
        if ((ctx->io[0x41] & 3) == 3) {
            gbrt_log_vram_write(ctx, addr, value, 0, "mode-blocked");
            return;
        }

        ctx->vram[(ctx->vram_bank * VRAM_SIZE) + (addr - 0x8000)] = value;
        gbrt_log_vram_write(ctx, addr, value, 1, "cpu");
        return;
    }
    if (addr < 0xC000) {
        /* External RAM / RTC Write */
        if (!ctx->ram_enabled) return;
        
        /* MBC3 RTC mode */
        if (ctx->rtc_mode) {
            /* RTC Register Write */
            switch (ctx->rtc_reg) {
                case 0x08: ctx->rtc.s = value % 60; break;
                case 0x09: ctx->rtc.m = value % 60; break;
                case 0x0A: ctx->rtc.h = value % 24; break;
                case 0x0B: ctx->rtc.dl = value; break;
                case 0x0C: 
                    ctx->rtc.dh = value; 
                    ctx->rtc.active = !(value & 0x40); /* Bit 6 is Halt */
                    break;
            }
            return;
        }
        
        /* MBC2: 512x4 bit internal RAM (only lower 4 bits stored) */
        if (ctx->mbc_type >= 0x05 && ctx->mbc_type <= 0x06) {
            if (ctx->eram) {
                ctx->eram[(addr - 0xA000) & 0x1FF] = value & 0x0F;
            }
            return;
        }
        
        /* Standard external RAM */
        if (ctx->eram) {
            uint32_t eram_addr = ((uint32_t)ctx->ram_bank * 0x2000) + (addr - 0xA000);
            if (eram_addr < ctx->eram_size) {
                ctx->eram[eram_addr] = value;
            }
        }
        return;
    }
    if (addr < 0xD000) { ctx->wram[addr - 0xC000] = value; return; }
    if (addr < 0xE000) { ctx->wram[(ctx->wram_bank * WRAM_BANK_SIZE) + (addr - 0xD000)] = value; return; }
    if (addr < 0xFE00) {
        gbrt_write8_impl(ctx,
                         (uint16_t)(addr - 0x2000),
                         value,
                         trigger_oam_bug);
        return;
    }
    if (addr < 0xFF00) {
        gb_sync(ctx);
        if (trigger_oam_bug) {
            gbrt_trigger_oam_bug_write(ctx, addr);
        }
        if (addr >= 0xFEA0) {
            return;
        }
        /* OAM is not CPU-accessible during modes 2 and 3. */
        if (gb_ppu_blocks_cpu_oam_write(ctx)) {
            gbrt_log_oam_write(ctx, addr, value, 0, "mode-blocked");
            return;
        }

        ctx->oam[addr - 0xFE00] = value;
        gbrt_log_oam_write(ctx, addr, value, 1, "cpu");
        return;
    }
    if (addr < 0xFF80) {
        if (addr == 0xFF46) {
            gb_sync(ctx);
            /* OAM DMA: start transfer and expose the written source page. */
            if (ctx->ppu) {
                ((GBPPU*)ctx->ppu)->dma = value;
            }
            ctx->io[0x46] = value;
            gbrt_log_dma_start(ctx, value);
            ctx->dma.source_high = value;
            if (!ctx->dma.active) {
                ctx->dma.active_source_high = value;
                ctx->dma.progress = 0;
                ctx->dma.cycles_remaining = 640;
            }
            /* The write is M=0. One complete M-cycle remains accessible, and
             * the new transfer owns the bus when M=2 begins. If another DMA
             * is already active it keeps running during this delay.
             */
            ctx->dma.startup_delay = 8;
            ctx->dma.pending = 1;
            return;
        }
        if (addr == 0xFF02) {
            uint8_t sc = (uint8_t)(0x7C | (value & 0x83));
            if (!gb_is_cgb_hardware(ctx)) {
                sc |= 0x02;
            }
            ctx->io[0x02] = sc;

            ctx->serial_transfer.active = 0;
            ctx->serial_transfer.fast_clock = 0;
            ctx->serial_transfer.cycles_remaining = 0;

            if ((sc & 0x80) && (sc & 0x01) && ctx->config.enable_serial) {
                ctx->serial_transfer.active = 1;
                ctx->serial_transfer.fast_clock =
                    (uint8_t)((gb_is_cgb_mode(ctx) && (sc & 0x02)) ? 1 : 0);
                ctx->serial_transfer.cycles_remaining =
                    ctx->serial_transfer.fast_clock ? 128u : 4096u;
            }
            return;
        }
        if (addr == 0xFF4D) {
            if (gb_is_cgb_mode(ctx)) {
                ctx->io[0x4D] = value & 0x01;
            }
            return;
        }
        if (addr == 0xFF4F) {
            if (gb_is_cgb_mode(ctx)) {
                ctx->vram_bank = value & 0x01;
                ctx->io[0x4F] = (uint8_t)(0xFE | ctx->vram_bank);
            }
            return;
        }
        if (addr == 0xFF51) {
            if (gb_is_cgb_mode(ctx)) {
                ctx->io[0x51] = value;
                ctx->hdma.source = (uint16_t)(((uint16_t)value << 8) | (ctx->hdma.source & 0x00F0));
            }
            return;
        }
        if (addr == 0xFF52) {
            if (gb_is_cgb_mode(ctx)) {
                ctx->io[0x52] = value & 0xF0;
                ctx->hdma.source = (uint16_t)((ctx->hdma.source & 0xFF00) | (value & 0xF0));
            }
            return;
        }
        if (addr == 0xFF53) {
            if (gb_is_cgb_mode(ctx)) {
                ctx->io[0x53] = value & 0x1F;
                ctx->hdma.dest = (uint16_t)(0x8000 | (((uint16_t)value & 0x1F) << 8) | (ctx->hdma.dest & 0x00F0));
            }
            return;
        }
        if (addr == 0xFF54) {
            if (gb_is_cgb_mode(ctx)) {
                ctx->io[0x54] = value & 0xF0;
                ctx->hdma.dest = (uint16_t)(0x8000 | (ctx->hdma.dest & 0x1F00) | (value & 0xF0));
            }
            return;
        }
        if (addr == 0xFF55) {
            gb_sync(ctx);
            gb_hdma_start(ctx, value);
            return;
        }
        if (addr == 0xFF56) {
            if (gb_is_cgb_mode(ctx)) {
                ctx->io[0x56] = (value & 0xC1) | 0x3E;
            }
            return;
        }
        if (addr == 0xFF6C) {
            if (gb_is_cgb_mode(ctx) && ctx->ppu) {
                ((GBPPU*)ctx->ppu)->opri = value & 0x01;
                ctx->io[0x6C] = (uint8_t)(0xFE | (((GBPPU*)ctx->ppu)->opri & 0x01));
            }
            return;
        }
        if (addr == 0xFF72 || addr == 0xFF73) {
            if (gb_is_cgb_hardware(ctx)) {
                ctx->io[addr - 0xFF00] = value;
            }
            return;
        }
        if (addr == 0xFF74) {
            if (gb_is_cgb_mode(ctx)) {
                ctx->io[0x74] = value;
            }
            return;
        }
        if (addr == 0xFF75) {
            if (gb_is_cgb_hardware(ctx)) {
                ctx->io[0x75] = (uint8_t)(value & 0x70);
            }
            return;
        }
        if (addr == 0xFF76 || addr == 0xFF77) {
            return;
        }
        if (addr == 0xFF70) {
            if (gb_is_cgb_mode(ctx)) {
                ctx->wram_bank = value & 0x07;
                if (ctx->wram_bank == 0) ctx->wram_bank = 1;
                ctx->io[0x70] = (uint8_t)(0xF8 | ctx->wram_bank);
            }
            return;
        }
        if ((addr >= 0xFF40 && addr <= 0xFF4B) || (addr >= 0xFF68 && addr <= 0xFF6B)) {
            ppu_write_register((GBPPU*)ctx->ppu, ctx, addr, value);
            return;
        }
        if (addr >= 0xFF10 && addr <= 0xFF3F) {
            gbrt_audio_sync(ctx);
            gb_audio_write(ctx, addr, value);
            return;
        }
        if (addr == 0xFF04) {
            gb_timer_write_div(ctx);
            return;
        }
        if (addr == 0xFF05) {
            if (ctx->tima_reload_pending & GB_TIMA_RELOAD_CYCLE_FLAG) {
                return;
            }
            ctx->io[0x05] = value;
            ctx->tima_reload_pending = 0;
            return;
        }
        if (addr == 0xFF06) {
            ctx->io[0x06] = value;
            if (ctx->tima_reload_pending & GB_TIMA_RELOAD_CYCLE_FLAG) {
                ctx->io[0x05] = value;
            }
            return;
        }
        if (addr == 0xFF07) {
            gb_timer_write_tac(ctx, value);
            return;
        }
        if (addr == 0xFF0F) {
            ctx->io[0x0F] = value & 0x1F;
            return;
        }
        if ((addr >= 0xFF40 && addr <= 0xFF4B) || (addr >= 0xFF68 && addr <= 0xFF6B)) {
            gb_sync(ctx);
        }
        ctx->io[addr - 0xFF00] = value;
        return;
    }
    if (addr < 0xFFFF) { 
        // if (addr >= 0xFF80 && addr <= 0xFF8F) {
        //      DBG_GENERAL("Writing to HRAM[%04X]: %02X", addr, value);
        // }
        ctx->hram[addr - 0xFF80] = value; return; 
    }
    if (addr == 0xFFFF) { ctx->io[0x80] = value; return; }
}

void gb_write8(GBContext* ctx, uint16_t addr, uint8_t value) {
    gbrt_write8_impl(ctx, addr, value, true);
}

uint16_t gb_read16(GBContext* ctx, uint16_t addr) {
    return (uint16_t)gb_read8(ctx, addr) | ((uint16_t)gb_read8(ctx, addr + 1) << 8);
}

void gb_write16(GBContext* ctx, uint16_t addr, uint16_t value) {
    gb_write8(ctx, addr, value & 0xFF);
    gb_write8(ctx, addr + 1, value >> 8);
}

static uint16_t gbrt_load_oam_word(const uint8_t* oam, size_t offset) {
    return (uint16_t)(oam[offset] | ((uint16_t)oam[offset + 1] << 8));
}

static void gbrt_store_oam_word(uint8_t* oam,
                                size_t offset,
                                uint16_t value) {
    oam[offset] = (uint8_t)value;
    oam[offset + 1] = (uint8_t)(value >> 8);
}

static bool gbrt_dmg_accessed_oam_row(const GBContext* ctx,
                                      size_t* row_out) {
    if (!ctx || !ctx->ppu || !row_out || gb_is_cgb_hardware(ctx)) {
        return false;
    }

    const GBPPU* ppu = (const GBPPU*)ctx->ppu;
    const bool normal_oam_scan = ppu->mode == PPU_MODE_OAM;
    const bool lcd_startup_oam_scan =
        ppu->mode == PPU_MODE_HBLANK && ppu->lcd_startup_phase == 1u;
    if (!(ppu->lcdc & LCDC_LCD_ENABLE) ||
        (!normal_oam_scan && !lcd_startup_oam_scan) ||
        ppu->ly >= VISIBLE_SCANLINES) {
        return false;
    }

    size_t row;
    if (normal_oam_scan) {
        /* The PPU's internal OAM bus is eight dots ahead of the runtime's
         * visible mode-2 counter. At counter dot 0 the second OAM row is
         * already exposed; dot 76 has advanced beyond the 160-byte array. */
        row = 8u + (size_t)((ppu->mode_cycles / 4u) * 8u);
    } else {
        if (ppu->mode_cycles < 6u) {
            return false;
        }
        row = 8u + (size_t)(((ppu->mode_cycles - 6u) / 4u) * 8u);
    }
    if (row >= OAM_SIZE) {
        return false;
    }
    *row_out = row;
    return true;
}

static void gbrt_trigger_oam_bug_write(GBContext* ctx, uint16_t address) {
    if (address < 0xFE00 || address >= 0xFF00) {
        return;
    }

    size_t row = 0;
    const bool row_valid = gbrt_dmg_accessed_oam_row(ctx, &row);
    if (gbrt_trace_enabled) {
        const GBPPU* ppu = ctx && ctx->ppu ? (const GBPPU*)ctx->ppu : NULL;
        fprintf(stderr,
                "[OAM-BUG] access=write addr=%04X model=%u ly=%u "
                "mode=%u visible=%u startup=%u dot=%u row=%s%zu "
                "cycles=%u pc=%04X\n",
                address,
                ctx ? (unsigned)ctx->config.model : 0u,
                ppu ? (unsigned)ppu->ly : 0u,
                ppu ? (unsigned)ppu->mode : 0u,
                ppu ? (unsigned)ppu->visible_mode : 0u,
                ppu ? (unsigned)ppu->lcd_startup_phase : 0u,
                ppu ? (unsigned)ppu->mode_cycles : 0u,
                row_valid ? "" : "none/",
                row,
                ctx ? ctx->cycles : 0u,
                ctx ? ctx->pc : 0u);
    }
    if (!row_valid) {
        return;
    }

    const uint16_t current = gbrt_load_oam_word(ctx->oam, row);
    const uint16_t previous_first = gbrt_load_oam_word(ctx->oam, row - 8u);
    const uint16_t previous_third = gbrt_load_oam_word(ctx->oam, row - 4u);
    const uint16_t glitched =
        (uint16_t)(((current ^ previous_third) &
                    (previous_first ^ previous_third)) ^
                   previous_third);

    gbrt_store_oam_word(ctx->oam, row, glitched);
    memcpy(ctx->oam + row + 2u, ctx->oam + row - 6u, 6u);
}

static uint16_t gbrt_oam_read_secondary(uint16_t a,
                                        uint16_t b,
                                        uint16_t c,
                                        uint16_t d) {
    return (uint16_t)((b & (a | c | d)) | (a & c & d));
}

static uint16_t gbrt_oam_read_tertiary(uint16_t a,
                                       uint16_t b,
                                       uint16_t c,
                                       uint16_t d,
                                       uint16_t e,
                                       unsigned variant) {
    if (variant == 1u) {
        return (uint16_t)(c | (a & b & d & e));
    }
    if (variant == 2u) {
        return (uint16_t)((c & (a | b | d | e)) | (a & b & d & e));
    }
    return (uint16_t)((c & (a | b | d | e)) | (b & d & e));
}

static uint16_t gbrt_oam_read_quaternary_dmg(uint16_t b,
                                              uint16_t c,
                                              uint16_t d,
                                              uint16_t e,
                                              uint16_t f,
                                              uint16_t g,
                                              uint16_t h) {
    return (uint16_t)((e & (h | g | ((uint16_t)~d & f) | c | b)) |
                      (c & g & h));
}

static void gbrt_trigger_oam_bug_read(GBContext* ctx, uint16_t address) {
    if (address < 0xFE00 || address >= 0xFF00) {
        return;
    }

    size_t row = 0;
    const bool row_valid = gbrt_dmg_accessed_oam_row(ctx, &row);
    if (gbrt_trace_enabled) {
        const GBPPU* ppu = ctx && ctx->ppu ? (const GBPPU*)ctx->ppu : NULL;
        fprintf(stderr,
                "[OAM-BUG] access=read addr=%04X model=%u ly=%u "
                "mode=%u visible=%u startup=%u dot=%u row=%s%zu "
                "cycles=%u pc=%04X\n",
                address,
                ctx ? (unsigned)ctx->config.model : 0u,
                ppu ? (unsigned)ppu->ly : 0u,
                ppu ? (unsigned)ppu->mode : 0u,
                ppu ? (unsigned)ppu->visible_mode : 0u,
                ppu ? (unsigned)ppu->lcd_startup_phase : 0u,
                ppu ? (unsigned)ppu->mode_cycles : 0u,
                row_valid ? "" : "none/",
                row,
                ctx ? ctx->cycles : 0u,
                ctx ? ctx->pc : 0u);
    }
    if (!row_valid) {
        return;
    }

    if ((row & 0x18u) == 0x10u) {
        gbrt_store_oam_word(
            ctx->oam,
            row - 8u,
            gbrt_oam_read_secondary(
                gbrt_load_oam_word(ctx->oam, row - 16u),
                gbrt_load_oam_word(ctx->oam, row - 8u),
                gbrt_load_oam_word(ctx->oam, row),
                gbrt_load_oam_word(ctx->oam, row - 4u)));
        if (row < 0x98u) {
            memcpy(ctx->oam + row - 16u, ctx->oam + row - 8u, 8u);
        }
    } else if ((row & 0x18u) == 0u) {
        unsigned variant = 1u;
        if (row == 0x20u) {
            variant = 2u;
        } else if (row == 0x60u) {
            variant = 3u;
        }

        uint16_t glitched;
        if (row == 0x40u) {
            glitched = gbrt_oam_read_quaternary_dmg(
                gbrt_load_oam_word(ctx->oam, row),
                gbrt_load_oam_word(ctx->oam, row - 4u),
                gbrt_load_oam_word(ctx->oam, row - 6u),
                gbrt_load_oam_word(ctx->oam, row - 8u),
                gbrt_load_oam_word(ctx->oam, row - 14u),
                gbrt_load_oam_word(ctx->oam, row - 16u),
                gbrt_load_oam_word(ctx->oam, row - 32u));
        } else {
            glitched = gbrt_oam_read_tertiary(
                gbrt_load_oam_word(ctx->oam, row),
                gbrt_load_oam_word(ctx->oam, row - 4u),
                gbrt_load_oam_word(ctx->oam, row - 8u),
                gbrt_load_oam_word(ctx->oam, row - 16u),
                gbrt_load_oam_word(ctx->oam, row - 32u),
                variant);
        }
        gbrt_store_oam_word(ctx->oam, row - 8u, glitched);
        if (row < 0x98u) {
            memcpy(ctx->oam + row - 16u, ctx->oam + row - 8u, 8u);
            memcpy(ctx->oam + row - 32u, ctx->oam + row - 8u, 8u);
        }
    } else {
        const uint16_t glitched =
            (uint16_t)(gbrt_load_oam_word(ctx->oam, row - 8u) |
                       (gbrt_load_oam_word(ctx->oam, row) &
                        gbrt_load_oam_word(ctx->oam, row - 4u)));
        gbrt_store_oam_word(ctx->oam, row - 8u, glitched);
        gbrt_store_oam_word(ctx->oam, row, glitched);
    }

    memcpy(ctx->oam + row, ctx->oam + row - 8u, 8u);
    if (row == 0x80u) {
        memcpy(ctx->oam, ctx->oam + row, 8u);
    }
}

void gb_push16(GBContext* ctx, uint16_t value) {
    ctx->sp -= 2;
    gb_write16(ctx, ctx->sp, value);
}

uint16_t gb_pop16(GBContext* ctx) {
    uint16_t val = gb_read16(ctx, ctx->sp);
    ctx->sp += 2;
    return val;
}

/* Stack bus primitives shared by generated code, the interpreter, copied
 * RAM/HRAM execution, and interrupt entry. Memory is sampled late in each
 * M-cycle, matching the final-T-cycle convention used by the other timed bus
 * helpers in this runtime. */
static void gbrt_timed_stack_write16(GBContext* ctx,
                                     uint16_t value,
                                     uint8_t leading_cycles,
                                     bool commit_pc,
                                     uint16_t target_pc) {
    /* PUSH-like instructions expose the original SP through the IDU during
     * their internal machine cycle, before either byte decrement/write. */
    gb_tick(ctx, (uint32_t)leading_cycles - 4u);
    gbrt_trigger_oam_bug_write(ctx, ctx->sp);
    gb_tick(ctx, 4u);
    ctx->sp--;
    gbrt_trigger_oam_bug_write(ctx, ctx->sp);
    gb_tick(ctx, 3u);
    gbrt_write8_impl(ctx, ctx->sp, (uint8_t)(value >> 8), false);
    gb_tick(ctx, 1);

    ctx->sp--;
    gbrt_trigger_oam_bug_write(ctx, ctx->sp);
    gb_tick(ctx, 3u);
    gbrt_write8_impl(ctx, ctx->sp, (uint8_t)value, false);
    if (commit_pc) {
        ctx->pc = target_pc;
    }
    gb_tick(ctx, 1);
}

static uint16_t gbrt_timed_stack_read16(GBContext* ctx,
                                        uint8_t leading_cycles,
                                        bool commit_pc) {
    gb_tick(ctx, leading_cycles);
    const uint16_t low_address = ctx->sp;
    gbrt_trigger_oam_bug_read(ctx, low_address);
    gb_tick(ctx, 3u);
    const uint8_t low = gbrt_read8_impl(ctx, low_address, false);
    ctx->sp++;
    gb_tick(ctx, 1);

    const uint16_t high_address = ctx->sp;
    gbrt_trigger_oam_bug_read(ctx, high_address);
    gb_tick(ctx, 3);
    const uint8_t high = gbrt_read8_impl(ctx, high_address, false);
    ctx->sp++;
    const uint16_t value = (uint16_t)(low | ((uint16_t)high << 8));
    if (commit_pc) {
        ctx->pc = value;
    }
    gb_tick(ctx, 1);
    return value;
}

void gbrt_timed_push16(GBContext* ctx, uint16_t value) {
    /* Opcode fetch + internal stack cycle, then two writes. */
    gbrt_timed_stack_write16(ctx, value, 8, false, 0);
}

uint16_t gbrt_timed_pop16(GBContext* ctx) {
    /* Opcode fetch, then two reads. */
    return gbrt_timed_stack_read16(ctx, 4, false);
}

/* ============================================================================
 * ALU
 * ========================================================================== */

void gb_add8(GBContext* ctx, uint8_t value) {
    uint32_t res = (uint32_t)ctx->a + value;
    ctx->f_z = (res & 0xFF) == 0;
    ctx->f_n = 0;
    ctx->f_h = ((ctx->a & 0x0F) + (value & 0x0F)) > 0x0F;
    ctx->f_c = res > 0xFF;
    ctx->a = (uint8_t)res;
}
void gb_adc8(GBContext* ctx, uint8_t value) {
    uint8_t carry = ctx->f_c ? 1 : 0;
    uint32_t res = (uint32_t)ctx->a + value + carry;
    ctx->f_z = (res & 0xFF) == 0;
    ctx->f_n = 0;
    ctx->f_h = ((ctx->a & 0x0F) + (value & 0x0F) + carry) > 0x0F;
    ctx->f_c = res > 0xFF;
    ctx->a = (uint8_t)res;
}
void gb_sub8(GBContext* ctx, uint8_t value) {
    ctx->f_z = ctx->a == value;
    ctx->f_n = 1;
    ctx->f_h = (ctx->a & 0x0F) < (value & 0x0F);
    ctx->f_c = ctx->a < value;
    ctx->a -= value;
}
void gb_sbc8(GBContext* ctx, uint8_t value) {
    uint8_t carry = ctx->f_c ? 1 : 0;
    int res = (int)ctx->a - (int)value - carry;
    ctx->f_z = (res & 0xFF) == 0;
    ctx->f_n = 1;
    ctx->f_h = ((int)(ctx->a & 0x0F) - (int)(value & 0x0F) - (int)carry) < 0;
    ctx->f_c = res < 0;
    ctx->a = (uint8_t)res;
}
void gb_and8(GBContext* ctx, uint8_t value) { ctx->a &= value; ctx->f_z = ctx->a == 0; ctx->f_n = 0; ctx->f_h = 1; ctx->f_c = 0; }
void gb_or8(GBContext* ctx, uint8_t value) { ctx->a |= value; ctx->f_z = ctx->a == 0; ctx->f_n = 0; ctx->f_h = 0; ctx->f_c = 0; }
void gb_xor8(GBContext* ctx, uint8_t value) { ctx->a ^= value; ctx->f_z = ctx->a == 0; ctx->f_n = 0; ctx->f_h = 0; ctx->f_c = 0; }
void gb_cp8(GBContext* ctx, uint8_t value) {
    ctx->f_z = ctx->a == value;
    ctx->f_n = 1;
    ctx->f_h = (ctx->a & 0x0F) < (value & 0x0F);
    ctx->f_c = ctx->a < value;
}
uint8_t gb_inc8(GBContext* ctx, uint8_t val) {
    ctx->f_h = (val & 0x0F) == 0x0F;
    val++;
    ctx->f_z = val == 0;
    ctx->f_n = 0;
    return val;
}
uint8_t gb_dec8(GBContext* ctx, uint8_t val) {
    ctx->f_h = (val & 0x0F) == 0;
    val--;
    ctx->f_z = val == 0;
    ctx->f_n = 1;
    return val;
}
void gb_add16(GBContext* ctx, uint16_t val) {
    uint32_t res = (uint32_t)ctx->hl + val;
    ctx->f_n = 0;
    ctx->f_h = ((ctx->hl & 0x0FFF) + (val & 0x0FFF)) > 0x0FFF;
    ctx->f_c = res > 0xFFFF;
    ctx->hl = (uint16_t)res;
}

void gbrt_timed_inc16(GBContext* ctx, uint16_t* value) {
    const uint16_t address_bus = *value;
    gb_tick(ctx, 4);
    gbrt_trigger_oam_bug_write(ctx, address_bus);
    (*value)++;
    gb_tick(ctx, 4);
}

void gbrt_timed_dec16(GBContext* ctx, uint16_t* value) {
    const uint16_t address_bus = *value;
    gb_tick(ctx, 4);
    gbrt_trigger_oam_bug_write(ctx, address_bus);
    (*value)--;
    gb_tick(ctx, 4);
}

uint8_t gbrt_timed_hl_read_auto(GBContext* ctx, int8_t delta) {
    const uint16_t address_bus = ctx->hl;
    gb_tick(ctx, 4);
    ctx->hl = (uint16_t)(ctx->hl + delta);
    gbrt_trigger_oam_bug_read(ctx, address_bus);
    gb_tick(ctx, 3);
    const uint8_t value = gbrt_read8_impl(ctx, address_bus, false);
    gb_tick(ctx, 1);
    return value;
}

void gbrt_timed_hl_write_auto(GBContext* ctx, uint8_t value, int8_t delta) {
    const uint16_t address_bus = ctx->hl;
    gb_tick(ctx, 4);
    ctx->hl = (uint16_t)(ctx->hl + delta);
    gbrt_trigger_oam_bug_write(ctx, address_bus);
    gb_tick(ctx, 3);
    gbrt_write8_impl(ctx, address_bus, value, false);
    gb_tick(ctx, 1);
}

void gb_add_sp(GBContext* ctx, int8_t off) {
    ctx->f_z = 0; ctx->f_n = 0;
    ctx->f_h = ((ctx->sp & 0x0F) + (off & 0x0F)) > 0x0F;
    ctx->f_c = ((ctx->sp & 0xFF) + (off & 0xFF)) > 0xFF;
    ctx->sp += off;
}

void gbrt_timed_add_sp(GBContext* ctx, uint16_t immediate_addr) {
    /* M0 fetches the opcode. Sample e late in M1, then retire the two idle
     * machine cycles before exposing the updated stack pointer. */
    gb_tick(ctx, 7);
    const int8_t offset = (int8_t)gb_read8(ctx, immediate_addr);
    gb_tick(ctx, 9);
    gb_add_sp(ctx, offset);
}

void gb_ld_hl_sp_n(GBContext* ctx, int8_t off) {
    ctx->f_z = 0; ctx->f_n = 0;
    ctx->f_h = ((ctx->sp & 0x0F) + (off & 0x0F)) > 0x0F;
    ctx->f_c = ((ctx->sp & 0xFF) + (off & 0xFF)) > 0xFF;
    ctx->hl = ctx->sp + off;
}

void gbrt_timed_ld_hl_sp_n(GBContext* ctx, uint16_t immediate_addr) {
    /* M0 fetches the opcode. Sample e late in M1, then retire the single idle
     * machine cycle before exposing the updated HL value. */
    gb_tick(ctx, 7);
    const int8_t offset = (int8_t)gb_read8(ctx, immediate_addr);
    gb_tick(ctx, 5);
    gb_ld_hl_sp_n(ctx, offset);
}

uint8_t gb_rlc(GBContext* ctx, uint8_t v) { ctx->f_c = v >> 7; v = (v << 1) | ctx->f_c; ctx->f_z = v == 0; ctx->f_n = 0; ctx->f_h = 0; return v; }
uint8_t gb_rrc(GBContext* ctx, uint8_t v) { ctx->f_c = v & 1; v = (v >> 1) | (ctx->f_c << 7); ctx->f_z = v == 0; ctx->f_n = 0; ctx->f_h = 0; return v; }
uint8_t gb_rl(GBContext* ctx, uint8_t v) { uint8_t c = ctx->f_c; ctx->f_c = v >> 7; v = (v << 1) | c; ctx->f_z = v == 0; ctx->f_n = 0; ctx->f_h = 0; return v; }
uint8_t gb_rr(GBContext* ctx, uint8_t v) { uint8_t c = ctx->f_c; ctx->f_c = v & 1; v = (v >> 1) | (c << 7); ctx->f_z = v == 0; ctx->f_n = 0; ctx->f_h = 0; return v; }
uint8_t gb_sla(GBContext* ctx, uint8_t v) { ctx->f_c = v >> 7; v <<= 1; ctx->f_z = v == 0; ctx->f_n = 0; ctx->f_h = 0; return v; }
uint8_t gb_sra(GBContext* ctx, uint8_t v) { ctx->f_c = v & 1; v = (uint8_t)((int8_t)v >> 1); ctx->f_z = v == 0; ctx->f_n = 0; ctx->f_h = 0; return v; }
uint8_t gb_swap(GBContext* ctx, uint8_t v) { v = (uint8_t)((v << 4) | (v >> 4)); ctx->f_z = v == 0; ctx->f_n = 0; ctx->f_h = 0; ctx->f_c = 0; return v; }
uint8_t gb_srl(GBContext* ctx, uint8_t v) { ctx->f_c = v & 1; v >>= 1; ctx->f_z = v == 0; ctx->f_n = 0; ctx->f_h = 0; return v; }
void gb_bit(GBContext* ctx, uint8_t bit, uint8_t v) { ctx->f_z = !(v & (1 << bit)); ctx->f_n = 0; ctx->f_h = 1; }

void gb_rlca(GBContext* ctx) { ctx->a = gb_rlc(ctx, ctx->a); ctx->f_z = 0; }
void gb_rrca(GBContext* ctx) { ctx->a = gb_rrc(ctx, ctx->a); ctx->f_z = 0; }
void gb_rla(GBContext* ctx) { ctx->a = gb_rl(ctx, ctx->a); ctx->f_z = 0; }
void gb_rra(GBContext* ctx) { ctx->a = gb_rr(ctx, ctx->a); ctx->f_z = 0; }

void gb_daa(GBContext* ctx) {
   int a = ctx->a;
   if (!ctx->f_n) {
       if (ctx->f_h || (a & 0xF) > 9) a += 0x06;
       if (ctx->f_c || a > 0x9F) a += 0x60;
   } else {
       if (ctx->f_h) a = (a - 6) & 0xFF;
       if (ctx->f_c) a -= 0x60;
   }
   
   ctx->f_h = 0;
   if ((a & 0x100) == 0x100) ctx->f_c = 1;
   
   a &= 0xFF;
   ctx->f_z = (a == 0);
   ctx->a = (uint8_t)a;
}

/* ============================================================================
 * Control Flow helpers
 * ========================================================================== */

void gb_ret(GBContext* ctx) {
    ctx->pc = gb_pop16(ctx);
#ifdef GBRT_ENABLE_NATIVE_PATCHES
    gbrt_native_patch_on_return(ctx);
#endif
}

void gbrt_timed_call(GBContext* ctx,
                     uint16_t target,
                     uint16_t return_address) {
    /* Opcode + two immediate reads + internal stack cycle, then two writes. */
    gbrt_timed_stack_write16(ctx, return_address, 16, true, target);
}

void gbrt_timed_call_after_imm16(GBContext* ctx,
                                 uint16_t target,
                                 uint16_t return_address) {
    /* The opcode and two immediate-read M-cycles have already retired. */
    gbrt_timed_stack_write16(ctx, return_address, 4, true, target);
}

void gbrt_timed_ret(GBContext* ctx) {
    /* Opcode fetch, two stack reads, then the internal jump cycle. */
    (void)gbrt_timed_stack_read16(ctx, 4, true);
    gb_tick(ctx, 4);
#ifdef GBRT_ENABLE_NATIVE_PATCHES
    gbrt_native_patch_on_return(ctx);
#endif
}

void gbrt_timed_ret_cc(GBContext* ctx) {
    /* Conditional RET has one extra internal cycle before the stack reads. */
    (void)gbrt_timed_stack_read16(ctx, 8, true);
    gb_tick(ctx, 4);
#ifdef GBRT_ENABLE_NATIVE_PATCHES
    gbrt_native_patch_on_return(ctx);
#endif
}

void gbrt_timed_reti(GBContext* ctx) {
    gbrt_timed_ret(ctx);
    ctx->ime = 1;
    ctx->ime_pending = 0;
    /* RETI enables IME at its instruction boundary. A pending source must be
     * serviced before compiled dispatch executes the instruction at the
     * restored PC. */
    if (ctx->io[0x0F] & ctx->io[0x80] & 0x1F) {
        ctx->stopped = 1;
    }
}

void gbrt_timed_jump(GBContext* ctx,
                     uint16_t target,
                     uint8_t instruction_cycles) {
    if (instruction_cycles == 0) {
        ctx->pc = target;
        return;
    }
    gb_tick(ctx, (uint32_t)instruction_cycles - 1u);
    ctx->pc = target;
    gb_tick(ctx, 1);
}

void gbrt_jump_hl(GBContext* ctx) { ctx->pc = ctx->hl; }

bool gbrt_fast_forward_visible_ly_wait(GBContext* ctx,
                                       uint8_t target_ly,
                                       uint32_t miss_iteration_cycles,
                                       uint32_t hit_iteration_cycles) {
    if (!ctx || !ctx->ppu || target_ly >= VISIBLE_SCANLINES ||
        miss_iteration_cycles == 0 || hit_iteration_cycles == 0) {
        return false;
    }
    if (ctx->cgb_double_speed ||
        ctx->dma.pending ||
        ctx->dma.active ||
        ctx->serial_transfer.active ||
        (ctx->io[0x07] & 0x04) ||
        ctx->tima_reload_pending > 0 ||
        !(ctx->io[0x40] & LCDC_LCD_ENABLE)) {
        return false;
    }

    const GBPPU* ppu = (const GBPPU*)ctx->ppu;
    uint32_t line_progress = 0;
    switch (ppu->mode) {
        case PPU_MODE_OAM:
            line_progress = ppu->mode_cycles;
            break;
        case PPU_MODE_DRAW:
            line_progress = CYCLES_OAM_SCAN + ppu->mode_cycles;
            break;
        case PPU_MODE_HBLANK:
            line_progress = CYCLES_OAM_SCAN + ppu->mode3_length + ppu->mode_cycles;
            break;
        default:
            return false;
    }
    if (line_progress >= CYCLES_SCANLINE || ppu->ly >= VISIBLE_SCANLINES) {
        return false;
    }
    if (target_ly < ppu->ly) {
        return false;
    }

    const uint32_t frame_pos = ((uint32_t)ppu->ly * CYCLES_SCANLINE) + line_progress;
    const uint32_t target_start = (uint32_t)target_ly * CYCLES_SCANLINE;
    const uint32_t target_end = target_start + CYCLES_SCANLINE;
    uint32_t cycles_until_hit = 0;
    if (frame_pos < target_start) {
        cycles_until_hit = ((target_start - frame_pos + miss_iteration_cycles - 1) /
                            miss_iteration_cycles) * miss_iteration_cycles;
        if (frame_pos + cycles_until_hit >= target_end) {
            return false;
        }
    }

    ctx->a = target_ly;
    ctx->f_z = 1;
    ctx->f_n = 1;
    ctx->f_h = 0;
    ctx->f_c = 0;
    gb_tick(ctx, cycles_until_hit + hit_iteration_cycles);
    return !ctx->stopped;
}

void gb_rst(GBContext* ctx, uint8_t vec) { gb_push16(ctx, ctx->pc); ctx->pc = vec; }

void gbrt_timed_rst(GBContext* ctx,
                    uint8_t vec,
                    uint16_t return_address) {
    /* Opcode fetch + internal stack cycle, then two writes. */
    gbrt_timed_stack_write16(ctx, return_address, 8, true, vec);
}

static bool gbrt_condition_true(const GBContext* ctx, uint8_t condition) {
    switch (condition) {
        case 0: return !ctx->f_z;      /* NZ */
        case 1: return ctx->f_z != 0;  /* Z */
        case 2: return !ctx->f_c;      /* NC */
        case 3: return ctx->f_c != 0;  /* C */
        default: return false;
    }
}

uint8_t gbrt_try_execute_highmem_stub(GBContext* ctx, uint16_t addr) {
    if (!ctx) {
        return 0;
    }

    if (ctx->dma.active) {
        return 0;
    }

    if (addr < 0xFF00 || addr >= 0xFF80) {
        return 0;
    }

    uint8_t opcode = gb_read8(ctx, addr);
    switch (opcode) {
        case 0x00: /* NOP */
            ctx->pc = (uint16_t)(addr + 1);
            gb_tick(ctx, 4);
            return 1;

        case 0x18: { /* JR e */
            int8_t offset = (int8_t)gb_read8(ctx, (uint16_t)(addr + 1));
            ctx->pc = (uint16_t)(addr + 2 + offset);
            gb_tick(ctx, 12);
            return 1;
        }

        case 0x20: /* JR NZ,e */
        case 0x28: /* JR Z,e */
        case 0x30: /* JR NC,e */
        case 0x38: { /* JR C,e */
            static const uint8_t conditions[] = {0, 1, 2, 3};
            uint8_t condition = conditions[(opcode - 0x20) >> 3];
            int8_t offset = (int8_t)gb_read8(ctx, (uint16_t)(addr + 1));
            if (gbrt_condition_true(ctx, condition)) {
                ctx->pc = (uint16_t)(addr + 2 + offset);
                gb_tick(ctx, 12);
            } else {
                ctx->pc = (uint16_t)(addr + 2);
                gb_tick(ctx, 8);
            }
            return 1;
        }

        case 0xC0: /* RET NZ */
        case 0xC8: /* RET Z */
        case 0xD0: /* RET NC */
        case 0xD8: { /* RET C */
            static const uint8_t conditions[] = {0, 1, 2, 3};
            uint8_t condition = conditions[(opcode - 0xC0) >> 3];
            if (gbrt_condition_true(ctx, condition)) {
                gbrt_timed_ret_cc(ctx);
            } else {
                gbrt_timed_jump(ctx, (uint16_t)(addr + 1), 8);
            }
            return 1;
        }

        case 0xC2: /* JP NZ,nn */
        case 0xCA: /* JP Z,nn */
        case 0xD2: /* JP NC,nn */
        case 0xDA: { /* JP C,nn */
            static const uint8_t conditions[] = {0, 1, 2, 3};
            uint8_t condition = conditions[(opcode - 0xC2) >> 3];
            uint16_t target = (uint16_t)(gb_read8(ctx, (uint16_t)(addr + 1)) |
                                         (gb_read8(ctx, (uint16_t)(addr + 2)) << 8));
            if (gbrt_condition_true(ctx, condition)) {
                gbrt_timed_jump(ctx, target, 16);
            } else {
                gbrt_timed_jump(ctx, (uint16_t)(addr + 3), 12);
            }
            return 1;
        }

        case 0xC3: { /* JP nn */
            uint16_t target = (uint16_t)(gb_read8(ctx, (uint16_t)(addr + 1)) |
                                         (gb_read8(ctx, (uint16_t)(addr + 2)) << 8));
            gbrt_timed_jump(ctx, target, 16);
            return 1;
        }

        case 0xC4: /* CALL NZ,nn */
        case 0xCC: /* CALL Z,nn */
        case 0xD4: /* CALL NC,nn */
        case 0xDC: { /* CALL C,nn */
            static const uint8_t conditions[] = {0, 1, 2, 3};
            uint8_t condition = conditions[(opcode - 0xC4) >> 3];
            uint16_t target = (uint16_t)(gb_read8(ctx, (uint16_t)(addr + 1)) |
                                         (gb_read8(ctx, (uint16_t)(addr + 2)) << 8));
            if (gbrt_condition_true(ctx, condition)) {
                gbrt_timed_call(ctx, target, (uint16_t)(addr + 3));
            } else {
                gbrt_timed_jump(ctx, (uint16_t)(addr + 3), 12);
            }
            return 1;
        }

        case 0xC7: /* RST 00 */
        case 0xCF: /* RST 08 */
        case 0xD7: /* RST 10 */
        case 0xDF: /* RST 18 */
        case 0xE7: /* RST 20 */
        case 0xEF: /* RST 28 */
        case 0xF7: /* RST 30 */
        case 0xFF: /* RST 38 */
            gbrt_timed_rst(ctx,
                           (uint8_t)(opcode & 0x38),
                           (uint16_t)(addr + 1));
            return 1;

        case 0xC9: /* RET */
            gbrt_timed_ret(ctx);
            return 1;

        case 0xCD: { /* CALL nn */
            uint16_t target = (uint16_t)(gb_read8(ctx, (uint16_t)(addr + 1)) |
                                         (gb_read8(ctx, (uint16_t)(addr + 2)) << 8));
            gbrt_timed_call(ctx, target, (uint16_t)(addr + 3));
            return 1;
        }

        case 0xD9: /* RETI */
            gbrt_timed_reti(ctx);
            return 1;

        case 0xE9: /* JP HL */
            gbrt_timed_jump(ctx, ctx->hl, 4);
            return 1;

        case 0xF3: /* DI */
            ctx->ime = 0;
            ctx->pc = (uint16_t)(addr + 1);
            gb_tick(ctx, 4);
            return 1;

        case 0xFB: /* EI */
            ctx->ime = 1;
            ctx->pc = (uint16_t)(addr + 1);
            gb_tick(ctx, 4);
            return 1;

        default:
            return 0;
    }
}

static uint8_t gbrt_match_hram_bytes(GBContext* ctx,
                                     uint16_t addr,
                                     const uint8_t* pattern,
                                     size_t pattern_len) {
    if (!ctx || !pattern || pattern_len == 0) {
        return 0;
    }

    if (addr < 0xFF80 || addr > 0xFFFE) {
        return 0;
    }

    uint32_t end_addr = (uint32_t)addr + (uint32_t)pattern_len - 1u;
    if (end_addr > 0xFFFEu) {
        return 0;
    }

    for (size_t i = 0; i < pattern_len; ++i) {
        if (gb_read8(ctx, (uint16_t)(addr + (uint16_t)i)) != pattern[i]) {
            return 0;
        }
    }
    return 1;
}

static uint8_t gbrt_execute_oam_dma_wait_loop(GBContext* ctx,
                                              uint16_t addr,
                                              uint8_t start_dma,
                                              uint8_t initialize_counter) {
    uint32_t cycles = 0;
    ctx->pc = addr;

    if (start_dma) {
        gb_write8(ctx, 0xFF46, ctx->a);
        ctx->pc = (uint16_t)(addr + 2);
        cycles += 12;
    }

    if (initialize_counter) {
        ctx->a = 0x28;
        ctx->pc = (uint16_t)(ctx->pc + 2);
        cycles += 8;
    }

    while (1) {
        ctx->a = gb_dec8(ctx, ctx->a);
        ctx->pc = (uint16_t)(ctx->pc + 1);
        cycles += 4;

        if (!ctx->f_z) {
            ctx->pc = (uint16_t)(ctx->pc - 1);
            cycles += 12;
            continue;
        }

        ctx->pc = (uint16_t)(ctx->pc + 2);
        cycles += 8;
        break;
    }

    gb_tick(ctx, cycles);
    if (ctx->stopped) {
        return 1;
    }

    gbrt_timed_ret(ctx);
    return 1;
}

uint8_t gbrt_try_execute_hram_stub(GBContext* ctx, uint16_t addr) {
    static const uint8_t dma_wait_full[] = {0xE0, 0x46, 0x3E, 0x28, 0x3D, 0x20, 0xFD, 0xC9};
    static const uint8_t dma_wait_delay[] = {0x3E, 0x28, 0x3D, 0x20, 0xFD, 0xC9};
    static const uint8_t dma_wait_loop[] = {0x3D, 0x20, 0xFD, 0xC9};

    if (!ctx) {
        return 0;
    }

    /*
     * Some games build the standard OAM DMA wait helper dynamically in HRAM
     * and call different entrypoints into the same tiny loop. We cannot return
     * early after starting DMA because the RET must happen only after enough
     * cycles have elapsed for HRAM stack access to become valid again.
     */
    if (gbrt_match_hram_bytes(ctx, addr, dma_wait_full, sizeof(dma_wait_full))) {
        return gbrt_execute_oam_dma_wait_loop(ctx, addr, 1, 1);
    }
    if (gbrt_match_hram_bytes(ctx, addr, dma_wait_delay, sizeof(dma_wait_delay))) {
        return gbrt_execute_oam_dma_wait_loop(ctx, addr, 0, 1);
    }
    if (gbrt_match_hram_bytes(ctx, addr, dma_wait_loop, sizeof(dma_wait_loop))) {
        return gbrt_execute_oam_dma_wait_loop(ctx, addr, 0, 0);
    }

    return 0;
}

static uint8_t gbrt_stub_final_read8(GBContext* ctx,
                                     uint16_t addr,
                                     uint8_t cycles) {
    return gbrt_timed_bus_read8(ctx, addr, (uint8_t)(cycles - 1u));
}

static void gbrt_stub_final_write8(GBContext* ctx,
                                   uint16_t addr,
                                   uint8_t value,
                                   uint8_t cycles) {
    gbrt_timed_bus_write8(ctx,
                          addr,
                          value,
                          (uint8_t)(cycles - 1u));
}

uint8_t gbrt_try_execute_ram_stub(GBContext* ctx, uint16_t addr) {
    if (!ctx) {
        return 0;
    }

    /* Only handle copied helper code from writable memory areas. */
    bool in_wram = (addr >= 0xC000 && addr < 0xFE00);
    bool in_hram = (addr >= 0xFF80 && addr <= 0xFFFE);
    if (!in_wram && !in_hram) {
        return 0;
    }

    if (ctx->dma.active && !in_hram) {
        /* The DMG CPU cannot execute copied WRAM code while OAM DMA owns the
         * external bus. HRAM remains available and its helpers must continue
         * with instruction-accurate timing until the transfer completes.
         */
        return 0;
    }

    uint8_t opcode = gb_read8(ctx, addr);
    switch (opcode) {
        case 0x00: /* NOP */
            ctx->pc = (uint16_t)(addr + 1);
            gb_tick(ctx, 4);
            return 1;

        case 0x40: /* LD B,B - opt-in test harness breakpoint */
            ctx->pc = (uint16_t)(addr + 1);
            gb_tick(ctx, 4);
            if (gbrt_test_breakpoint_enabled) {
                ctx->stopped = 1;
            }
            return 1;

        case 0x04: ctx->b = gb_inc8(ctx, ctx->b); ctx->pc = (uint16_t)(addr + 1); gb_tick(ctx, 4); return 1;
        case 0x05: ctx->b = gb_dec8(ctx, ctx->b); ctx->pc = (uint16_t)(addr + 1); gb_tick(ctx, 4); return 1;
        case 0x0C: ctx->c = gb_inc8(ctx, ctx->c); ctx->pc = (uint16_t)(addr + 1); gb_tick(ctx, 4); return 1;
        case 0x0D: ctx->c = gb_dec8(ctx, ctx->c); ctx->pc = (uint16_t)(addr + 1); gb_tick(ctx, 4); return 1;
        case 0x14: ctx->d = gb_inc8(ctx, ctx->d); ctx->pc = (uint16_t)(addr + 1); gb_tick(ctx, 4); return 1;
        case 0x15: ctx->d = gb_dec8(ctx, ctx->d); ctx->pc = (uint16_t)(addr + 1); gb_tick(ctx, 4); return 1;
        case 0x1C: ctx->e = gb_inc8(ctx, ctx->e); ctx->pc = (uint16_t)(addr + 1); gb_tick(ctx, 4); return 1;
        case 0x1D: ctx->e = gb_dec8(ctx, ctx->e); ctx->pc = (uint16_t)(addr + 1); gb_tick(ctx, 4); return 1;
        case 0x24: ctx->h = gb_inc8(ctx, ctx->h); ctx->pc = (uint16_t)(addr + 1); gb_tick(ctx, 4); return 1;
        case 0x25: ctx->h = gb_dec8(ctx, ctx->h); ctx->pc = (uint16_t)(addr + 1); gb_tick(ctx, 4); return 1;
        case 0x2C: ctx->l = gb_inc8(ctx, ctx->l); ctx->pc = (uint16_t)(addr + 1); gb_tick(ctx, 4); return 1;
        case 0x2D: ctx->l = gb_dec8(ctx, ctx->l); ctx->pc = (uint16_t)(addr + 1); gb_tick(ctx, 4); return 1;
        case 0x3C: ctx->a = gb_inc8(ctx, ctx->a); ctx->pc = (uint16_t)(addr + 1); gb_tick(ctx, 4); return 1;
        case 0x3D: ctx->a = gb_dec8(ctx, ctx->a); ctx->pc = (uint16_t)(addr + 1); gb_tick(ctx, 4); return 1;

        case 0x06: if (addr == 0xFFFF) return 0; ctx->b = gb_read8(ctx, (uint16_t)(addr + 1)); ctx->pc = (uint16_t)(addr + 2); gb_tick(ctx, 8); return 1;
        case 0x0E: if (addr == 0xFFFF) return 0; ctx->c = gb_read8(ctx, (uint16_t)(addr + 1)); ctx->pc = (uint16_t)(addr + 2); gb_tick(ctx, 8); return 1;
        case 0x16: if (addr == 0xFFFF) return 0; ctx->d = gb_read8(ctx, (uint16_t)(addr + 1)); ctx->pc = (uint16_t)(addr + 2); gb_tick(ctx, 8); return 1;
        case 0x1E: if (addr == 0xFFFF) return 0; ctx->e = gb_read8(ctx, (uint16_t)(addr + 1)); ctx->pc = (uint16_t)(addr + 2); gb_tick(ctx, 8); return 1;
        case 0x26: if (addr == 0xFFFF) return 0; ctx->h = gb_read8(ctx, (uint16_t)(addr + 1)); ctx->pc = (uint16_t)(addr + 2); gb_tick(ctx, 8); return 1;
        case 0x2E: if (addr == 0xFFFF) return 0; ctx->l = gb_read8(ctx, (uint16_t)(addr + 1)); ctx->pc = (uint16_t)(addr + 2); gb_tick(ctx, 8); return 1;
        case 0x3E: if (addr == 0xFFFF) return 0; ctx->a = gb_read8(ctx, (uint16_t)(addr + 1)); ctx->pc = (uint16_t)(addr + 2); gb_tick(ctx, 8); return 1;

        case 0x18: { /* JR e */
            if (addr == 0xFFFF) {
                return 0;
            }
            int8_t offset = (int8_t)gb_read8(ctx, (uint16_t)(addr + 1));
            ctx->pc = (uint16_t)(addr + 2 + offset);
            gb_tick(ctx, 12);
            return 1;
        }

        case 0x20: /* JR NZ,e */
        case 0x28: /* JR Z,e */
        case 0x30: /* JR NC,e */
        case 0x38: { /* JR C,e */
            if (addr == 0xFFFF) {
                return 0;
            }
            int8_t offset = (int8_t)gb_read8(ctx, (uint16_t)(addr + 1));
            bool taken = gbrt_condition_true(ctx, (uint8_t)((opcode >> 3) & 0x03));
            ctx->pc = taken ? (uint16_t)(addr + 2 + offset) : (uint16_t)(addr + 2);
            gb_tick(ctx, taken ? 12 : 8);
            return 1;
        }

        case 0x22: /* LD (HL+),A */
            gbrt_timed_hl_write_auto(ctx, ctx->a, 1);
            ctx->pc = (uint16_t)(addr + 1);
            return 1;

        case 0x2A: /* LD A,(HL+) */
            ctx->a = gbrt_timed_hl_read_auto(ctx, 1);
            ctx->pc = (uint16_t)(addr + 1);
            return 1;

        case 0x32: /* LD (HL-),A */
            gbrt_timed_hl_write_auto(ctx, ctx->a, -1);
            ctx->pc = (uint16_t)(addr + 1);
            return 1;

        case 0x3A: /* LD A,(HL-) */
            ctx->a = gbrt_timed_hl_read_auto(ctx, -1);
            ctx->pc = (uint16_t)(addr + 1);
            return 1;

        case 0x77: /* LD (HL),A */
            gbrt_stub_final_write8(ctx, ctx->hl, ctx->a, 8);
            ctx->pc = (uint16_t)(addr + 1);
            return 1;

        case 0x7E: /* LD A,(HL) */
            ctx->a = gbrt_stub_final_read8(ctx, ctx->hl, 8);
            ctx->pc = (uint16_t)(addr + 1);
            return 1;

        case 0xAF: /* XOR A */
            gb_xor8(ctx, ctx->a);
            ctx->pc = (uint16_t)(addr + 1);
            gb_tick(ctx, 4);
            return 1;

        case 0xC3: { /* JP nn */
            if (addr == 0xFFFF) {
                return 0;
            }
            uint16_t target = gb_read16(ctx, (uint16_t)(addr + 1));
            gbrt_timed_jump(ctx, target, 16);
            return 1;
        }

        case 0xC9: /* RET */
            gbrt_timed_ret(ctx);
            return 1;

        case 0xC0: /* RET NZ */
        case 0xC8: /* RET Z */
        case 0xD0: /* RET NC */
        case 0xD8: { /* RET C */
            bool taken = gbrt_condition_true(ctx, (uint8_t)((opcode >> 3) & 0x03));
            if (taken) {
                gbrt_timed_ret_cc(ctx);
            } else {
                gbrt_timed_jump(ctx, (uint16_t)(addr + 1), 8);
            }
            return 1;
        }

        case 0xC2: /* JP NZ,nn */
        case 0xCA: /* JP Z,nn */
        case 0xD2: /* JP NC,nn */
        case 0xDA: { /* JP C,nn */
            if (addr >= 0xFFFE) {
                return 0;
            }
            uint16_t target = gb_read16(ctx, (uint16_t)(addr + 1));
            bool taken = gbrt_condition_true(ctx, (uint8_t)((opcode >> 3) & 0x03));
            gbrt_timed_jump(ctx,
                            taken ? target : (uint16_t)(addr + 3),
                            taken ? 16 : 12);
            return 1;
        }

        case 0xCD: { /* CALL nn */
            if (addr >= 0xFFFD) {
                return 0;
            }
            uint16_t target = gb_read16(ctx, (uint16_t)(addr + 1));
            gbrt_timed_call(ctx, target, (uint16_t)(addr + 3));
            return 1;
        }

        case 0xC4: /* CALL NZ,nn */
        case 0xCC: /* CALL Z,nn */
        case 0xD4: /* CALL NC,nn */
        case 0xDC: { /* CALL C,nn */
            if (addr >= 0xFFFD) {
                return 0;
            }
            uint16_t target = gb_read16(ctx, (uint16_t)(addr + 1));
            bool taken = gbrt_condition_true(ctx, (uint8_t)((opcode >> 3) & 0x03));
            if (taken) {
                gbrt_timed_call(ctx, target, (uint16_t)(addr + 3));
            } else {
                gbrt_timed_jump(ctx, (uint16_t)(addr + 3), 12);
            }
            return 1;
        }

        case 0xD9: /* RETI */
            gbrt_timed_reti(ctx);
            return 1;

        case 0xE0: { /* LDH (n),A */
            if (addr == 0xFFFF) {
                return 0;
            }
            uint8_t offset = gb_read8(ctx, (uint16_t)(addr + 1));
            gbrt_stub_final_write8(ctx,
                                   (uint16_t)(0xFF00u + offset),
                                   ctx->a,
                                   12);
            ctx->pc = (uint16_t)(addr + 2);
            return 1;
        }

        case 0xE2: /* LD (C),A */
            gbrt_stub_final_write8(ctx,
                                   (uint16_t)(0xFF00u + ctx->c),
                                   ctx->a,
                                   8);
            ctx->pc = (uint16_t)(addr + 1);
            return 1;

        case 0xE6: /* AND n */
            if (addr == 0xFFFF) {
                return 0;
            }
            gb_and8(ctx, gb_read8(ctx, (uint16_t)(addr + 1)));
            ctx->pc = (uint16_t)(addr + 2);
            gb_tick(ctx, 8);
            return 1;

        case 0xE8: /* ADD SP,e */
            if (addr == 0xFFFF) {
                return 0;
            }
            ctx->pc = (uint16_t)(addr + 2);
            gbrt_timed_add_sp(ctx, (uint16_t)(addr + 1));
            return 1;

        case 0xE9: /* JP HL */
            gbrt_timed_jump(ctx, ctx->hl, 4);
            return 1;

        case 0xEA: { /* LD (nn),A */
            if (addr >= 0xFFFE) {
                return 0;
            }
            uint16_t target = gb_read16(ctx, (uint16_t)(addr + 1));
            gbrt_stub_final_write8(ctx, target, ctx->a, 16);
            ctx->pc = (uint16_t)(addr + 3);
            return 1;
        }

        case 0xEE: /* XOR n */
            if (addr == 0xFFFF) {
                return 0;
            }
            gb_xor8(ctx, gb_read8(ctx, (uint16_t)(addr + 1)));
            ctx->pc = (uint16_t)(addr + 2);
            gb_tick(ctx, 8);
            return 1;

        case 0xF0: { /* LDH A,(n) */
            if (addr == 0xFFFF) {
                return 0;
            }
            uint8_t offset = gb_read8(ctx, (uint16_t)(addr + 1));
            ctx->a = gbrt_stub_final_read8(ctx,
                                           (uint16_t)(0xFF00u + offset),
                                           12);
            ctx->pc = (uint16_t)(addr + 2);
            return 1;
        }

        case 0xF2: /* LD A,(C) */
            ctx->a = gbrt_stub_final_read8(ctx,
                                           (uint16_t)(0xFF00u + ctx->c),
                                           8);
            ctx->pc = (uint16_t)(addr + 1);
            return 1;

        case 0xF6: /* OR n */
            if (addr == 0xFFFF) {
                return 0;
            }
            gb_or8(ctx, gb_read8(ctx, (uint16_t)(addr + 1)));
            ctx->pc = (uint16_t)(addr + 2);
            gb_tick(ctx, 8);
            return 1;

        case 0xF8: /* LD HL,SP+e */
            if (addr == 0xFFFF) {
                return 0;
            }
            ctx->pc = (uint16_t)(addr + 2);
            gbrt_timed_ld_hl_sp_n(ctx, (uint16_t)(addr + 1));
            return 1;

        case 0xFA: { /* LD A,(nn) */
            if (addr >= 0xFFFE) {
                return 0;
            }
            uint16_t target = gb_read16(ctx, (uint16_t)(addr + 1));
            ctx->a = gbrt_stub_final_read8(ctx, target, 16);
            ctx->pc = (uint16_t)(addr + 3);
            return 1;
        }

        case 0xFE: /* CP n */
            if (addr == 0xFFFF) {
                return 0;
            }
            gb_cp8(ctx, gb_read8(ctx, (uint16_t)(addr + 1)));
            ctx->pc = (uint16_t)(addr + 2);
            gb_tick(ctx, 8);
            return 1;

        default:
            return 0;
    }
}

void gbrt_set_trace_file(const char* filename) {
    if (gbrt_trace_filename) free(gbrt_trace_filename);
    if (filename) gbrt_trace_filename = strdup(filename);
    else gbrt_trace_filename = NULL;
}

void gbrt_log_trace(GBContext* ctx, uint16_t bank, uint16_t addr) {
    if (ctx->trace_entries_enabled && ctx->trace_file) {
        fprintf((FILE*)ctx->trace_file, "%d:%04x\n", (int)bank, (int)addr);
    }
}

void gbrt_log_ppu_scanline(GBContext* ctx,
                           uint8_t ly,
                           uint8_t mode,
                           uint8_t lcdc,
                           uint8_t stat,
                           uint8_t scx,
                           uint8_t scy,
                           uint8_t wx,
                           uint8_t wy,
                           uint8_t bgp,
                           uint8_t obp0,
                           uint8_t obp1,
                           uint8_t window_line,
                           bool window_triggered) {
    uint64_t frame_index = ctx ? (ctx->completed_frames + 1) : 0;
    const GBPPU* ppu = ctx ? (const GBPPU*)ctx->ppu : NULL;
    if (!gbrt_ppu_trace_enabled_for_frame(ctx, frame_index)) {
        return;
    }

    fprintf((FILE*)ctx->ppu_trace_file,
            "[PPU-LINE] frame=%llu cyc=%u ly=%u mode=%u visible_mode=%u irq_mode=%u mode3_dots=%u hblank_dots=%u sprites=%u lcdc=%02X stat=%02X scx=%u scy=%u wx=%u wy=%u bgp=%02X obp0=%02X obp1=%02X window_line=%u window_triggered=%u\n",
            (unsigned long long)frame_index,
            ctx->frame_cycles,
            ly,
            mode,
            ppu ? ppu->visible_mode : mode,
            ppu ? ppu->stat_irq_mode : mode,
            ppu ? ppu->mode3_length : 0u,
            ppu ? ppu->hblank_length : 0u,
            ppu ? ppu->visible_sprite_count : 0u,
            lcdc,
            stat,
            scx,
            scy,
            wx,
            wy,
            bgp,
            obp0,
            obp1,
            window_line,
            window_triggered ? 1u : 0u);
}

void gbrt_log_ppu_register_write(GBContext* ctx,
                                 uint16_t addr,
                                 uint8_t old_value,
                                 uint8_t new_value,
                                 uint8_t ly,
                                 uint8_t mode) {
    uint64_t frame_index = ctx ? (ctx->completed_frames + 1) : 0;
    if (!gbrt_ppu_trace_enabled_for_frame(ctx, frame_index)) {
        return;
    }

    fprintf((FILE*)ctx->ppu_trace_file,
            "[PPU-WRITE] frame=%llu cyc=%u ly=%u mode=%u addr=%04X old=%02X new=%02X\n",
            (unsigned long long)frame_index,
            ctx->frame_cycles,
            ly,
            mode,
            addr,
            old_value,
            new_value);
}

void gbrt_log_oam_snapshot(GBContext* ctx, const char* reason) {
    uint64_t frame_index = ctx ? (ctx->completed_frames + 1) : 0;
    if (!gbrt_ppu_trace_enabled_for_frame(ctx, frame_index)) {
        return;
    }

    fprintf((FILE*)ctx->ppu_trace_file,
            "[OAM-SNAPSHOT] frame=%llu cyc=%u pc=%04X bank=%u ly=%u mode=%u reason=%s\n",
            (unsigned long long)frame_index,
            ctx->frame_cycles,
            ctx->pc,
            (unsigned)gb_resolve_rom_bank(ctx, ctx->pc),
            ctx->io[0x44],
            ctx->io[0x41] & 0x03,
            reason ? reason : "-");

    for (int i = 0; i < 40; i++) {
        const uint8_t* sprite = ctx->oam + (i * 4);
        fprintf((FILE*)ctx->ppu_trace_file,
                "[OAM] frame=%llu idx=%02d y=%02X x=%02X tile=%02X flags=%02X\n",
                (unsigned long long)frame_index,
                i,
                sprite[0],
                sprite[1],
                sprite[2],
                sprite[3]);
    }
}

void gbrt_log_stat_irq_check(GBContext* ctx,
                             const char* reason,
                             uint8_t ly,
                             uint8_t mode,
                             uint8_t stat,
                             uint8_t source_state_mask,
                             uint8_t source_enable_mask,
                             uint8_t active_source_mask,
                             bool previous_line_state,
                             bool current_line_state) {
    uint64_t frame_index = ctx ? (ctx->completed_frames + 1) : 0;
    if (!gbrt_ppu_trace_enabled_for_frame(ctx, frame_index)) {
        return;
    }

    fprintf((FILE*)ctx->ppu_trace_file,
            "[STAT-CHECK] frame=%llu cyc=%u pc=%04X bank=%u ly=%u mode=%u stat=%02X if=%02X ie=%02X reason=%s state=%X enable=%X active=%X prev=%u line=%u\n",
            (unsigned long long)frame_index,
            ctx->frame_cycles,
            ctx->pc,
            (unsigned)gb_resolve_rom_bank(ctx, ctx->pc),
            ly,
            mode,
            stat,
            ctx->io[0x0F],
            ctx->io[0x80],
            reason ? reason : "-",
            source_state_mask,
            source_enable_mask,
            active_source_mask,
            previous_line_state ? 1u : 0u,
            current_line_state ? 1u : 0u);
}

void gbrt_log_stat_irq_request(GBContext* ctx,
                               const char* reason,
                               uint8_t ly,
                               uint8_t mode,
                               uint8_t stat,
                               uint8_t active_source_mask,
                               uint8_t if_before,
                               uint8_t if_after) {
    uint64_t frame_index = ctx ? (ctx->completed_frames + 1) : 0;
    if (!gbrt_ppu_trace_enabled_for_frame(ctx, frame_index)) {
        return;
    }

    fprintf((FILE*)ctx->ppu_trace_file,
            "[STAT-REQ] frame=%llu cyc=%u pc=%04X bank=%u ly=%u mode=%u stat=%02X reason=%s active=%X if_before=%02X if_after=%02X\n",
            (unsigned long long)frame_index,
            ctx->frame_cycles,
            ctx->pc,
            (unsigned)gb_resolve_rom_bank(ctx, ctx->pc),
            ly,
            mode,
            stat,
            reason ? reason : "-",
            active_source_mask,
            if_before,
            if_after);
}

void gbrt_log_interrupt_service(GBContext* ctx,
                                const char* name,
                                uint16_t vector,
                                uint8_t if_before,
                                uint8_t ie_reg,
                                uint8_t interrupt_bit,
                                uint16_t pc_before,
                                uint16_t sp_before) {
    uint64_t frame_index = ctx ? (ctx->completed_frames + 1) : 0;
    if (!gbrt_ppu_trace_enabled_for_frame(ctx, frame_index)) {
        return;
    }

    fprintf((FILE*)ctx->ppu_trace_file,
            "[IRQ-SVC] frame=%llu cyc=%u pc=%04X bank=%u sp=%04X vec=%04X name=%s bit=%02X if_before=%02X ie=%02X\n",
            (unsigned long long)frame_index,
            ctx->frame_cycles,
            pc_before,
            (unsigned)gb_resolve_rom_bank(ctx, pc_before),
            sp_before,
            vector,
            name ? name : "-",
            interrupt_bit,
            if_before,
            ie_reg);
}

#ifndef _MSC_VER
__attribute__((weak)) void gb_dispatch(GBContext* ctx, uint16_t addr) {
    gbrt_log_trace(ctx, gb_resolve_rom_bank(ctx, addr), addr);
    ctx->pc = addr;
    gb_interpret(ctx, addr);
}

__attribute__((weak)) void gb_dispatch_call(GBContext* ctx, uint16_t addr) {
    gbrt_log_trace(ctx, gb_resolve_rom_bank(ctx, addr), addr);
    ctx->pc = addr;
}
#endif

void gbrt_note_dispatch_fallback(GBContext* ctx, uint16_t bank, uint16_t addr) {
    if (!ctx) return;
#ifdef GBRT_ENABLE_PERFORMANCE_COUNTERS
    ctx->performance_counters.interpreter_fallbacks++;
    if (gbrt_visibility_estimator_enabled) {
        ctx->performance_counters.profile_unit_visibility_mask |=
            GBRT_PROFILE_VISIBILITY_FALLBACK;
    }
#endif
    if (!gbrt_dispatch_fallback_tracking_enabled) return;
    if (ctx->frame_dispatch_fallbacks == 0) {
        ctx->frame_first_fallback_bank = bank;
        ctx->frame_first_fallback_addr = addr;
    }
    ctx->used_dispatch_fallback = 1;
    ctx->dispatch_fallback_bank = bank;
    ctx->dispatch_fallback_addr = addr;
    ctx->frame_last_fallback_bank = bank;
    ctx->frame_last_fallback_addr = addr;
    ctx->frame_dispatch_fallbacks++;
    ctx->total_dispatch_fallbacks++;
}

const char* gbrt_dispatch_fallback_reason_name(GBDispatchFallbackReason reason) {
    switch (reason) {
        case GB_DISPATCH_FALLBACK_ADDRESS_NOT_COMPILED:
            return "address_not_compiled";
        case GB_DISPATCH_FALLBACK_BANK_NOT_COMPILED:
            return "bank_not_compiled";
        case GB_DISPATCH_FALLBACK_PAGE_NOT_COMPILED:
            return "page_not_compiled";
        case GB_DISPATCH_FALLBACK_WRITABLE_HRAM:
            return "writable_hram";
        default:
            return "unknown";
    }
}

static GBDispatchFallbackSite* gbrt_find_or_allocate_dispatch_fallback_site(
    GBContext* ctx,
    uint16_t bank,
    uint16_t addr,
    GBDispatchFallbackReason reason,
    uint16_t compiled_bank_variants) {
    for (uint16_t i = 0; i < ctx->dispatch_fallback_site_count; ++i) {
        GBDispatchFallbackSite* site = &ctx->dispatch_fallback_sites[i];
        if (site->bank == bank && site->addr == addr && site->reason == reason) {
            return site;
        }
    }
    if (ctx->dispatch_fallback_site_count >= GBRT_DISPATCH_FALLBACK_SITE_CAPACITY) {
        ctx->dispatch_fallback_sites_dropped++;
        return NULL;
    }

    GBDispatchFallbackSite* site =
        &ctx->dispatch_fallback_sites[ctx->dispatch_fallback_site_count++];
    memset(site, 0, sizeof(*site));
    site->valid = 1;
    site->reason = reason;
    site->bank = bank;
    site->addr = addr;
    site->compiled_bank_variants = compiled_bank_variants;
    site->first_frame = ctx->completed_frames + 1;
    site->last_frame = site->first_frame;
    return site;
}

void gbrt_execute_dispatch_fallback(GBContext* ctx,
                                    uint16_t bank,
                                    uint16_t addr,
                                    GBDispatchFallbackReason reason,
                                    uint16_t compiled_bank_variants) {
    if (!ctx) {
        return;
    }

    const uint64_t instructions_before = ctx->total_interpreter_instructions;
    const uint32_t cycles_before = ctx->cycles;
    gbrt_note_dispatch_fallback(ctx, bank, addr);
    GBDispatchFallbackSite* site = NULL;
    if (gbrt_dispatch_fallback_tracking_enabled) {
        site = gbrt_find_or_allocate_dispatch_fallback_site(
            ctx, bank, addr, reason, compiled_bank_variants);
    }

    gb_interpret(ctx, addr);

    if (site) {
        site->entries++;
        site->instructions +=
            ctx->total_interpreter_instructions - instructions_before;
        site->cycles += (uint32_t)(ctx->cycles - cycles_before);
        site->last_frame = ctx->completed_frames + 1;
    }
}

bool gbrt_performance_counters_available(void) {
#ifdef GBRT_ENABLE_PERFORMANCE_COUNTERS
    return true;
#else
    return false;
#endif
}

void gbrt_reset_performance_counters(GBContext* ctx) {
    if (!ctx) {
        return;
    }
    memset(&ctx->performance_counters, 0, sizeof(ctx->performance_counters));
}

#ifdef GBRT_ENABLE_PERFORMANCE_COUNTERS
static void gbrt_report_histogram(const char* name,
                                  const char* buckets,
                                  const uint64_t* counts,
                                  size_t count) {
    fprintf(stderr, "[PERF-HISTOGRAM] name=%s buckets=%s counts=", name, buckets);
    for (size_t i = 0; i < count; ++i) {
        fprintf(stderr, "%s%llu",
                i == 0 ? "" : ",",
                (unsigned long long)counts[i]);
    }
    fputc('\n', stderr);
}

void gbrt_report_performance_counters(GBContext* ctx) {
    if (!ctx) {
        return;
    }
    gbrt_audio_sync(ctx);
    fprintf(stderr,
            "[PERF-COUNTERS] available=%u tick_commits=%llu tick_cycles=%llu generated_safepoints=%llu direct_transitions=%llu indirect_dispatches=%llu generic_reads=%llu specialized_reads=%llu generic_writes=%llu specialized_writes=%llu interpreter_fallbacks=%llu ppu_tick_calls=%llu ppu_dots=%llu ppu_draw_dots=%llu ppu_rendered_pixels=%llu ppu_stable_spans=%llu ppu_stable_span_dots=%llu audio_samples=%llu rtc_tick_calls=%llu rtc_tick_cycles=%llu dma_tick_calls=%llu dma_tick_cycles=%llu serial_tick_calls=%llu serial_tick_cycles=%llu timer_tick_calls=%llu timer_tick_cycles=%llu audio_step_calls=%llu audio_step_cycles=%llu interrupt_checks=%llu interrupt_stops=%llu frame_stops=%llu region_candidate_units=%llu region_groups=%llu region_grouped_units=%llu region_grouped_tick_commits=%llu region_estimated_removable_tick_commits=%llu region_estimated_removable_safepoints=%llu region_reject_visibility=%llu region_reject_deadline=%llu\n",
            gbrt_performance_counters_available() ? 1u : 0u,
            (unsigned long long)ctx->performance_counters.tick_commits,
            (unsigned long long)ctx->performance_counters.tick_cycles,
            (unsigned long long)ctx->performance_counters.generated_safepoints,
            (unsigned long long)ctx->performance_counters.generated_direct_transitions,
            (unsigned long long)ctx->performance_counters.generated_indirect_dispatches,
            (unsigned long long)ctx->performance_counters.generated_generic_reads,
            (unsigned long long)ctx->performance_counters.generated_specialized_reads,
            (unsigned long long)ctx->performance_counters.generated_generic_writes,
            (unsigned long long)ctx->performance_counters.generated_specialized_writes,
            (unsigned long long)ctx->performance_counters.interpreter_fallbacks,
            (unsigned long long)ctx->performance_counters.ppu_tick_calls,
            (unsigned long long)ctx->performance_counters.ppu_dots,
            (unsigned long long)ctx->performance_counters.ppu_draw_dots,
            (unsigned long long)ctx->performance_counters.ppu_rendered_pixels,
            (unsigned long long)ctx->performance_counters.ppu_stable_spans,
            (unsigned long long)ctx->performance_counters.ppu_stable_span_dots,
            (unsigned long long)ctx->performance_counters.audio_samples,
            (unsigned long long)ctx->performance_counters.rtc_tick_calls,
            (unsigned long long)ctx->performance_counters.rtc_tick_cycles,
            (unsigned long long)ctx->performance_counters.dma_tick_calls,
            (unsigned long long)ctx->performance_counters.dma_tick_cycles,
            (unsigned long long)ctx->performance_counters.serial_tick_calls,
            (unsigned long long)ctx->performance_counters.serial_tick_cycles,
            (unsigned long long)ctx->performance_counters.timer_tick_calls,
            (unsigned long long)ctx->performance_counters.timer_tick_cycles,
            (unsigned long long)ctx->performance_counters.audio_step_calls,
            (unsigned long long)ctx->performance_counters.audio_step_cycles,
            (unsigned long long)ctx->performance_counters.interrupt_checks,
            (unsigned long long)ctx->performance_counters.interrupt_stops,
            (unsigned long long)ctx->performance_counters.frame_stops,
            (unsigned long long)ctx->performance_counters.region_candidate_units,
            (unsigned long long)ctx->performance_counters.region_groups,
            (unsigned long long)ctx->performance_counters.region_grouped_units,
            (unsigned long long)ctx->performance_counters.region_grouped_tick_commits,
            (unsigned long long)ctx->performance_counters.region_estimated_removable_tick_commits,
            (unsigned long long)ctx->performance_counters.region_estimated_removable_safepoints,
            (unsigned long long)ctx->performance_counters.region_reject_visibility,
            (unsigned long long)ctx->performance_counters.region_reject_deadline);

    gbrt_report_histogram("tick_cycles", "1,2,3,4,5-7,8,9-11,12,13-15,16,17-31,32+",
                          ctx->performance_counters.tick_cycle_histogram, 12);
    gbrt_report_histogram("ppu_mode", "lcd_off,oam,draw,hblank,vblank",
                          ctx->performance_counters.ppu_mode_tick_commits, 5);
    gbrt_report_histogram("ppu_mode_cycles", "lcd_off,oam,draw,hblank,vblank",
                          ctx->performance_counters.ppu_mode_tick_cycles, 5);
    gbrt_report_histogram("timer_state", "disabled,enabled,reload",
                          ctx->performance_counters.timer_state_tick_commits, 3);
    gbrt_report_histogram("timer_state_cycles", "disabled,enabled,reload",
                          ctx->performance_counters.timer_state_tick_cycles, 3);
    gbrt_report_histogram("deadline", "zero,one,2-3,4-7,8-15,16-31,32-63,64-255,256+,infinite",
                          ctx->performance_counters.deadline_histogram, 10);
    gbrt_report_histogram("visibility_units", "register_only,safe_memory,generic_read,generic_write,transition,fallback,stopped",
                          ctx->performance_counters.visibility_unit_histogram, 7);
    gbrt_report_histogram("region_group_size", "one,two,3-4,5-8,9-16,17+",
                          ctx->performance_counters.region_group_size_histogram, 6);
}
#else
void gbrt_report_performance_counters(GBContext* ctx) {
    if (!ctx) {
        return;
    }
    gbrt_audio_sync(ctx);
    fprintf(stderr,
            "[PERF-COUNTERS] available=0 tick_commits=%llu tick_cycles=%llu generated_safepoints=%llu direct_transitions=%llu indirect_dispatches=%llu generic_reads=%llu specialized_reads=%llu generic_writes=%llu specialized_writes=%llu interpreter_fallbacks=%llu ppu_tick_calls=%llu ppu_dots=%llu ppu_draw_dots=%llu ppu_rendered_pixels=%llu ppu_stable_spans=%llu ppu_stable_span_dots=%llu audio_samples=%llu\n",
            (unsigned long long)ctx->performance_counters.tick_commits,
            (unsigned long long)ctx->performance_counters.tick_cycles,
            (unsigned long long)ctx->performance_counters.generated_safepoints,
            (unsigned long long)ctx->performance_counters.generated_direct_transitions,
            (unsigned long long)ctx->performance_counters.generated_indirect_dispatches,
            (unsigned long long)ctx->performance_counters.generated_generic_reads,
            (unsigned long long)ctx->performance_counters.generated_specialized_reads,
            (unsigned long long)ctx->performance_counters.generated_generic_writes,
            (unsigned long long)ctx->performance_counters.generated_specialized_writes,
            (unsigned long long)ctx->performance_counters.interpreter_fallbacks,
            (unsigned long long)ctx->performance_counters.ppu_tick_calls,
            (unsigned long long)ctx->performance_counters.ppu_dots,
            (unsigned long long)ctx->performance_counters.ppu_draw_dots,
            (unsigned long long)ctx->performance_counters.ppu_rendered_pixels,
            (unsigned long long)ctx->performance_counters.ppu_stable_spans,
            (unsigned long long)ctx->performance_counters.ppu_stable_span_dots,
            (unsigned long long)ctx->performance_counters.audio_samples);
}
#endif

void gbrt_note_interpreter_session(GBContext* ctx,
                                   uint16_t bank,
                                   uint16_t addr,
                                   uint32_t instructions,
                                   uint32_t cycles) {
    if (!ctx) {
        return;
    }

    ctx->total_interpreter_entries++;
    ctx->total_interpreter_instructions += instructions;
    ctx->total_interpreter_cycles += cycles;
    ctx->frame_interpreter_instructions += instructions;
    ctx->frame_interpreter_cycles += cycles;

    size_t slot = gbrt_find_or_allocate_interpreter_hotspot(ctx, bank, addr);
    GBInterpreterHotspot* hotspot = &ctx->interpreter_hotspots[slot];
    hotspot->entries++;
    hotspot->instructions += instructions;
    hotspot->cycles += cycles;
    hotspot->last_frame = ctx->completed_frames + 1;

    gbrt_sort_interpreter_hotspots(ctx);
}

void gbrt_note_unimplemented_interpreter_opcode(GBContext* ctx,
                                                uint16_t bank,
                                                uint16_t addr,
                                                uint8_t opcode) {
    if (!ctx) {
        return;
    }

    ctx->has_unimplemented_interpreter_opcode = 1;
    ctx->last_unimplemented_opcode = opcode;
    ctx->last_unimplemented_bank = bank;
    ctx->last_unimplemented_addr = addr;
}

void gbrt_note_lcd_transition(GBContext* ctx, bool lcd_enabled, uint8_t old_lcdc, uint8_t new_lcdc, uint8_t ly, uint8_t mode) {
    if (!ctx) return;

    ctx->frame_lcd_transition_count++;
    ctx->total_lcd_transition_count++;

    if (!lcd_enabled) {
        ctx->lcd_off_active = 1;
        ctx->lcd_off_start_cycles = ctx->cycles;
        ctx->lcd_off_start_frame_cycles = ctx->frame_cycles;

        if (gbrt_log_lcd_transitions) {
            fprintf(stderr,
                    "[LCD] OFF cyc=%u frame_cycles=%u ly=%u mode=%s old=%02X new=%02X transition=%llu\n",
                    ctx->cycles,
                    ctx->frame_cycles,
                    (unsigned)ly,
                    ppu_mode_name(mode),
                    (unsigned)old_lcdc,
                    (unsigned)new_lcdc,
                    (unsigned long long)ctx->total_lcd_transition_count);
        }
        return;
    }

    if (ctx->lcd_off_active) {
        uint32_t span_cycles = ctx->cycles - ctx->lcd_off_start_cycles;
        uint32_t span_frame_cycles = ctx->frame_cycles - ctx->lcd_off_start_frame_cycles;
        ctx->lcd_off_active = 0;
        ctx->last_lcd_off_span_cycles = span_cycles;
        ctx->frame_lcd_off_span_count++;
        ctx->total_lcd_off_span_count++;

        if (gbrt_log_lcd_transitions) {
            fprintf(stderr,
                    "[LCD] ON cyc=%u frame_cycles=%u ly=%u mode=%s old=%02X new=%02X span_cycles=%u span_frame_cycles=%u frame_lcd_off_cycles=%u span_index=%llu\n",
                    ctx->cycles,
                    ctx->frame_cycles,
                    (unsigned)ly,
                    ppu_mode_name(mode),
                    (unsigned)old_lcdc,
                    (unsigned)new_lcdc,
                    span_cycles,
                    span_frame_cycles,
                    ctx->frame_lcd_off_cycles,
                    (unsigned long long)ctx->total_lcd_off_span_count);
        }
    } else if (gbrt_log_lcd_transitions) {
        fprintf(stderr,
                "[LCD] ON cyc=%u frame_cycles=%u ly=%u mode=%s old=%02X new=%02X span_cycles=0 span_frame_cycles=0 frame_lcd_off_cycles=%u span_index=%llu\n",
                ctx->cycles,
                ctx->frame_cycles,
                (unsigned)ly,
                ppu_mode_name(mode),
                (unsigned)old_lcdc,
                (unsigned)new_lcdc,
                ctx->frame_lcd_off_cycles,
                (unsigned long long)ctx->total_lcd_off_span_count);
    }
}

/* ============================================================================
 * Timing & Hardware Sync
 * ========================================================================== */

static inline void gb_sync(GBContext* ctx) {
    while (1) {
        uint32_t current = ctx->cycles;
        uint32_t delta = current - ctx->last_sync_cycles;
        if (delta > 0) {
            ctx->last_sync_cycles = current;
            if (ctx->ppu) ppu_tick((GBPPU*)ctx->ppu, ctx, delta);
        }

        if (ctx->hdma.cpu_stall_cycles > 0 &&
            !ctx->hdma.processing_stall) {
            const uint16_t stall = ctx->hdma.cpu_stall_cycles;
            ctx->hdma.cpu_stall_cycles = 0;
            ctx->hdma.processing_stall = 1;
            gb_tick(ctx, stall);
            ctx->hdma.processing_stall = 0;
            continue;
        }
        break;
    }
}

void gb_add_cycles(GBContext* ctx, uint32_t cycles) {
    ctx->cycles += cycles;
    ctx->total_cycles += cycles;
    ctx->frame_cycles += cycles;
    if (ctx->run_cycle_budget > 0 &&
        (ctx->cycles - ctx->run_cycle_budget_start) >= ctx->run_cycle_budget) {
        ctx->stopped = 1;
    }
    if (ctx->lcd_off_active) {
        ctx->frame_lcd_off_cycles += cycles;
        ctx->total_lcd_off_cycles += cycles;
    }
}

static void gb_rtc_tick(GBContext* ctx, uint32_t cycles) {
    if (!ctx->rtc.active) return;
    
    /* Update RTC time */
    ctx->rtc.last_time += cycles;
    while (ctx->rtc.last_time >= 4194304) { /* 1 second at 4.194304 MHz */
        ctx->rtc.last_time -= 4194304;
        
        ctx->rtc.s++;
        if (ctx->rtc.s >= 60) {
            ctx->rtc.s = 0;
            ctx->rtc.m++;
            if (ctx->rtc.m >= 60) {
                ctx->rtc.m = 0;
                ctx->rtc.h++;
                if (ctx->rtc.h >= 24) {
                    ctx->rtc.h = 0;
                    uint16_t d = ctx->rtc.dl | ((ctx->rtc.dh & 1) << 8);
                    d++;
                    ctx->rtc.dl = d & 0xFF;
                    if (d > 0x1FF) {
                        ctx->rtc.dh |= 0x80; /* Overflow */
                        ctx->rtc.dh &= 0xFE; /* Clear 9th bit */
                    } else {
                        ctx->rtc.dh = (ctx->rtc.dh & 0xFE) | ((d >> 8) & 1);
                    }
                }
            }
        }
    }
}

static void gb_dma_advance_active(GBContext* ctx, uint32_t cycles) {
    while (cycles > 0 && ctx->dma.active) {
        /* Preserve the sub-M-cycle phase across gb_tick() calls. Memory
         * accesses intentionally split an instruction around its final bus
         * cycle, so a transfer can be advanced by 1 then 3 T-cycles rather
         * than a single aligned 4-cycle call.
         */
        uint32_t until_byte = ctx->dma.cycles_remaining % 4u;
        if (until_byte == 0) {
            until_byte = 4;
        }
        uint32_t byte_cycles = cycles < until_byte ? cycles : until_byte;
        cycles -= byte_cycles;
        ctx->dma.cycles_remaining -= byte_cycles;
        
        /* Copy one byte every 4 T-cycles */
        if (ctx->dma.progress < 160 && (ctx->dma.cycles_remaining % 4) == 0) {
            uint16_t src_addr =
                ((uint16_t)ctx->dma.active_source_high << 8) | ctx->dma.progress;
            /* DMA owns the source bus, so bypass CPU conflict checks while
             * retaining mapper/bank/bounds behavior. */
            uint8_t byte = gb_direct_read_oam_dma_source(ctx, src_addr);
            ctx->oam[ctx->dma.progress] = byte;
            ctx->dma.progress++;
        }
        
        /* Check if DMA is complete */
        if (ctx->dma.progress >= 160 || ctx->dma.cycles_remaining == 0) {
            ctx->dma.active = 0;
        }
    }
}

/**
 * Process OAM DMA transfer.
 * DMA takes 160 M-cycles (640 T-cycles), copying 1 byte per M-cycle. A fresh
 * FF46 write occurs in M=0, leaves M=1 accessible, and activates the new
 * transfer at M=2. During a restart, the old transfer retains the bus through
 * that startup interval.
 */
static void gb_dma_tick(GBContext* ctx, uint32_t cycles) {
    if (ctx->dma.pending && ctx->dma.startup_delay > 0) {
        const uint32_t startup_cycles =
            cycles < ctx->dma.startup_delay ? cycles : ctx->dma.startup_delay;

        if (ctx->dma.active) {
            gb_dma_advance_active(ctx, startup_cycles);
        }
        ctx->dma.startup_delay =
            (uint8_t)(ctx->dma.startup_delay - startup_cycles);
        cycles -= startup_cycles;

        if (ctx->dma.startup_delay > 0) {
            return;
        }

        ctx->dma.pending = 0;
        ctx->dma.active = 1;
        ctx->dma.active_source_high = ctx->dma.source_high;
        ctx->dma.progress = 0;
        ctx->dma.cycles_remaining = 640;
    }

    if (cycles > 0 && ctx->dma.active) {
        gb_dma_advance_active(ctx, cycles);
    }
}

static void gb_serial_tick(GBContext* ctx, uint32_t cpu_cycles) {
    if (!ctx->serial_transfer.active) {
        return;
    }

    if (cpu_cycles >= ctx->serial_transfer.cycles_remaining) {
        uint8_t outgoing = ctx->io[0x01];
        ctx->serial_transfer.active = 0;
        ctx->serial_transfer.cycles_remaining = 0;
        ctx->io[0x02] &= (uint8_t)~0x80;
        ctx->io[0x01] = 0xFF;
        ctx->io[0x0F] |= 0x08;

        if (ctx->callbacks.on_serial_byte) {
            ctx->callbacks.on_serial_byte(ctx, outgoing);
        }
        return;
    }

    ctx->serial_transfer.cycles_remaining -= cpu_cycles;
}

static bool gb_stop_should_resume(GBContext* ctx) {
    if (!ctx || !ctx->stop_mode_active) {
        return false;
    }

    if (ctx->io[0x0F] & ctx->io[0x80] & 0x1F) {
        return true;
    }

    return (gb_read8(ctx, 0xFF00) & 0x0F) != 0x0F;
}

static uint32_t gb_halt_fast_forward_cycles(GBContext* ctx, uint32_t run_start, uint32_t max_cycles) {
    uint32_t cycles = 4;

    if (!ctx || !ctx->ppu || !(ctx->io[0x40] & LCDC_LCD_ENABLE)) {
        return cycles;
    }

    if (ctx->config.model != GB_MODEL_DMG) {
        return cycles;
    }

    if ((ctx->io[0x07] & 0x04) ||
        ctx->tima_reload_pending > 0 ||
        ctx->dma.pending ||
        ctx->dma.active ||
        ctx->serial_transfer.active) {
        return cycles;
    }

    const GBPPU* ppu = (const GBPPU*)ctx->ppu;
    if (gbrt_benchmark_fast_tick_enabled &&
        (ctx->io[0x80] & 0x1F) == 0x01 &&
        (ctx->io[0x0F] & 0x01) == 0) {
        uint32_t line_progress = 0;
        switch (ppu->mode) {
            case PPU_MODE_OAM:
                line_progress = ppu->mode_cycles;
                break;
            case PPU_MODE_DRAW:
                line_progress = CYCLES_OAM_SCAN + ppu->mode_cycles;
                break;
            case PPU_MODE_HBLANK:
                line_progress = CYCLES_OAM_SCAN + ppu->mode3_length + ppu->mode_cycles;
                break;
            case PPU_MODE_VBLANK:
                line_progress = ppu->mode_cycles;
                break;
            default:
                line_progress = 0;
                break;
        }
        if (line_progress < CYCLES_SCANLINE && ppu->scanline < TOTAL_SCANLINES) {
            const uint32_t frame_cycles = TOTAL_SCANLINES * CYCLES_SCANLINE;
            const uint32_t vblank_start = VISIBLE_SCANLINES * CYCLES_SCANLINE;
            uint32_t frame_pos =
                ((uint32_t)ppu->scanline * CYCLES_SCANLINE) + line_progress;
            uint32_t cycles_until_vblank =
                (frame_pos < vblank_start) ?
                    (vblank_start - frame_pos) :
                    (frame_cycles - frame_pos + vblank_start);
            if (cycles_until_vblank > 0) {
                cycles = cycles_until_vblank & ~3u;
                if (cycles == 0) {
                    cycles = 4;
                }
                if (max_cycles > 0 && max_cycles != UINT32_MAX) {
                    uint32_t elapsed = ctx->cycles - run_start;
                    if (elapsed >= max_cycles) {
                        return 4;
                    }
                    uint32_t remaining = max_cycles - elapsed;
                    if (cycles > remaining) {
                        cycles = remaining & ~3u;
                        if (cycles == 0) {
                            cycles = remaining;
                        }
                    }
                }
                return cycles;
            }
        }
    }

    uint32_t target = 0;
    switch (ppu->mode) {
        case PPU_MODE_OAM: target = CYCLES_OAM_SCAN; break;
        /* Mode 3 ends on a dynamic FIFO event; advance only one CPU M-cycle. */
        case PPU_MODE_DRAW: target = ppu->mode_cycles + 4u; break;
        case PPU_MODE_HBLANK: target = ppu->hblank_length; break;
        case PPU_MODE_VBLANK:
            target = (ppu->scanline == 153 && ppu->mode_cycles < 4)
                ? 4u
                : CYCLES_SCANLINE;
            break;
        default: break;
    }

    if (target > ppu->mode_cycles) {
        cycles = target - ppu->mode_cycles;
        cycles &= ~3u;
        if (cycles == 0) {
            cycles = 4;
        }
    }

    if (max_cycles > 0 && max_cycles != UINT32_MAX) {
        uint32_t elapsed = ctx->cycles - run_start;
        if (elapsed >= max_cycles) {
            return 4;
        }
        uint32_t remaining = max_cycles - elapsed;
        if (cycles > remaining) {
            cycles = remaining & ~3u;
            if (cycles == 0) {
                cycles = remaining;
            }
        }
    }

    return cycles;
}

void gb_tick(GBContext* ctx, uint32_t cycles) {
    static uint32_t last_log = 0;
#ifdef GBRT_ENABLE_PERFORMANCE_COUNTERS
    gbrt_profile_note_tick(ctx, cycles);
#endif
    uint32_t cpu_cycles = cycles;
    uint32_t system_cycles = cycles;
    const uint8_t previous_system_cycle_remainder = ctx->cgb_system_cycle_remainder & 1u;
    if (ctx->cgb_double_speed) {
        const uint8_t previous_remainder = previous_system_cycle_remainder;
        system_cycles = (cycles >> 1) +
                        ((cycles & 1u) && previous_remainder ? 1u : 0u);
        ctx->cgb_system_cycle_remainder =
            (uint8_t)(previous_remainder ^ (uint8_t)(cycles & 1u));
    } else {
        ctx->cgb_system_cycle_remainder = 0;
    }

    if (gbrt_benchmark_fast_tick_enabled &&
        cycles == 4 &&
        !ctx->cgb_double_speed &&
        !ctx->dma.pending &&
        !ctx->dma.active &&
        !ctx->serial_transfer.active &&
        !(ctx->io[0x07] & 0x04) &&
        !ctx->tima_reload_pending &&
        (((ctx->cycles + 4u) & 0x3FFu) >= 4u) &&
        !(ctx->ime && (ctx->io[0x0F] & ctx->io[0x80] & 0x1F))) {
        ctx->cycles += 4;
        ctx->total_cycles += 4;
        ctx->frame_cycles += 4;
        if (ctx->run_cycle_budget > 0 &&
            (ctx->cycles - ctx->run_cycle_budget_start) >= ctx->run_cycle_budget) {
            ctx->stopped = 1;
        }
        if (ctx->lcd_off_active) {
            ctx->frame_lcd_off_cycles += 4;
            ctx->total_lcd_off_cycles += 4;
        }
#ifdef GBRT_ENABLE_PERFORMANCE_COUNTERS
        ctx->performance_counters.timer_tick_calls++;
        ctx->performance_counters.timer_tick_cycles += 4u;
        ctx->performance_counters.interrupt_checks++;
#endif
        ctx->div_counter = (uint16_t)(ctx->div_counter + 4);
        ctx->io[0x04] = (uint8_t)(ctx->div_counter >> 8);
        if (ctx->ime_pending) { ctx->ime = 1; ctx->ime_pending = 0; }
        return;
    }

    if (gbrt_benchmark_fast_tick_enabled && !ctx->cgb_double_speed) {
        gb_add_cycles(ctx, cycles);

        if (ctx->dma.pending || ctx->dma.active) {
#ifdef GBRT_ENABLE_PERFORMANCE_COUNTERS
            ctx->performance_counters.dma_tick_calls++;
            ctx->performance_counters.dma_tick_cycles += cpu_cycles;
#endif
            gb_dma_tick(ctx, cpu_cycles);
        }
        if (ctx->serial_transfer.active) {
#ifdef GBRT_ENABLE_PERFORMANCE_COUNTERS
            ctx->performance_counters.serial_tick_calls++;
            ctx->performance_counters.serial_tick_cycles += cpu_cycles;
#endif
            gb_serial_tick(ctx, cpu_cycles);
        }

#ifdef GBRT_ENABLE_PERFORMANCE_COUNTERS
        ctx->performance_counters.timer_tick_calls++;
        ctx->performance_counters.timer_tick_cycles += cpu_cycles;
#endif
        gb_timer_tick(ctx, cpu_cycles);

        if ((ctx->cycles & 0x3FF) < cycles ||
            (ctx->ime && (ctx->io[0x0F] & ctx->io[0x80] & 0x1F))) {
            gb_sync(ctx);
            const bool interrupt_pending =
                ctx->ime && (ctx->io[0x0F] & ctx->io[0x80] & 0x1F);
#ifdef GBRT_ENABLE_PERFORMANCE_COUNTERS
            ctx->performance_counters.interrupt_checks++;
            if (ctx->frame_done) ctx->performance_counters.frame_stops++;
            if (interrupt_pending) ctx->performance_counters.interrupt_stops++;
#endif
            if (ctx->frame_done || interrupt_pending) ctx->stopped = 1;
        }
        if (ctx->ime_pending) { ctx->ime = 1; ctx->ime_pending = 0; }
        return;
    }
    
    // Check limit
    if (gbrt_instruction_limit > 0) {
        gbrt_instruction_count++;
        if (gbrt_instruction_count >= gbrt_instruction_limit) {
            printf("Instruction limit reached (%llu)\n", (unsigned long long)gbrt_instruction_limit);
            if (gbrt_instruction_limit_callback != NULL) {
                gbrt_instruction_limit_callback();
            }
            exit(0);
        }
    }

    if (gbrt_trace_enabled && ctx->cycles - last_log >= 10000) {
        last_log = ctx->cycles;
        fprintf(stderr, "[TICK] Cycles: %u, PC: 0x%04X, IME: %d, IF: 0x%02X, IE: 0x%02X\n", 
                ctx->cycles, ctx->pc, ctx->ime, ctx->io[0x0F], ctx->io[0x80]);
    }

    gb_add_cycles(ctx, system_cycles);
    
    /* RTC Tick */
    if (ctx->rtc.active && ctx->rom && ctx->rom_size > 0x147 && gb_cart_type_has_rtc(ctx->rom[0x147])) {
#ifdef GBRT_ENABLE_PERFORMANCE_COUNTERS
        ctx->performance_counters.rtc_tick_calls++;
        ctx->performance_counters.rtc_tick_cycles += system_cycles;
#endif
        gb_rtc_tick(ctx, system_cycles);
    }
    
    /* OAM DMA Tick */
    if (ctx->dma.pending || ctx->dma.active) {
#ifdef GBRT_ENABLE_PERFORMANCE_COUNTERS
        ctx->performance_counters.dma_tick_calls++;
        ctx->performance_counters.dma_tick_cycles += cpu_cycles;
#endif
        gb_dma_tick(ctx, cpu_cycles);
    }

    /* Serial Tick */
    if (ctx->serial_transfer.active) {
#ifdef GBRT_ENABLE_PERFORMANCE_COUNTERS
        ctx->performance_counters.serial_tick_calls++;
        ctx->performance_counters.serial_tick_cycles += cpu_cycles;
#endif
        gb_serial_tick(ctx, cpu_cycles);
    }

    /* Update DIV and TIMA */
    uint16_t old_div = ctx->div_counter;
#ifdef GBRT_ENABLE_PERFORMANCE_COUNTERS
    ctx->performance_counters.timer_tick_calls++;
    ctx->performance_counters.timer_tick_cycles += cpu_cycles;
#endif
    gb_timer_tick(ctx, cpu_cycles);
    
    /* The reference execution path must publish PPU mode/LY/STAT events at
     * every CPU instruction boundary.  Deferring this to a 256-cycle bucket
     * makes HALT-based STAT waits resume on the wrong scanline even though the
     * PPU itself models the correct dot.  Benchmark mode retains its explicit
     * coarse-sync path above; normal and headless execution stay accurate. */
    if (system_cycles > 0) {
        gb_sync(ctx);
    }
    /* Interrupt acceptance is a CPU instruction-boundary concern. In CGB
     * double-speed mode a final one-T bus phase can carry no complete system
     * cycle, but it must still stop compiled execution so the pending IRQ is
     * serviced before the next instruction. */
    const bool interrupt_pending =
        ctx->ime && (ctx->io[0x0F] & ctx->io[0x80] & 0x1F);
#ifdef GBRT_ENABLE_PERFORMANCE_COUNTERS
    ctx->performance_counters.interrupt_checks++;
    if (ctx->frame_done) ctx->performance_counters.frame_stops++;
    if (interrupt_pending) ctx->performance_counters.interrupt_stops++;
#endif
    if (ctx->frame_done || interrupt_pending) {
        ctx->stopped = 1;
    }
    gbrt_audio_schedule(ctx,
                        old_div,
                        cpu_cycles,
                        system_cycles,
                        ctx->cgb_double_speed != 0,
                        previous_system_cycle_remainder);
    if (ctx->ime_pending) { ctx->ime = 1; ctx->ime_pending = 0; }
}

void gb_handle_interrupts(GBContext* ctx) {
    if (!ctx->ime) return;
    uint8_t if_reg = ctx->io[0x0F];
    uint8_t ie_reg = ctx->io[0x80];
    uint8_t pending = if_reg & ie_reg & 0x1F;
    if (pending) {
        ctx->ime = 0; ctx->halted = 0; ctx->stop_mode_active = 0;
        const uint16_t return_pc = ctx->pc;

        /* ISR entry is re-evaluated around the two stack writes. This matters
         * when SP aliases IE or IF: an upper-byte write to IE can cancel or
         * reprioritize the dispatch, while a lower-byte write to IE is too
         * late. The IF write conflict uses the value from before the write. */
        gb_tick(ctx, 11);
        ctx->sp--;
        gb_write8(ctx, ctx->sp, (uint8_t)(return_pc >> 8));
        gb_tick(ctx, 1);
        pending = ctx->io[0x80] & 0x1F;

        gb_tick(ctx, 3);
        ctx->sp--;
        const uint8_t if_before_low_write = ctx->io[0x0F] & 0x1F;
        gb_write8(ctx, ctx->sp, (uint8_t)return_pc);
        pending &= ctx->sp == 0xFF0F
            ? if_before_low_write
            : (ctx->io[0x0F] & 0x1F);
        gb_tick(ctx, 1);

        uint16_t vec = 0;
        uint8_t bit = 0;
        const char* name = "CANCELLED";
        if (pending & 0x01) { vec = 0x0040; bit = 0x01; name = "VBLANK"; }
        else if (pending & 0x02) { vec = 0x0048; bit = 0x02; name = "STAT"; }
        else if (pending & 0x04) { vec = 0x0050; bit = 0x04; name = "TIMER"; }
        else if (pending & 0x08) { vec = 0x0058; bit = 0x08; name = "SERIAL"; }
        else if (pending & 0x10) { vec = 0x0060; bit = 0x10; name = "JOYPAD"; }

        gbrt_log_interrupt_service(ctx,
                                   name,
                                   vec,
                                   if_reg,
                                   ie_reg,
                                   bit,
                                   return_pc,
                                   (uint16_t)(ctx->sp + 2));
        if (bit) {
            ctx->io[0x0F] &= (uint8_t)~bit;
        }
        gbrt_timed_jump(ctx, vec, 4);
        ctx->stopped = 1;
    }
}

/* ============================================================================
 * Execution
 * ========================================================================== */

uint32_t gb_run_frame(GBContext* ctx) {
    gb_reset_frame(ctx);
    return gb_run_cycles(ctx, 0);
}

uint32_t gb_run_cycles(GBContext* ctx, uint32_t max_cycles) {
    uint32_t start = ctx->cycles;
    uint32_t previous_budget = ctx->run_cycle_budget;
    uint32_t previous_budget_start = ctx->run_cycle_budget_start;
    bool bounded_run = (max_cycles > 0 && max_cycles != UINT32_MAX);

    if (bounded_run) {
        ctx->run_cycle_budget = max_cycles;
        ctx->run_cycle_budget_start = start;
    }

    while (!ctx->frame_done) {
#ifdef GBRT_ENABLE_NATIVE_PATCHES
        if (gb_native_patch_failed(ctx)) {
            break;
        }
#endif
        if (max_cycles > 0 && (ctx->cycles - start) >= max_cycles) {
            break;
        }

        if (ctx->ime && (ctx->io[0x0F] & ctx->io[0x80] & 0x1F)) {
            gb_handle_interrupts(ctx);
        }
        
        /* Check for HALT exit condition (even if IME=0) */
        if (ctx->halted) {
             if (ctx->io[0x0F] & ctx->io[0x80] & 0x1F) {
                 ctx->halted = 0;
             }
        }
        
        ctx->stopped = 0;
        if (ctx->stop_mode_active) {
            if (gb_stop_should_resume(ctx)) {
                ctx->stop_mode_active = 0;
            } else {
                gb_tick(ctx, 4);
                gb_sync(ctx);
                continue;
            }
        }
        if (ctx->halted) {
            gb_tick(ctx, gb_halt_fast_forward_cycles(ctx, start, max_cycles));
            continue;
        }
        gb_step(ctx);
    }

    if (bounded_run) {
        ctx->run_cycle_budget = previous_budget;
        ctx->run_cycle_budget_start = previous_budget_start;
    }

    return ctx->cycles - start;
}

uint32_t gb_step(GBContext* ctx) {
    if (gbrt_instruction_limit > 0 && ++gbrt_instruction_count >= gbrt_instruction_limit) {
        printf("Instruction limit reached (%llu)\n", (unsigned long long)gbrt_instruction_limit);
        if (gbrt_instruction_limit_callback != NULL) {
            gbrt_instruction_limit_callback();
        }
        exit(0);
    }
    
    /* Handle HALT bug by falling back to interpreter for the next instruction */
    if (ctx->halt_bug) {
        gb_interpret(ctx, ctx->pc);
        return 0; /* Cycle counting handled by interpreter */
    }

    uint32_t start = ctx->cycles;
    gb_dispatch(ctx, ctx->pc);
    return ctx->cycles - start;
}

uint32_t gb_debug_step(GBContext* ctx, GBExecutionMode mode) {
    uint32_t start = ctx->cycles;

    gb_handle_interrupts(ctx);

    /* Match gb_run_frame() scheduling around HALT exit and stopped state. */
    if (ctx->halted && (ctx->io[0x0F] & ctx->io[0x80] & 0x1F)) {
        ctx->halted = 0;
    }

    ctx->stopped = 0;

    if (ctx->stop_mode_active) {
        if (gb_stop_should_resume(ctx)) {
            ctx->stop_mode_active = 0;
        } else {
            gb_tick(ctx, 4);
            return ctx->cycles - start;
        }
    }

    if (ctx->halted) {
        gb_tick(ctx, 4);
        return ctx->cycles - start;
    }

    uint8_t saved_single_step = ctx->single_step_mode;
    ctx->single_step_mode = 1;
    ctx->used_dispatch_fallback = 0;

    if (mode == GB_EXECUTION_INTERPRETER || ctx->halt_bug) {
        gb_interpret(ctx, ctx->pc);
    } else {
        gb_dispatch(ctx, ctx->pc);
    }

    ctx->single_step_mode = saved_single_step;
    return ctx->cycles - start;
}

void gb_reset_frame(GBContext* ctx) {
    if (ctx->frame_done || ctx->frame_cycles > 0) {
        ctx->completed_frames++;
    }
    ctx->frame_done = 0;
    ctx->frame_cycles = 0;
    ctx->frame_dispatch_fallbacks = 0;
    ctx->frame_first_fallback_bank = 0;
    ctx->frame_first_fallback_addr = 0;
    ctx->frame_last_fallback_bank = 0;
    ctx->frame_last_fallback_addr = 0;
    ctx->frame_interpreter_instructions = 0;
    ctx->frame_interpreter_cycles = 0;
    ctx->frame_lcd_off_cycles = 0;
    ctx->frame_lcd_transition_count = 0;
    ctx->frame_lcd_off_span_count = 0;
    if (ctx->ppu) ppu_clear_frame_ready((GBPPU*)ctx->ppu);
}

const uint32_t* gb_get_framebuffer(GBContext* ctx) {
    if (ctx->ppu) return ppu_get_framebuffer((GBPPU*)ctx->ppu);
    return NULL;
}

void gb_halt(GBContext* ctx) { ctx->halted = 1; }

void gbrt_execute_halt(GBContext* ctx, uint16_t next_pc, uint32_t cycles) {
    ctx->pc = next_pc;
    if (!ctx->ime &&
        (gb_read8(ctx, 0xFFFF) & gb_read8(ctx, 0xFF0F) & 0x1F)) {
        ctx->halted = 0;
        ctx->halt_bug = 1;
    } else {
        ctx->halt_bug = 0;
        gb_halt(ctx);
    }
    gb_tick(ctx, cycles);
}

void gb_stop(GBContext* ctx) {
    if (!ctx) {
        return;
    }

    if (gb_is_cgb_mode(ctx) && (ctx->io[0x4D] & 0x01)) {
        gbrt_audio_sync(ctx);
        ctx->io[0x4D] &= (uint8_t)~0x01;
        ctx->cgb_double_speed ^= 1;
        ctx->stopped = 1;
        return;
    }

    ctx->stop_mode_active = 1;
    ctx->stopped = 1;
}
bool gb_frame_complete(GBContext* ctx) { return ctx->frame_done != 0; }

void gb_set_platform_callbacks(GBContext* ctx, const GBPlatformCallbacks* c) {
    if (ctx && c) {
        bool had_load_battery_ram = ctx->callbacks.load_battery_ram != NULL;
        ctx->callbacks = *c;
        if (!had_load_battery_ram && ctx->callbacks.load_battery_ram) {
            gb_context_try_load_battery_ram(ctx);
            gb_context_try_load_rtc(ctx);
        }
    }
}

void gb_context_set_save_id(GBContext* ctx, const char* save_id) {
    if (!ctx) {
        return;
    }

    memset(ctx->save_id, 0, sizeof(ctx->save_id));
    if (!save_id || !save_id[0]) {
        return;
    }

    snprintf(ctx->save_id, sizeof(ctx->save_id), "%s", save_id);
}

void gb_audio_callback(GBContext* ctx, int16_t l, int16_t r) {
    if (ctx && ctx->callbacks.on_audio_sample) {
        ctx->callbacks.on_audio_sample(ctx, l, r);
    }
}
