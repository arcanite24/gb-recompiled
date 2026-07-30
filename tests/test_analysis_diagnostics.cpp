#include "recompiler/analyzer.h"
#include "recompiler/codegen/c_emitter.h"
#include "recompiler/ir/ir_builder.h"
#include "recompiler/rom.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

const gbrecomp::AnalysisDiagnostic* find_diagnostic(
    const gbrecomp::AnalysisResult& result,
    const std::string& kind,
    uint16_t address) {
    for (const auto& [id, diagnostic] : result.analysis_diagnostics) {
        (void)id;
        if (diagnostic.kind == kind && diagnostic.address == address) {
            return &diagnostic;
        }
    }
    return nullptr;
}

} // namespace

int main() {
    std::vector<uint8_t> bytes(32u * 1024u, 0);
    bytes[0x147] = 0x01;
    bytes[0x148] = 0x00;

    bytes[0x100] = 0xC3; // JP $0150
    bytes[0x101] = 0x50;
    bytes[0x102] = 0x01;
    bytes[0x150] = 0xE9; // JP HL, intentionally unresolved
    bytes[0x200] = 0xD3; // undefined SM83 opcode
    bytes[0x300] = 0xC9; // manual route/fallback recovery entry
    const uint8_t scan_candidate[] = {
        0x04, 0x80, 0xA9, 0x15, 0x1C, 0x2C, 0x3D, 0xAF,
        0x0C, 0x81, 0xAA, 0xC9, 0x14, 0x82, 0xAB, 0x1D,
        0x24, 0x83, 0xAC, 0x2D, 0x34, 0x84, 0xAD, 0x3C,
        0x05, 0x85, 0xAE, 0x0D, 0x1C, 0x86, 0xA8, 0x15,
        0x2C, 0x87, 0xA9, 0x25, 0x3C, 0x80, 0xAA, 0x35,
        0x04, 0x81, 0xAB, 0x0C, 0x14, 0x82, 0xAC, 0x1D,
    };
    for (size_t i = 0; i < sizeof(scan_candidate); ++i) {
        bytes[0x400 + i] = scan_candidate[i];
    }
    bytes[0x500] = 0xC9; // source for copied-HRAM overlay

    auto rom =
        gbrecomp::ROM::load_from_buffer(std::move(bytes), "analysis-diagnostics");
    if (!rom || !rom->is_valid()) {
        std::cerr << "failed to load analysis diagnostic fixture\n";
        return 2;
    }

    gbrecomp::AnalyzerOptions options;
    options.analyze_all_banks = false;
    options.aggressive_scan = true;
    options.max_instructions = 100;
    options.entry_points = {0x00000300u, 0x00000200u};
    options.ram_overlays.push_back({0xFF80, 0x00000500u, 1});
    const auto result = gbrecomp::analyze(*rom, options);

    const auto* indirect =
        find_diagnostic(result, "unresolved_indirect_jump", 0x0150);
    const auto* undefined =
        find_diagnostic(result, "undefined_instruction", 0x0200);
    const auto* manual =
        find_diagnostic(result, "manual_entry_point", 0x0300);
    const auto* scanned =
        find_diagnostic(result, "data_as_code_candidate", 0x0407);
    const auto* overlay =
        find_diagnostic(result, "ram_overlay", 0xFF80);

    if (!indirect || indirect->status != "unresolved" ||
        indirect->relationship != "potential_dispatch_fallback" ||
        indirect->suggested_annotation.empty()) {
        std::cerr << "unresolved JP HL diagnostic is not actionable\n";
        return 1;
    }
    if (!undefined || undefined->evidence.find("opcode") == std::string::npos ||
        undefined->suggested_annotation.empty()) {
        std::cerr << "undefined-instruction diagnostic is not actionable\n";
        return 1;
    }
    if (!manual || manual->status != "configured" ||
        manual->relationship != "resolved_fallback_entry_point") {
        std::cerr << "manual fallback relationship was not exported\n";
        return 1;
    }
    if (!scanned || scanned->status != "candidate" ||
        scanned->suggested_annotation.empty()) {
        std::cerr << "data-as-code diagnostic was not exported\n";
        return 1;
    }
    if (!overlay || overlay->memory_space != "hram" ||
        !overlay->has_related_address ||
        overlay->related_address != 0x0500 ||
        overlay->related_memory_space != "physical_rom") {
        std::cerr << "RAM overlay relationship was not exported\n";
        return 1;
    }

    const auto program = gbrecomp::ir::IRBuilder().build(
        result, "analysis-diagnostics");
    if (program.analysis_diagnostics.size() !=
        result.analysis_diagnostics.size()) {
        std::cerr << "analysis diagnostics were lost before code generation\n";
        return 1;
    }

    gbrecomp::codegen::GeneratorOptions generator_options;
    generator_options.output_prefix = "analysis_diagnostics";
    const auto generated = gbrecomp::codegen::generate_output(
        program, rom->data(), rom->size(), generator_options);
    const std::string* metadata = nullptr;
    for (const auto& file : generated.extra_files) {
        if (file.filename == "analysis_diagnostics_metadata.json") {
            metadata = &file.content;
            break;
        }
    }
    if (!metadata ||
        metadata->find("\"analysis_diagnostics\": [") == std::string::npos ||
        metadata->find("\"kind\": \"unresolved_indirect_jump\"") ==
            std::string::npos ||
        metadata->find("\"relationship\": \"copied_from_physical_rom\"") ==
            std::string::npos ||
        metadata->find("\"suggested_annotation\":") == std::string::npos) {
        std::cerr << "generated metadata omitted actionable diagnostics\n";
        return 1;
    }

    return 0;
}
