#include "recompiler/codegen/c_emitter.h"
#include "recompiler/ir/ir.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

int main() {
    gbrecomp::ir::Program program;
    program.rom_name = "mbc5-codegen";
    program.mbc_type = 0x19;
    program.rom_bank_count = 512;

    gbrecomp::ir::BasicBlock block;
    block.id = 0;
    block.label = "block_100_4000";
    block.bank = 256;
    block.start_address = 0x4000;
    block.end_address = 0x4001;
    block.is_entry = true;
    block.instructions.push_back(
        gbrecomp::ir::IRInstruction::make_ret(256, 0x4000)
    );
    program.blocks.emplace(block.id, block);

    gbrecomp::ir::Function function;
    function.name = "func_100_4000";
    function.bank = 256;
    function.entry_address = 0x4000;
    function.block_ids.push_back(block.id);
    function.is_entry_point = true;
    program.functions.emplace(function.name, function);

    std::vector<uint8_t> rom(0x150, 0);
    rom[0x147] = 0x19;
    gbrecomp::codegen::GeneratorOptions options;
    options.output_prefix = "mbc5_codegen";
    const auto output = gbrecomp::codegen::generate_output(
        program, rom.data(), rom.size(), options
    );

    std::string generated = output.source_content;
    for (const auto& extra : output.extra_files) {
        generated += extra.content;
    }
    if (generated.find("uint16_t bank") == std::string::npos ||
        generated.find("gb_resolve_rom_bank(ctx, addr)") == std::string::npos ||
        generated.find("case 256") == std::string::npos ||
        generated.find("\"bank\": 256") == std::string::npos) {
        std::cerr << "generated dispatch or metadata truncated MBC5 bank 256\n";
        return 1;
    }
    return 0;
}
