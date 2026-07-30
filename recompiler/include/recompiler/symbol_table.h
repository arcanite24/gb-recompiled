#ifndef RECOMPILER_SYMBOL_TABLE_H
#define RECOMPILER_SYMBOL_TABLE_H

#include "analyzer.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace gbrecomp {

enum class SymbolType {
    FUNCTION,
    LABEL,
    DATA,
    UNKNOWN,
};

enum class SymbolMemorySpace {
    PHYSICAL_ROM,
    VRAM,
    EXTERNAL_RAM,
    WRAM,
    BANKED_WRAM,
    ECHO_RAM,
    OAM,
    UNUSABLE,
    MMIO,
    HRAM,
};

SymbolMemorySpace classify_symbol_memory_space(uint16_t address);
const char* symbol_memory_space_name(SymbolMemorySpace space);

struct Symbol {
    std::string source_name;
    std::vector<std::string> source_names;
    std::string c_name;
    uint32_t addr;
    SymbolType type;
    std::string provenance;
    uint32_t width = 1;
    std::string comment;
};

struct RGBDSConstant {
    std::string name;
    uint32_t value;
    std::string comment;
};

struct SymbolLoadStats {
    size_t address_records = 0;
    size_t unique_addresses = 0;
    size_t duplicate_address_records = 0;
    size_t constant_records = 0;
    size_t unique_constants = 0;
    size_t duplicate_constant_records = 0;
};

enum class SymbolAnalysisPolicy {
    INFER_BOUNDARIES,
    NAMES_ONLY,
};

class SymbolTable {
public:
    bool load_sym_file(const std::string& path,
                       const ROM* rom = nullptr,
                       std::string* error = nullptr);
    bool load_annotation_file(const std::string& path, std::string* error = nullptr);

    void clear();

    const Symbol* get_symbol(uint32_t addr) const;
    const Symbol* get_symbol(BankId bank, uint16_t addr) const;
    const RGBDSConstant* get_constant(const std::string& name) const;
    const std::unordered_map<uint32_t, Symbol>& symbols() const;
    const std::unordered_map<std::string, RGBDSConstant>& constants() const;
    const std::vector<AnalysisAnnotation>& annotations() const;
    const SymbolLoadStats& load_stats() const;

    bool has_symbol(uint32_t addr) const;
    size_t size() const;
    size_t constant_count() const;
    size_t annotation_count() const;

private:
    std::unordered_map<uint32_t, Symbol> symbols_;
    std::unordered_map<std::string, RGBDSConstant> constants_;
    std::vector<AnalysisAnnotation> annotations_;
    SymbolLoadStats load_stats_;
};

std::vector<AnalysisAnnotation> build_analysis_annotations(
    const SymbolTable& symbols,
    SymbolAnalysisPolicy policy = SymbolAnalysisPolicy::INFER_BOUNDARIES);
void apply_symbols_to_analysis(const SymbolTable& symbols, AnalysisResult& analysis);

} // namespace gbrecomp

#endif // RECOMPILER_SYMBOL_TABLE_H
