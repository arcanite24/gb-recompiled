// Home-bank code calling into the switchable ROM window must be bound to
// the bank the code actually selected, or left for the runtime dispatcher
// when no bank switch is visible. Regression test for the Link's Awakening
// door bug: `ld hl,$2100 / ld [hl],$02 / call $5FD1` was bound to bank 1.
#include "recompiler/analyzer.h"
#include "recompiler/rom.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

struct Fixture {
    std::string name;
    std::vector<uint8_t> body;      // placed at 0x0150
    gbrecomp::BankId expected_bank; // resolved bank of the CALL at call_addr
    uint16_t call_addr;
};

int run(const Fixture& fixture) {
    constexpr size_t kBanks = 4;
    std::vector<uint8_t> bytes(kBanks * 0x4000, 0);
    bytes[0x100] = 0xC3;  // JP $0150
    bytes[0x101] = 0x50;
    bytes[0x102] = 0x01;
    bytes[0x147] = 0x01;  // MBC1
    bytes[0x148] = 0x01;  // 64 KiB (4 banks)
    for (size_t i = 0; i < fixture.body.size(); ++i) {
        bytes[0x150 + i] = fixture.body[i];
    }
    // RST $38 vector -> $0200: LD A,A x5 / CALL $5FD1 (at $0205) / RET.
    bytes[0x38] = 0xC3;
    bytes[0x39] = 0x00;
    bytes[0x3A] = 0x02;
    const uint8_t vector_body[] = {0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0xCD, 0xD1, 0x5F, 0xC9};
    for (size_t i = 0; i < sizeof(vector_body); ++i) {
        bytes[0x200 + i] = vector_body[i];
    }
    // Plausible callee at $5FD1 in every switchable bank: LD A,$01 / RET.
    for (size_t bank = 1; bank < kBanks; ++bank) {
        const size_t base = bank * 0x4000 + 0x1FD1;
        bytes[base] = 0x3E;
        bytes[base + 1] = 0x01;
        bytes[base + 2] = 0xC9;
    }

    auto rom = gbrecomp::ROM::load_from_buffer(std::move(bytes), fixture.name);
    if (!rom || !rom->is_valid()) {
        std::cerr << fixture.name << ": failed to load fixture\n";
        return 2;
    }

    gbrecomp::AnalyzerOptions options;
    options.aggressive_scan = false;
    options.analyze_all_banks = false;
    options.max_instructions = 100;
    const auto result = gbrecomp::analyze(*rom, options);

    const gbrecomp::Instruction* call = result.get_instruction(0, fixture.call_addr);
    if (call == nullptr || !call->is_call) {
        std::cerr << fixture.name << ": CALL at " << std::hex << fixture.call_addr
                  << " was not analyzed" << std::dec << "\n";
        return 1;
    }
    if (call->resolved_target_bank != fixture.expected_bank) {
        std::cerr << fixture.name << ": CALL at " << std::hex << fixture.call_addr
                  << " resolved to bank " << call->resolved_target_bank
                  << ", expected " << fixture.expected_bank << std::dec << "\n";
        return 1;
    }
    return 0;
}

}  // namespace

int main() {
    const std::vector<Fixture> fixtures = {
        {
            "ld-hl-n-selects-bank-2",
            {
                0x21, 0x00, 0x21,  // LD HL,$2100
                0x36, 0x02,        // LD (HL),$02
                0xCD, 0xD1, 0x5F,  // CALL $5FD1  (at $0155)
                0xC9,
            },
            2,
            0x0155,
        },
        {
            "ld-nn-a-selects-bank-3",
            {
                0x3E, 0x03,        // LD A,$03
                0xEA, 0x00, 0x21,  // LD ($2100),A
                0xCD, 0xD1, 0x5F,  // CALL $5FD1  (at $0155)
                0xC9,
            },
            3,
            0x0155,
        },
        {
            // Reached through the RST $38 vector, which can run with any
            // bank mapped: no visible switch means the dispatcher decides.
            "no-visible-switch-is-dynamic",
            {
                0xC9,              // entry returns immediately
            },
            gbrecomp::UNKNOWN_BANK,
            0x0205,
        },
    };

    for (const Fixture& fixture : fixtures) {
        if (int rc = run(fixture)) {
            return rc;
        }
    }
    std::cout << "home-bank call resolution: ok\n";
    return 0;
}
