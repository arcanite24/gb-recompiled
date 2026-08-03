#include "gbrt_gameplay_mutation.h"
#include "gbrt_native_patch.h"
#include "challenge_rules.h"

#include <stdio.h>
#include <string.h>

#define CRYSTAL_ROM_SHA256 \
    "fdcc3c8c43813cf8731fc037d2a6d191bac75439c34b24ba1c27526e6acdc8a2"
#define CRYSTAL_ROM_SIZE 2097152u
#define CRYSTAL_WILD_BATTLE 1u
#define CRYSTAL_NORMAL_BATTLE_TYPE 0u
#define CRYSTAL_CAN_LOSE_BATTLE_TYPE 1u
#define CRYSTAL_OT_PARTY_MON 1u
#define CRYSTAL_PARTY_LENGTH 6u
#define CRYSTAL_W_LINK_MODE 0xc2dcu
#define CRYSTAL_W_MON_TYPE 0xcf5fu
#define CRYSTAL_W_IN_BATTLE_TOWER_BATTLE 0xcfc0u
#define CRYSTAL_CHALLENGE_CONFIGURATION_SCHEMA "gbrecomp.host-configuration"
#define CRYSTAL_W_BATTLE_MODE 0xd22du
#define CRYSTAL_W_OTHER_TRAINER_CLASS 0xd22fu
#define CRYSTAL_W_BATTLE_TYPE 0xd230u
#define CRYSTAL_W_CUR_PARTY_LEVEL 0xd143u
#define CRYSTAL_W_OT_PARTY_COUNT 0xd280u
#define CRYSTAL_W_JOHTO_BADGES 0xd857u
#define CRYSTAL_W_PARTY_COUNT 0xdcd7u
#define CRYSTAL_W_PARTY_MONS 0xdcdfu
#define CRYSTAL_PARTY_MON_SIZE 48u
#define CRYSTAL_PARTY_MON_LEVEL_OFFSET 31u
#define CRYSTAL_PARTY_MON_HP_OFFSET 34u

static const GBGameplayMutationFieldSpec crystal_wild_level_field = {
    "battle.enemy_level",
    GB_GAMEPLAY_MUTATION_U8,
    GB_SEMANTIC_BANKED_WRAM,
    1,
    CRYSTAL_W_CUR_PARTY_LEVEL,
    1,
    CRYSTAL_CHALLENGE_LEVEL_MAX,
};

static const GBGameplayMutationSpec crystal_wild_level_spec = {
    GB_GAMEPLAY_MUTATION_ABI_VERSION,
    "crystal.wild-level.v1",
    CRYSTAL_ROM_SHA256,
    CRYSTAL_ROM_SIZE,
    GB_NATIVE_FUNCTION_ID(0x000f, 0x68eb),
    &crystal_wild_level_field,
    1,
};

static const GBGameplayMutationFieldSpec crystal_trainer_level_field = {
    "battle.opponent_party_level",
    GB_GAMEPLAY_MUTATION_U8,
    GB_SEMANTIC_BANKED_WRAM,
    1,
    CRYSTAL_W_CUR_PARTY_LEVEL,
    1,
    CRYSTAL_CHALLENGE_LEVEL_MAX,
};

static const GBGameplayMutationSpec crystal_trainer_level_spec = {
    GB_GAMEPLAY_MUTATION_ABI_VERSION,
    "crystal.trainer-party-level.v1",
    CRYSTAL_ROM_SHA256,
    CRYSTAL_ROM_SIZE,
    GB_NATIVE_FUNCTION_ID(0x0003, 0x588c),
    &crystal_trainer_level_field,
    1,
};

typedef struct CrystalWildValidation {
    uint8_t expected_level;
    uint8_t expected_reference_level;
    uint8_t expected_badges;
} CrystalWildValidation;

static GBSemanticStatus crystal_read_u8(
    const GBSemanticReader* reader,
    uint16_t address,
    uint8_t* value) {
    return gbrt_semantic_read(
        reader,
        CRYSTAL_ROM_SHA256,
        GB_SEMANTIC_READ_LIVE,
        GB_SEMANTIC_BANKED_WRAM,
        1,
        address,
        value,
        1);
}

static GBSemanticStatus crystal_read_wram_u8(
    const GBSemanticReader* reader,
    uint16_t address,
    uint8_t* value) {
    return gbrt_semantic_read(
        reader,
        CRYSTAL_ROM_SHA256,
        GB_SEMANTIC_READ_LIVE,
        GB_SEMANTIC_WRAM,
        0,
        address,
        value,
        1);
}

static uint8_t crystal_popcount_u8(uint8_t value) {
    uint8_t count = 0;
    while (value != 0u) {
        count = (uint8_t)(count + (value & 1u));
        value >>= 1u;
    }
    return count;
}

