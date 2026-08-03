/**
 * @file audio.c
 * @brief GameBoy Audio Processing Unit implementation
 */

#include "audio.h"
#include "gbrt_debug.h"
#include "audio_stats.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* Debug audio logging - now controlled by runtime flag */
/* #define AUDIO_DEBUG_LOGGING */
#define DEBUG_AUDIO_MAX_SAMPLES (44100 * 10) /* 10 seconds */
#define DEBUG_AUDIO_BUFFER_FRAMES 2048
#define AUDIO_CLOCK_HZ UINT32_C(4194304)
#define AUDIO_SAMPLE_RATE UINT32_C(44100)

static bool g_audio_debug_enabled = false;
static bool g_audio_debug_trace_enabled = false;
static FILE* g_debug_audio_file = NULL;
static FILE* g_debug_audio_trace_file = NULL;
static int g_debug_sample_count = 0;
static int g_debug_capture_limit_samples = DEBUG_AUDIO_MAX_SAMPLES;
static int16_t g_debug_audio_buffer[DEBUG_AUDIO_BUFFER_FRAMES * 2];
static int g_debug_audio_buffer_count = 0;
static uint64_t g_debug_trace_total_samples = 0;
static uint32_t g_debug_trace_window_samples = 0;
static uint32_t g_debug_trace_window_nonzero = 0;
static uint32_t g_debug_trace_window_channel_nonzero[4] = {0};
static int32_t g_debug_trace_window_peak = 0;
static bool g_debug_trace_first_nonzero_logged = false;

static void audio_debug_trace_log(const char* fmt, ...) {
    if (!g_audio_debug_trace_enabled) return;

    if (!g_debug_audio_trace_file) {
        g_debug_audio_trace_file = fopen("debug_audio_trace.log", "w");
        if (!g_debug_audio_trace_file) return;
    }

    va_list args;
    va_start(args, fmt);
    vfprintf(g_debug_audio_trace_file, fmt, args);
    va_end(args);
    fflush(g_debug_audio_trace_file);
}

static void audio_debug_capture_flush(void) {
    if (!g_debug_audio_file || g_debug_audio_buffer_count == 0) return;
    fwrite(g_debug_audio_buffer, sizeof(int16_t), (size_t)g_debug_audio_buffer_count * 2, g_debug_audio_file);
    g_debug_audio_buffer_count = 0;
}

static void audio_debug_capture_reset(void) {
    audio_debug_capture_flush();
    if (g_debug_audio_file) {
        fclose(g_debug_audio_file);
        g_debug_audio_file = NULL;
    }
    g_debug_sample_count = 0;
    g_debug_audio_buffer_count = 0;
    g_debug_trace_total_samples = 0;
    g_debug_trace_window_samples = 0;
    g_debug_trace_window_nonzero = 0;
    memset(g_debug_trace_window_channel_nonzero, 0, sizeof(g_debug_trace_window_channel_nonzero));
    g_debug_trace_window_peak = 0;
    g_debug_trace_first_nonzero_logged = false;
    if (g_debug_audio_trace_file) {
        fclose(g_debug_audio_trace_file);
        g_debug_audio_trace_file = NULL;
    }
}

void gb_audio_set_debug(bool enabled) {
    if (enabled != g_audio_debug_enabled) {
        audio_debug_capture_reset();
    }
    g_audio_debug_enabled = enabled;
    if (enabled) {
        printf("[AUDIO] Debug audio capture enabled\n");
    }
}

void gb_audio_set_debug_capture_seconds(uint32_t seconds) {
    if (seconds == 0) {
        g_debug_capture_limit_samples = DEBUG_AUDIO_MAX_SAMPLES;
    } else {
        uint64_t sample_limit = (uint64_t)seconds * 44100ULL;
        if (sample_limit > 0x7fffffffULL) sample_limit = 0x7fffffffULL;
        g_debug_capture_limit_samples = (int)sample_limit;
    }
}

void gb_audio_set_debug_trace(bool enabled) {
    if (enabled != g_audio_debug_trace_enabled) {
        audio_debug_capture_reset();
    }
    g_audio_debug_trace_enabled = enabled;
    if (enabled) {
        printf("[AUDIO] Debug audio trace enabled\n");
    }
}

/* ============================================================================
 * Internal Structures
 * ========================================================================== */

typedef struct {
    /* Registers */
    uint8_t nr10; /* Sweep */
    uint8_t nr11; /* Duty/Length */
    uint8_t nr12; /* Envelope */
    uint8_t nr13; /* Freq Lo */
    uint8_t nr14; /* Freq Hi */
    
    /* Internal State */
    uint16_t freq;       /* Calculated frequency */
    int length_counter;  /* Length counter */
    bool length_enabled;
    int volume;
    int env_timer;
    bool enabled;

    /* Sweep State */
    int sweep_timer;
    uint16_t shadow_freq;
    bool sweep_enabled;
    
    /* Generation */
    uint32_t timer;
    int wave_pos;
} Channel1;

typedef struct {
    uint8_t nr21;
    uint8_t nr22;
    uint8_t nr23;
    uint8_t nr24;
    
    uint16_t freq;
    int length_counter;
    bool length_enabled;
    int volume;
    int env_timer;
    bool enabled;
    
    uint32_t timer;
    int wave_pos;
} Channel2;

typedef struct {
    uint8_t nr30; /* Enable */
    uint8_t nr31; /* Length */
    uint8_t nr32; /* Level */
    uint8_t nr33; /* Freq Lo */
    uint8_t nr34; /* Freq Hi */
    
    uint8_t wave_ram[16];
    
    uint16_t freq;
    int length_counter;
    bool length_enabled;
    bool enabled;
    int wave_pos;

    /* Generation */
    uint32_t timer;
} Channel3;

