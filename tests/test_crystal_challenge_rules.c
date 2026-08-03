#include "challenge_rules.h"

#include <stdint.h>
#include <string.h>

static GBGameplayMutationPolicyResult calculate(
    uint32_t original,
    uint32_t reference,
    uint32_t badges,
    int32_t offset,
    uint32_t minimum,
    uint32_t maximum) {
    const GBGameplayMutationPolicyInput input = {
        GB_GAMEPLAY_MUTATION_ABI_VERSION,
        original,
        reference,
        badges,
        offset,
        minimum,
        maximum,
    };
    return crystal_challenge_calculate_level(&input);
}

static int expect_apply(
    GBGameplayMutationPolicyResult result,
    uint32_t expected,
    uint32_t baseline,
    uint32_t progress_adjustment,
    int clamped_minimum,
    int clamped_maximum) {
    return result.outcome == GB_GAMEPLAY_MUTATION_POLICY_APPLY &&
           result.reason == GB_GAMEPLAY_MUTATION_POLICY_REASON_APPLIED &&
           result.proposed_value == expected &&
           result.baseline_value == baseline &&
           result.progress_adjustment == progress_adjustment &&
           result.clamped_minimum == clamped_minimum &&
           result.clamped_maximum == clamped_maximum;
}

int main(void) {
    GBGameplayMutationPolicyResult result;

    result = calculate(2, 5, 0, 3, 1, 100);
    if (!expect_apply(result, 8, 5, 0, 0, 0) ||
        result.abi_version != GB_GAMEPLAY_MUTATION_ABI_VERSION ||
        strcmp(result.policy_id, CRYSTAL_CHALLENGE_RULESET_ID) != 0 ||
        result.original_value != 2 || result.reference_value != 5 ||
        result.progress_value != 0 || result.offset != 3 ||
        result.minimum != 1 || result.maximum != 100) {
        return 1;
    }

    result = calculate(10, 8, 4, 0, 1, 100);
    if (!expect_apply(result, 10, 10, 1, 0, 0)) return 1;

    result = calculate(5, 8, 3, 0, 1, 100);
    if (!expect_apply(result, 8, 8, 0, 0, 0)) return 1;
    result = calculate(5, 8, 4, 0, 1, 100);
    if (!expect_apply(result, 9, 8, 1, 0, 0)) return 1;

    result = calculate(2, 1, 0, -5, 1, 100);
    if (!expect_apply(result, 2, 2, 0, 0, 0)) return 1;
    result = calculate(2, 1, 0, -5, 5, 100);
    if (!expect_apply(result, 5, 2, 0, 1, 0)) return 1;
    result = calculate(98, 99, 16, 5, 1, 100);
    if (!expect_apply(result, 100, 99, 4, 0, 1)) return 1;

    result = calculate(42, 0, 8, 3, 1, 100);
    if (result.outcome != GB_GAMEPLAY_MUTATION_POLICY_PRESERVE_ORIGINAL ||
        result.reason != GB_GAMEPLAY_MUTATION_POLICY_REASON_NO_REFERENCE ||
        result.proposed_value != 42) {
        return 1;
    }

    result = calculate(5, 8, 17, 0, 1, 100);
    if (result.outcome != GB_GAMEPLAY_MUTATION_POLICY_INVALID_INPUT ||
        result.reason != GB_GAMEPLAY_MUTATION_POLICY_REASON_INVALID_INPUT) {
        return 1;
    }
    result = calculate(0, 8, 0, 0, 1, 100);
    if (result.outcome != GB_GAMEPLAY_MUTATION_POLICY_INVALID_INPUT) return 1;
    result = calculate(5, 101, 0, 0, 1, 100);
    if (result.outcome != GB_GAMEPLAY_MUTATION_POLICY_INVALID_INPUT) return 1;
    result = calculate(5, 8, 0, -6, 1, 100);
    if (result.outcome != GB_GAMEPLAY_MUTATION_POLICY_INVALID_INPUT) return 1;
    result = calculate(5, 8, 0, 6, 1, 100);
    if (result.outcome != GB_GAMEPLAY_MUTATION_POLICY_INVALID_INPUT) return 1;
    result = calculate(5, 8, 0, 0, 20, 10);
    if (result.outcome != GB_GAMEPLAY_MUTATION_POLICY_INVALID_INPUT) return 1;

    const GBGameplayMutationPolicyResult repeated =
        calculate(31, 28, 12, 2, 1, 100);
    for (int index = 0; index < 100; ++index) {
        const GBGameplayMutationPolicyResult next =
            calculate(31, 28, 12, 2, 1, 100);
        if (memcmp(&repeated, &next, sizeof(repeated)) != 0) return 1;
    }

    if (strcmp(
            gb_gameplay_mutation_policy_reason_string(
                GB_GAMEPLAY_MUTATION_POLICY_REASON_APPLIED),
            "applied") != 0 ||
        strcmp(
            gb_gameplay_mutation_policy_reason_string(
                GB_GAMEPLAY_MUTATION_POLICY_REASON_NO_REFERENCE),
            "no-reference") != 0 ||
        strcmp(
            gb_gameplay_mutation_policy_reason_string(
                GB_GAMEPLAY_MUTATION_POLICY_REASON_INVALID_INPUT),
            "invalid-input") != 0) {
        return 1;
    }

    return 0;
}
