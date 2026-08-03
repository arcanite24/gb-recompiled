#include "recompiler/analyzer.h"
#include "recompiler/rom.h"

#include <cstdint>
#include <iostream>
#include <vector>

int main() {
    std::vector<uint8_t> bytes(32u * 1024u, 0);
    bytes[0x147] = 0x01;
    bytes[0x148] = 0x00;

    bytes[0x100] = 0xC3;  // JP $0150 (past the cartridge header)
    bytes[0x101] = 0x50;
    bytes[0x102] = 0x01;

    // LD A,2; CALL $0200; LD ($2000),A; RET
    const uint8_t caller[] = {
        0x3E, 0x02,
        0xCD, 0x00, 0x02,
        0xEA, 0x00, 0x20,
        0xC9,
    };
    for (size_t i = 0; i < sizeof(caller); ++i) {
        bytes[0x150 + i] = caller[i];
    }
    // The callee overwrites A. Analysis must not carry the caller's A=2
    // through the return without an interprocedural summary.
    bytes[0x200] = 0x3E;
    bytes[0x201] = 0x03;
    bytes[0x202] = 0xC9;

    auto rom = gbrecomp::ROM::load_from_buffer(std::move(bytes), "call-clobber");
    if (!rom || !rom->is_valid()) {
        std::cerr << "failed to load call-clobber fixture\n";
        return 2;
    }

    gbrecomp::AnalyzerOptions options;
    options.aggressive_scan = false;
    options.analyze_all_banks = false;
    options.max_instructions = 100;
    const auto result = gbrecomp::analyze(*rom, options);

    for (const auto& bank_switch : result.bank_tracker.switches()) {
        if ((bank_switch.addr & 0xFFFFu) == 0x0155 &&
            bank_switch.target_bank == 2 &&
            !bank_switch.is_dynamic) {
            std::cerr << "CALL preserved stale A=2 into the mapper write\n";
            return 1;
        }
    }
    return 0;
}