typedef struct {
    uint8_t nr41; /* Length */
    uint8_t nr42; /* Envelope */
    uint8_t nr43; /* Poly */
    uint8_t nr44; /* Counter/Consecutive */
    
    int length_counter;
    bool length_enabled;
    int volume;
    int env_timer;
    uint16_t lfsr;
    bool enabled;
    
    uint32_t accum;
} Channel4;

typedef struct GBAudio {
    Channel1 ch1;
    Channel2 ch2;
    Channel3 ch3;
    Channel4 ch4;
    
    uint8_t nr50; /* Channel control / ON-OFF / Volume */
    uint8_t nr51; /* Selection of Sound output terminal */
    uint8_t nr52; /* Sound on/off */
    
    /* Frame Sequencer */
    uint32_t fs_timer;
    int fs_step;
    
    /* Sample Generation */
    uint32_t sample_timer;
    uint32_t sample_period; /* Cycles per sample */
    
    /* Exact rational sample phase: add 44100 per system cycle and emit at
     * 4194304. This avoids long-run drift from a rounded fixed-point period. */
    uint32_t sample_phase;

    /* Simple DC-blocker state for the final stereo mix */
    int32_t hp_prev_in_l;
    int32_t hp_prev_in_r;
    int32_t hp_prev_out_l;
    int32_t hp_prev_out_r;

    /* Last stereo sample produced by the mixer. */
    int16_t last_output_left;
    int16_t last_output_right;
    
} GBAudio;

/* ============================================================================
 * Internal Helpers
 * ========================================================================== */

static const char* audio_reg_name(uint16_t addr) {
    switch (addr) {
        case 0xFF10: return "NR10";
        case 0xFF11: return "NR11";
        case 0xFF12: return "NR12";
        case 0xFF13: return "NR13";
        case 0xFF14: return "NR14";
        case 0xFF16: return "NR21";
        case 0xFF17: return "NR22";
        case 0xFF18: return "NR23";
        case 0xFF19: return "NR24";
        case 0xFF1A: return "NR30";
        case 0xFF1B: return "NR31";
        case 0xFF1C: return "NR32";
        case 0xFF1D: return "NR33";
        case 0xFF1E: return "NR34";
        case 0xFF20: return "NR41";
        case 0xFF21: return "NR42";
        case 0xFF22: return "NR43";
        case 0xFF23: return "NR44";
        case 0xFF24: return "NR50";
        case 0xFF25: return "NR51";
        case 0xFF26: return "NR52";
        default: return "WAVE";
    }
}

static void audio_debug_dump_state(GBContext* ctx, GBAudio* apu, const char* reason) {
    if (!g_audio_debug_trace_enabled) return;
    audio_debug_trace_log(
        "[STATE] cyc=%u reason=%s "
        "nr50=%02X nr51=%02X nr52=%02X "
        "| ch1 en=%d len=%d vol=%d duty=%d freq=%u pos=%d "
        "| ch2 en=%d len=%d vol=%d duty=%d freq=%u pos=%d "
        "| ch3 en=%d len=%d level=%u freq=%u pos=%d "
        "| ch4 en=%d len=%d vol=%d poly=%02X lfsr=%04X\n",
        ctx ? ctx->cycles : 0,
        reason ? reason : "-",
        apu->nr50, apu->nr51, apu->nr52,
        apu->ch1.enabled, apu->ch1.length_counter, apu->ch1.volume, (apu->ch1.nr11 >> 6) & 3,
        apu->ch1.nr13 | ((apu->ch1.nr14 & 0x07) << 8), apu->ch1.wave_pos,
        apu->ch2.enabled, apu->ch2.length_counter, apu->ch2.volume, (apu->ch2.nr21 >> 6) & 3,
        apu->ch2.nr23 | ((apu->ch2.nr24 & 0x07) << 8), apu->ch2.wave_pos,
        apu->ch3.enabled, apu->ch3.length_counter, (apu->ch3.nr32 >> 5) & 3,
        apu->ch3.nr33 | ((apu->ch3.nr34 & 0x07) << 8), apu->ch3.wave_pos,
        apu->ch4.enabled, apu->ch4.length_counter, apu->ch4.volume, apu->ch4.nr43, apu->ch4.lfsr);
}

static const uint8_t DUTY_CYCLES[4][8] = {
    {0, 0, 0, 0, 0, 0, 0, 1}, /* 12.5% */
    {1, 0, 0, 0, 0, 0, 0, 1}, /* 25% */
    {1, 0, 0, 0, 0, 1, 1, 1}, /* 50% */
    {0, 1, 1, 1, 1, 1, 1, 0}  /* 75% */
};

/* Noise Divisors: (Divider code) => (Divisor) */
static const int NOISE_DIVISORS[8] = { 8, 16, 32, 48, 64, 80, 96, 112 };

static uint8_t audio_channel1_pcm(const GBAudio* apu) {
    if (!apu || !apu->ch1.enabled || !(apu->nr52 & 0x80)) return 0;
    uint8_t duty = apu->ch1.nr11 >> 6;
    return DUTY_CYCLES[duty][apu->ch1.wave_pos & 7] ? (uint8_t)(apu->ch1.volume & 0x0F) : 0;
}

static uint8_t audio_channel2_pcm(const GBAudio* apu) {
    if (!apu || !apu->ch2.enabled || !(apu->nr52 & 0x80)) return 0;
    uint8_t duty = apu->ch2.nr21 >> 6;
    return DUTY_CYCLES[duty][apu->ch2.wave_pos & 7] ? (uint8_t)(apu->ch2.volume & 0x0F) : 0;
}

