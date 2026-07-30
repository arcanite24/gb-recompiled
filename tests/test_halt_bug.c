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

static int test_immediate_opcode_is_duplicated(void) {
    GBContext* ctx = make_context();
    if (!ctx) {
        return 2;
    }

    /* Pan Docs: with IME clear and IE & IF non-zero, HALT does not sleep.
     * Instead, the next opcode fetch omits its normal PC increment. For a
     * multi-byte instruction this makes the opcode byte become its operand. */
    gb_write8(ctx, 0xC001, 0x3E); /* LD A,n */
    gb_write8(ctx, 0xC002, 0x12);
    ctx->pc = 0xC001;
    ctx->ime = 0;
    ctx->io[0x80] = 0x01;
    ctx->io[0x0F] = 0x01;

    const uint32_t halt_start = ctx->cycles;
    gbrt_execute_halt(ctx, 0xC001, 4);
    if (ctx->cycles - halt_start != 4 || ctx->pc != 0xC001 ||
        ctx->halted || !ctx->halt_bug) {
        fprintf(stderr,
                "HALT bug entry pc=%04X halted=%u bug=%u cycles=%u\n",
                ctx->pc, ctx->halted, ctx->halt_bug,
                ctx->cycles - halt_start);
        gb_context_destroy(ctx);
        return 1;
    }

    const uint32_t instruction_start = ctx->cycles;
    if (gb_debug_step(ctx, GB_EXECUTION_INTERPRETER) != 8 ||
        ctx->cycles - instruction_start != 8 || ctx->pc != 0xC002 ||
        ctx->a != 0x3E || ctx->halt_bug) {
        fprintf(stderr,
                "HALT bug fetch pc=%04X a=%02X bug=%u cycles=%u\n",
                ctx->pc, ctx->a, ctx->halt_bug,
                ctx->cycles - instruction_start);
        gb_context_destroy(ctx);
        return 1;
    }

    gb_context_destroy(ctx);
    return 0;
}

static int test_halt_opcode_rearms_bug(void) {
    GBContext* ctx = make_context();
    if (!ctx) {
        return 2;
    }

    gb_write8(ctx, 0xC010, 0x76); /* HALT */
    ctx->ime = 0;
    ctx->io[0x80] = 0x01;
    ctx->io[0x0F] = 0x01;
    gbrt_execute_halt(ctx, 0xC010, 4);

    if (gb_debug_step(ctx, GB_EXECUTION_INTERPRETER) != 4 ||
        ctx->pc != 0xC010 || ctx->halted || !ctx->halt_bug) {
        fprintf(stderr,
                "repeated HALT did not rearm bug pc=%04X halted=%u bug=%u\n",
                ctx->pc, ctx->halted, ctx->halt_bug);
        gb_context_destroy(ctx);
        return 1;
    }

    /* Once the pending condition disappears, the duplicated HALT is still
     * fetched without incrementing PC but now enters the normal halt state. */
    ctx->io[0x0F] = 0;
    if (gb_debug_step(ctx, GB_EXECUTION_INTERPRETER) != 4 ||
        ctx->pc != 0xC010 || !ctx->halted || ctx->halt_bug) {
        fprintf(stderr,
                "repeated HALT did not settle pc=%04X halted=%u bug=%u\n",
                ctx->pc, ctx->halted, ctx->halt_bug);
        gb_context_destroy(ctx);
        return 1;
    }

    gb_context_destroy(ctx);
    return 0;
}

