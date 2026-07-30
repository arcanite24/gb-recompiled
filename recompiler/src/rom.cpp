/**
 * @file rom.cpp
 * @brief GameBoy ROM loader implementation
 */

#include "recompiler/rom.h"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <algorithm>

namespace gbrecomp {

/* ============================================================================
 * MBC Type Helpers
 * ========================================================================== */

const char* mbc_type_name(MBCType type) {
    switch (type) {
        case MBCType::NONE: return "ROM ONLY";
        case MBCType::MBC1: return "MBC1";
        case MBCType::MBC1_RAM: return "MBC1+RAM";
        case MBCType::MBC1_RAM_BATTERY: return "MBC1+RAM+BATTERY";
        case MBCType::MBC2: return "MBC2";
        case MBCType::MBC2_BATTERY: return "MBC2+BATTERY";
        case MBCType::ROM_RAM: return "ROM+RAM";
        case MBCType::ROM_RAM_BATTERY: return "ROM+RAM+BATTERY";
        case MBCType::MMM01: return "MMM01";
        case MBCType::MMM01_RAM: return "MMM01+RAM";
        case MBCType::MMM01_RAM_BATTERY: return "MMM01+RAM+BATTERY";
        case MBCType::MBC3_TIMER_BATTERY: return "MBC3+TIMER+BATTERY";
        case MBCType::MBC3_TIMER_RAM_BATTERY: return "MBC3+TIMER+RAM+BATTERY";
        case MBCType::MBC3: return "MBC3";
        case MBCType::MBC3_RAM: return "MBC3+RAM";
        case MBCType::MBC3_RAM_BATTERY: return "MBC3+RAM+BATTERY";
        case MBCType::MBC5: return "MBC5";
        case MBCType::MBC5_RAM: return "MBC5+RAM";
        case MBCType::MBC5_RAM_BATTERY: return "MBC5+RAM+BATTERY";
        case MBCType::MBC5_RUMBLE: return "MBC5+RUMBLE";
        case MBCType::MBC5_RUMBLE_RAM: return "MBC5+RUMBLE+RAM";
        case MBCType::MBC5_RUMBLE_RAM_BATTERY: return "MBC5+RUMBLE+RAM+BATTERY";
        case MBCType::MBC6: return "MBC6";
        case MBCType::MBC7_SENSOR_RUMBLE_RAM_BATTERY: return "MBC7+SENSOR+RUMBLE+RAM+BATTERY";
        case MBCType::POCKET_CAMERA: return "POCKET CAMERA";
        case MBCType::BANDAI_TAMA5: return "BANDAI TAMA5";
        case MBCType::HUC3: return "HuC3";
        case MBCType::HUC1_RAM_BATTERY: return "HuC1+RAM+BATTERY";
        default: return "UNKNOWN";
    }
}

bool mbc_has_ram(MBCType type) {
    switch (type) {
        case MBCType::MBC1_RAM:
        case MBCType::MBC1_RAM_BATTERY:
        case MBCType::MBC2:
        case MBCType::MBC2_BATTERY:
        case MBCType::ROM_RAM:
        case MBCType::ROM_RAM_BATTERY:
        case MBCType::MMM01_RAM:
        case MBCType::MMM01_RAM_BATTERY:
        case MBCType::MBC3_TIMER_RAM_BATTERY:
        case MBCType::MBC3_RAM:
        case MBCType::MBC3_RAM_BATTERY:
        case MBCType::MBC5_RAM:
        case MBCType::MBC5_RAM_BATTERY:
        case MBCType::MBC5_RUMBLE_RAM:
        case MBCType::MBC5_RUMBLE_RAM_BATTERY:
        case MBCType::MBC7_SENSOR_RUMBLE_RAM_BATTERY:
        case MBCType::HUC1_RAM_BATTERY:
            return true;
        default:
            return false;
    }
}

bool mbc_has_battery(MBCType type) {
    switch (type) {
        case MBCType::MBC1_RAM_BATTERY:
        case MBCType::MBC2_BATTERY:
        case MBCType::ROM_RAM_BATTERY:
        case MBCType::MMM01_RAM_BATTERY:
        case MBCType::MBC3_TIMER_BATTERY:
        case MBCType::MBC3_TIMER_RAM_BATTERY:
        case MBCType::MBC3_RAM_BATTERY:
        case MBCType::MBC5_RAM_BATTERY:
        case MBCType::MBC5_RUMBLE_RAM_BATTERY:
        case MBCType::MBC7_SENSOR_RUMBLE_RAM_BATTERY:
        case MBCType::HUC1_RAM_BATTERY:
            return true;
        default:
            return false;
    }
}

bool mbc_has_rtc(MBCType type) {
    switch (type) {
        case MBCType::MBC3_TIMER_BATTERY:
        case MBCType::MBC3_TIMER_RAM_BATTERY:
            return true;
        default:
            return false;
    }
}

/* ============================================================================
 * ROM Size Tables
 * ========================================================================== */

static size_t rom_size_from_code(uint8_t code) {
    switch (code) {
        case 0x00: return 32 * 1024;      // 32 KB
        case 0x01: return 64 * 1024;      // 64 KB
        case 0x02: return 128 * 1024;     // 128 KB
        case 0x03: return 256 * 1024;     // 256 KB
        case 0x04: return 512 * 1024;     // 512 KB
        case 0x05: return 1024 * 1024;    // 1 MB
        case 0x06: return 2 * 1024 * 1024; // 2 MB
        case 0x07: return 4 * 1024 * 1024; // 4 MB
        case 0x08: return 8 * 1024 * 1024; // 8 MB
        // Unofficial sizes
        case 0x52: return 1179648;        // 1.1 MB
        case 0x53: return 1310720;        // 1.2 MB
        case 0x54: return 1572864;        // 1.5 MB
        default: return 0;
    }
}

static size_t ram_size_from_code(uint8_t code) {
    switch (code) {
        case 0x00: return 0;
        case 0x01: return 2 * 1024;       // 2 KB (unused)
        case 0x02: return 8 * 1024;       // 8 KB
        case 0x03: return 32 * 1024;      // 32 KB
        case 0x04: return 128 * 1024;     // 128 KB
        case 0x05: return 64 * 1024;      // 64 KB
        default: return 0;
    }
}

/* ============================================================================
 * Nintendo Logo (for validation)
 * ========================================================================== */

static const uint8_t NINTENDO_LOGO[48] = {
    0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B,
    0x03, 0x73, 0x00, 0x83, 0x00, 0x0C, 0x00, 0x0D,
    0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E,
    0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99,
    0xBB, 0xBB, 0x67, 0x63, 0x6E, 0x0E, 0xEC, 0xCC,
    0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E
};

/* ============================================================================
 * ROM Implementation
 * ========================================================================== */

std::optional<ROM> ROM::load(const std::filesystem::path& path) {
    ROM rom;
    rom.path_ = path;
    rom.name_ = path.stem().string();
    
    // Open file
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        rom.error_ = "Failed to open file";
        return rom;
    }
    
