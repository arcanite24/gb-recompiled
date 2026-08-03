#ifndef CRYSTAL_OVERWORLD_H
#define CRYSTAL_OVERWORLD_H

#include "gbrt_presentation.h"
#include "gbrt_semantic.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum CrystalOverworldStatus {
    CRYSTAL_OVERWORLD_OK = 0,
    CRYSTAL_OVERWORLD_INVALID_ARGUMENT,
    CRYSTAL_OVERWORLD_ROM_MISMATCH,
    CRYSTAL_OVERWORLD_READ_FAILED,
    CRYSTAL_OVERWORLD_UNSUPPORTED_MAP,
    CRYSTAL_OVERWORLD_LAYOUT_MISMATCH,
} CrystalOverworldStatus;

/*
 * Bounded M7 prototype: New Bark Town plus its reviewed west connection to
 * Route 29. All blocks and live object state come from the user's exact ROM
 * and paused semantic reader. No ROM-derived graphics are embedded.
 */
CrystalOverworldStatus crystal_overworld_build_new_bark_scene(
    const GBSemanticReader* reader,
    int16_t camera_x,
    int16_t camera_y,
    bool transition_active,
    bool raster_effect_active,
    GBPresentationScene* scene);

GBPresentationWidescreenStyle crystal_overworld_style(void);

#endif
