#define GB_INTERNAL

#include <Core/gb.h>

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint64_t start;
    uint64_t end;
    uint8_t keys;
} InputPulse;

typedef struct {
    uint64_t frame;
} Checkpoint;

static uint32_t pixels[160 * 144];
static uint64_t pending_frames;

static void on_vblank(GB_gameboy_t *gb, GB_vblank_type_t type)
{
    (void)gb;
    if (type == GB_VBLANK_TYPE_NORMAL_FRAME ||
        type == GB_VBLANK_TYPE_LCD_OFF) {
        pending_frames++;
    }
}

static uint32_t encode_rgb(GB_gameboy_t *gb, uint8_t r, uint8_t g, uint8_t b)
{
    (void)gb;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static void fail(const char *message, const char *path)
{
    if (path) {
        fprintf(stderr, "error: %s: %s\n", message, path);
    }
    else {
        fprintf(stderr, "error: %s\n", message);
    }
    exit(1);
}

static uint64_t parse_u64(const char *text, const char *name)
{
    char *end = NULL;
    errno = 0;
    unsigned long long value = strtoull(text, &end, 10);
    if (errno || !end || *end) {
        fail(name, text);
    }
    return (uint64_t)value;
}

static InputPulse *load_pulses(const char *path, size_t *count)
{
    FILE *file = fopen(path, "r");
    if (!file) fail("cannot open input schedule", path);

    size_t capacity = 64;
    InputPulse *pulses = malloc(capacity * sizeof(*pulses));
    if (!pulses) fail("out of memory", NULL);

    *count = 0;
    while (true) {
        InputPulse pulse;
        unsigned keys;
        int matched = fscanf(file,
                             "%" SCNu64 " %" SCNu64 " %x",
                             &pulse.start,
                             &pulse.end,
                             &keys);
        if (matched == EOF) break;
        if (matched != 3 || pulse.end <= pulse.start || keys > 0xFF) {
            fclose(file);
            fail("malformed input schedule", path);
        }
        if (*count == capacity) {
            capacity *= 2;
            InputPulse *grown = realloc(pulses, capacity * sizeof(*pulses));
            if (!grown) {
                fclose(file);
                free(pulses);
                fail("out of memory", NULL);
            }
            pulses = grown;
        }
        pulse.keys = (uint8_t)keys;
        pulses[(*count)++] = pulse;
    }
    fclose(file);
    return pulses;
}

static Checkpoint *load_checkpoints(const char *path, size_t *count)
{
    FILE *file = fopen(path, "r");
    if (!file) fail("cannot open checkpoint list", path);

    size_t capacity = 16;
    Checkpoint *checkpoints = malloc(capacity * sizeof(*checkpoints));
    if (!checkpoints) fail("out of memory", NULL);

    *count = 0;
    while (true) {
        Checkpoint checkpoint;
        int matched = fscanf(file, "%" SCNu64, &checkpoint.frame);
        if (matched == EOF) break;
        if (matched != 1 || checkpoint.frame == 0) {
            fclose(file);
            fail("malformed checkpoint list", path);
        }
        if (*count == capacity) {
            capacity *= 2;
            Checkpoint *grown =
                realloc(checkpoints, capacity * sizeof(*checkpoints));
            if (!grown) {
                fclose(file);
                free(checkpoints);
                fail("out of memory", NULL);
            }
            checkpoints = grown;
        }
        checkpoints[(*count)++] = checkpoint;
    }
    fclose(file);
    if (*count == 0) fail("checkpoint list is empty", path);
    return checkpoints;
}

static uint8_t active_keys(const InputPulse *pulses,
                           size_t pulse_count,
                           uint64_t cycle)
{
    uint8_t keys = 0;
    for (size_t index = 0; index < pulse_count; index++) {
        if (cycle >= pulses[index].start && cycle < pulses[index].end) {
            keys |= pulses[index].keys;
        }
    }
    return keys;
}

static void apply_keys(GB_gameboy_t *gb, uint8_t keys)
{
    for (unsigned index = 0; index < GB_KEY_MAX; index++) {
        GB_set_key_state(gb, (GB_key_t)index, (keys & (1u << index)) != 0);
    }
}

static void write_ppm(const char *output_dir, uint64_t frame)
{
    char path[4096];
    int written = snprintf(path,
                           sizeof(path),
                           "%s/frame_%05" PRIu64 ".ppm",
                           output_dir,
                           frame);
    if (written < 0 || (size_t)written >= sizeof(path)) {
        fail("output path is too long", output_dir);
    }

    FILE *file = fopen(path, "wb");
    if (!file) fail("cannot write frame", path);
    fprintf(file, "P6\n160 144\n255\n");
    for (size_t index = 0; index < 160 * 144; index++) {
        uint8_t rgb[3] = {
            (uint8_t)(pixels[index] >> 16),
            (uint8_t)(pixels[index] >> 8),
            (uint8_t)pixels[index],
        };
        if (fwrite(rgb, 1, sizeof(rgb), file) != sizeof(rgb)) {
            fclose(file);
            fail("short frame write", path);
        }
    }
    if (fclose(file) != 0) fail("cannot close frame", path);
}

static void write_state(const char *output_dir,
                        GB_gameboy_t *gb,
                        uint64_t frame,
                        uint64_t cycles)
{
    char path[4096];
    int written = snprintf(path,
                           sizeof(path),
                           "%s/state_%05" PRIu64 ".json",
                           output_dir,
                           frame);
    if (written < 0 || (size_t)written >= sizeof(path)) {
        fail("output path is too long", output_dir);
    }

    size_t ram_size = 0;
    uint16_t ram_bank = 0;
    const uint8_t *ram =
        GB_get_direct_access(gb, GB_DIRECT_ACCESS_RAM, &ram_size, &ram_bank);
    GB_registers_t *registers = GB_get_registers(gb);
    if (!ram || ram_size < 0x2000 || !registers) {
        fail("SameBoy did not expose expected CGB state", NULL);
    }

    FILE *file = fopen(path, "w");
    if (!file) fail("cannot write state", path);
    fprintf(file,
            "{\n"
            "  \"frame\": %" PRIu64 ",\n"
            "  \"cycles\": %" PRIu64 ",\n"
            "  \"pc\": %u,\n"
            "  \"sp\": %u,\n"
            "  \"wram_bank\": %u,\n"
            "  \"wram_bank_1_d000_dfff\": {\n"
            "    \"557\": %u,\n"
            "    \"2124\": %u,\n"
            "    \"2442\": %u,\n"
            "    \"3253\": %u,\n"
            "    \"3254\": %u,\n"
            "    \"3255\": %u,\n"
            "    \"3256\": %u\n"
            "  }\n"
            "}\n",
            frame,
            cycles,
            registers->pc,
            registers->sp,
            ram_bank,
            ram[0x1000 + 557],
            ram[0x1000 + 2124],
            ram[0x1000 + 2442],
            ram[0x1000 + 3253],
            ram[0x1000 + 3254],
            ram[0x1000 + 3255],
            ram[0x1000 + 3256]);
    if (fclose(file) != 0) fail("cannot close state", path);
}

int main(int argc, char **argv)
{
    if (argc != 9) {
        fprintf(stderr,
                "usage: %s ROM BOOT_ROM BATTERY_OR_DASH SCHEDULE CHECKPOINTS OUTPUT_DIR "
                "FRAME_LIMIT RTC_UNIX_TIME\n",
                argv[0]);
        return 2;
    }

    const char *rom_path = argv[1];
    const char *boot_rom_path = argv[2];
    const char *battery_path = argv[3];
    const char *schedule_path = argv[4];
    const char *checkpoint_path = argv[5];
    const char *output_dir = argv[6];
    uint64_t frame_limit = parse_u64(argv[7], "invalid frame limit");
    uint64_t rtc_time = parse_u64(argv[8], "invalid RTC time");
    if (frame_limit == 0) fail("frame limit must be positive", NULL);

    size_t pulse_count = 0;
    size_t checkpoint_count = 0;
    InputPulse *pulses = load_pulses(schedule_path, &pulse_count);
    Checkpoint *checkpoints = load_checkpoints(checkpoint_path, &checkpoint_count);
    if (checkpoints[checkpoint_count - 1].frame > frame_limit) {
        fail("checkpoint exceeds frame limit", checkpoint_path);
    }

    GB_gameboy_t gb;
    GB_init(&gb, GB_MODEL_CGB_E);
    if (GB_load_boot_rom(&gb, boot_rom_path) != 0) {
        fail("cannot load SameBoy boot ROM", boot_rom_path);
    }
    if (GB_load_rom(&gb, rom_path) != 0) {
        fail("cannot load ROM", rom_path);
    }
    if (strcmp(battery_path, "-") != 0 &&
        GB_load_battery(&gb, battery_path) != 0) {
        fail("cannot load battery", battery_path);
    }

    GB_set_color_correction_mode(&gb, GB_COLOR_CORRECTION_DISABLED);
    GB_set_rgb_encode_callback(&gb, encode_rgb);
    GB_set_pixels_output(&gb, pixels);
    GB_set_vblank_callback(&gb, on_vblank);
    GB_set_rtc_mode(&gb, GB_RTC_MODE_ACCURATE);
    GB_set_turbo_mode(&gb, true, true);

    while (!gb.boot_rom_finished) {
        GB_run(&gb);
    }

    /*
     * Route time begins only after SameBoy's independently built boot ROM
     * hands control to cartridge address $0100. This mirrors GB Recompiled's
     * documented skip-boot contract without copying its internal device state.
     */
    pending_frames = 0;
    uint64_t cycles = 0;
    uint64_t frames = 0;
    gb.rtc_real.seconds = 0;
    gb.rtc_real.minutes = 0;
    gb.rtc_real.hours = 0;
    gb.rtc_real.days = 0;
    gb.rtc_real.high = 0;
    gb.last_rtc_second = rtc_time;
    gb.rtc_cycles = 0;
    apply_keys(&gb, active_keys(pulses, pulse_count, cycles));

    size_t next_checkpoint = 0;
    while (frames < frame_limit) {
        unsigned elapsed_8mhz = GB_run(&gb);
        cycles += elapsed_8mhz / 2;
        while (pending_frames > 0 && frames < frame_limit) {
            pending_frames--;
            frames++;

            while (next_checkpoint < checkpoint_count &&
                   checkpoints[next_checkpoint].frame == frames) {
                write_ppm(output_dir, frames);
                write_state(output_dir, &gb, frames, cycles);
                next_checkpoint++;
            }
            apply_keys(&gb, active_keys(pulses, pulse_count, cycles));
        }
    }

    printf("PASS frames=%" PRIu64 " cycles=%" PRIu64
           " checkpoints=%zu pulses=%zu\n",
           frames,
           cycles,
           checkpoint_count,
           pulse_count);
    GB_free(&gb);
    free(checkpoints);
    free(pulses);
    return next_checkpoint == checkpoint_count ? 0 : 1;
}