static GBSemanticStatus crystal_challenge_inputs(
    const GBSemanticReader* reader,
    uint8_t* strongest_level,
    uint8_t* badges) {
    uint8_t party_count = 0;
    uint8_t badge_bytes[2] = {0, 0};
    uint8_t strongest = 0;
    uint8_t index;
    if (reader == NULL || strongest_level == NULL || badges == NULL ||
        crystal_read_u8(reader, CRYSTAL_W_PARTY_COUNT, &party_count) !=
            GB_SEMANTIC_OK ||
        party_count > CRYSTAL_PARTY_LENGTH ||
        gbrt_semantic_read(
            reader,
            CRYSTAL_ROM_SHA256,
            GB_SEMANTIC_READ_LIVE,
            GB_SEMANTIC_BANKED_WRAM,
            1,
            CRYSTAL_W_JOHTO_BADGES,
            badge_bytes,
            sizeof(badge_bytes)) != GB_SEMANTIC_OK) {
        return GB_SEMANTIC_INVALID_DATA;
    }
    for (index = 0; index < party_count; ++index) {
        const uint16_t mon = (uint16_t)(
            CRYSTAL_W_PARTY_MONS + CRYSTAL_PARTY_MON_SIZE * index);
        uint8_t level = 0;
        uint8_t hp[2] = {0, 0};
        if (crystal_read_u8(
                reader,
                (uint16_t)(mon + CRYSTAL_PARTY_MON_LEVEL_OFFSET),
                &level) != GB_SEMANTIC_OK ||
            gbrt_semantic_read(
                reader,
                CRYSTAL_ROM_SHA256,
                GB_SEMANTIC_READ_LIVE,
                GB_SEMANTIC_BANKED_WRAM,
                1,
                (uint16_t)(mon + CRYSTAL_PARTY_MON_HP_OFFSET),
                hp,
                sizeof(hp)) != GB_SEMANTIC_OK) {
            return GB_SEMANTIC_INVALID_DATA;
        }
        if ((hp[0] != 0u || hp[1] != 0u) &&
            (level < CRYSTAL_CHALLENGE_LEVEL_MIN ||
             level > CRYSTAL_CHALLENGE_LEVEL_MAX)) {
            return GB_SEMANTIC_INVALID_DATA;
        }
        if ((hp[0] != 0u || hp[1] != 0u) && level > strongest) {
            strongest = level;
        }
    }
    *strongest_level = strongest;
    *badges = (uint8_t)(
        crystal_popcount_u8(badge_bytes[0]) +
        crystal_popcount_u8(badge_bytes[1]));
    return GB_SEMANTIC_OK;
}

static GBGameplayMutationPolicyResult crystal_challenge_policy(
    const GBContext* context,
    uint8_t original_level,
    uint8_t strongest_level,
    uint8_t badges) {
    const GBHostConfiguration* configuration =
        context == NULL ? NULL : &context->config.host_configuration;
    if (configuration == NULL ||
        configuration->abi_version != GB_HOST_CONFIGURATION_ABI_VERSION ||
        !configuration->present || !configuration->applied ||
        !configuration->enabled ||
        configuration->schema_version != 1u ||
        strcmp(
            configuration->schema,
            CRYSTAL_CHALLENGE_CONFIGURATION_SCHEMA) != 0 ||
        strcmp(configuration->policy_id, CRYSTAL_CHALLENGE_RULESET_ID) != 0) {
        const GBGameplayMutationPolicyInput disabled = {
            GB_GAMEPLAY_MUTATION_ABI_VERSION,
            original_level,
            0,
            badges,
            0,
            CRYSTAL_CHALLENGE_LEVEL_MIN,
            CRYSTAL_CHALLENGE_LEVEL_MAX,
        };
        return crystal_challenge_calculate_level(&disabled);
    }
    const GBGameplayMutationPolicyInput input = {
        GB_GAMEPLAY_MUTATION_ABI_VERSION,
        original_level,
        strongest_level,
        badges,
        configuration->offset,
        configuration->minimum,
        configuration->maximum,
    };
    return crystal_challenge_calculate_level(&input);
}

static GBSemanticStatus crystal_validate_wild_level(
    const GBSemanticReader* reader,
    void* user) {
    const CrystalWildValidation* validation =
        (const CrystalWildValidation*)user;
    uint8_t battle_mode = 0;
    uint8_t level = 0;
    uint8_t strongest_level = 0;
    uint8_t badges = 0;
    if (validation == NULL ||
        crystal_read_u8(reader, CRYSTAL_W_BATTLE_MODE, &battle_mode) !=
            GB_SEMANTIC_OK ||
        crystal_read_u8(reader, CRYSTAL_W_CUR_PARTY_LEVEL, &level) !=
            GB_SEMANTIC_OK ||
        crystal_challenge_inputs(reader, &strongest_level, &badges) !=
            GB_SEMANTIC_OK ||
        battle_mode != CRYSTAL_WILD_BATTLE ||
        level != validation->expected_level ||
        strongest_level != validation->expected_reference_level ||
        badges != validation->expected_badges) {
        return GB_SEMANTIC_INVALID_DATA;
    }
    return GB_SEMANTIC_OK;
}

