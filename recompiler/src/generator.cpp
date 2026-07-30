/**
 * @file generator.cpp
 * @brief Orchestrates the code generation process
 */

#include "recompiler/rom.h"
#include "recompiler/decoder.h"
#include "recompiler/analyzer.h"
#include "recompiler/ir/ir.h"
#include "recompiler/ir/ir_builder.h"
#include "recompiler/codegen/c_emitter.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <vector>

namespace gbrecomp {

namespace {

std::vector<uint16_t> find_oam_dma_overlay_destinations(const std::vector<uint8_t>& data,
                                                        uint16_t source_addr,
                                                        uint16_t overlay_size) {
    std::vector<uint16_t> destinations;
    if (overlay_size == 0 || overlay_size > 0xFF || data.size() < 14) {
        return destinations;
    }

    for (size_t i = 0; i + 13 < data.size(); ++i) {
        if (data[i] != 0x0E ||       // LD C,imm8
            data[i + 2] != 0x06 ||   // LD B,imm8
            data[i + 4] != 0x21 ||   // LD HL,imm16
            data[i + 7] != 0x2A ||   // LDI A,(HL)
            data[i + 8] != 0xE2 ||   // LD (C),A
            data[i + 9] != 0x0C ||   // INC C
            data[i + 10] != 0x05 ||  // DEC B
            data[i + 11] != 0x20 ||  // JR NZ,rel8
            data[i + 12] != 0xFA ||  // rel8 = -6
            data[i + 13] != 0xC9) {  // RET
            continue;
        }

        uint8_t count = data[i + 3];
        uint16_t copied_source = static_cast<uint16_t>(data[i + 5] | (data[i + 6] << 8));
        if (count != overlay_size || copied_source != source_addr) {
            continue;
        }

        destinations.push_back(static_cast<uint16_t>(0xFF00 | data[i + 1]));
    }

    std::sort(destinations.begin(), destinations.end());
    destinations.erase(std::unique(destinations.begin(), destinations.end()), destinations.end());
    return destinations;
}

std::vector<AnalyzerOptions::RamOverlay> detect_oam_dma_overlays(const ROM& rom) {
    const std::vector<uint8_t>& data = rom.bytes();
    const std::vector<uint8_t> wait_pattern = {0xE0, 0x46, 0x3E, 0x28, 0x3D, 0x20, 0xFD, 0xC9};
    std::vector<AnalyzerOptions::RamOverlay> overlays;

    auto it = std::search(data.begin(), data.end(), wait_pattern.begin(), wait_pattern.end());
    if (it == data.end()) {
        return overlays;
    }

    size_t rom_idx = static_cast<size_t>(std::distance(data.begin(), it));
    size_t overlay_start = rom_idx;
    uint16_t overlay_size = static_cast<uint16_t>(wait_pattern.size());

    if (rom_idx >= 2 && data[rom_idx - 2] == 0x3E) {
        overlay_start = rom_idx - 2;
        overlay_size = static_cast<uint16_t>(overlay_size + 2);
    }

    BankId bank = static_cast<BankId>(overlay_start / 0x4000);
    uint16_t offset = static_cast<uint16_t>(overlay_start % 0x4000);
    if (bank > 0) {
        offset = static_cast<uint16_t>(offset + 0x4000);
    }

    std::vector<uint16_t> destinations = find_oam_dma_overlay_destinations(data, offset, overlay_size);
    if (destinations.empty()) {
        destinations.push_back(0xFF80);
    }

    for (uint16_t ram_addr : destinations) {
        AnalyzerOptions::RamOverlay overlay;
        overlay.ram_addr = ram_addr;
        overlay.rom_addr = AnalysisResult::make_addr(bank, offset);
        overlay.size = overlay_size;
        overlays.push_back(overlay);
    }

    return overlays;
}

void append_codegen_ram_overlays(const ROM& rom,
                                 const std::vector<AnalyzerOptions::RamOverlay>& overlays,
                                 codegen::GeneratorOptions& emit_config) {
    const uint8_t* rom_bytes = rom.data();
    for (const auto& overlay : overlays) {
        BankId bank = static_cast<BankId>(overlay.rom_addr >> 16);
        uint16_t offset = static_cast<uint16_t>(overlay.rom_addr & 0xFFFF);
        size_t rom_idx = (offset < 0x4000)
            ? offset
            : static_cast<size_t>(bank) * 0x4000 + (offset - 0x4000);
        if (rom_idx >= rom.size()) {
            continue;
        }

        size_t available = std::min<size_t>(overlay.size, rom.size() - rom_idx);
        if (available == 0) {
            continue;
        }

        codegen::GeneratorOptions::RamOverlay codegen_overlay;
        codegen_overlay.ram_addr = overlay.ram_addr;
        codegen_overlay.bytes.assign(rom_bytes + rom_idx, rom_bytes + rom_idx + available);
        emit_config.ram_overlays.push_back(std::move(codegen_overlay));
    }
}

} // namespace

/**
 * @brief Code generator orchestrator
 * 
 * Coordinates the full recompilation pipeline:
 * 1. ROM loading and parsing
 * 2. Instruction decoding
 * 3. Control flow analysis
 * 4. IR generation
 * 5. IR optimization (optional)
 * 6. C code emission
 */
class Generator {
public:
    struct Options {
        bool verbose;
        bool emit_comments;
        bool optimize;
        bool generate_dispatch;
        std::string output_prefix;
        
