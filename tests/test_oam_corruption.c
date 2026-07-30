#include "gbrt.h"
#include "ppu.h"

#include <stdio.h>
#include <string.h>

static GBContext* make_context(GBModel model) {
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
    return ctx;
}

static uint16_t load_word(const uint8_t* bytes, size_t offset) {
    return (uint16_t)(bytes[offset] | ((uint16_t)bytes[offset + 1] << 8));
}

static void store_word(uint8_t* bytes, size_t offset, uint16_t value) {
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1] = (uint8_t)(value >> 8);
}

static uint16_t write_glitch(uint16_t current,
                             uint16_t previous_first,
                             uint16_t previous_third) {
    return (uint16_t)(((current ^ previous_third) &
                       (previous_first ^ previous_third)) ^
                      previous_third);
}

static void apply_write_glitch(uint8_t* bytes, size_t row) {
    store_word(bytes, row,
               write_glitch(load_word(bytes, row),
                            load_word(bytes, row - 8),
                            load_word(bytes, row - 4)));
    memcpy(bytes + row + 2, bytes + row - 6, 6);
}

static uint16_t read_glitch(uint16_t current,
                            uint16_t previous_first,
                            uint16_t previous_third) {
    return (uint16_t)(previous_first | (current & previous_third));
}

static uint16_t read_secondary(uint16_t a,
                               uint16_t b,
                               uint16_t c,
                               uint16_t d) {
    return (uint16_t)((b & (a | c | d)) | (a & c & d));
}

static uint16_t read_tertiary(uint16_t a,
                              uint16_t b,
                              uint16_t c,
                              uint16_t d,
                              uint16_t e,
                              unsigned variant) {
    if (variant == 1) {
        return (uint16_t)(c | (a & b & d & e));
    }
    if (variant == 2) {
        return (uint16_t)((c & (a | b | d | e)) | (a & b & d & e));
    }
    return (uint16_t)((c & (a | b | d | e)) | (b & d & e));
}

static uint16_t read_quaternary(uint16_t b,
                                uint16_t c,
                                uint16_t d,
                                uint16_t e,
                                uint16_t f,
                                uint16_t g,
                                uint16_t h) {
    return (uint16_t)((e & (h | g | ((uint16_t)~d & f) | c | b)) |
                      (c & g & h));
}

static void apply_combined_read_glitch(uint8_t* bytes, size_t row) {
    if ((row & 0x18u) == 0x10u) {
        store_word(bytes, row - 8,
                   read_secondary(load_word(bytes, row - 16),
                                  load_word(bytes, row - 8),
                                  load_word(bytes, row),
                                  load_word(bytes, row - 4)));
        if (row < 0x98u) {
            memcpy(bytes + row - 16, bytes + row - 8, 8);
        }
    } else if ((row & 0x18u) == 0u) {
        unsigned variant = 1;
        if (row == 0x20u) {
            variant = 2;
        } else if (row == 0x60u) {
            variant = 3;
        }
        if (row == 0x40u) {
            store_word(bytes, row - 8,
                       read_quaternary(load_word(bytes, row),
                                       load_word(bytes, row - 4),
                                       load_word(bytes, row - 6),
                                       load_word(bytes, row - 8),
                                       load_word(bytes, row - 14),
                                       load_word(bytes, row - 16),
                                       load_word(bytes, row - 32)));
        } else {
            store_word(bytes, row - 8,
                       read_tertiary(load_word(bytes, row),
                                     load_word(bytes, row - 4),
                                     load_word(bytes, row - 8),
                                     load_word(bytes, row - 16),
                                     load_word(bytes, row - 32),
                                     variant));
        }
        if (row < 0x98u) {
            memcpy(bytes + row - 16, bytes + row - 8, 8);
            memcpy(bytes + row - 32, bytes + row - 8, 8);
        }
    } else {
        const uint16_t glitched =
            read_glitch(load_word(bytes, row),
                        load_word(bytes, row - 8),
                        load_word(bytes, row - 4));
        store_word(bytes, row - 8, glitched);
        store_word(bytes, row, glitched);
    }

    memcpy(bytes + row, bytes + row - 8, 8);
    if (row == 0x80u) {
        memcpy(bytes, bytes + row, 8);
    }
}