static void crystal_challenge_diagnostic(
    const char* event_id,
    GBGameplayMutationStatus status) {
    fprintf(
        stderr,
        "[GBRT][challenge-mode:%s] "
        "mutation skipped (%s); original behavior retained\n",
        event_id,
        gb_gameplay_mutation_status_string(status));
}

GB_NATIVE_HOOK(crystal_challenge_wild_level) {
    GBContext* context = gb_native_context(call);
    GBSemanticReader reader;
    uint8_t battle_mode = 0;
    uint8_t original_level = 0;
    uint8_t strongest_level = 0;
    uint8_t badges = 0;
    GBGameplayMutationPolicyResult policy;
    GBGameplayMutationStatus status;

    if (gbrt_semantic_reader_init_live(
            &reader, context, CRYSTAL_ROM_SHA256) != GB_SEMANTIC_OK ||
        crystal_read_u8(&reader, CRYSTAL_W_BATTLE_MODE, &battle_mode) !=
            GB_SEMANTIC_OK ||
        crystal_read_u8(
            &reader, CRYSTAL_W_CUR_PARTY_LEVEL, &original_level) !=
            GB_SEMANTIC_OK ||
        crystal_challenge_inputs(&reader, &strongest_level, &badges) !=
            GB_SEMANTIC_OK) {
        crystal_challenge_diagnostic(
            crystal_wild_level_spec.event_id,
            GB_GAMEPLAY_MUTATION_VALIDATION_FAILED);
        return GB_NATIVE_STATUS_OK;
    }
    if (battle_mode != CRYSTAL_WILD_BATTLE) {
        return GB_NATIVE_STATUS_OK;
    }
    policy = crystal_challenge_policy(
        context, original_level, strongest_level, badges);
    if (policy.outcome != GB_GAMEPLAY_MUTATION_POLICY_APPLY) {
        return GB_NATIVE_STATUS_OK;
    }
    const GBGameplayMutationValue value = {
        "battle.enemy_level",
        policy.proposed_value,
    };
    CrystalWildValidation validation = {
        (uint8_t)policy.proposed_value,
        strongest_level,
        badges,
    };
    const GBGameplayMutationRequest request = {
        GB_GAMEPLAY_MUTATION_ABI_VERSION,
        &value,
        1,
        crystal_validate_wild_level,
        &validation,
    };
    status = gb_native_apply_gameplay_mutation(
        call, &crystal_wild_level_spec, &request);
    if (status != GB_GAMEPLAY_MUTATION_APPLIED) {
        crystal_challenge_diagnostic(
            crystal_wild_level_spec.event_id, status);
    }
    return GB_NATIVE_STATUS_OK;
}

typedef struct CrystalTrainerValidation {
    uint8_t expected_level;
    uint8_t expected_reference_level;
    uint8_t expected_badges;
} CrystalTrainerValidation;

static GBSemanticStatus crystal_validate_trainer_level(
    const GBSemanticReader* reader,
    void* user) {
    const CrystalTrainerValidation* validation =
        (const CrystalTrainerValidation*)user;
    uint8_t battle_type = 0;
    uint8_t trainer_class = 0;
    uint8_t level = 0;
    uint8_t party_count = 0;
    uint8_t mon_type = 0;
    uint8_t link_mode = 0;
    uint8_t battle_tower = 0;
    uint8_t strongest_level = 0;
    uint8_t badges = 0;
    if (validation == NULL ||
        crystal_read_u8(reader, CRYSTAL_W_BATTLE_TYPE, &battle_type) !=
            GB_SEMANTIC_OK ||
        crystal_read_u8(
            reader, CRYSTAL_W_OTHER_TRAINER_CLASS, &trainer_class) !=
            GB_SEMANTIC_OK ||
        crystal_read_u8(reader, CRYSTAL_W_CUR_PARTY_LEVEL, &level) !=
            GB_SEMANTIC_OK ||
        crystal_read_u8(reader, CRYSTAL_W_OT_PARTY_COUNT, &party_count) !=
            GB_SEMANTIC_OK ||
        crystal_read_wram_u8(reader, CRYSTAL_W_MON_TYPE, &mon_type) !=
            GB_SEMANTIC_OK ||
        crystal_read_wram_u8(reader, CRYSTAL_W_LINK_MODE, &link_mode) !=
            GB_SEMANTIC_OK ||
        crystal_read_wram_u8(
            reader, CRYSTAL_W_IN_BATTLE_TOWER_BATTLE, &battle_tower) !=
            GB_SEMANTIC_OK ||
        crystal_challenge_inputs(reader, &strongest_level, &badges) !=
            GB_SEMANTIC_OK ||
        battle_type > CRYSTAL_CAN_LOSE_BATTLE_TYPE ||
        trainer_class == 0u ||
        (mon_type & 0x0fu) != CRYSTAL_OT_PARTY_MON ||
        link_mode != 0u ||
        (battle_tower & 0x01u) != 0u ||
        party_count >= CRYSTAL_PARTY_LENGTH ||
        level != validation->expected_level ||
        strongest_level != validation->expected_reference_level ||
        badges != validation->expected_badges) {
        return GB_SEMANTIC_INVALID_DATA;
    }
    return GB_SEMANTIC_OK;
}