static uint8_t audio_channel3_pcm(const GBAudio* apu) {
    if (!apu || !apu->ch3.enabled || !(apu->nr52 & 0x80)) return 0;

    uint8_t byte = apu->ch3.wave_ram[(apu->ch3.wave_pos & 31) / 2];
    uint8_t sample = (apu->ch3.wave_pos & 1) ? (byte & 0x0F) : (byte >> 4);

    switch ((apu->ch3.nr32 >> 5) & 0x03) {
        case 0: return 0;
        case 1: return sample;
        case 2: return (uint8_t)(sample >> 1);
        case 3: return (uint8_t)(sample >> 2);
    }

    return 0;
}

static uint8_t audio_channel4_pcm(const GBAudio* apu) {
    if (!apu || !apu->ch4.enabled || !(apu->nr52 & 0x80)) return 0;
    return !(apu->ch4.lfsr & 1) ? (uint8_t)(apu->ch4.volume & 0x0F) : 0;
}

static uint16_t calc_sweep(Channel1* ch) {
    uint16_t new_freq = ch->shadow_freq;
    uint8_t shift = ch->nr10 & 0x07;
    bool sub = (ch->nr10 & 0x08) != 0;
    
    if (shift > 0) {
        uint16_t delta = new_freq >> shift;
        if (sub) new_freq -= delta;
        else     new_freq += delta;
    }
    return new_freq;
}

static void step_envelope(int* timer, int* volume, uint8_t env_reg) {
    if (*timer > 0) {
        (*timer)--;
        if (*timer == 0) {
            uint8_t period = env_reg & 0x07;
            if (period > 0) {
                *timer = period;
                bool up = (env_reg & 0x08) != 0;
                if (up && *volume < 15) (*volume)++;
                else if (!up && *volume > 0) (*volume)--;
            } else {
                *timer = 8; /* If period is 0, it acts like 8 for reload? Actually spec says 8 */
            }
        }
    }
}

static void audio_reset_timing_state(GBAudio* apu) {
    apu->fs_timer = 0;
    /* Store the previously executed step so the next clock runs step 0. */
    apu->fs_step = 7;
    apu->sample_timer = 0;
    apu->sample_phase = 0;
    apu->last_output_left = 0;
    apu->last_output_right = 0;
}

static void audio_clock_frame_sequencer(GBAudio* apu) {
    apu->fs_step = (apu->fs_step + 1) & 7;

    /* Step Length (256 Hz): Steps 0, 2, 4, 6 */
    if ((apu->fs_step & 1) == 0) {
        if (apu->ch1.length_enabled && apu->ch1.length_counter > 0) {
            apu->ch1.length_counter--;
            if (apu->ch1.length_counter == 0) apu->ch1.enabled = false;
        }
        if (apu->ch2.length_enabled && apu->ch2.length_counter > 0) {
            apu->ch2.length_counter--;
            if (apu->ch2.length_counter == 0) apu->ch2.enabled = false;
        }
        if (apu->ch3.length_enabled && apu->ch3.length_counter > 0) {
            apu->ch3.length_counter--;
            if (apu->ch3.length_counter == 0) apu->ch3.enabled = false;
        }
        if (apu->ch4.length_enabled && apu->ch4.length_counter > 0) {
            apu->ch4.length_counter--;
            if (apu->ch4.length_counter == 0) apu->ch4.enabled = false;
        }
    }

    /* Step Sweep (128 Hz): Steps 2, 6 */
    if (apu->fs_step == 2 || apu->fs_step == 6) {
        Channel1* ch1 = &apu->ch1;
        if (ch1->sweep_enabled && ch1->enabled) {
            ch1->sweep_timer--;
            if (ch1->sweep_timer <= 0) {
                uint8_t period = (ch1->nr10 >> 4) & 0x07;
                ch1->sweep_timer = (period == 0) ? 8 : period;

                if (period > 0 && (ch1->nr10 & 0x07) > 0) {
                    uint16_t new_freq = calc_sweep(ch1);
                    if (new_freq <= 2047) {
                        ch1->shadow_freq = new_freq;
                        ch1->nr13 = new_freq & 0xFF;
                        ch1->nr14 = (ch1->nr14 & ~0x07) | ((new_freq >> 8) & 0x07);
                        if (calc_sweep(ch1) > 2047) ch1->enabled = false;
                    } else {
                        ch1->enabled = false;
                    }
                }
            }
        }
    }

    /* Step Envelope (64 Hz): Step 7 */
    if (apu->fs_step == 7) {
        if (apu->ch1.enabled) step_envelope(&apu->ch1.env_timer, &apu->ch1.volume, apu->ch1.nr12);
        if (apu->ch2.enabled) step_envelope(&apu->ch2.env_timer, &apu->ch2.volume, apu->ch2.nr22);
        if (apu->ch4.enabled) step_envelope(&apu->ch4.env_timer, &apu->ch4.volume, apu->ch4.nr42);
    }
}

static int16_t audio_apply_highpass(int32_t input, int32_t* prev_in, int32_t* prev_out) {
    /* One-pole DC blocker using Q15 fixed point (R ~= 0.996) */
    const int32_t HP_R = 32640;
    int32_t output = input - *prev_in + (int32_t)(((int64_t)HP_R * *prev_out) >> 15);
    *prev_in = input;
    *prev_out = output;
    if (output > 32767) return 32767;
    if (output < -32768) return -32768;
    return (int16_t)output;
}