        Options() : verbose(false), emit_comments(true), optimize(true), 
                    generate_dispatch(true), output_prefix("game") {}
    };
    
    explicit Generator(const Options& opts) : opts_(opts) {}
    Generator() : opts_() {}
    
    /**
     * @brief Generate code from a ROM file
     */
    bool generate(const std::filesystem::path& rom_path,
                  const std::filesystem::path& output_dir) {
        // Load ROM
        if (opts_.verbose) {
            std::cout << "Loading ROM: " << rom_path << "\n";
        }
        
        auto rom_opt = ROM::load(rom_path);
        if (!rom_opt || !rom_opt->is_valid()) {
            std::cerr << "Error: Failed to load ROM: " 
                      << (rom_opt ? rom_opt->error() : "unknown error") << "\n";
            return false;
        }
        
        const ROM& rom = *rom_opt;
        
        if (opts_.verbose) {
            print_rom_info(rom);
        }
        
        // Create decoder
        Decoder decoder(rom);
        
        // Analyze control flow
        if (opts_.verbose) {
            std::cout << "\nAnalyzing control flow...\n";
        }
        
        AnalyzerOptions analyzer_opts;
        // analyzer_opts.verbose = opts_.verbose; // analyzer options doesn't have verbose
        analyzer_opts.trace_log = false; // can be enabled if needed

        const std::vector<AnalyzerOptions::RamOverlay> dma_overlays =
            detect_oam_dma_overlays(rom);
        for (const auto& overlay : dma_overlays) {
             if (opts_.verbose) {
                 BankId bank = static_cast<BankId>(overlay.rom_addr >> 16);
                 uint16_t offset = static_cast<uint16_t>(overlay.rom_addr & 0xFFFF);
                 size_t rom_idx = (offset < 0x4000)
                     ? offset
                     : static_cast<size_t>(bank) * 0x4000 + (offset - 0x4000);
                 std::cout << "Detected OAM DMA routine at ROM 0x" << std::hex << rom_idx
                           << " (Bank " << (int)bank << ":0x" << offset << ", size "
                           << std::dec << overlay.size << "). Mapping to HRAM "
                           << std::hex << std::showbase << overlay.ram_addr
                           << std::noshowbase << std::dec << ".\n";
             }

             analyzer_opts.ram_overlays.push_back(overlay);
        }

        // Use free function analyze() instead of Analyzer class
        AnalysisResult analysis = analyze(rom, analyzer_opts);
        
        if (opts_.verbose) {
            print_analysis_summary(analysis);
        }
        
        // Build IR
        if (opts_.verbose) {
            std::cout << "\nBuilding IR...\n";
        }
        
        ir::IRBuilder builder;
        ir::Program program = builder.build(analysis, rom.name());
        
        if (opts_.verbose) {
            std::cout << "Generated IR for " << program.functions.size() << " functions\n";
        }
        
        // Generate C code
        if (opts_.verbose) {
            std::cout << "\nGenerating C code...\n";
        }
        
        codegen::GeneratorOptions emit_config;
        emit_config.emit_address_comments = opts_.emit_comments;
        emit_config.emit_comments = true;
        emit_config.output_dir = output_dir.string();
        emit_config.generate_bank_dispatch = opts_.generate_dispatch;
        append_codegen_ram_overlays(rom, analyzer_opts.ram_overlays, emit_config);

        codegen::GeneratedOutput output = codegen::generate_output(
            program, rom.data(), rom.size(), emit_config);
        
        // Write output files
        if (!std::filesystem::exists(output_dir)) {
            std::filesystem::create_directories(output_dir);
        }
        
        if (!codegen::write_output(output, output_dir.string())) {
            std::cerr << "Error: Failed to write output files\n";
            return false;
        }
        
        if (opts_.verbose) {
            std::cout << "Output written to: " << output_dir << "\n";
            std::cout << "  - " << output.header_file << "\n";
            std::cout << "  - " << output.source_file << "\n";
            std::cout << "  - " << output.rom_data_file << "\n";
        }
        
        return true;
    }
    
private:
    Options opts_;
};

} // namespace gbrecomp