static int test_inc_de_corrupts_scanned_row(void) {
    GBContext* ctx = make_context(GB_MODEL_DMG);
    if (!ctx) {
        return 2;
    }

    for (size_t i = 0; i < OAM_SIZE; ++i) {
        ctx->oam[i] = (uint8_t)i;
    }

    uint8_t expected[OAM_SIZE];
    memcpy(expected, ctx->oam, sizeof(expected));
    store_word(expected, 24,
               write_glitch(load_word(expected, 24),
                            load_word(expected, 16),
                            load_word(expected, 20)));
    memcpy(expected + 26, expected + 18, 6);

    GBPPU* ppu = (GBPPU*)ctx->ppu;
    ppu->lcdc = LCDC_LCD_ENABLE;
    ppu->mode = PPU_MODE_OAM;
    ppu->visible_mode = PPU_MODE_OAM;
    ppu->stat_irq_mode = PPU_MODE_OAM;
    ppu->mode_cycles = 6;
    ppu->ly = 0;
    ctx->io[0x40] = LCDC_LCD_ENABLE;
    ctx->io[0x41] = PPU_MODE_OAM;
    ctx->io[0x44] = 0;

    gb_write8(ctx, 0xC000, 0x13); /* INC DE */
    ctx->pc = 0xC000;
    ctx->de = 0xFE00;
    const uint32_t start_cycles = ctx->cycles;

    if (gb_debug_step(ctx, GB_EXECUTION_INTERPRETER) != 8 ||
        ctx->cycles - start_cycles != 8 || ctx->de != 0xFE01 ||
        ppu->mode_cycles != 14) {
        fprintf(stderr,
                "INC DE result de=%04X pc=%04X mode_dot=%u cycles=%u\n",
                ctx->de, ctx->pc, ppu->mode_cycles,
                ctx->cycles - start_cycles);
        gb_context_destroy(ctx);
        return 1;
    }

    if (memcmp(ctx->oam, expected, OAM_SIZE) != 0) {
        for (size_t i = 0; i < OAM_SIZE; ++i) {
            if (ctx->oam[i] != expected[i]) {
                fprintf(stderr,
                        "INC DE OAM[%zu]=%02X expected %02X at mode-2 dot 10\n",
                        i, ctx->oam[i], expected[i]);
                break;
            }
        }
        gb_context_destroy(ctx);
        return 1;
    }

    gb_context_destroy(ctx);
    return 0;
}

static int test_dec_de_corrupts_from_predecrement_address(void) {
    GBContext* ctx = make_context(GB_MODEL_DMG);
    if (!ctx) {
        return 2;
    }

    for (size_t i = 0; i < OAM_SIZE; ++i) {
        ctx->oam[i] = (uint8_t)i;
    }

    uint8_t expected[OAM_SIZE];
    memcpy(expected, ctx->oam, sizeof(expected));
    store_word(expected, 24,
               write_glitch(load_word(expected, 24),
                            load_word(expected, 16),
                            load_word(expected, 20)));
    memcpy(expected + 26, expected + 18, 6);

    GBPPU* ppu = (GBPPU*)ctx->ppu;
    ppu->lcdc = LCDC_LCD_ENABLE;
    ppu->mode = PPU_MODE_OAM;
    ppu->visible_mode = PPU_MODE_OAM;
    ppu->stat_irq_mode = PPU_MODE_OAM;
    ppu->mode_cycles = 6;
    ppu->ly = 0;
    ctx->io[0x40] = LCDC_LCD_ENABLE;
    ctx->io[0x41] = PPU_MODE_OAM;
    ctx->io[0x44] = 0;

    gb_write8(ctx, 0xC000, 0x1B); /* DEC DE */
    ctx->pc = 0xC000;
    ctx->de = 0xFE00;
    const uint32_t start_cycles = ctx->cycles;

    if (gb_debug_step(ctx, GB_EXECUTION_INTERPRETER) != 8 ||
        ctx->cycles - start_cycles != 8 || ctx->de != 0xFDFF ||
        ppu->mode_cycles != 14) {
        fprintf(stderr,
                "DEC DE result de=%04X pc=%04X mode_dot=%u cycles=%u\n",
                ctx->de, ctx->pc, ppu->mode_cycles,
                ctx->cycles - start_cycles);
        gb_context_destroy(ctx);
        return 1;
    }

    if (memcmp(ctx->oam, expected, OAM_SIZE) != 0) {
        for (size_t i = 0; i < OAM_SIZE; ++i) {
            if (ctx->oam[i] != expected[i]) {
                fprintf(stderr,
                        "DEC DE OAM[%zu]=%02X expected %02X at mode-2 dot 10\n",
                        i, ctx->oam[i], expected[i]);
                break;
            }
        }
        gb_context_destroy(ctx);
        return 1;
    }

    gb_context_destroy(ctx);
    return 0;
}

