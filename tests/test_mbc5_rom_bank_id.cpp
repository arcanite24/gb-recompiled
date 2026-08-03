#include "recompiler/rom.h"

#include <cstdint>
#include <iostream>
#include <vector>

int main() {
    std::vector<uint8_t> bytes(8u * 1024u * 1024u, 0);
    bytes[0x147] = 0x19;  // MBC5
    bytes[0x148] = 0x08;  // 8 MiB / 512 banks
    bytes[0x4000] = 0x11;
    bytes[(256u * 0x4000u)] = 0xA5;
    bytes[(511u * 0x4000u)] = 0x5A;

    auto rom = gbrecomp::ROM::load_from_buffer(std::move(bytes), "mbc5-bank-id");
    if (!rom || !rom->is_valid()) {
        std::cerr << "failed to load synthetic 8 MiB MBC5 ROM\n";
        return 2;
    }

    const uint8_t bank_256 = rom->read_banked(256, 0x4000);
    const uint8_t bank_511 = rom->read_banked(511, 0x4000);
    if (bank_256 != 0xA5 || bank_511 != 0x5A) {
        std::cerr << "MBC5 bank IDs were truncated: bank 256=0x" << std::hex
                  << static_cast<unsigned>(bank_256) << ", bank 511=0x"
                  << static_cast<unsigned>(bank_511) << "\n";
        return 1;
    }
    return 0;
}
