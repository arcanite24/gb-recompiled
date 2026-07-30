#include "gbrt.h"

#include <stdio.h>
#include <string.h>

static GBContext* make_context(void) {
    GBConfig config;
    memset(&config, 0, sizeof(config));
    config.model = GB_MODEL_DMG;

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

static int dispatch(GBContext* ctx,
                    uint16_t pc,
                    uint16_t sp,
                    uint8_t ie,
                    uint8_t interrupt_flags,
                    uint16_t expected_pc,
                    uint8_t expected_ie,
                    uint8_t expected_if) {
    ctx->pc = pc;
    ctx->sp = sp;
    ctx->ime = 1;
    ctx->io[0x80] = ie;
    ctx->io[0x0F] = interrupt_flags;
    const uint32_t start_cycles = ctx->cycles;
    gb_handle_interrupts(ctx);
    if (ctx->cycles - start_cycles != 20 || ctx->pc != expected_pc ||
        ctx->io[0x80] != expected_ie ||
        (ctx->io[0x0F] & 0x1F) != expected_if || ctx->ime != 0) {
        fprintf(stderr,
                "interrupt entry pc=%04X ie=%02X if=%02X sp=%04X cycles=%u\n",
                ctx->pc, ctx->io[0x80], ctx->io[0x0F], ctx->sp,
                ctx->cycles - start_cycles);
        return 1;
    }
    return 0;
}

int main(void) {
    GBContext* ctx = make_context();
    if (!ctx) {
        return 2;
    }

    gb_write8(ctx, 0xFF0F, 0x08);
    if (gb_read8(ctx, 0xFF0F) != 0xE8 || ctx->io[0x0F] != 0x08) {
        fputs("IF did not preserve five writable bits and three high read bits\n",
              stderr);
        gb_context_destroy(ctx);
        return 1;
    }

    /* Upper PC byte writes $02 to IE and cancels a timer dispatch. */
    if (dispatch(ctx, 0x0235, 0x0000, 0x04, 0x04, 0x0000, 0x02, 0x04)) {
        fputs("IE upper-byte push did not cancel interrupt entry\n", stderr);
        gb_context_destroy(ctx);
        return 1;
    }

    /* Lower PC byte writes IE too late to cancel the selected serial source. */
    if (dispatch(ctx, 0x0235, 0x0001, 0x08, 0x08, 0x0058, 0x35, 0x00)) {
        fputs("IE lower-byte push incorrectly cancelled interrupt entry\n", stderr);
        gb_context_destroy(ctx);
        return 1;
    }

    /* Upper push leaves only STAT enabled, so it wins over VBlank. */
    if (dispatch(ctx, 0x0235, 0x0000, 0x03, 0x03, 0x0048, 0x02, 0x01)) {
        fputs("IE upper-byte push did not reprioritize interrupt entry\n", stderr);
        gb_context_destroy(ctx);
        return 1;
    }

    /* Normal entry still pushes PC and takes exactly five M-cycles. */
    ctx->sp = 0xD002;
    if (dispatch(ctx, 0x1234, ctx->sp, 0x04, 0x04, 0x0050, 0x04, 0x00) ||
        ctx->sp != 0xD000 || ctx->wram[0x1001] != 0x12 ||
        ctx->wram[0x1000] != 0x34) {
        fputs("normal interrupt entry did not push PC in five M-cycles\n", stderr);
        gb_context_destroy(ctx);
        return 1;
    }

    /* Odd bus-phase splits must retain their half system cycle in CGB double
     * speed instead of rounding both calls down independently. */
    ctx->cgb_double_speed = 1;
    ctx->cgb_system_cycle_remainder = 0;
    ctx->ime = 0;
    ctx->io[0x0F] = 0;
    ctx->io[0x80] = 0;
    ctx->stopped = 0;
    uint32_t double_speed_start = ctx->cycles;
    gb_tick(ctx, 15);
    if (ctx->cycles - double_speed_start != 7 ||
        ctx->cgb_system_cycle_remainder != 1) {
        fputs("CGB double-speed lost the first half system cycle\n", stderr);
        gb_context_destroy(ctx);
        return 1;
    }
    gb_tick(ctx, 1);
    if (ctx->cycles - double_speed_start != 8 ||
        ctx->cgb_system_cycle_remainder != 0) {
        fputs("CGB double-speed did not carry an odd bus phase\n", stderr);
        gb_context_destroy(ctx);
        return 1;
    }

    /* A final one-T CPU phase can still represent no whole system cycle. The
     * scheduler must nevertheless observe an IRQ at the instruction boundary. */
    ctx->cgb_system_cycle_remainder = 0;
    ctx->ime = 1;
    ctx->io[0x0F] = 0x08;
    ctx->io[0x80] = 0x08;
    ctx->stopped = 0;
    double_speed_start = ctx->cycles;
    gb_tick(ctx, 1);
    if (!ctx->stopped || ctx->cycles != double_speed_start ||
        ctx->cgb_system_cycle_remainder != 1) {
        fputs("CGB double-speed missed IRQ on a zero-system-cycle boundary\n",
              stderr);
        gb_context_destroy(ctx);
        return 1;
    }

    /* RETI must expose an already-pending lower-priority source before the
     * restored-PC instruction can run. */
    ctx->cgb_double_speed = 0;
    ctx->cgb_system_cycle_remainder = 0;
    ctx->sp = 0xD000;
    ctx->wram[0x1000] = 0x78;
    ctx->wram[0x1001] = 0x56;
    ctx->ime = 0;
    ctx->io[0x0F] = 0x08;
    ctx->io[0x80] = 0x08;
    ctx->stopped = 0;
    uint32_t reti_start = ctx->cycles;
    gbrt_timed_reti(ctx);
    if (ctx->pc != 0x5678 || ctx->sp != 0xD002 || !ctx->ime ||
        !ctx->stopped || ctx->cycles - reti_start != 16) {
        fputs("RETI did not expose a pending interrupt at its boundary\n",
              stderr);
        gb_context_destroy(ctx);
        return 1;
    }

    gb_context_destroy(ctx);
    return 0;
}