static int test_lcd_startup_hidden_scan_corrupts_second_oam_row(void) {
    GBContext* ctx = make_context(GB_MODEL_DMG);
    if (!ctx) {
        return 2;
    }

    for (size_t i = 0; i < OAM_SIZE; ++i) {
        ctx->oam[i] = (uint8_t)i;
    }

    uint8_t expected[OAM_SIZE];
    memcpy(expected, ctx->oam, sizeof(expected));
    store_word(expected, 8,
               write_glitch(load_word(expected, 8),
                            load_word(expected, 0),
                            load_word(expected, 4)));
    memcpy(expected + 10, expected + 2, 6);

    gb_write8(ctx, 0xFF40, 0x00);
    gb_write8(ctx, 0xFF40, LCDC_LCD_ENABLE);
    gb_tick(ctx, 2);

    ctx->de = 0xFE00;
    gbrt_timed_inc16(ctx, &ctx->de);

    GBPPU* ppu = (GBPPU*)ctx->ppu;
    if (ctx->de != 0xFE01 || ppu->mode != PPU_MODE_HBLANK ||
        ppu->mode_cycles != 10) {
        fprintf(stderr,
                "startup INC DE result de=%04X mode=%u dot=%u\n",
                ctx->de, (unsigned)ppu->mode, ppu->mode_cycles);
        gb_context_destroy(ctx);
        return 1;
    }

    if (memcmp(ctx->oam, expected, OAM_SIZE) != 0) {
        for (size_t i = 0; i < OAM_SIZE; ++i) {
            if (ctx->oam[i] != expected[i]) {
                fprintf(stderr,
                        "startup INC DE OAM[%zu]=%02X expected %02X\n",
                        i, ctx->oam[i], expected[i]);
                break;
            }
        }
        gb_context_destroy(ctx);
        return 1;
    }

    gb_context_destroy(ctx);
    return 0;
}

static int test_blocked_oam_write_corrupts_current_scan_row(void) {
    GBContext* ctx = make_context(GB_MODEL_DMG);
    if (!ctx) {
        return 2;
    }

    for (size_t i = 0; i < OAM_SIZE; ++i) {
        ctx->oam[i] = (uint8_t)i;
    }

    uint8_t expected[OAM_SIZE];
    memcpy(expected, ctx->oam, sizeof(expected));
    store_word(expected, 24,
               write_glitch(load_word(expected, 24),
                            load_word(expected, 16),
                            load_word(expected, 20)));
    memcpy(expected + 26, expected + 18, 6);

    GBPPU* ppu = (GBPPU*)ctx->ppu;
    ppu->lcdc = LCDC_LCD_ENABLE;
    ppu->mode = PPU_MODE_OAM;
    ppu->visible_mode = PPU_MODE_OAM;
    ppu->stat_irq_mode = PPU_MODE_OAM;
    ppu->mode_cycles = 10;
    ppu->ly = 0;
    ctx->io[0x40] = LCDC_LCD_ENABLE;
    ctx->io[0x41] = PPU_MODE_OAM;
    ctx->io[0x44] = 0;

    gb_write8(ctx, 0xFE00, 0xA5);

    if (memcmp(ctx->oam, expected, OAM_SIZE) != 0) {
        for (size_t i = 0; i < OAM_SIZE; ++i) {
            if (ctx->oam[i] != expected[i]) {
                fprintf(stderr,
                        "blocked write OAM[%zu]=%02X expected %02X\n",
                        i, ctx->oam[i], expected[i]);
                break;
            }
        }
        gb_context_destroy(ctx);
        return 1;
    }

    gb_context_destroy(ctx);
    return 0;
}

static int test_blocked_oam_read_corrupts_current_scan_row(void) {
    GBContext* ctx = make_context(GB_MODEL_DMG);
    if (!ctx) {
        return 2;
    }

    for (size_t i = 0; i < OAM_SIZE; ++i) {
        ctx->oam[i] = (uint8_t)i;
    }

    uint8_t expected[OAM_SIZE];
    memcpy(expected, ctx->oam, sizeof(expected));
    apply_combined_read_glitch(expected, 24);

    GBPPU* ppu = (GBPPU*)ctx->ppu;
    ppu->lcdc = LCDC_LCD_ENABLE;
    ppu->mode = PPU_MODE_OAM;
    ppu->visible_mode = PPU_MODE_OAM;
    ppu->stat_irq_mode = PPU_MODE_OAM;
    ppu->mode_cycles = 10;
    ppu->ly = 0;
    ctx->io[0x40] = LCDC_LCD_ENABLE;
    ctx->io[0x41] = PPU_MODE_OAM;
    ctx->io[0x44] = 0;

    const uint8_t value = gb_read8(ctx, 0xFE00);
    if (value != 0xFF) {
        fprintf(stderr, "blocked OAM read returned %02X expected FF\n", value);
        gb_context_destroy(ctx);
        return 1;
    }

    if (memcmp(ctx->oam, expected, OAM_SIZE) != 0) {
        for (size_t i = 0; i < OAM_SIZE; ++i) {
            if (ctx->oam[i] != expected[i]) {
                fprintf(stderr,
                        "blocked read OAM[%zu]=%02X expected %02X\n",
                        i, ctx->oam[i], expected[i]);
                break;
            }
        }
        gb_context_destroy(ctx);
        return 1;
    }

    gb_context_destroy(ctx);
    return 0;
}