static int test_ime_clear_halt_wakes_without_dispatch(void) {
    GBContext* ctx = make_context();
    if (!ctx) {
        return 2;
    }

    gb_write8(ctx, 0xC021, 0x00); /* NOP */
    ctx->ime = 0;
    ctx->io[0x80] = 0x01;
    ctx->io[0x0F] = 0;
    gbrt_execute_halt(ctx, 0xC021, 4);
    if (!ctx->halted || ctx->halt_bug || ctx->pc != 0xC021) {
        fputs("IME-clear HALT did not enter the normal halt state\n", stderr);
        gb_context_destroy(ctx);
        return 1;
    }

    ctx->io[0x0F] = 0x01;
    if (gb_debug_step(ctx, GB_EXECUTION_INTERPRETER) != 4 ||
        ctx->pc != 0xC022 || ctx->halted || ctx->halt_bug ||
        (ctx->io[0x0F] & 0x1F) != 0x01) {
        fprintf(stderr,
                "IME-clear wake dispatched IRQ pc=%04X if=%02X halted=%u bug=%u\n",
                ctx->pc, ctx->io[0x0F], ctx->halted, ctx->halt_bug);
        gb_context_destroy(ctx);
        return 1;
    }

    gb_context_destroy(ctx);
    return 0;
}

static int test_halt_bug_rst_pushes_rst_address(void) {
    GBContext* ctx = make_context();
    if (!ctx) {
        return 2;
    }

    gb_write8(ctx, 0xC030, 0xC7); /* RST $00 */
    ctx->sp = 0xD002;
    ctx->ime = 0;
    ctx->io[0x80] = 0x01;
    ctx->io[0x0F] = 0x01;
    gbrt_execute_halt(ctx, 0xC030, 4);

    if (gb_debug_step(ctx, GB_EXECUTION_INTERPRETER) != 16 ||
        ctx->pc != 0x0000 || ctx->sp != 0xD000 || ctx->halt_bug ||
        ctx->wram[0x1001] != 0xC0 || ctx->wram[0x1000] != 0x30) {
        fprintf(stderr,
                "HALT-bug RST pc=%04X sp=%04X return=%02X%02X bug=%u\n",
                ctx->pc, ctx->sp, ctx->wram[0x1001], ctx->wram[0x1000],
                ctx->halt_bug);
        gb_context_destroy(ctx);
        return 1;
    }

    gb_context_destroy(ctx);
    return 0;
}

static int test_ime_set_halt_dispatches_from_next_pc(void) {
    GBContext* ctx = make_context();
    if (!ctx) {
        return 2;
    }

    ctx->pc = 0xC041;
    ctx->sp = 0xD002;
    ctx->ime = 1;
    ctx->io[0x80] = 0x01;
    ctx->io[0x0F] = 0;
    gbrt_execute_halt(ctx, 0xC041, 4);
    if (!ctx->halted || ctx->halt_bug) {
        fputs("IME-set HALT did not enter the normal halt state\n", stderr);
        gb_context_destroy(ctx);
        return 1;
    }

    ctx->io[0x0F] = 0x01;
    const uint32_t interrupt_start = ctx->cycles;
    gb_handle_interrupts(ctx);
    if (ctx->cycles - interrupt_start != 20 || ctx->pc != 0x0040 ||
        ctx->sp != 0xD000 || ctx->halted || ctx->ime ||
        ctx->wram[0x1001] != 0xC0 || ctx->wram[0x1000] != 0x41 ||
        (ctx->io[0x0F] & 0x1F)) {
        fprintf(stderr,
                "HALT interrupt pc=%04X sp=%04X return=%02X%02X ime=%u if=%02X cycles=%u\n",
                ctx->pc, ctx->sp, ctx->wram[0x1001], ctx->wram[0x1000],
                ctx->ime, ctx->io[0x0F], ctx->cycles - interrupt_start);
        gb_context_destroy(ctx);
        return 1;
    }

    gb_context_destroy(ctx);
    return 0;
}

int main(void) {
    if (test_immediate_opcode_is_duplicated() ||
        test_halt_opcode_rearms_bug() ||
        test_ime_clear_halt_wakes_without_dispatch() ||
        test_halt_bug_rst_pushes_rst_address() ||
        test_ime_set_halt_dispatches_from_next_pc()) {
        return 1;
    }
    return 0;
}
