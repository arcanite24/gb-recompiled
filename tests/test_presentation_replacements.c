#include "gbrt_presentation.h"

#include <string.h>

int main(void) {
    static const uint8_t panel[] = "crystal-panel-v1";
    static const char* rom =
        "fdcc3c8c43813cf8731fc037d2a6d191bac75439c34b24ba1c27526e6acdc8a2";
    GBPresentationReplacementConfig config = {
        .abi_version = GB_PRESENTATION_ABI_VERSION,
        .mode = GB_PRESENTATION_MODE_NATIVE,
        .output_width = 1280,
        .output_height = 720,
        .effect_seed = 0x43525953u,
        .asset_count = 1,
        .assets = {
            {
                .asset_id = "crystal.ui.panel-v1",
                .sha256 =
                    "872607dd784e100966e6c90b9499b7ea511a0dfdfbeceadfcc14044cd46a24da",
                .license_spdx = "CC0-1.0",
                .data = panel,
                .data_size = sizeof(panel) - 1u,
            },
        },
    };
    memcpy(config.rom_sha256, rom, 65);
    if (gbrt_presentation_validate_replacements(&config, rom) !=
        GB_PRESENTATION_OK) {
        return 1;
    }
    config.assets[0].sha256[0] = '0';
    if (gbrt_presentation_validate_replacements(&config, rom) !=
        GB_PRESENTATION_INVALID_ASSET) {
        return 2;
    }
    config.assets[0].sha256[0] = '8';
    config.asset_count = 2;
    config.assets[1] = config.assets[0];
    if (gbrt_presentation_validate_replacements(&config, rom) !=
        GB_PRESENTATION_INVALID_ASSET) {
        return 3;
    }
    config.mode = GB_PRESENTATION_MODE_ORIGINAL;
    config.asset_count = 0;
    if (gbrt_presentation_validate_replacements(&config, rom) !=
        GB_PRESENTATION_OK) {
        return 4;
    }
    config.mode = GB_PRESENTATION_MODE_NATIVE;
    if (gbrt_presentation_validate_replacements(&config, rom) !=
        GB_PRESENTATION_INVALID_ASSET) {
        return 5;
    }
    config.abi_version++;
    if (gbrt_presentation_validate_replacements(&config, rom) !=
        GB_PRESENTATION_ABI_MISMATCH) {
        return 6;
    }
    return 0;
}
