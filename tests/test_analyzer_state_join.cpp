#include "recompiler/analyzer.h"
#include "recompiler/rom.h"

#include <cstdint>
#include <iostream>
#include <vector>

int main() {
    std::vector<uint8_t> bytes(32u * 1024u, 0);
    bytes[0x100] = 0xC3; // JP $0150
    bytes[0x101] = 0x50;
    bytes[0x102] = 0x01;
    bytes[0x147] = 0x01; // MBC1
    bytes[0x148] = 0x00;

    // JR Z,path_b; LD A,2; JR join; path_b: LD A,3;
    // join: LD ($2000),A; RET
    //
    // Both paths are reachable because the analyzer does not know Z. The
    // mapper write must therefore see A as unknown after the control-flow
    // join, independent of work-queue order.
    const uint8_t body[] = {
        0x28, 0x04,
        0x3E, 0x02,
        0x18, 0x02,
        0x3E, 0x03,
        0xEA, 0x00, 0x20,
        0xC9,
    };
    for (size_t i = 0; i < sizeof(body); ++i) {
        bytes[0x150 + i] = body[i];
    }

    auto rom = gbrecomp::ROM::load_from_buffer(std::move(bytes), "state-join");
    if (!rom || !rom->is_valid()) {
        std::cerr << "failed to load state-join fixture\n";
        return 2;
    }

    gbrecomp::AnalyzerOptions options;
    options.aggressive_scan = false;
    options.analyze_all_banks = false;
    options.max_instructions = 100;
    const auto result = gbrecomp::analyze(*rom, options);

    size_t join_switches = 0;
    for (const auto& bank_switch : result.bank_tracker.switches()) {
        if ((bank_switch.addr & 0xFFFFu) != 0x0158) {
            continue;
        }
        ++join_switches;
        if (!bank_switch.is_dynamic ||
            bank_switch.target_bank != gbrecomp::UNKNOWN_BANK) {
            std::cerr << "control-flow join retained one path's bank "
                      << bank_switch.target_bank << "\n";
            return 1;
        }
    }

    if (join_switches != 1) {
        std::cerr << "expected one joined mapper-write record, got "
                  << join_switches << "\n";
        return 1;
    }
    return 0;
}