/* ============================================================================
 * Public Interface
 * ========================================================================== */

void* gb_audio_create(void) {
    GBAudio* apu = (GBAudio*)calloc(1, sizeof(GBAudio));
    if (!apu) return NULL;
    
    /* Approx 70224 cycles per frame / 59.7 fps = ~4.19 MHz */
    /* 4194304 / 44100 = ~95 cycles per sample */
    apu->sample_period = 95;
    
    return apu;
}

void gb_audio_destroy(void* apu) {
    audio_debug_capture_reset();
    free(apu);
}

void gb_audio_reset(void* apu_ptr) {
    GBAudio* apu = (GBAudio*)apu_ptr;
    audio_debug_capture_reset();
    memset(apu, 0, sizeof(GBAudio));
    audio_reset_timing_state(apu);
    apu->sample_period = 95;
    
    /* Initial Register Values (Standard DMG) */
    apu->ch1.nr10 = 0x80;
    apu->ch1.nr11 = 0xBF;
    apu->ch1.nr12 = 0xF3;
    apu->ch1.nr14 = 0xBF;
    
    apu->ch2.nr21 = 0x3F;
    apu->ch2.nr22 = 0x00;
    apu->ch2.nr24 = 0xBF;
    
    apu->ch3.nr30 = 0x7F;
    apu->ch3.nr31 = 0xFF;
    apu->ch3.nr32 = 0x9F;
    apu->ch3.nr34 = 0xBF;
    
    apu->ch4.nr41 = 0xFF;
    apu->ch4.nr42 = 0x00;
    apu->ch4.nr43 = 0x00;
    apu->ch4.nr44 = 0xBF;
    
    apu->nr50 = 0x77;
    apu->nr51 = 0xF3;
    apu->nr52 = 0xF1; /* Audio ON */
    audio_debug_dump_state(NULL, apu, "reset");
}

void gb_audio_get_samples(void* audio, int16_t* left, int16_t* right) {
    GBAudio* apu = (GBAudio*)audio;

    if (left) {
        *left = (apu != NULL) ? apu->last_output_left : 0;
    }
    if (right) {
        *right = (apu != NULL) ? apu->last_output_right : 0;
    }
}

uint32_t gb_audio_cycles_until_sample(const void* audio) {
    const GBAudio* apu = (const GBAudio*)audio;
    if (!apu || !(apu->nr52 & 0x80)) {
        return UINT32_MAX;
    }
    const uint32_t phase_until_sample = AUDIO_CLOCK_HZ - apu->sample_phase;
    const uint32_t cycles =
        (phase_until_sample + AUDIO_SAMPLE_RATE - 1u) / AUDIO_SAMPLE_RATE;
    return cycles > 0u ? cycles : 1u;
}

uint8_t gb_audio_read_pcm12(void* audio) {
    const GBAudio* apu = (const GBAudio*)audio;
    return (uint8_t)((audio_channel2_pcm(apu) << 4) | audio_channel1_pcm(apu));
}

uint8_t gb_audio_read_pcm34(void* audio) {
    const GBAudio* apu = (const GBAudio*)audio;
    return (uint8_t)((audio_channel4_pcm(apu) << 4) | audio_channel3_pcm(apu));
}

size_t gb_audio_state_size(void) {
    return sizeof(GBAudio);
}

bool gb_audio_save_state(const void* apu, void* out_data, size_t size) {
    if (!apu || !out_data || size != sizeof(GBAudio)) {
        return false;
    }

    memcpy(out_data, apu, sizeof(GBAudio));
    return true;
}

bool gb_audio_load_state(void* apu, const void* data, size_t size) {
    if (!apu || !data || size != sizeof(GBAudio)) {
        return false;
    }

    memcpy(apu, data, sizeof(GBAudio));
    return true;
}

uint8_t gb_audio_read(GBContext* ctx, uint16_t addr) {
    GBAudio* apu = (GBAudio*)ctx->apu;
    if (!apu) return 0xFF;

    if (addr >= 0xFF30 && addr <= 0xFF3F) {
        if (apu->ch3.enabled) {
            /* On DMG, if channel 3 is enabled, reading Wave RAM returns the byte currently being accessed */
            return apu->ch3.wave_ram[apu->ch3.wave_pos / 2];
        }
        return apu->ch3.wave_ram[addr - 0xFF30];
    }
    
    /* If audio is disabled via NR52 bit 7, most registers are 0xFF? 
       Actually, on DMG they are mostly readable. Stick to mask behavior for now. */
       
    switch (addr) {
        /* Channel 1 */
        case 0xFF10: return apu->ch1.nr10 | 0x80;
        case 0xFF11: return apu->ch1.nr11 | 0x3F;
        case 0xFF12: return apu->ch1.nr12;
        case 0xFF13: return 0xFF; /* Write only */
        case 0xFF14: return apu->ch1.nr14 | 0xBF;
        
        /* Channel 2 */
        case 0xFF15: return 0xFF; /* Not used */
        case 0xFF16: return apu->ch2.nr21 | 0x3F;
        case 0xFF17: return apu->ch2.nr22;
        case 0xFF18: return 0xFF; /* Write only */
        case 0xFF19: return apu->ch2.nr24 | 0xBF;
        
        /* Channel 3 */
        case 0xFF1A: return apu->ch3.nr30 | 0x7F;
        case 0xFF1B: return 0xFF; /* Write only */
        case 0xFF1C: return apu->ch3.nr32 | 0x9F;
        case 0xFF1D: return 0xFF; /* Write only */
        case 0xFF1E: return apu->ch3.nr34 | 0xBF;
        
        /* Channel 4 */
        case 0xFF1F: return 0xFF; /* Not used */
        case 0xFF20: return apu->ch4.nr41 | 0xFF;
        case 0xFF21: return apu->ch4.nr42;
        case 0xFF22: return apu->ch4.nr43;
        case 0xFF23: return apu->ch4.nr44 | 0xBF;
        
        /* Control */
        case 0xFF24: return apu->nr50;
        case 0xFF25: return apu->nr51;
        case 0xFF26: {
            /* NR52 - Power Status */
            /* Bit 7: Power ON/OFF (Read/Write) */
            /* Bits 0-3: Channel ON status (Read Only) */
            uint8_t val = (apu->nr52 & 0x80) | 0x70;
            if (apu->ch1.enabled) val |= 0x01;
            if (apu->ch2.enabled) val |= 0x02;
            if (apu->ch3.enabled) val |= 0x04;
            if (apu->ch4.enabled) val |= 0x08;
            return val;
        }
        
        default: return 0xFF;
    }
}


