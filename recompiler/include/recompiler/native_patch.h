#ifndef RECOMPILER_NATIVE_PATCH_H
#define RECOMPILER_NATIVE_PATCH_H

#include "ir/ir.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gbrecomp {

struct NativePatchSource {
    std::string relative_path;
    std::string content;
    bool is_translation_unit = true;
};

struct NativePatchBinding {
    BankId bank = 0;
    uint16_t address = 0;
    std::string function_id;
    std::string pre_callback;
    std::string replace_callback;
    std::string post_callback;
    bool allow_return_stack_entry = false;
};

struct NativePatchPackage {
    bool enabled = false;
    std::string patch_id;
    std::string rom_sha256;
    size_t rom_size = 0;
    std::string manifest_json;
    bool host_configuration_enabled = false;
    std::string host_configuration_schema;
    uint32_t host_configuration_version = 0;
    std::string host_configuration_policy_id;
    uint32_t host_configuration_offset_limit = 0;
    uint32_t host_configuration_value_minimum = 0;
    uint32_t host_configuration_value_maximum = 0;
    std::vector<NativePatchSource> sources;
    std::vector<NativePatchBinding> bindings;
};

std::string sha256_hex(const uint8_t* data, size_t size);

std::string native_function_id(BankId bank, uint16_t address);

bool is_native_patchable_function(const ir::Program& program,
                                  const ir::Function& function,
                                  size_t rom_size);

bool load_native_patch_manifest(const std::filesystem::path& manifest_path,
                                const uint8_t* rom_data,
                                size_t rom_size,
                                NativePatchPackage& package,
                                std::string& error);

bool validate_native_patch_bindings(const NativePatchPackage& package,
                                    const ir::Program& program,
                                    std::string& error);

} // namespace gbrecomp

#endif