    // Get file size
    auto file_size = file.tellg();
    if (file_size < 0x150) {
        rom.error_ = "File too small to be a valid ROM";
        return rom;
    }
    
    // Read file
    rom.data_.resize(static_cast<size_t>(file_size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(rom.data_.data()), file_size);
    
    if (!file) {
        rom.error_ = "Failed to read file";
        return rom;
    }
    
    // Parse header
    if (!rom.parse_header()) {
        return rom;
    }
    
    // Validate
    if (!rom.validate()) {
        return rom;
    }
    
    rom.valid_ = true;
    return rom;
}

std::optional<ROM> ROM::load_from_buffer(std::vector<uint8_t> data,
                                          const std::string& name) {
    ROM rom;
    rom.data_ = std::move(data);
    rom.name_ = name;
    
    if (rom.data_.size() < 0x150) {
        rom.error_ = "Data too small to be a valid ROM";
        return rom;
    }
    
    if (!rom.parse_header()) {
        return rom;
    }
    
    if (!rom.validate()) {
        return rom;
    }
    
    rom.valid_ = true;
    return rom;
}

bool ROM::parse_header() {
    // Entry point (0x100-0x103)
    std::copy_n(data_.data() + 0x100, 4, header_.entry_point);
    
    // Nintendo logo (0x104-0x133)
    std::copy_n(data_.data() + 0x104, 48, header_.nintendo_logo);
    
    // Title (0x134-0x143)
    // For CGB games, title is only 11 bytes (0x134-0x13E)
    // Manufacturer code is at 0x13F-0x142
    // CGB flag is at 0x143
    header_.cgb_flag = data_[0x143];
    header_.is_cgb = (header_.cgb_flag == 0x80 || header_.cgb_flag == 0xC0);
    header_.is_cgb_only = (header_.cgb_flag == 0xC0);
    
    if (header_.is_cgb) {
        // CGB game - shorter title
        header_.title = std::string(reinterpret_cast<char*>(data_.data() + 0x134), 11);
        header_.manufacturer_code = std::string(reinterpret_cast<char*>(data_.data() + 0x13F), 4);
    } else {
        // DMG game - full title
        header_.title = std::string(reinterpret_cast<char*>(data_.data() + 0x134), 16);
    }
    
    // Remove trailing nulls/spaces from title
    header_.title.erase(header_.title.find_last_not_of(" \0", std::string::npos, 2) + 1);
    
    // New licensee code (0x144-0x145)
    header_.new_licensee_code = std::string(reinterpret_cast<char*>(data_.data() + 0x144), 2);
    
    // SGB flag (0x146)
    header_.sgb_flag = data_[0x146];
    header_.is_sgb = (header_.sgb_flag == 0x03);
    
    // Cartridge type (0x147)
    uint8_t cart_type = data_[0x147];
    if (cart_type <= 0x22 || cart_type >= 0xFC) {
        header_.mbc_type = static_cast<MBCType>(cart_type);
    } else {
        header_.mbc_type = MBCType::UNKNOWN;
    }
    
    // ROM size (0x148)
    header_.rom_size_code = data_[0x148];
    header_.rom_size_bytes = rom_size_from_code(header_.rom_size_code);
    header_.rom_banks = static_cast<uint16_t>(header_.rom_size_bytes / (16 * 1024));
    
    // RAM size (0x149)
    header_.ram_size_code = data_[0x149];
    header_.ram_size_bytes = ram_size_from_code(header_.ram_size_code);
    header_.ram_banks = header_.ram_size_bytes > 0 
        ? static_cast<uint8_t>(header_.ram_size_bytes / (8 * 1024))
        : 0;
    
    // MBC2 has 512x4 bits of RAM built-in
    if (header_.mbc_type == MBCType::MBC2 || header_.mbc_type == MBCType::MBC2_BATTERY) {
        header_.ram_size_bytes = 512;
        header_.ram_banks = 1;
    }
    
    // Destination code (0x14A)
    header_.destination_code = data_[0x14A];
    
    // Old licensee code (0x14B)
    header_.old_licensee_code = data_[0x14B];
    
    // ROM version (0x14C)
    header_.rom_version = data_[0x14C];
    
    // Header checksum (0x14D)
    header_.header_checksum = data_[0x14D];
    
    // Global checksum (0x14E-0x14F)
    header_.global_checksum = (static_cast<uint16_t>(data_[0x14E]) << 8) | data_[0x14F];
    
    return true;
}

bool ROM::validate() {
    // Validate logo
    header_.logo_valid = std::equal(
        header_.nintendo_logo, 
        header_.nintendo_logo + 48,
        NINTENDO_LOGO
    );
    
    // Validate header checksum
    uint8_t checksum = 0;
    for (uint16_t addr = 0x134; addr <= 0x14C; addr++) {
        checksum = checksum - data_[addr] - 1;
    }
    header_.header_checksum_valid = (checksum == header_.header_checksum);
    
    // Validate global checksum (optional - not checked by boot ROM)
    uint16_t global_sum = 0;
    for (size_t i = 0; i < data_.size(); i++) {
        if (i != 0x14E && i != 0x14F) {
            global_sum += data_[i];
        }
    }
    header_.global_checksum_valid = (global_sum == header_.global_checksum);
    
    // Check size matches header
    if (header_.rom_size_bytes > 0 && data_.size() < header_.rom_size_bytes) {
        error_ = "ROM size smaller than header indicates";
        return false;
    }
    
    // Logo check is essential
    if (!header_.logo_valid) {
        error_ = "Invalid Nintendo logo (may be corrupted or not a GB ROM)";
        // Don't fail - some homebrew has invalid logos
    }
    
    // Header checksum is verified by boot ROM
    if (!header_.header_checksum_valid) {
        error_ = "Invalid header checksum";
        // Don't fail - allow continuing anyway
    }
    
    return true;
}

uint8_t ROM::read(uint16_t addr) const {
    if (addr < data_.size()) {
        return data_[addr];
    }
    return 0xFF;
}

uint8_t ROM::read_banked(BankId bank, uint16_t addr) const {
    if (addr < 0x4000) {
        // Usually physical bank 0. MBC1 advanced mode can map another
        // physical bank into this CPU window, represented by bank > 0.
        size_t offset = (static_cast<size_t>(bank) * 0x4000) + addr;
        if (offset < data_.size()) {
            return data_[offset];
        }
    } else if (addr < 0x8000) {
        // Switchable bank
        size_t offset = (static_cast<size_t>(bank) * 0x4000) + (addr - 0x4000);
        if (offset < data_.size()) {
            return data_[offset];
        }
    }
    return 0xFF;
}

/* ============================================================================
 * Utility Functions
 * ========================================================================== */

bool validate_rom_file(const std::filesystem::path& path, std::string& error) {
    auto rom = ROM::load(path);
    if (!rom || !rom->is_valid()) {
        error = rom ? rom->error() : "Failed to load ROM";
        return false;
    }
    return true;
}

void print_rom_info(const ROM& rom) {
    const auto& h = rom.header();
    
    std::cout << "\nROM Information:\n";
    std::cout << "  Title:        " << h.title << "\n";
    std::cout << "  MBC Type:     " << mbc_type_name(h.mbc_type) << "\n";
    std::cout << "  ROM Size:     " << (h.rom_size_bytes / 1024) << " KB (" 
              << h.rom_banks << " banks)\n";
    
    if (h.ram_size_bytes > 0) {
        std::cout << "  RAM Size:     " << (h.ram_size_bytes / 1024) << " KB\n";
    }
    
    std::cout << "  CGB Support:  ";
    if (h.is_cgb_only) std::cout << "CGB Only\n";
    else if (h.is_cgb) std::cout << "CGB Enhanced\n";
    else std::cout << "DMG\n";
    
    if (h.is_sgb) {
        std::cout << "  SGB Support:  Yes\n";
    }
    
    std::cout << "  ROM Version:  " << (int)h.rom_version << "\n";
    std::cout << "  Logo Valid:   " << (h.logo_valid ? "Yes" : "No") << "\n";
    std::cout << "  Checksum:     " << (h.header_checksum_valid ? "OK" : "FAIL") << "\n";
}

} // namespace gbrecomp
