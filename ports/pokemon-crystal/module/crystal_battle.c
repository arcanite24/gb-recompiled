#include "crystal_battle.h"

#include "crystal_semantic.h"

#include <string.h>

GBSemanticStatus crystal_battle_build_scene(
    const GBSemanticReader* reader,
    GBPresentationScene* scene) {
    if (reader == NULL || scene == NULL) {
        return GB_SEMANTIC_INVALID_ARGUMENT;
    }
    CrystalBattleMon player = {0};
    CrystalBattleMon enemy = {0};
    CrystalBattleContext context = {0};
    GBSemanticStatus status = crystal_semantic_read_battle(
        reader,
        GB_SEMANTIC_READ_LIVE,
        &player,
        &enemy,
        &context);
    if (status != GB_SEMANTIC_OK) {
        return status;
    }
    memset(scene, 0, sizeof(*scene));
    scene->abi_version = GB_PRESENTATION_ABI_VERSION;
    scene->kind = GB_PRESENTATION_SCENE_BATTLE;
    memcpy(
        scene->scene_id,
        "crystal.battle.native-v1",
        sizeof("crystal.battle.native-v1"));
    scene->battle.valid = true;
    scene->battle.player_species = player.species;
    scene->battle.enemy_species = enemy.species;
    scene->battle.player_level = player.level;
    scene->battle.enemy_level = enemy.level;
    scene->battle.phase =
        (uint32_t)context.mode |
        ((uint32_t)context.temp_wild_species << 8u) |
        ((uint32_t)context.trainer_class << 16u) |
        ((uint32_t)context.battle_type << 24u);
    return GB_SEMANTIC_OK;
}