static int test_blocked_unusable_oam_read_corrupts_current_scan_row(void) {
    GBContext* ctx = make_context(GB_MODEL_DMG);
    if (!ctx) {
        return 2;
    }

    for (size_t i = 0; i < OAM_SIZE; ++i) {
        ctx->oam[i] = (uint8_t)i;
    }

    uint8_t expected[OAM_SIZE];
    memcpy(expected, ctx->oam, sizeof(expected));
    apply_combined_read_glitch(expected, 24);

    GBPPU* ppu = (GBPPU*)ctx->ppu;
    ppu->lcdc = LCDC_LCD_ENABLE;
    ppu->mode = PPU_MODE_OAM;
    ppu->visible_mode = PPU_MODE_OAM;
    ppu->stat_irq_mode = PPU_MODE_OAM;
    ppu->mode_cycles = 10;
    ppu->ly = 0;
    ctx->io[0x40] = LCDC_LCD_ENABLE;
    ctx->io[0x41] = PPU_MODE_OAM;
    ctx->io[0x44] = 0;

    const uint8_t value = gb_read8(ctx, 0xFEA0);
    if (value != 0xFF) {
        fprintf(stderr, "unusable OAM read returned %02X expected FF\n", value);
        gb_context_destroy(ctx);
        return 1;
    }

    if (memcmp(ctx->oam, expected, OAM_SIZE) != 0) {
        for (size_t i = 0; i < OAM_SIZE; ++i) {
            if (ctx->oam[i] != expected[i]) {
                fprintf(stderr,
                        "unusable read OAM[%zu]=%02X expected %02X\n",
                        i, ctx->oam[i], expected[i]);
                break;
            }
        }
        gb_context_destroy(ctx);
        return 1;
    }

    gb_context_destroy(ctx);
    return 0;
}

static int test_blocked_unusable_oam_write_corrupts_current_scan_row(void) {
    GBContext* ctx = make_context(GB_MODEL_DMG);
    if (!ctx) {
        return 2;
    }

    for (size_t i = 0; i < OAM_SIZE; ++i) {
        ctx->oam[i] = (uint8_t)i;
    }

    uint8_t expected[OAM_SIZE];
    memcpy(expected, ctx->oam, sizeof(expected));
    apply_write_glitch(expected, 24);

    GBPPU* ppu = (GBPPU*)ctx->ppu;
    ppu->lcdc = LCDC_LCD_ENABLE;
    ppu->mode = PPU_MODE_OAM;
    ppu->visible_mode = PPU_MODE_OAM;
    ppu->stat_irq_mode = PPU_MODE_OAM;
    ppu->mode_cycles = 10;
    ppu->ly = 0;
    ctx->io[0x40] = LCDC_LCD_ENABLE;
    ctx->io[0x41] = PPU_MODE_OAM;
    ctx->io[0x44] = 0;

    gb_write8(ctx, 0xFEA0, 0xA5);

    if (memcmp(ctx->oam, expected, OAM_SIZE) != 0) {
        for (size_t i = 0; i < OAM_SIZE; ++i) {
            if (ctx->oam[i] != expected[i]) {
                fprintf(stderr,
                        "unusable write OAM[%zu]=%02X expected %02X\n",
                        i, ctx->oam[i], expected[i]);
                break;
            }
        }
        gb_context_destroy(ctx);
        return 1;
    }

    gb_context_destroy(ctx);
    return 0;
}