GB_NATIVE_HOOK(crystal_challenge_trainer_level) {
    GBContext* context = gb_native_context(call);
    GBSemanticReader reader;
    uint8_t battle_type = 0;
    uint8_t trainer_class = 0;
    uint8_t original_level = 0;
    uint8_t party_count = 0;
    uint8_t mon_type = 0;
    uint8_t link_mode = 0;
    uint8_t battle_tower = 0;
    uint8_t strongest_level = 0;
    uint8_t badges = 0;
    GBGameplayMutationPolicyResult policy;
    GBGameplayMutationStatus status;

    if (gbrt_semantic_reader_init_live(
            &reader, context, CRYSTAL_ROM_SHA256) != GB_SEMANTIC_OK ||
        crystal_read_u8(&reader, CRYSTAL_W_BATTLE_TYPE, &battle_type) !=
            GB_SEMANTIC_OK ||
        crystal_read_u8(
            &reader, CRYSTAL_W_OTHER_TRAINER_CLASS, &trainer_class) !=
            GB_SEMANTIC_OK ||
        crystal_read_u8(
            &reader, CRYSTAL_W_CUR_PARTY_LEVEL, &original_level) !=
            GB_SEMANTIC_OK ||
        crystal_read_u8(&reader, CRYSTAL_W_OT_PARTY_COUNT, &party_count) !=
            GB_SEMANTIC_OK ||
        crystal_read_wram_u8(&reader, CRYSTAL_W_MON_TYPE, &mon_type) !=
            GB_SEMANTIC_OK ||
        crystal_read_wram_u8(&reader, CRYSTAL_W_LINK_MODE, &link_mode) !=
            GB_SEMANTIC_OK ||
        crystal_read_wram_u8(
            &reader,
            CRYSTAL_W_IN_BATTLE_TOWER_BATTLE,
            &battle_tower) != GB_SEMANTIC_OK ||
        crystal_challenge_inputs(&reader, &strongest_level, &badges) !=
            GB_SEMANTIC_OK) {
        crystal_challenge_diagnostic(
            crystal_trainer_level_spec.event_id,
            GB_GAMEPLAY_MUTATION_VALIDATION_FAILED);
        return GB_NATIVE_STATUS_OK;
    }
    if (battle_type > CRYSTAL_CAN_LOSE_BATTLE_TYPE ||
        trainer_class == 0u ||
        (mon_type & 0x0fu) != CRYSTAL_OT_PARTY_MON ||
        link_mode != 0u ||
        (battle_tower & 0x01u) != 0u ||
        party_count >= CRYSTAL_PARTY_LENGTH) {
        return GB_NATIVE_STATUS_OK;
    }
    policy = crystal_challenge_policy(
        context, original_level, strongest_level, badges);
    if (policy.outcome != GB_GAMEPLAY_MUTATION_POLICY_APPLY) {
        return GB_NATIVE_STATUS_OK;
    }
    const GBGameplayMutationValue value = {
        "battle.opponent_party_level",
        policy.proposed_value,
    };
    CrystalTrainerValidation validation = {
        (uint8_t)policy.proposed_value,
        strongest_level,
        badges,
    };
    const GBGameplayMutationRequest request = {
        GB_GAMEPLAY_MUTATION_ABI_VERSION,
        &value,
        1,
        crystal_validate_trainer_level,
        &validation,
    };
    status = gb_native_apply_gameplay_mutation(
        call, &crystal_trainer_level_spec, &request);
    if (status != GB_GAMEPLAY_MUTATION_APPLIED) {
        crystal_challenge_diagnostic(
            crystal_trainer_level_spec.event_id, status);
    }
    return GB_NATIVE_STATUS_OK;
}
