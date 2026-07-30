#include "recompiler/analyzer.h"
#include "recompiler/codegen/c_emitter.h"
#include "recompiler/ir/ir_builder.h"
#include "recompiler/rom.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

int main() {
    std::vector<uint8_t> bytes(1024u * 1024u, 0);
    bytes[0x100] = 0xC3;
    bytes[0x101] = 0x50;
    bytes[0x102] = 0x01;
    bytes[0x147] = 0x01;
    bytes[0x148] = 0x05;

    // Enter MBC1 advanced mode, select high register 1, then jump into the
    // lower CPU window. Physical bank 32 contains different code at $0200.
    const uint8_t entry[] = {
        0x3E, 0x01,
        0xEA, 0x00, 0x60,
        0x3E, 0x01,
        0xEA, 0x00, 0x40,
        0xC3, 0x00, 0x02,
    };
    for (size_t i = 0; i < sizeof(entry); ++i) {
        bytes[0x150 + i] = entry[i];
    }
    bytes[(32u * 0x4000u) + 0x0200u] = 0xC9; // RET

    auto rom = gbrecomp::ROM::load_from_buffer(std::move(bytes), "mbc1-lower-code");
    if (!rom || !rom->is_valid()) {
        return 2;
    }

    gbrecomp::AnalyzerOptions options;
    options.aggressive_scan = false;
    options.analyze_all_banks = false;
    options.max_instructions = 100;
    const auto result = gbrecomp::analyze(*rom, options);
    const auto* instruction = result.get_instruction(32, 0x0200);
    if (!instruction || instruction->opcode != 0xC9) {
        std::cerr << "MBC1 lower-window code was not analyzed as physical bank 32\n";
        return 1;
    }

    const gbrecomp::ir::Program program =
        gbrecomp::ir::IRBuilder().build(result, "mbc1-lower-code");
    gbrecomp::codegen::GeneratorOptions generator_options;
    generator_options.output_prefix = "mbc1_lower_code";
    const auto generated = gbrecomp::codegen::generate_output(
        program,
        rom->data(),
        rom->size(),
        generator_options);
    std::string generated_sources = generated.source_content;
    for (const auto& extra : generated.extra_files) {
        if (extra.is_source) {
            generated_sources += extra.content;
        }
    }
    if (generated_sources.find("case 32") == std::string::npos ||
        generated_sources.find("gb_resolve_rom_bank(ctx, addr)") == std::string::npos) {
        std::cerr << "generated dispatch cannot select compiled lower bank 32\n";
        return 1;
    }
    return 0;
}