static int test_push_uses_predecrement_sp_for_oam_idu_write(void) {
    GBContext* ctx = make_context(GB_MODEL_DMG);
    if (!ctx) {
        return 2;
    }

    for (size_t i = 0; i < OAM_SIZE; ++i) {
        ctx->oam[i] = (uint8_t)i;
    }

    uint8_t expected[OAM_SIZE];
    memcpy(expected, ctx->oam, sizeof(expected));
    store_word(expected, 16,
               write_glitch(load_word(expected, 16),
                            load_word(expected, 8),
                            load_word(expected, 12)));
    memcpy(expected + 18, expected + 10, 6);

    GBPPU* ppu = (GBPPU*)ctx->ppu;
    ppu->lcdc = LCDC_LCD_ENABLE;
    ppu->mode = PPU_MODE_OAM;
    ppu->visible_mode = PPU_MODE_OAM;
    ppu->stat_irq_mode = PPU_MODE_OAM;
    ppu->mode_cycles = 2;
    ppu->ly = 0;
    ctx->io[0x40] = LCDC_LCD_ENABLE;
    ctx->io[0x41] = PPU_MODE_OAM;
    ctx->io[0x44] = 0;

    gb_write8(ctx, 0xC000, 0xC5); /* PUSH BC */
    ctx->pc = 0xC000;
    ctx->bc = 0x1234;
    ctx->sp = 0xFE00;
    const uint32_t start_cycles = ctx->cycles;

    if (gb_debug_step(ctx, GB_EXECUTION_INTERPRETER) != 16 ||
        ctx->cycles - start_cycles != 16 || ctx->sp != 0xFDFE ||
        gb_read8(ctx, 0xFDFE) != 0x34 || gb_read8(ctx, 0xFDFF) != 0x12) {
        fprintf(stderr,
                "PUSH BC result sp=%04X bytes=%02X%02X cycles=%u\n",
                ctx->sp, gb_read8(ctx, 0xFDFF), gb_read8(ctx, 0xFDFE),
                ctx->cycles - start_cycles);
        gb_context_destroy(ctx);
        return 1;
    }

    if (memcmp(ctx->oam, expected, OAM_SIZE) != 0) {
        for (size_t i = 0; i < OAM_SIZE; ++i) {
            if (ctx->oam[i] != expected[i]) {
                fprintf(stderr,
                        "PUSH BC OAM[%zu]=%02X expected %02X\n",
                        i, ctx->oam[i], expected[i]);
                break;
            }
        }
        gb_context_destroy(ctx);
        return 1;
    }

    gb_context_destroy(ctx);
    return 0;
}

static int test_pop_places_oam_reads_at_each_mcycle_start(void) {
    GBContext* ctx = make_context(GB_MODEL_DMG);
    if (!ctx) {
        return 2;
    }

    for (size_t i = 0; i < OAM_SIZE; ++i) {
        ctx->oam[i] = (uint8_t)i;
    }

    uint8_t expected[OAM_SIZE];
    memcpy(expected, ctx->oam, sizeof(expected));
    apply_combined_read_glitch(expected, 16);
    apply_combined_read_glitch(expected, 24);

    GBPPU* ppu = (GBPPU*)ctx->ppu;
    ppu->lcdc = LCDC_LCD_ENABLE;
    ppu->mode = PPU_MODE_OAM;
    ppu->visible_mode = PPU_MODE_OAM;
    ppu->stat_irq_mode = PPU_MODE_OAM;
    ppu->mode_cycles = 3;
    ppu->ly = 0;
    ctx->io[0x40] = LCDC_LCD_ENABLE;
    ctx->io[0x41] = PPU_MODE_OAM;
    ctx->io[0x44] = 0;

    gb_write8(ctx, 0xC000, 0xC1); /* POP BC */
    ctx->pc = 0xC000;
    ctx->sp = 0xFEF0;
    const uint32_t start_cycles = ctx->cycles;

    if (gb_debug_step(ctx, GB_EXECUTION_INTERPRETER) != 12 ||
        ctx->cycles - start_cycles != 12 || ctx->sp != 0xFEF2 ||
        ctx->bc != 0xFFFF || ppu->mode_cycles != 15) {
        fprintf(stderr,
                "POP BC result bc=%04X sp=%04X dot=%u cycles=%u\n",
                ctx->bc, ctx->sp, ppu->mode_cycles,
                ctx->cycles - start_cycles);
        gb_context_destroy(ctx);
        return 1;
    }

    if (memcmp(ctx->oam, expected, OAM_SIZE) != 0) {
        for (size_t i = 0; i < OAM_SIZE; ++i) {
            if (ctx->oam[i] != expected[i]) {
                fprintf(stderr,
                        "POP BC OAM[%zu]=%02X expected %02X\n",
                        i, ctx->oam[i], expected[i]);
                break;
            }
        }
        gb_context_destroy(ctx);
        return 1;
    }

    gb_context_destroy(ctx);
    return 0;
}

