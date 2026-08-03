/**
 * @file rom.h
 * @brief GameBoy ROM loader and header parser
 * 
 * Handles loading ROM files, parsing the cartridge header,
 * detecting MBC type, and validating ROM structure.
 */

#ifndef RECOMPILER_ROM_H
#define RECOMPILER_ROM_H

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <filesystem>

namespace gbrecomp {

/** Physical ROM bank identifier. MBC5 exposes 9 bits (0x000-0x1FF). */
using BankId = uint16_t;
constexpr BankId UNKNOWN_BANK = UINT16_MAX;

/* ============================================================================
 * MBC Types
 * ========================================================================== */

enum class MBCType : uint8_t {
    NONE = 0x00,           // 32KB ROM only
    MBC1 = 0x01,           // MBC1
    MBC1_RAM = 0x02,       // MBC1 + RAM
    MBC1_RAM_BATTERY = 0x03,
    MBC2 = 0x05,
    MBC2_BATTERY = 0x06,
    ROM_RAM = 0x08,
    ROM_RAM_BATTERY = 0x09,
    MMM01 = 0x0B,
    MMM01_RAM = 0x0C,
    MMM01_RAM_BATTERY = 0x0D,
    MBC3_TIMER_BATTERY = 0x0F,
    MBC3_TIMER_RAM_BATTERY = 0x10,
    MBC3 = 0x11,
    MBC3_RAM = 0x12,
    MBC3_RAM_BATTERY = 0x13,
    MBC5 = 0x19,
    MBC5_RAM = 0x1A,
    MBC5_RAM_BATTERY = 0x1B,
    MBC5_RUMBLE = 0x1C,
    MBC5_RUMBLE_RAM = 0x1D,
    MBC5_RUMBLE_RAM_BATTERY = 0x1E,
    MBC6 = 0x20,
    MBC7_SENSOR_RUMBLE_RAM_BATTERY = 0x22,
    POCKET_CAMERA = 0xFC,
    BANDAI_TAMA5 = 0xFD,
    HUC3 = 0xFE,
    HUC1_RAM_BATTERY = 0xFF,
    
    UNKNOWN = 0xFE
};

/**
 * @brief Get human-readable name for MBC type
 */
const char* mbc_type_name(MBCType type);

/**
 * @brief Check if MBC type has RAM
 */
bool mbc_has_ram(MBCType type);

/**
 * @brief Check if MBC type has battery backup
 */
bool mbc_has_battery(MBCType type);

/**
 * @brief Check if MBC type has RTC (real-time clock)
 */
bool mbc_has_rtc(MBCType type);

/* ============================================================================
 * ROM Header Structure
 * ========================================================================== */

/**
 * @brief Parsed ROM header information
 * 
 * GameBoy cartridge header is located at 0x0100-0x014F
 */
struct ROMHeader {
    // 0x0100-0x0103: Entry point (usually NOP; JP 0x0150)
    uint8_t entry_point[4];
    
    // 0x0104-0x0133: Nintendo logo (48 bytes)
    uint8_t nintendo_logo[48];
    
    // 0x0134-0x0143: Title (up to 16 chars, or 11 for CGB)
    std::string title;
    
    // 0x013F-0x0142: Manufacturer code (newer games)
    std::string manufacturer_code;
    
    // 0x0143: CGB flag
    uint8_t cgb_flag;
    
    // 0x0144-0x0145: New licensee code
    std::string new_licensee_code;
    
    // 0x0146: SGB flag
    uint8_t sgb_flag;
    
    // 0x0147: Cartridge type (MBC)
    MBCType mbc_type;
    
    // 0x0148: ROM size code
    uint8_t rom_size_code;
    
    // 0x0149: RAM size code
    uint8_t ram_size_code;
    
    // 0x014A: Destination code (0=Japan, 1=Overseas)
    uint8_t destination_code;
    
    // 0x014B: Old licensee code
    uint8_t old_licensee_code;
    
    // 0x014C: ROM version
    uint8_t rom_version;
    
    // 0x014D: Header checksum
    uint8_t header_checksum;
    
    // 0x014E-0x014F: Global checksum
    uint16_t global_checksum;
    
    // Computed values
    size_t rom_size_bytes;       // Actual ROM size
    size_t ram_size_bytes;       // External RAM size
    uint16_t rom_banks;          // Number of ROM banks
    uint8_t ram_banks;           // Number of RAM banks
    
    bool is_cgb;                 // CGB enhanced or only
    bool is_cgb_only;            // CGB only (won't run on DMG)
    bool is_sgb;                 // SGB enhanced
    
    bool header_checksum_valid;
    bool global_checksum_valid;
    bool logo_valid;
};

/* ============================================================================
 * ROM Class
 * ========================================================================== */

/**
 * @brief Loaded ROM with parsed header
 */
class ROM {
public:
    /**
     * @brief Load ROM from file
     * @param path Path to ROM file
     * @return Loaded ROM or nullopt on failure
     */
    static std::optional<ROM> load(const std::filesystem::path& path);
    
    /**
     * @brief Load ROM from memory buffer
     * @param data ROM data
     * @param name ROM name (for display)
     * @return Loaded ROM or nullopt on failure
     */
    static std::optional<ROM> load_from_buffer(std::vector<uint8_t> data,
                                                const std::string& name);
    
    // Access ROM data
    const uint8_t* data() const { return data_.data(); }
    size_t size() const { return data_.size(); }
    const std::vector<uint8_t>& bytes() const { return data_; }
    
    // Read at address (with optional bank for banked addresses)
    uint8_t read(uint16_t addr) const;
    uint8_t read_banked(BankId bank, uint16_t addr) const;
    
    // Access header
    const ROMHeader& header() const { return header_; }
    
    // ROM info
    const std::string& name() const { return name_; }
    const std::filesystem::path& path() const { return path_; }
    
    // Validation
    bool is_valid() const { return valid_; }
    const std::string& error() const { return error_; }
    
    // Helper methods
    bool has_banking() const { return header_.rom_banks > 2; }
    bool has_ram() const { return header_.ram_size_bytes > 0; }
    uint16_t bank_count() const { return header_.rom_banks; }

private:
    ROM() = default;
    
    bool parse_header();
    bool validate();
    
    std::vector<uint8_t> data_;
    ROMHeader header_;
    std::string name_;
    std::filesystem::path path_;
    bool valid_ = false;
    std::string error_;
};

/* ============================================================================
 * Utility Functions
 * ========================================================================== */

/**
 * @brief Validate ROM file without fully loading
 */
bool validate_rom_file(const std::filesystem::path& path, std::string& error);

/**
 * @brief Print ROM info to stdout
 */
void print_rom_info(const ROM& rom);

} // namespace gbrecomp

#endif // RECOMPILER_ROM_H
