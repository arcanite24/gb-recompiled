#include "gbrt_gameplay_mutation.h"
#include "gbrt_native_patch_internal.h"

#include <string.h>

#ifndef GBRT_ENABLE_NATIVE_PATCHES
#error "gbrt_gameplay_mutation.c requires GBRT_ENABLE_NATIVE_PATCHES"
#endif

typedef struct GBGameplayMutationValidation {
    const GBGameplayMutationSpec* spec;
    const GBGameplayMutationRequest* request;
} GBGameplayMutationValidation;

static int hex_nibble(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static int decode_sha256(const char* text, uint8_t digest[32]) {
    size_t index;
    if (text == NULL || strlen(text) != 64u) return 0;
    for (index = 0; index < 32u; ++index) {
        const int high = hex_nibble(text[index * 2u]);
        const int low = hex_nibble(text[index * 2u + 1u]);
        if (high < 0 || low < 0) return 0;
        digest[index] = (uint8_t)((high << 4u) | low);
    }
    return 1;
}

static const GBGameplayMutationValue* find_mutation_value(
    const GBGameplayMutationRequest* request,
    const char* field_id,
    size_t* matches) {
    const GBGameplayMutationValue* found = NULL;
    size_t index;
    *matches = 0;
    for (index = 0; index < request->value_count; ++index) {
        const GBGameplayMutationValue* value = &request->values[index];
        if (value->field_id != NULL && strcmp(value->field_id, field_id) == 0) {
            found = value;
            ++*matches;
        }
    }
    return found;
}

static GBSemanticStatus validate_gameplay_mutation(
    const GBSemanticReader* reader,
    void* user) {
    const GBGameplayMutationValidation* validation =
        (const GBGameplayMutationValidation*)user;
    size_t index;
    for (index = 0; index < validation->spec->field_count; ++index) {
        const GBGameplayMutationFieldSpec* field =
            &validation->spec->fields[index];
        const GBGameplayMutationValue* value = NULL;
        size_t matches = 0;
        uint8_t staged = 0;
        value = find_mutation_value(
            validation->request, field->field_id, &matches);
        if (matches != 1u || value == NULL ||
            gbrt_semantic_read(
                reader,
                validation->spec->rom_sha256,
                GB_SEMANTIC_READ_LIVE,
                field->space,
                field->bank,
                field->address,
                &staged,
                sizeof(staged)) != GB_SEMANTIC_OK ||
            staged != (uint8_t)value->value) {
            return GB_SEMANTIC_INVALID_DATA;
        }
    }
    if (validation->request->validate != NULL) {
        return validation->request->validate(
            reader, validation->request->validate_user);
    }
    return GB_SEMANTIC_OK;
}

const char* gb_gameplay_mutation_status_string(
    GBGameplayMutationStatus status) {
    switch (status) {
        case GB_GAMEPLAY_MUTATION_APPLIED: return "applied";
        case GB_GAMEPLAY_MUTATION_INVALID_ARGUMENT: return "invalid-argument";
        case GB_GAMEPLAY_MUTATION_ABI_MISMATCH: return "abi-mismatch";
        case GB_GAMEPLAY_MUTATION_WRONG_PHASE: return "wrong-phase";
        case GB_GAMEPLAY_MUTATION_ROM_MISMATCH: return "rom-mismatch";
        case GB_GAMEPLAY_MUTATION_HOOK_MISMATCH: return "hook-mismatch";
        case GB_GAMEPLAY_MUTATION_FIELD_MISMATCH: return "field-mismatch";
        case GB_GAMEPLAY_MUTATION_OUT_OF_RANGE: return "out-of-range";
        case GB_GAMEPLAY_MUTATION_TRANSACTION_FAILED:
            return "transaction-failed";
        case GB_GAMEPLAY_MUTATION_VALIDATION_FAILED:
            return "validation-failed";
        default: return "unknown";
    }
}

const char* gb_gameplay_mutation_policy_reason_string(
    GBGameplayMutationPolicyReason reason) {
    switch (reason) {
        case GB_GAMEPLAY_MUTATION_POLICY_REASON_APPLIED: return "applied";
        case GB_GAMEPLAY_MUTATION_POLICY_REASON_NO_REFERENCE:
            return "no-reference";
        case GB_GAMEPLAY_MUTATION_POLICY_REASON_INVALID_INPUT:
            return "invalid-input";
        default: return "unknown";
    }
}

GBGameplayMutationStatus gb_native_apply_gameplay_mutation(
    GBNativeCall* call,
    const GBGameplayMutationSpec* spec,
    const GBGameplayMutationRequest* request) {
    GBSemanticTransaction transaction = {0};
    GBGameplayMutationValidation validation;
    uint8_t expected_digest[32];
    size_t index;

    if (call == NULL || spec == NULL || request == NULL ||
        call->ctx == NULL || call->binding == NULL ||
        spec->event_id == NULL || spec->event_id[0] == '\0' ||
        spec->fields == NULL || request->values == NULL ||
        spec->field_count == 0u ||
        spec->field_count > GB_GAMEPLAY_MUTATION_MAX_FIELDS ||
        request->value_count != spec->field_count) {
        return GB_GAMEPLAY_MUTATION_INVALID_ARGUMENT;
    }
    if (spec->abi_version != GB_GAMEPLAY_MUTATION_ABI_VERSION ||
        request->abi_version != GB_GAMEPLAY_MUTATION_ABI_VERSION) {
        return GB_GAMEPLAY_MUTATION_ABI_MISMATCH;
    }
    if (call->phase != GB_NATIVE_PHASE_PRE) {
        return GB_GAMEPLAY_MUTATION_WRONG_PHASE;
    }
    if (spec->function_id != call->binding->function_id ||
        spec->function_id != gb_native_function_id(call)) {
        return GB_GAMEPLAY_MUTATION_HOOK_MISMATCH;
    }
    if (spec->rom_size != call->binding->rom_size ||
        !decode_sha256(spec->rom_sha256, expected_digest) ||
        memcmp(
            expected_digest,
            call->binding->rom_sha256,
            sizeof(expected_digest)) != 0) {
        return GB_GAMEPLAY_MUTATION_ROM_MISMATCH;
    }

    for (index = 0; index < spec->field_count; ++index) {
        const GBGameplayMutationFieldSpec* field = &spec->fields[index];
        const GBGameplayMutationValue* value = NULL;
        size_t matches = 0;
        size_t other;
        if (field->field_id == NULL || field->field_id[0] == '\0' ||
            field->type != GB_GAMEPLAY_MUTATION_U8 ||
            field->minimum > field->maximum || field->maximum > UINT8_MAX) {
            return GB_GAMEPLAY_MUTATION_INVALID_ARGUMENT;
        }
        for (other = index + 1u; other < spec->field_count; ++other) {
            if (spec->fields[other].field_id != NULL &&
                strcmp(field->field_id, spec->fields[other].field_id) == 0) {
                return GB_GAMEPLAY_MUTATION_FIELD_MISMATCH;
            }
        }
        value = find_mutation_value(request, field->field_id, &matches);
        if (matches != 1u || value == NULL) {
            return GB_GAMEPLAY_MUTATION_FIELD_MISMATCH;
        }
        if (value->value < field->minimum || value->value > field->maximum) {
            return GB_GAMEPLAY_MUTATION_OUT_OF_RANGE;
        }
    }

    if (gbrt_semantic_transaction_begin(
            &transaction,
            call->ctx,
            spec->rom_sha256,
            spec->rom_sha256) != GB_SEMANTIC_OK) {
        return GB_GAMEPLAY_MUTATION_TRANSACTION_FAILED;
    }
    for (index = 0; index < spec->field_count; ++index) {
        const GBGameplayMutationFieldSpec* field = &spec->fields[index];
        const GBGameplayMutationValue* value = NULL;
        size_t matches = 0;
        uint8_t encoded;
        value = find_mutation_value(request, field->field_id, &matches);
        encoded = (uint8_t)value->value;
        if (gbrt_semantic_transaction_write(
                &transaction,
                field->space,
                field->bank,
                field->address,
                &encoded,
                sizeof(encoded)) != GB_SEMANTIC_OK) {
            gbrt_semantic_transaction_abort(&transaction);
            return GB_GAMEPLAY_MUTATION_TRANSACTION_FAILED;
        }
    }

    validation = (GBGameplayMutationValidation){spec, request};
    if (gbrt_semantic_transaction_validate(
            &transaction,
            validate_gameplay_mutation,
            &validation) != GB_SEMANTIC_OK) {
        return GB_GAMEPLAY_MUTATION_VALIDATION_FAILED;
    }
    if (gbrt_semantic_transaction_commit(&transaction) != GB_SEMANTIC_OK) {
        return GB_GAMEPLAY_MUTATION_TRANSACTION_FAILED;
    }
    return GB_GAMEPLAY_MUTATION_APPLIED;
}