static int test_push_places_all_three_oam_writes_at_mcycle_start(void) {
    GBContext* ctx = make_context(GB_MODEL_DMG);
    if (!ctx) {
        return 2;
    }

    for (size_t i = 0; i < OAM_SIZE; ++i) {
        ctx->oam[i] = (uint8_t)i;
    }

    uint8_t expected[OAM_SIZE];
    memcpy(expected, ctx->oam, sizeof(expected));
    apply_write_glitch(expected, 16);
    apply_write_glitch(expected, 24);
    apply_write_glitch(expected, 32);

    GBPPU* ppu = (GBPPU*)ctx->ppu;
    ppu->lcdc = LCDC_LCD_ENABLE;
    ppu->mode = PPU_MODE_OAM;
    ppu->visible_mode = PPU_MODE_OAM;
    ppu->stat_irq_mode = PPU_MODE_OAM;
    ppu->mode_cycles = 1;
    ppu->ly = 0;
    ctx->io[0x40] = LCDC_LCD_ENABLE;
    ctx->io[0x41] = PPU_MODE_OAM;
    ctx->io[0x44] = 0;

    gb_write8(ctx, 0xC000, 0xC5); /* PUSH BC */
    ctx->pc = 0xC000;
    ctx->bc = 0x1234;
    ctx->sp = 0xFEF0;
    const uint32_t start_cycles = ctx->cycles;

    if (gb_debug_step(ctx, GB_EXECUTION_INTERPRETER) != 16 ||
        ctx->cycles - start_cycles != 16 || ctx->sp != 0xFEEE ||
        ppu->mode_cycles != 17) {
        fprintf(stderr,
                "OAM PUSH result sp=%04X dot=%u cycles=%u\n",
                ctx->sp, ppu->mode_cycles, ctx->cycles - start_cycles);
        gb_context_destroy(ctx);
        return 1;
    }

    if (memcmp(ctx->oam, expected, OAM_SIZE) != 0) {
        for (size_t i = 0; i < OAM_SIZE; ++i) {
            if (ctx->oam[i] != expected[i]) {
                fprintf(stderr,
                        "OAM PUSH OAM[%zu]=%02X expected %02X\n",
                        i, ctx->oam[i], expected[i]);
                break;
            }
        }
        gb_context_destroy(ctx);
        return 1;
    }

    gb_context_destroy(ctx);
    return 0;
}

static int test_ldi_read_uses_original_hl_at_data_mcycle_start(void) {
    GBContext* ctx = make_context(GB_MODEL_DMG);
    if (!ctx) {
        return 2;
    }

    for (size_t i = 0; i < OAM_SIZE; ++i) {
        ctx->oam[i] = (uint8_t)i;
    }

    uint8_t expected[OAM_SIZE];
    memcpy(expected, ctx->oam, sizeof(expected));
    apply_combined_read_glitch(expected, 16);

    GBPPU* ppu = (GBPPU*)ctx->ppu;
    ppu->lcdc = LCDC_LCD_ENABLE;
    ppu->mode = PPU_MODE_OAM;
    ppu->visible_mode = PPU_MODE_OAM;
    ppu->stat_irq_mode = PPU_MODE_OAM;
    ppu->mode_cycles = 1;
    ppu->ly = 0;
    ctx->io[0x40] = LCDC_LCD_ENABLE;
    ctx->io[0x41] = PPU_MODE_OAM;
    ctx->io[0x44] = 0;

    gb_write8(ctx, 0xC000, 0x2A); /* LD A,(HL+) */
    ctx->pc = 0xC000;
    ctx->hl = 0xFEF0;
    ctx->a = 0;
    const uint32_t start_cycles = ctx->cycles;

    if (gb_debug_step(ctx, GB_EXECUTION_INTERPRETER) != 8 ||
        ctx->cycles - start_cycles != 8 || ctx->hl != 0xFEF1 ||
        ctx->a != 0xFF || ppu->mode_cycles != 9) {
        fprintf(stderr,
                "LD A,(HL+) result a=%02X hl=%04X dot=%u cycles=%u\n",
                ctx->a, ctx->hl, ppu->mode_cycles,
                ctx->cycles - start_cycles);
        gb_context_destroy(ctx);
        return 1;
    }

    if (memcmp(ctx->oam, expected, OAM_SIZE) != 0) {
        for (size_t i = 0; i < OAM_SIZE; ++i) {
            if (ctx->oam[i] != expected[i]) {
                fprintf(stderr,
                        "LD A,(HL+) OAM[%zu]=%02X expected %02X\n",
                        i, ctx->oam[i], expected[i]);
                break;
            }
        }
        gb_context_destroy(ctx);
        return 1;
    }

    gb_context_destroy(ctx);
    return 0;
}