void gb_audio_write(GBContext* ctx, uint16_t addr, uint8_t value) {
    GBAudio* apu = (GBAudio*)ctx->apu;
    if (!apu) return;

    if (g_audio_debug_trace_enabled) {
        audio_debug_trace_log("[WRITE] cyc=%u addr=%04X %s <= %02X\n",
            ctx ? ctx->cycles : 0, addr, audio_reg_name(addr), value);
    }
    
    /* If APU disabled (NR52 bit 7 off), write to registers ignored unless it's NR52 or Wave RAM */
    bool power_on = (apu->nr52 & 0x80) != 0;

    if (addr >= 0xFF30 && addr <= 0xFF3F) {
        /* If channel 3 is enabled, writes to Wave RAM are ignored (or weird on DMG).
         * For simplicity/safety, we block writes if enabled.
         */
        if (!apu->ch3.enabled) {
            apu->ch3.wave_ram[addr - 0xFF30] = value;
        }
        return;
    }
    
    if (addr == 0xFF26) {
        /* NR52 - Power Control */
        bool was_powered = (apu->nr52 & 0x80) != 0;
        bool power_enable = (value & 0x80) != 0;
        apu->nr52 = (apu->nr52 & 0x0F) | (value & 0x80); /* Keep status bits, update power bit */
        if (!power_enable) {
            /* Power off: clear all registers */
            memset(&apu->ch1, 0, sizeof(Channel1));
            memset(&apu->ch2, 0, sizeof(Channel2));
            memset(&apu->ch3, 0, sizeof(Channel3));
            memset(&apu->ch4, 0, sizeof(Channel4));
            apu->nr50 = 0;
            apu->nr51 = 0;
            audio_reset_timing_state(apu);
        } else if (!was_powered) {
            audio_reset_timing_state(apu);
        }
        audio_debug_dump_state(ctx, apu, power_enable ? "nr52-power-on" : "nr52-power-off");
        return;
    }
    
    if (!power_on && addr != 0xFF26 && !(addr >= 0xFF30 && addr <= 0xFF3F)) {
        return;
    }
    
    switch (addr) {
        /* Channel 1 */
        case 0xFF10: apu->ch1.nr10 = value; break;
        case 0xFF11: 
            apu->ch1.nr11 = value; 
            apu->ch1.length_counter = 64 - (value & 0x3F);
            break;
        case 0xFF12: 
            apu->ch1.nr12 = value; 
            /* DAC Check: If top 5 bits are 0, DAC is off and channel is disabled */
            if ((value & 0xF8) == 0) apu->ch1.enabled = false;
            break;
        case 0xFF13: apu->ch1.nr13 = value; break;
        case 0xFF14: 
            apu->ch1.nr14 = value;
            apu->ch1.length_enabled = (value & 0x40) != 0;
            if (value & 0x80) {
                /* Trigger Event */
                apu->ch1.enabled = (apu->ch1.nr12 & 0xF8) != 0;
                if (apu->ch1.length_counter == 0) apu->ch1.length_counter = 64;
                apu->ch1.timer = 0;
                apu->ch1.wave_pos = 0;
                
                /* Envelope Reload */
                apu->ch1.volume = (apu->ch1.nr12 >> 4);
                apu->ch1.env_timer = (apu->ch1.nr12 & 0x07);
                if (apu->ch1.env_timer == 0) apu->ch1.env_timer = 8;
                
                /* Sweep Reload */
                apu->ch1.shadow_freq = apu->ch1.nr13 | ((apu->ch1.nr14 & 0x07) << 8);
                uint8_t sweep_period = (apu->ch1.nr10 >> 4) & 0x07;
                apu->ch1.sweep_timer = (sweep_period == 0) ? 8 : sweep_period;
                apu->ch1.sweep_enabled = (sweep_period > 0) || ((apu->ch1.nr10 & 0x07) > 0);
                if ((apu->ch1.nr10 & 0x07) > 0) {
                    /* Overflow check on trigger */
                    if (calc_sweep(&apu->ch1) > 2047) apu->ch1.enabled = false;
                }
                audio_debug_dump_state(ctx, apu, "trigger-ch1");
            }
            break;
            
        /* Channel 2 */
        case 0xFF16: 
            apu->ch2.nr21 = value; 
            apu->ch2.length_counter = 64 - (value & 0x3F);
            break;
        case 0xFF17: 
            apu->ch2.nr22 = value; 
            if ((value & 0xF8) == 0) apu->ch2.enabled = false;
            break;
        case 0xFF18: apu->ch2.nr23 = value; break;
        case 0xFF19: 
            apu->ch2.nr24 = value;
            apu->ch2.length_enabled = (value & 0x40) != 0;
            if (value & 0x80) {
                apu->ch2.enabled = (apu->ch2.nr22 & 0xF8) != 0;
                if (apu->ch2.length_counter == 0) apu->ch2.length_counter = 64;
                apu->ch2.timer = 0;
                apu->ch2.wave_pos = 0;

                /* Envelope Reload */
                apu->ch2.volume = (apu->ch2.nr22 >> 4);
                apu->ch2.env_timer = (apu->ch2.nr22 & 0x07);
                if (apu->ch2.env_timer == 0) apu->ch2.env_timer = 8;
                audio_debug_dump_state(ctx, apu, "trigger-ch2");
            }
            break;
            
        /* Channel 3 */
        case 0xFF1A: 
            apu->ch3.nr30 = value; 
            if (!(value & 0x80)) apu->ch3.enabled = false;
            break;
        case 0xFF1B:
            apu->ch3.nr31 = value;
            apu->ch3.length_counter = 256 - value;
            break;
        case 0xFF1C: apu->ch3.nr32 = value; break;
        case 0xFF1D: apu->ch3.nr33 = value; break;
        case 0xFF1E: 
            apu->ch3.nr34 = value;
            apu->ch3.length_enabled = (value & 0x40) != 0;
            if (value & 0x80) {
                apu->ch3.enabled = (apu->ch3.nr30 & 0x80) != 0;
                if (apu->ch3.length_counter == 0) apu->ch3.length_counter = 256;
                apu->ch3.timer = 0;
                apu->ch3.wave_pos = 0;
                audio_debug_dump_state(ctx, apu, "trigger-ch3");
            }
            break;
            
        /* Channel 4 */
        case 0xFF20:
            apu->ch4.nr41 = value;
            apu->ch4.length_counter = 64 - (value & 0x3F);
            break;
        case 0xFF21: 
            apu->ch4.nr42 = value; 
            if ((value & 0xF8) == 0) apu->ch4.enabled = false;
            break;
        case 0xFF22: apu->ch4.nr43 = value; break;
        case 0xFF23: 
            apu->ch4.nr44 = value;
            apu->ch4.length_enabled = (value & 0x40) != 0;
            if (value & 0x80) {
                apu->ch4.enabled = (apu->ch4.nr42 & 0xF8) != 0;
                if (apu->ch4.length_counter == 0) apu->ch4.length_counter = 64;
                apu->ch4.accum = 0;
                
                /* Envelope Reload */
                apu->ch4.volume = (apu->ch4.nr42 >> 4);
                apu->ch4.env_timer = (apu->ch4.nr42 & 0x07);
                if (apu->ch4.env_timer == 0) apu->ch4.env_timer = 8;
                
                /* LFSR Reload */
                apu->ch4.lfsr = 0x7FFF;
                audio_debug_dump_state(ctx, apu, "trigger-ch4");
            }
            break;
            
        /* Control */
        case 0xFF24: apu->nr50 = value; break;
        case 0xFF25: apu->nr51 = value; break;
        
    }
}

