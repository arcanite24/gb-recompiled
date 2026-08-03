#ifndef CRYSTAL_CHALLENGE_RULES_H
#define CRYSTAL_CHALLENGE_RULES_H

#include "gbrt_gameplay_mutation.h"

#define CRYSTAL_CHALLENGE_RULESET_ID "challenge-v1"
#define CRYSTAL_CHALLENGE_OFFSET_MIN (-5)
#define CRYSTAL_CHALLENGE_OFFSET_MAX 5
#define CRYSTAL_CHALLENGE_BADGE_MAX 16u
#define CRYSTAL_CHALLENGE_LEVEL_MIN 1u
#define CRYSTAL_CHALLENGE_LEVEL_MAX 100u

GBGameplayMutationPolicyResult crystal_challenge_calculate_level(
    const GBGameplayMutationPolicyInput* input);

#endif