static int test_ldd_read_uses_original_hl_at_data_mcycle_start(void) {
    GBContext* ctx = make_context(GB_MODEL_DMG);
    if (!ctx) {
        return 2;
    }

    for (size_t i = 0; i < OAM_SIZE; ++i) {
        ctx->oam[i] = (uint8_t)i;
    }

    uint8_t expected[OAM_SIZE];
    memcpy(expected, ctx->oam, sizeof(expected));
    apply_combined_read_glitch(expected, 16);

    GBPPU* ppu = (GBPPU*)ctx->ppu;
    ppu->lcdc = LCDC_LCD_ENABLE;
    ppu->mode = PPU_MODE_OAM;
    ppu->visible_mode = PPU_MODE_OAM;
    ppu->stat_irq_mode = PPU_MODE_OAM;
    ppu->mode_cycles = 1;
    ppu->ly = 0;
    ctx->io[0x40] = LCDC_LCD_ENABLE;
    ctx->io[0x41] = PPU_MODE_OAM;
    ctx->io[0x44] = 0;

    gb_write8(ctx, 0xC000, 0x3A); /* LD A,(HL-) */
    ctx->pc = 0xC000;
    ctx->hl = 0xFEF0;
    ctx->a = 0;
    const uint32_t start_cycles = ctx->cycles;

    if (gb_debug_step(ctx, GB_EXECUTION_INTERPRETER) != 8 ||
        ctx->cycles - start_cycles != 8 || ctx->hl != 0xFEEF ||
        ctx->a != 0xFF || ppu->mode_cycles != 9) {
        fprintf(stderr,
                "LD A,(HL-) result a=%02X hl=%04X dot=%u cycles=%u\n",
                ctx->a, ctx->hl, ppu->mode_cycles,
                ctx->cycles - start_cycles);
        gb_context_destroy(ctx);
        return 1;
    }

    if (memcmp(ctx->oam, expected, OAM_SIZE) != 0) {
        for (size_t i = 0; i < OAM_SIZE; ++i) {
            if (ctx->oam[i] != expected[i]) {
                fprintf(stderr,
                        "LD A,(HL-) OAM[%zu]=%02X expected %02X\n",
                        i, ctx->oam[i], expected[i]);
                break;
            }
        }
        gb_context_destroy(ctx);
        return 1;
    }

    gb_context_destroy(ctx);
    return 0;
}

static int test_ldi_write_uses_original_hl_at_data_mcycle_start(void) {
    GBContext* ctx = make_context(GB_MODEL_DMG);
    if (!ctx) {
        return 2;
    }

    for (size_t i = 0; i < OAM_SIZE; ++i) {
        ctx->oam[i] = (uint8_t)i;
    }

    uint8_t expected[OAM_SIZE];
    memcpy(expected, ctx->oam, sizeof(expected));
    apply_write_glitch(expected, 16);

    GBPPU* ppu = (GBPPU*)ctx->ppu;
    ppu->lcdc = LCDC_LCD_ENABLE;
    ppu->mode = PPU_MODE_OAM;
    ppu->visible_mode = PPU_MODE_OAM;
    ppu->stat_irq_mode = PPU_MODE_OAM;
    ppu->mode_cycles = 1;
    ppu->ly = 0;
    ctx->io[0x40] = LCDC_LCD_ENABLE;
    ctx->io[0x41] = PPU_MODE_OAM;
    ctx->io[0x44] = 0;

    gb_write8(ctx, 0xC000, 0x22); /* LD (HL+),A */
    ctx->pc = 0xC000;
    ctx->hl = 0xFEF0;
    ctx->a = 0xA5;
    const uint32_t start_cycles = ctx->cycles;

    if (gb_debug_step(ctx, GB_EXECUTION_INTERPRETER) != 8 ||
        ctx->cycles - start_cycles != 8 || ctx->hl != 0xFEF1 ||
        ppu->mode_cycles != 9) {
        fprintf(stderr,
                "LD (HL+),A result hl=%04X dot=%u cycles=%u\n",
                ctx->hl, ppu->mode_cycles, ctx->cycles - start_cycles);
        gb_context_destroy(ctx);
        return 1;
    }

    if (memcmp(ctx->oam, expected, OAM_SIZE) != 0) {
        for (size_t i = 0; i < OAM_SIZE; ++i) {
            if (ctx->oam[i] != expected[i]) {
                fprintf(stderr,
                        "LD (HL+),A OAM[%zu]=%02X expected %02X\n",
                        i, ctx->oam[i], expected[i]);
                break;
            }
        }
        gb_context_destroy(ctx);
        return 1;
    }

    gb_context_destroy(ctx);
    return 0;
}

