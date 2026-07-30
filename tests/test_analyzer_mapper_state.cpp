#include "recompiler/analyzer.h"
#include "recompiler/rom.h"

#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

struct ExpectedSwitch {
    uint16_t addr;
    gbrecomp::BankId bank;
};

static int check_fixture(std::string_view name,
                         uint8_t mapper,
                         uint8_t size_code,
                         size_t byte_size,
                         std::span<const uint8_t> body,
                         std::span<const ExpectedSwitch> expected) {
    std::vector<uint8_t> bytes(byte_size, 0);
    bytes[0x100] = 0xC3;
    bytes[0x101] = 0x50;
    bytes[0x102] = 0x01;
    bytes[0x147] = mapper;
    bytes[0x148] = size_code;
    for (size_t i = 0; i < body.size(); ++i) {
        bytes[0x150 + i] = body[i];
    }

    auto rom = gbrecomp::ROM::load_from_buffer(std::move(bytes), std::string(name));
    if (!rom || !rom->is_valid()) {
        std::cerr << name << ": failed to load fixture\n";
        return 2;
    }

    gbrecomp::AnalyzerOptions options;
    options.aggressive_scan = false;
    options.analyze_all_banks = false;
    options.max_instructions = 100;
    const auto result = gbrecomp::analyze(*rom, options);

    for (const ExpectedSwitch& wanted : expected) {
        bool found = false;
        for (const auto& actual : result.bank_tracker.switches()) {
            if ((actual.addr & 0xFFFFu) != wanted.addr) {
                continue;
            }
            found = true;
            if (actual.is_dynamic || actual.target_bank != wanted.bank) {
                std::cerr << name << ": mapper write at " << std::hex
                          << wanted.addr << " selected " << actual.target_bank
                          << " instead of " << wanted.bank << std::dec << "\n";
                return 1;
            }
        }
        if (!found) {
            std::cerr << name << ": mapper write at " << std::hex
                      << wanted.addr << " was not recognized" << std::dec << "\n";
            return 1;
        }
    }
    return 0;
}

int main() {
    const std::vector<uint8_t> mbc1 = {
        0x3E, 0x02,             // LD A,2
        0xEA, 0x00, 0x40,       // secondary register = 2
        0x3E, 0x00,             // LD A,0
        0xEA, 0x00, 0x20,       // low register = 0, treated as 1
        0xC9,
    };
    const std::vector<ExpectedSwitch> mbc1_expected = {
        {0x0152, 0x41},
        {0x0157, 0x41},
    };
    if (int rc = check_fixture("mbc1-state", 0x01, 0x06, 2u * 1024u * 1024u,
                               mbc1, mbc1_expected)) {
        return rc;
    }

    const std::vector<uint8_t> mbc3 = {
        0x3E, 0xFF,
        0xEA, 0x00, 0x20,
        0xC9,
    };
    const std::vector<ExpectedSwitch> mbc3_expected = {{0x0152, 0x7F}};
    if (int rc = check_fixture("mbc3-state", 0x11, 0x06, 2u * 1024u * 1024u,
                               mbc3, mbc3_expected)) {
        return rc;
    }

    const std::vector<uint8_t> mbc5 = {
        0x3E, 0x00,             // LD A,0: MBC5 bank zero is legal
        0xEA, 0x00, 0x20,       // low eight bits = 0
        0x21, 0x00, 0x30,       // LD HL,$3000
        0x3E, 0x01,
        0x77,                   // LD (HL),A: ninth bit = 1
        0xC9,
    };
    const std::vector<ExpectedSwitch> mbc5_expected = {
        {0x0152, 0x000},
        {0x015A, 0x100},
    };
    return check_fixture("mbc5-state", 0x19, 0x08, 8u * 1024u * 1024u,
                         mbc5, mbc5_expected);
}
