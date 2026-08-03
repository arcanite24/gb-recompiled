#include "gbrt_gameplay_mutation.h"
#include "gbrt_native_patch_internal.h"

#include <stdint.h>
#include <string.h>

#define TEST_ROM_SHA256 \
    "0000000000000000000000000000000000000000000000000000000000000000"
#define TEST_FUNCTION_ID GB_NATIVE_FUNCTION_ID(3, 0x4567)

static GBSemanticStatus reject_validation(
    const GBSemanticReader* reader,
    void* user) {
    (void)reader;
    (void)user;
    return GB_SEMANTIC_INVALID_DATA;
}

static int expect_unchanged(
    const uint8_t* wram,
    GBGameplayMutationStatus actual,
    GBGameplayMutationStatus expected) {
    return actual == expected && wram[0x1100] == 7 && wram[0x1101] == 9;
}

int main(void) {
    uint8_t rom[0x8000] = {0};
    uint8_t wram[0x8000] = {0};
    const GBGameplayMutationFieldSpec fields[] = {
        {
            .field_id = "battle.enemy_level",
            .type = GB_GAMEPLAY_MUTATION_U8,
            .space = GB_SEMANTIC_BANKED_WRAM,
            .bank = 1,
            .address = 0xD100,
            .minimum = 1,
            .maximum = 100,
        },
        {
            .field_id = "battle.party_level",
            .type = GB_GAMEPLAY_MUTATION_U8,
            .space = GB_SEMANTIC_BANKED_WRAM,
            .bank = 1,
            .address = 0xD101,
            .minimum = 1,
            .maximum = 100,
        },
    };
    const GBGameplayMutationValue values[] = {
        {.field_id = "battle.enemy_level", .value = 42},
        {.field_id = "battle.party_level", .value = 43},
    };
    GBGameplayMutationSpec spec = {
        .abi_version = GB_GAMEPLAY_MUTATION_ABI_VERSION,
        .event_id = "test.battle-level.v1",
        .rom_sha256 = TEST_ROM_SHA256,
        .rom_size = sizeof(rom),
        .function_id = TEST_FUNCTION_ID,
        .fields = fields,
        .field_count = 2,
    };
    GBGameplayMutationRequest request = {
        .abi_version = GB_GAMEPLAY_MUTATION_ABI_VERSION,
        .values = values,
        .value_count = 2,
    };
    GBNativeBinding binding = {
        .abi_version = GB_NATIVE_PATCH_ABI_VERSION,
        .patch_id = "test.gameplay-mutation",
        .function_id = TEST_FUNCTION_ID,
        .rom_size = sizeof(rom),
    };
    GBContext context = {
        .rom = rom,
        .rom_size = sizeof(rom),
        .wram = wram,
    };
    GBNativeCall call = {
        .ctx = &context,
        .binding = &binding,
        .phase = GB_NATIVE_PHASE_PRE,
    };

    wram[0x1100] = 7;
    wram[0x1101] = 9;
    if (gb_native_apply_gameplay_mutation(&call, &spec, &request) !=
            GB_GAMEPLAY_MUTATION_APPLIED ||
        wram[0x1100] != 42 || wram[0x1101] != 43 ||
        context.semantic_transaction_outcome !=
            GB_SEMANTIC_TRANSACTION_COMMITTED ||
        context.semantic_transaction_dirty_count != 1 ||
        context.semantic_transaction_dirty[0].address != 0xD100 ||
        context.semantic_transaction_dirty[0].width != 2) {
        return 1;
    }

    wram[0x1100] = 7;
    wram[0x1101] = 9;
    call.phase = GB_NATIVE_PHASE_POST;
    if (!expect_unchanged(
            wram,
            gb_native_apply_gameplay_mutation(&call, &spec, &request),
            GB_GAMEPLAY_MUTATION_WRONG_PHASE)) {
        return 1;
    }
    call.phase = GB_NATIVE_PHASE_PRE;

    spec.function_id = GB_NATIVE_FUNCTION_ID(4, 0x4567);
    if (!expect_unchanged(
            wram,
            gb_native_apply_gameplay_mutation(&call, &spec, &request),
            GB_GAMEPLAY_MUTATION_HOOK_MISMATCH)) {
        return 1;
    }
    spec.function_id = TEST_FUNCTION_ID;

    spec.rom_sha256 =
        "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";
    if (!expect_unchanged(
            wram,
            gb_native_apply_gameplay_mutation(&call, &spec, &request),
            GB_GAMEPLAY_MUTATION_ROM_MISMATCH)) {
        return 1;
    }
    spec.rom_sha256 = TEST_ROM_SHA256;

    GBGameplayMutationValue invalid_values[2];
    memcpy(invalid_values, values, sizeof(values));
    invalid_values[1].value = 101;
    request.values = invalid_values;
    if (!expect_unchanged(
            wram,
            gb_native_apply_gameplay_mutation(&call, &spec, &request),
            GB_GAMEPLAY_MUTATION_OUT_OF_RANGE)) {
        return 1;
    }
    request.values = values;

    request.validate = reject_validation;
    if (!expect_unchanged(
            wram,
            gb_native_apply_gameplay_mutation(&call, &spec, &request),
            GB_GAMEPLAY_MUTATION_VALIDATION_FAILED) ||
        context.semantic_transaction_outcome !=
            GB_SEMANTIC_TRANSACTION_VALIDATION_FAILED) {
        return 1;
    }
    request.validate = NULL;

    GBGameplayMutationFieldSpec invalid_fields[2];
    memcpy(invalid_fields, fields, sizeof(fields));
    invalid_fields[1].space = GB_SEMANTIC_PHYSICAL_ROM;
    invalid_fields[1].bank = 0;
    invalid_fields[1].address = 0x0100;
    spec.fields = invalid_fields;
    if (!expect_unchanged(
            wram,
            gb_native_apply_gameplay_mutation(&call, &spec, &request),
            GB_GAMEPLAY_MUTATION_TRANSACTION_FAILED) ||
        context.semantic_transaction_outcome !=
            GB_SEMANTIC_TRANSACTION_ABORTED) {
        return 1;
    }

    return 0;
}
