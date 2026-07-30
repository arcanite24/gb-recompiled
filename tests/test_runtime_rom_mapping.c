#include "gbrt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t nintendo_logo[48] = {
    0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B,
    0x03, 0x73, 0x00, 0x83, 0x00, 0x0C, 0x00, 0x0D,
    0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E,
    0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99,
    0xBB, 0xBB, 0x67, 0x63, 0x6E, 0x0E, 0xEC, 0xCC,
    0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E,
};

int main(void) {
    const size_t rom_size = 2u * 1024u * 1024u;
    uint8_t* rom = (uint8_t*)calloc(rom_size, 1);
    if (!rom) {
        fputs("failed to allocate MBC1 fixture\n", stderr);
        return 2;
    }
    for (size_t bank = 0; bank < rom_size / 0x4000u; ++bank) {
        rom[bank * 0x4000u] = (uint8_t)bank;
    }

    GBContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.rom = rom;
    ctx.rom_size = rom_size;
    ctx.mbc_type = 0x01;
    ctx.rom_bank = 1;

    // Change the secondary register after mode 1 is already active. Both ROM
    // windows must derive their physical bank from the same live registers.
    gb_write8(&ctx, 0x6000, 1);
    gb_write8(&ctx, 0x4000, 1);

    const uint8_t lower = gb_read8(&ctx, 0x0000);
    const uint8_t upper = gb_read8(&ctx, 0x4000);
    const uint16_t lower_bank = gb_resolve_rom_bank(&ctx, 0x0000);
    const uint16_t upper_bank = gb_resolve_rom_bank(&ctx, 0x4000);
    free(rom);

    if (lower != 32 || upper != 33 || lower_bank != 32 || upper_bank != 33) {
        fprintf(stderr,
                "MBC1 mode-1 mapping disagreed: lower=%u upper=%u banks=%u/%u\n",
                lower,
                upper,
                lower_bank,
                upper_bank);
        return 1;
    }

    const size_t multicart_size = 1024u * 1024u;
    uint8_t* multicart_rom = (uint8_t*)calloc(multicart_size, 1);
    if (!multicart_rom) {
        return 2;
    }
    for (size_t bank = 0; bank < multicart_size / 0x4000u; ++bank) {
        multicart_rom[bank * 0x4000u] = (uint8_t)bank;
    }
    multicart_rom[0x147] = 0x01;
    multicart_rom[0x148] = 0x05;
    memcpy(multicart_rom + (0x10u * 0x4000u) + 0x0104u,
           nintendo_logo,
           sizeof(nintendo_logo));

    GBContext multicart;
    memset(&multicart, 0, sizeof(multicart));
    if (!gb_context_load_rom(&multicart, multicart_rom, multicart_size)) {
        free(multicart_rom);
        return 2;
    }
    free(multicart_rom);
    multicart.rom_bank = 1;
    multicart.rom_bank_low = 1;
    multicart.rom_bank_upper = 0;
    multicart.mbc_mode = 0;
    gb_write8(&multicart, 0x6000, 1);
    gb_write8(&multicart, 0x4000, 1);

    const uint8_t multicart_lower = gb_read8(&multicart, 0x0000);
    const uint8_t multicart_upper = gb_read8(&multicart, 0x4000);
    if (!multicart.mbc1_multicart ||
        multicart_lower != 16 || multicart_upper != 17 ||
        gb_resolve_rom_bank(&multicart, 0x0000) != 16 ||
        gb_resolve_rom_bank(&multicart, 0x4000) != 17) {
        fprintf(stderr,
                "MBC1M mapping disagreed: detected=%u lower=%u upper=%u\n",
                multicart.mbc1_multicart,
                multicart_lower,
                multicart_upper);
        free(multicart.rom);
        return 1;
    }
    free(multicart.rom);
    return 0;
}
