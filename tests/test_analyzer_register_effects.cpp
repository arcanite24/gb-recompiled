#include "recompiler/analyzer.h"
#include "recompiler/rom.h"

#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

static bool records_stale_bank_two(std::span<const uint8_t> body) {
    std::vector<uint8_t> bytes(32u * 1024u, 0);
    bytes[0x100] = 0xC3;
    bytes[0x101] = 0x50;
    bytes[0x102] = 0x01;
    bytes[0x147] = 0x01;
    bytes[0x148] = 0x00;
    for (size_t i = 0; i < body.size(); ++i) {
        bytes[0x150 + i] = body[i];
    }

    auto rom = gbrecomp::ROM::load_from_buffer(std::move(bytes), "register-effects");
    if (!rom || !rom->is_valid()) {
        return true;
    }

    gbrecomp::AnalyzerOptions options;
    options.aggressive_scan = false;
    options.analyze_all_banks = false;
    options.max_instructions = 100;
    const auto result = gbrecomp::analyze(*rom, options);
    for (const auto& bank_switch : result.bank_tracker.switches()) {
        if (bank_switch.target_bank == 2 && !bank_switch.is_dynamic) {
            return true;
        }
    }
    return false;
}

int main() {
    struct Case {
        std::string_view name;
        std::vector<uint8_t> body;
    };
    const std::vector<Case> cases = {
        {"LD A,(BC)", {0x3E, 0x02, 0x0A, 0xEA, 0x00, 0x20, 0xC9}},
        {"LD A,(nn)", {0x3E, 0x02, 0xFA, 0x00, 0xC0, 0xEA, 0x00, 0x20, 0xC9}},
        {"RLCA", {0x3E, 0x02, 0x07, 0xEA, 0x00, 0x20, 0xC9}},
        {"CB write", {0x06, 0x02, 0xCB, 0x00, 0x78, 0xEA, 0x00, 0x20, 0xC9}},
    };

    for (const Case& test_case : cases) {
        if (records_stale_bank_two(test_case.body)) {
            std::cerr << test_case.name
                      << " left a stale constant that invented bank 2\n";
            return 1;
        }
    }
    return 0;
}
