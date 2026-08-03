#include "challenge_rules.h"

#include <stdint.h>
#include <string.h>

static GBGameplayMutationPolicyResult crystal_challenge_result(
    const GBGameplayMutationPolicyInput* input) {
    GBGameplayMutationPolicyResult result;
    memset(&result, 0, sizeof(result));
    result.abi_version = GB_GAMEPLAY_MUTATION_ABI_VERSION;
    result.policy_id = CRYSTAL_CHALLENGE_RULESET_ID;
    result.outcome = GB_GAMEPLAY_MUTATION_POLICY_INVALID_INPUT;
    result.reason = GB_GAMEPLAY_MUTATION_POLICY_REASON_INVALID_INPUT;
    if (input != NULL) {
        result.original_value = input->original_value;
        result.reference_value = input->reference_value;
        result.progress_value = input->progress_value;
        result.offset = input->offset;
        result.minimum = input->minimum;
        result.maximum = input->maximum;
        result.proposed_value = input->original_value;
    }
    return result;
}

GBGameplayMutationPolicyResult crystal_challenge_calculate_level(
    const GBGameplayMutationPolicyInput* input) {
    GBGameplayMutationPolicyResult result = crystal_challenge_result(input);
    int64_t scaled_reference;
    uint32_t candidate;

    if (input == NULL ||
        input->abi_version != GB_GAMEPLAY_MUTATION_ABI_VERSION ||
        input->original_value < CRYSTAL_CHALLENGE_LEVEL_MIN ||
        input->original_value > CRYSTAL_CHALLENGE_LEVEL_MAX ||
        input->reference_value > CRYSTAL_CHALLENGE_LEVEL_MAX ||
        input->progress_value > CRYSTAL_CHALLENGE_BADGE_MAX ||
        input->offset < CRYSTAL_CHALLENGE_OFFSET_MIN ||
        input->offset > CRYSTAL_CHALLENGE_OFFSET_MAX ||
        input->minimum < CRYSTAL_CHALLENGE_LEVEL_MIN ||
        input->maximum > CRYSTAL_CHALLENGE_LEVEL_MAX ||
        input->minimum > input->maximum) {
        return result;
    }

    if (input->reference_value == 0u) {
        result.outcome = GB_GAMEPLAY_MUTATION_POLICY_PRESERVE_ORIGINAL;
        result.reason = GB_GAMEPLAY_MUTATION_POLICY_REASON_NO_REFERENCE;
        return result;
    }

    result.baseline_value = input->original_value > input->reference_value
        ? input->original_value
        : input->reference_value;
    result.progress_adjustment = input->progress_value / 4u;
    scaled_reference = (int64_t)input->reference_value +
        (int64_t)result.progress_adjustment + (int64_t)input->offset;
    if (scaled_reference < 0) scaled_reference = 0;
    candidate = (uint32_t)scaled_reference;
    if (candidate < input->original_value) candidate = input->original_value;
    if (candidate < input->minimum) {
        candidate = input->minimum;
        result.clamped_minimum = 1u;
    }
    if (candidate > input->maximum) {
        candidate = input->maximum;
        result.clamped_maximum = 1u;
    }

    result.outcome = GB_GAMEPLAY_MUTATION_POLICY_APPLY;
    result.reason = GB_GAMEPLAY_MUTATION_POLICY_REASON_APPLIED;
    result.proposed_value = candidate;
    return result;
}