static void audio_step_channels(GBAudio* apu, uint32_t cycles) {
    if (apu->ch1.enabled) {
        uint16_t freq_raw = apu->ch1.nr13 | ((apu->ch1.nr14 & 0x07) << 8);
        uint32_t period = (2048 - freq_raw) * 4;
        if (period == 0) period = 4;
        
        apu->ch1.timer += cycles;
        while (apu->ch1.timer >= period) {
            apu->ch1.timer -= period;
            apu->ch1.wave_pos = (apu->ch1.wave_pos + 1) & 7;
        }
    }
    
    /* Channel 2 Stepping */
    if (apu->ch2.enabled) {
        uint16_t freq_raw = apu->ch2.nr23 | ((apu->ch2.nr24 & 0x07) << 8);
        uint32_t period = (2048 - freq_raw) * 4;
        if (period == 0) period = 4;
        
        apu->ch2.timer += cycles;
        while (apu->ch2.timer >= period) {
            apu->ch2.timer -= period;
            apu->ch2.wave_pos = (apu->ch2.wave_pos + 1) & 7;
        }
    }

    /* Channel 3 Stepping */
    if (apu->ch3.enabled) {
        uint16_t freq_raw = apu->ch3.nr33 | ((apu->ch3.nr34 & 0x07) << 8);
        uint32_t period = (2048 - freq_raw) * 2;
        if (period == 0) period = 2;

        apu->ch3.timer += cycles;
        while (apu->ch3.timer >= period) {
            apu->ch3.timer -= period;
            apu->ch3.wave_pos = (apu->ch3.wave_pos + 1) & 31;
        }
    }

    /* Channel 4 Stepping */
    if (apu->ch4.enabled) {
        /* Polynomial Counter */
        uint8_t s = apu->ch4.nr43 >> 4;
        uint8_t r = apu->ch4.nr43 & 0x07;
        uint32_t period = NOISE_DIVISORS[r & 7] << s;
        
        /* Minimum period is 8 cycles? */
        if (period < 8) period = 8;

        apu->ch4.accum += cycles;

        uint32_t steps = apu->ch4.accum / period;
        apu->ch4.accum %= period;

        while (steps-- > 0) {
            /* Shift LFSR */
            uint16_t lfsr = apu->ch4.lfsr;
            bool xor_res = (lfsr & 1) ^ ((lfsr >> 1) & 1);
            lfsr >>= 1;
            lfsr |= (xor_res << 14);
            if (apu->ch4.nr43 & 0x08) { /* 7-bit mode */
                lfsr &= ~0x40;
                lfsr |= (xor_res << 6);
            }
            apu->ch4.lfsr = lfsr;
        }
    }
}

