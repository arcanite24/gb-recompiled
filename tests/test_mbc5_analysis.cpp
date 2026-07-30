#include "recompiler/analyzer.h"
#include "recompiler/ir/ir_builder.h"
#include "recompiler/rom.h"

#include <cstdint>
#include <iostream>
#include <vector>

int main() {
    std::vector<uint8_t> bytes(8u * 1024u * 1024u, 0);
    bytes[0x100] = 0xC9;
    bytes[0x147] = 0x19;
    bytes[0x148] = 0x08;

    const size_t bank_256_offset = 256u * 0x4000u;
    const size_t bank_511_offset = 511u * 0x4000u;
    bytes[bank_256_offset + 0] = 0x3E;
    bytes[bank_256_offset + 1] = 0xA5;
    bytes[bank_256_offset + 2] = 0xC9;
    bytes[bank_511_offset + 0] = 0x3E;
    bytes[bank_511_offset + 1] = 0x5A;
    bytes[bank_511_offset + 2] = 0xC9;

    auto rom = gbrecomp::ROM::load_from_buffer(std::move(bytes), "mbc5-analysis");
    if (!rom || !rom->is_valid()) {
        std::cerr << "failed to load synthetic MBC5 ROM\n";
        return 2;
    }

    gbrecomp::AnalyzerOptions options;
    options.aggressive_scan = false;
    options.max_instructions = 100;
    options.entry_points = {
        gbrecomp::AnalysisResult::make_addr(256, 0x4000),
        gbrecomp::AnalysisResult::make_addr(511, 0x4000),
    };

    const gbrecomp::AnalysisResult result = gbrecomp::analyze(*rom, options);
    const auto* bank_256 = result.get_instruction(256, 0x4000);
    const auto* bank_511 = result.get_instruction(511, 0x4000);
    if (!bank_256 || !bank_511 || bank_256->bank != 256 || bank_511->bank != 511) {
        std::cerr << "analyzer did not preserve MBC5 high-bank identity\n";
        return 1;
    }

    const gbrecomp::ir::Program program =
        gbrecomp::ir::IRBuilder().build(result, "mbc5-analysis");
    bool found_bank_256 = false;
    bool found_bank_511 = false;
    for (const auto& [name, function] : program.functions) {
        (void)name;
        found_bank_256 = found_bank_256 || function.bank == 256;
        found_bank_511 = found_bank_511 || function.bank == 511;
    }
    if (!found_bank_256 || !found_bank_511) {
        std::cerr << "IR did not preserve MBC5 high-bank identity\n";
        return 1;
    }
    return 0;
}