static int test_ldd_write_uses_original_hl_at_data_mcycle_start(void) {
    GBContext* ctx = make_context(GB_MODEL_DMG);
    if (!ctx) {
        return 2;
    }

    for (size_t i = 0; i < OAM_SIZE; ++i) {
        ctx->oam[i] = (uint8_t)i;
    }

    uint8_t expected[OAM_SIZE];
    memcpy(expected, ctx->oam, sizeof(expected));
    apply_write_glitch(expected, 16);

    GBPPU* ppu = (GBPPU*)ctx->ppu;
    ppu->lcdc = LCDC_LCD_ENABLE;
    ppu->mode = PPU_MODE_OAM;
    ppu->visible_mode = PPU_MODE_OAM;
    ppu->stat_irq_mode = PPU_MODE_OAM;
    ppu->mode_cycles = 1;
    ppu->ly = 0;
    ctx->io[0x40] = LCDC_LCD_ENABLE;
    ctx->io[0x41] = PPU_MODE_OAM;
    ctx->io[0x44] = 0;

    gb_write8(ctx, 0xC000, 0x32); /* LD (HL-),A */
    ctx->pc = 0xC000;
    ctx->hl = 0xFEF0;
    ctx->a = 0xA5;
    const uint32_t start_cycles = ctx->cycles;

    if (gb_debug_step(ctx, GB_EXECUTION_INTERPRETER) != 8 ||
        ctx->cycles - start_cycles != 8 || ctx->hl != 0xFEEF ||
        ppu->mode_cycles != 9) {
        fprintf(stderr,
                "LD (HL-),A result hl=%04X dot=%u cycles=%u\n",
                ctx->hl, ppu->mode_cycles, ctx->cycles - start_cycles);
        gb_context_destroy(ctx);
        return 1;
    }

    if (memcmp(ctx->oam, expected, OAM_SIZE) != 0) {
        for (size_t i = 0; i < OAM_SIZE; ++i) {
            if (ctx->oam[i] != expected[i]) {
                fprintf(stderr,
                        "LD (HL-),A OAM[%zu]=%02X expected %02X\n",
                        i, ctx->oam[i], expected[i]);
                break;
            }
        }
        gb_context_destroy(ctx);
        return 1;
    }

    gb_context_destroy(ctx);
    return 0;
}

static int test_cgb_hardware_does_not_apply_dmg_oam_corruption(void) {
    GBContext* ctx = make_context(GB_MODEL_CGB);
    if (!ctx) {
        return 2;
    }

    for (size_t i = 0; i < OAM_SIZE; ++i) {
        ctx->oam[i] = (uint8_t)i;
    }
    uint8_t expected[OAM_SIZE];
    memcpy(expected, ctx->oam, sizeof(expected));

    GBPPU* ppu = (GBPPU*)ctx->ppu;
    ppu->lcdc = LCDC_LCD_ENABLE;
    ppu->mode = PPU_MODE_OAM;
    ppu->visible_mode = PPU_MODE_OAM;
    ppu->stat_irq_mode = PPU_MODE_OAM;
    ppu->mode_cycles = 6;
    ppu->ly = 0;
    ctx->io[0x40] = LCDC_LCD_ENABLE;
    ctx->io[0x41] = PPU_MODE_OAM;
    ctx->io[0x44] = 0;

    ctx->de = 0xFE00;
    const uint32_t start_cycles = ctx->cycles;
    gbrt_timed_inc16(ctx, &ctx->de);

    if (ctx->de != 0xFE01 || ctx->cycles - start_cycles != 8 ||
        ppu->mode_cycles != 14 ||
        memcmp(ctx->oam, expected, OAM_SIZE) != 0) {
        fprintf(stderr,
                "CGB INC DE result de=%04X dot=%u cycles=%u corrupted=%d\n",
                ctx->de, ppu->mode_cycles, ctx->cycles - start_cycles,
                memcmp(ctx->oam, expected, OAM_SIZE) != 0);
        gb_context_destroy(ctx);
        return 1;
    }

    gb_context_destroy(ctx);
    return 0;
}

int main(void) {
    int result = test_inc_de_corrupts_scanned_row();
    if (result != 0) {
        return result;
    }
    result = test_dec_de_corrupts_from_predecrement_address();
    if (result != 0) {
        return result;
    }
    result = test_lcd_startup_hidden_scan_corrupts_second_oam_row();
    if (result != 0) {
        return result;
    }
    result = test_blocked_oam_write_corrupts_current_scan_row();
    if (result != 0) {
        return result;
    }
    result = test_blocked_oam_read_corrupts_current_scan_row();
    if (result != 0) {
        return result;
    }
    result = test_blocked_unusable_oam_read_corrupts_current_scan_row();
    if (result != 0) {
        return result;
    }
    result = test_blocked_unusable_oam_write_corrupts_current_scan_row();
    if (result != 0) {
        return result;
    }
    result = test_push_uses_predecrement_sp_for_oam_idu_write();
    if (result != 0) {
        return result;
    }
    result = test_pop_places_oam_reads_at_each_mcycle_start();
    if (result != 0) {
        return result;
    }
    result = test_push_places_all_three_oam_writes_at_mcycle_start();
    if (result != 0) {
        return result;
    }
    result = test_ldi_read_uses_original_hl_at_data_mcycle_start();
    if (result != 0) {
        return result;
    }
    result = test_ldd_read_uses_original_hl_at_data_mcycle_start();
    if (result != 0) {
        return result;
    }
    result = test_ldi_write_uses_original_hl_at_data_mcycle_start();
    if (result != 0) {
        return result;
    }
    result = test_ldd_write_uses_original_hl_at_data_mcycle_start();
    if (result != 0) {
        return result;
    }
    return test_cgb_hardware_does_not_apply_dmg_oam_corruption();
}