static void audio_emit_sample(GBContext* ctx, GBAudio* apu) {
    audio_stats_sample_generated();
    gbrt_note_audio_sample(ctx);

    int16_t left = 0;
    int16_t right = 0;
    int ch1_mix = 0;
    int ch2_mix = 0;
    int ch3_mix = 0;
    int ch4_mix = 0;

    if (apu->ch1.enabled && (apu->ch1.nr12 & 0xF0)) {
        int duty = (apu->ch1.nr11 >> 6) & 3;
        int output = DUTY_CYCLES[duty][apu->ch1.wave_pos] ? 1 : -1;
        ch1_mix = apu->ch1.volume * output;
        if (apu->nr51 & 0x01) right += ch1_mix;
        if (apu->nr51 & 0x10) left += ch1_mix;
    }

    if (apu->ch2.enabled && (apu->ch2.nr22 & 0xF0)) {
        int duty = (apu->ch2.nr21 >> 6) & 3;
        int output = DUTY_CYCLES[duty][apu->ch2.wave_pos] ? 1 : -1;
        ch2_mix = apu->ch2.volume * output;
        if (apu->nr51 & 0x02) right += ch2_mix;
        if (apu->nr51 & 0x20) left += ch2_mix;
    }

    if (apu->ch3.enabled && (apu->ch3.nr30 & 0x80)) {
        uint8_t byte = apu->ch3.wave_ram[apu->ch3.wave_pos / 2];
        uint8_t sample = (apu->ch3.wave_pos & 1) ? (byte & 0x0F) : (byte >> 4);
        switch ((apu->ch3.nr32 >> 5) & 3) {
            case 0: sample = 8; break;
            case 1: break;
            case 2: sample >>= 1; break;
            case 3: sample >>= 2; break;
        }
        ch3_mix = (int)sample - 8;
        if (apu->nr51 & 0x04) right += ch3_mix;
        if (apu->nr51 & 0x40) left += ch3_mix;
    }

    if (apu->ch4.enabled && (apu->ch4.nr42 & 0xF0)) {
        int output = !(apu->ch4.lfsr & 1) ? 1 : -1;
        ch4_mix = apu->ch4.volume * output;
        if (apu->nr51 & 0x08) right += ch4_mix;
        if (apu->nr51 & 0x80) left += ch4_mix;
    }

    int vol_l = (apu->nr50 >> 4) & 7;
    int vol_r = apu->nr50 & 7;
    int32_t mixed_left = left * (vol_l + 1) * 64;
    int32_t mixed_right = right * (vol_r + 1) * 64;
    left = audio_apply_highpass(mixed_left, &apu->hp_prev_in_l, &apu->hp_prev_out_l);
    right = audio_apply_highpass(mixed_right, &apu->hp_prev_in_r, &apu->hp_prev_out_r);
    apu->last_output_left = left;
    apu->last_output_right = right;

    if (g_audio_debug_trace_enabled) {
        bool nonzero = (left != 0) || (right != 0);
        int32_t peak = abs((int)left);
        if (abs((int)right) > peak) peak = abs((int)right);

        g_debug_trace_total_samples++;
        g_debug_trace_window_samples++;
        if (nonzero) g_debug_trace_window_nonzero++;
        if (ch1_mix != 0) g_debug_trace_window_channel_nonzero[0]++;
        if (ch2_mix != 0) g_debug_trace_window_channel_nonzero[1]++;
        if (ch3_mix != 0) g_debug_trace_window_channel_nonzero[2]++;
        if (ch4_mix != 0) g_debug_trace_window_channel_nonzero[3]++;
        if (peak > g_debug_trace_window_peak) g_debug_trace_window_peak = peak;

        if (nonzero && !g_debug_trace_first_nonzero_logged) {
            g_debug_trace_first_nonzero_logged = true;
            audio_debug_trace_log(
                "[FIRST-NONZERO] sample=%llu time_ms=%.2f cyc=%u left=%d right=%d "
                "ch=[%d,%d,%d,%d]\n",
                (unsigned long long)g_debug_trace_total_samples,
                (double)g_debug_trace_total_samples * 1000.0 / 44100.0,
                ctx ? ctx->cycles : 0,
                left, right,
                ch1_mix, ch2_mix, ch3_mix, ch4_mix);
            audio_debug_dump_state(ctx, apu, "first-nonzero");
        }

        if (g_debug_trace_window_samples >= 44100) {
            uint64_t second_index = g_debug_trace_total_samples / 44100;
            audio_debug_trace_log(
                "[SECOND %llu] cyc=%u nonzero=%u/%u peak=%d ch1=%u ch2=%u ch3=%u ch4=%u\n",
                (unsigned long long)second_index,
                ctx ? ctx->cycles : 0,
                g_debug_trace_window_nonzero,
                g_debug_trace_window_samples,
                g_debug_trace_window_peak,
                g_debug_trace_window_channel_nonzero[0],
                g_debug_trace_window_channel_nonzero[1],
                g_debug_trace_window_channel_nonzero[2],
                g_debug_trace_window_channel_nonzero[3]);
            audio_debug_dump_state(ctx, apu, "second-summary");
            g_debug_trace_window_samples = 0;
            g_debug_trace_window_nonzero = 0;
            memset(g_debug_trace_window_channel_nonzero, 0, sizeof(g_debug_trace_window_channel_nonzero));
            g_debug_trace_window_peak = 0;
        }
    }

    if (g_audio_debug_enabled && g_debug_sample_count < g_debug_capture_limit_samples) {
        if (!g_debug_audio_file) g_debug_audio_file = fopen("debug_audio.raw", "wb");
        if (g_debug_audio_file) {
            g_debug_audio_buffer[g_debug_audio_buffer_count * 2] = left;
            g_debug_audio_buffer[g_debug_audio_buffer_count * 2 + 1] = right;
            g_debug_audio_buffer_count++;
            g_debug_sample_count++;
            if (g_debug_audio_buffer_count >= DEBUG_AUDIO_BUFFER_FRAMES) {
                audio_debug_capture_flush();
            }
            if (g_debug_sample_count >= g_debug_capture_limit_samples) {
                audio_debug_capture_flush();
                printf("[AUDIO] Debug capture complete. Wrote %d samples to debug_audio.raw\n", g_debug_sample_count);
                fclose(g_debug_audio_file);
                g_debug_audio_file = NULL;
                g_debug_audio_buffer_count = 0;
            }
        }
    }
    gb_audio_callback(ctx, left, right);
}

