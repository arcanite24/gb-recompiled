#ifndef GBRT_GAMEPLAY_MUTATION_H
#define GBRT_GAMEPLAY_MUTATION_H

#include "gbrt_native_patch.h"
#include "gbrt_semantic.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GB_GAMEPLAY_MUTATION_ABI_VERSION 1u
#define GB_GAMEPLAY_MUTATION_MAX_FIELDS 8u

typedef enum GBGameplayMutationValueType {
    GB_GAMEPLAY_MUTATION_U8 = 1,
} GBGameplayMutationValueType;

typedef enum GBGameplayMutationStatus {
    GB_GAMEPLAY_MUTATION_APPLIED = 0,
    GB_GAMEPLAY_MUTATION_INVALID_ARGUMENT,
    GB_GAMEPLAY_MUTATION_ABI_MISMATCH,
    GB_GAMEPLAY_MUTATION_WRONG_PHASE,
    GB_GAMEPLAY_MUTATION_ROM_MISMATCH,
    GB_GAMEPLAY_MUTATION_HOOK_MISMATCH,
    GB_GAMEPLAY_MUTATION_FIELD_MISMATCH,
    GB_GAMEPLAY_MUTATION_OUT_OF_RANGE,
    GB_GAMEPLAY_MUTATION_TRANSACTION_FAILED,
    GB_GAMEPLAY_MUTATION_VALIDATION_FAILED,
} GBGameplayMutationStatus;

typedef enum GBGameplayMutationPolicyOutcome {
    GB_GAMEPLAY_MUTATION_POLICY_APPLY = 0,
    GB_GAMEPLAY_MUTATION_POLICY_PRESERVE_ORIGINAL,
    GB_GAMEPLAY_MUTATION_POLICY_INVALID_INPUT,
} GBGameplayMutationPolicyOutcome;

typedef enum GBGameplayMutationPolicyReason {
    GB_GAMEPLAY_MUTATION_POLICY_REASON_APPLIED = 0,
    GB_GAMEPLAY_MUTATION_POLICY_REASON_NO_REFERENCE,
    GB_GAMEPLAY_MUTATION_POLICY_REASON_INVALID_INPUT,
} GBGameplayMutationPolicyReason;

/*
 * Game-agnostic scalar inputs and result envelope for deterministic port
 * policies. The port owns the calculation; the runtime owns the stable ABI
 * shape and explanation vocabulary used by diagnostics and provenance.
 */
typedef struct GBGameplayMutationPolicyInput {
    uint32_t abi_version;
    uint32_t original_value;
    uint32_t reference_value;
    uint32_t progress_value;
    int32_t offset;
    uint32_t minimum;
    uint32_t maximum;
} GBGameplayMutationPolicyInput;

typedef struct GBGameplayMutationPolicyResult {
    uint32_t abi_version;
    const char* policy_id;
    GBGameplayMutationPolicyOutcome outcome;
    GBGameplayMutationPolicyReason reason;
    uint32_t original_value;
    uint32_t reference_value;
    uint32_t progress_value;
    int32_t offset;
    uint32_t minimum;
    uint32_t maximum;
    uint32_t baseline_value;
    uint32_t progress_adjustment;
    uint32_t proposed_value;
    uint8_t clamped_minimum;
    uint8_t clamped_maximum;
} GBGameplayMutationPolicyResult;

typedef struct GBGameplayMutationFieldSpec {
    const char* field_id;
    GBGameplayMutationValueType type;
    GBSemanticMemorySpace space;
    uint16_t bank;
    uint16_t address;
    uint32_t minimum;
    uint32_t maximum;
} GBGameplayMutationFieldSpec;

typedef struct GBGameplayMutationSpec {
    uint32_t abi_version;
    const char* event_id;
    const char* rom_sha256;
    size_t rom_size;
    GBNativeFunctionId function_id;
    const GBGameplayMutationFieldSpec* fields;
    size_t field_count;
} GBGameplayMutationSpec;

typedef struct GBGameplayMutationValue {
    const char* field_id;
    uint32_t value;
} GBGameplayMutationValue;

typedef struct GBGameplayMutationRequest {
    uint32_t abi_version;
    const GBGameplayMutationValue* values;
    size_t value_count;
    GBSemanticValidateFn validate;
    void* validate_user;
} GBGameplayMutationRequest;

/*
 * Apply every requested field as one synchronous semantic transaction.
 * This API is valid only from the pre phase of the exact native binding named
 * by the specification. A non-APPLIED result never commits a partial write.
 */
GBGameplayMutationStatus gb_native_apply_gameplay_mutation(
    GBNativeCall* call,
    const GBGameplayMutationSpec* spec,
    const GBGameplayMutationRequest* request);

const char* gb_gameplay_mutation_status_string(
    GBGameplayMutationStatus status);

const char* gb_gameplay_mutation_policy_reason_string(
    GBGameplayMutationPolicyReason reason);

#ifdef __cplusplus
}
#endif

#endif