static void audio_advance(GBContext* ctx, GBAudio* apu, uint32_t cycles) {
    while (cycles > 0) {
        const uint32_t phase_until_sample = AUDIO_CLOCK_HZ - apu->sample_phase;
        uint32_t cycles_until_sample =
            (phase_until_sample + AUDIO_SAMPLE_RATE - 1u) / AUDIO_SAMPLE_RATE;
        if (cycles_until_sample == 0) cycles_until_sample = 1;
        const uint32_t step = cycles < cycles_until_sample ? cycles : cycles_until_sample;

        audio_step_channels(apu, step);
        apu->sample_timer += step;
        apu->sample_phase += step * AUDIO_SAMPLE_RATE;
        cycles -= step;

        if (apu->sample_phase >= AUDIO_CLOCK_HZ) {
            apu->sample_phase -= AUDIO_CLOCK_HZ;
            audio_emit_sample(ctx, apu);
        }
    }
}

void gb_audio_step(GBContext* ctx, uint32_t cycles) {
    GBAudio* apu = ctx ? (GBAudio*)ctx->apu : NULL;
    if (!apu || !(apu->nr52 & 0x80) || cycles == 0) return;
    audio_advance(ctx, apu, cycles);
}

static uint32_t audio_cpu_to_system_cycles(uint32_t cpu_cycles,
                                           bool double_speed,
                                           uint8_t* remainder) {
    if (!double_speed) return cpu_cycles;
    const uint32_t system_cycles = (cpu_cycles + (*remainder & 1u)) >> 1;
    *remainder = (uint8_t)((*remainder + cpu_cycles) & 1u);
    return system_cycles;
}

void gb_audio_step_timed(GBContext* ctx,
                         uint16_t old_div,
                         uint32_t cpu_cycles,
                         bool double_speed,
                         uint8_t system_cycle_remainder) {
    GBAudio* apu = ctx ? (GBAudio*)ctx->apu : NULL;
    if (!apu || !(apu->nr52 & 0x80) || cpu_cycles == 0) return;

    const uint16_t fs_mask = (uint16_t)(1u << (double_speed ? 13 : 12));
    uint16_t current_div = old_div;
    uint32_t remaining = cpu_cycles;
    uint8_t remainder = system_cycle_remainder & 1u;

    while (remaining > 0) {
        const uint16_t next_fall =
            (uint16_t)((current_div | (uint16_t)((fs_mask * 2u) - 1u)) + 1u);
        uint32_t distance = (uint16_t)(next_fall - current_div);
        if (distance == 0) distance = (uint32_t)fs_mask * 2u;

        if (distance > remaining) {
            audio_advance(ctx,
                          apu,
                          audio_cpu_to_system_cycles(remaining, double_speed, &remainder));
            break;
        }

        if (distance > 1) {
            const uint32_t before_edge = distance - 1u;
            audio_advance(ctx,
                          apu,
                          audio_cpu_to_system_cycles(before_edge, double_speed, &remainder));
            current_div = (uint16_t)(current_div + before_edge);
            remaining -= before_edge;
        }

        audio_clock_frame_sequencer(apu);
        audio_advance(ctx,
                      apu,
                      audio_cpu_to_system_cycles(1u, double_speed, &remainder));
        current_div++;
        remaining--;
    }
}

void gb_audio_div_tick(void* apu_ptr, uint16_t old_div, uint16_t new_div, bool double_speed) {
    if (!apu_ptr) return;
    GBAudio* apu = (GBAudio*)apu_ptr;
    if (!(apu->nr52 & 0x80)) return;

    const uint16_t fs_mask = (uint16_t)(1 << (double_speed ? 13 : 12));
    uint16_t current = old_div;
    uint32_t cycles_left = (uint16_t)(new_div - old_div);

    while (cycles_left > 0) {
        uint16_t next_fall = (current | (uint16_t)((fs_mask * 2) - 1)) + 1;
        uint32_t dist = (uint16_t)(next_fall - current);
        if (dist == 0) dist = fs_mask * 2;

        if (cycles_left >= dist) {
            audio_clock_frame_sequencer(apu);
            current = (uint16_t)(current + dist);
            cycles_left -= dist;
        } else {
            break;
        }
    }
}

void gb_audio_div_reset(void* apu_ptr, uint16_t old_div, bool double_speed) {
    if (!apu_ptr) return;
    GBAudio* apu = (GBAudio*)apu_ptr;
    if (!(apu->nr52 & 0x80)) return;
    const uint16_t fs_mask = (uint16_t)(1u << (double_speed ? 13 : 12));
    if (old_div & fs_mask) {
        audio_clock_frame_sequencer(apu);
    }
}
