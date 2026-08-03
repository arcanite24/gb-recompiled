#include "gbrt_gameplay_mutation.h"
#include "gbrt_native_patch_internal.h"

#include <stdint.h>
#include <string.h>

#define TRAINER_FUNCTION_ID GB_NATIVE_FUNCTION_ID(0x0003, 0x588c)
#define WRAM0(address_) ((size_t)(address_) - 0xc000u)
#define WRAM1(address_) (0x1000u + (size_t)(address_) - 0xd000u)
#define PARTY_MON_1 0xdcdfu

GBNativeStatus crystal_challenge_trainer_level(GBNativeCall* call);

static const uint8_t crystal_digest[32] = {
    0xfd, 0xcc, 0x3c, 0x8c, 0x43, 0x81, 0x3c, 0xf8,
    0x73, 0x1f, 0xc0, 0x37, 0xd2, 0xa6, 0xd1, 0x91,
    0xba, 0xc7, 0x54, 0x39, 0xc3, 0x4b, 0x24, 0xba,
    0x1c, 0x27, 0x52, 0x6e, 0x6a, 0xcd, 0xc8, 0xa2,
};

typedef struct Fixture {
    uint8_t rom[1];
    uint8_t wram[0x8000];
    GBContext context;
    GBNativeBinding binding;
    GBNativeCall call;
} Fixture;

static void reset_fixture(Fixture* fixture) {
    memset(fixture, 0, sizeof(*fixture));
    fixture->context.rom = fixture->rom;
    fixture->context.rom_size = 2097152u;
    fixture->context.wram = fixture->wram;
    fixture->binding.abi_version = GB_NATIVE_PATCH_ABI_VERSION;
    fixture->binding.patch_id = "org.gbrecompiled.crystal.challenge-mode";
    fixture->binding.function_id = TRAINER_FUNCTION_ID;
    fixture->binding.rom_size = 2097152u;
    memcpy(
        fixture->binding.rom_sha256,
        crystal_digest,
        sizeof(crystal_digest));
    fixture->call.ctx = &fixture->context;
    fixture->call.binding = &fixture->binding;
    fixture->call.phase = GB_NATIVE_PHASE_PRE;

    fixture->context.config.host_configuration.abi_version =
        GB_HOST_CONFIGURATION_ABI_VERSION;
    fixture->context.config.host_configuration.present = 1;
    fixture->context.config.host_configuration.applied = 1;
    fixture->context.config.host_configuration.enabled = 1;
    strcpy(
        fixture->context.config.host_configuration.schema,
        "gbrecomp.host-configuration");
    fixture->context.config.host_configuration.schema_version = 1;
    strcpy(
        fixture->context.config.host_configuration.policy_id,
        "challenge-v1");
    fixture->context.config.host_configuration.offset = 3;
    fixture->context.config.host_configuration.minimum = 1;
    fixture->context.config.host_configuration.maximum = 100;

    fixture->wram[WRAM1(0xd22d)] = 0; /* party is built before battle mode */
    fixture->wram[WRAM1(0xd22f)] = 1; /* reviewed trainer class */
    fixture->wram[WRAM1(0xd230)] = 0; /* normal battle type */
    fixture->wram[WRAM1(0xd143)] = 5;
    fixture->wram[WRAM1(0xd280)] = 0;
    fixture->wram[WRAM0(0xcf5f)] = 1; /* opposing party mon */
    fixture->wram[WRAM0(0xc2dc)] = 0; /* not link mode */
    fixture->wram[WRAM0(0xcfc0)] = 0; /* not Battle Tower */
    fixture->wram[WRAM1(0xdcd7)] = 1; /* one player party member */
    fixture->wram[WRAM1(PARTY_MON_1 + 31u)] = 5;
    fixture->wram[WRAM1(PARTY_MON_1 + 34u)] = 0;
    fixture->wram[WRAM1(PARTY_MON_1 + 35u)] = 1; /* conscious */
}

static int run_guarded(Fixture* fixture) {
    if (crystal_challenge_trainer_level(&fixture->call) !=
        GB_NATIVE_STATUS_OK) {
        return 0;
    }
    return fixture->wram[WRAM1(0xd143)] == 5 &&
           fixture->context.semantic_transaction_sequence == 0 &&
           fixture->context.semantic_transaction_outcome ==
               GB_SEMANTIC_TRANSACTION_NONE;
}

int main(void) {
    Fixture fixture;
    reset_fixture(&fixture);
    if (crystal_challenge_trainer_level(&fixture.call) !=
            GB_NATIVE_STATUS_OK ||
        fixture.wram[WRAM1(0xd143)] != 8 ||
        fixture.context.semantic_transaction_sequence != 1 ||
        fixture.context.semantic_transaction_outcome !=
            GB_SEMANTIC_TRANSACTION_COMMITTED ||
        fixture.context.semantic_transaction_dirty_count != 1 ||
        fixture.context.semantic_transaction_dirty[0].space !=
            GB_SEMANTIC_BANKED_WRAM ||
        fixture.context.semantic_transaction_dirty[0].bank != 1 ||
        fixture.context.semantic_transaction_dirty[0].address != 0xd143 ||
        fixture.context.semantic_transaction_dirty[0].width != 1) {
        return 1;
    }

    reset_fixture(&fixture);
    fixture.wram[WRAM1(0xdcd7)] = 2;
    fixture.wram[WRAM1(PARTY_MON_1 + 48u + 31u)] = 20;
    fixture.wram[WRAM1(PARTY_MON_1 + 48u + 34u)] = 0;
    fixture.wram[WRAM1(PARTY_MON_1 + 48u + 35u)] = 0; /* fainted */
    if (crystal_challenge_trainer_level(&fixture.call) !=
            GB_NATIVE_STATUS_OK ||
        fixture.wram[WRAM1(0xd143)] != 8) {
        return 1;
    }

    reset_fixture(&fixture);
    fixture.wram[WRAM1(0xdcd7)] = 2;
    fixture.wram[WRAM1(PARTY_MON_1 + 48u + 31u)] = 20;
    fixture.wram[WRAM1(PARTY_MON_1 + 48u + 34u)] = 0;
    fixture.wram[WRAM1(PARTY_MON_1 + 48u + 35u)] = 1; /* conscious */
    fixture.wram[WRAM1(0xd857)] = 0x0f; /* four badges = +1 */
    if (crystal_challenge_trainer_level(&fixture.call) !=
            GB_NATIVE_STATUS_OK ||
        fixture.wram[WRAM1(0xd143)] != 24) {
        return 1;
    }

    reset_fixture(&fixture);
    fixture.wram[WRAM1(0xdcd7)] = 0;
    if (!run_guarded(&fixture)) return 1;

    reset_fixture(&fixture);
    fixture.context.config.host_configuration.present = 0;
    if (!run_guarded(&fixture)) return 1;

    reset_fixture(&fixture);
    fixture.context.config.host_configuration.applied = 0;
    if (!run_guarded(&fixture)) return 1;

    reset_fixture(&fixture);
    fixture.context.config.host_configuration.enabled = 0;
    if (!run_guarded(&fixture)) return 1;

    reset_fixture(&fixture);
    strcpy(
        fixture.context.config.host_configuration.policy_id,
        "wrong-v1");
    if (!run_guarded(&fixture)) return 1;

    reset_fixture(&fixture);
    fixture.wram[WRAM1(0xdcd7)] = 7;
    if (!run_guarded(&fixture)) return 1;

    for (uint8_t mon_type = 0; mon_type <= 4; ++mon_type) {
        if (mon_type == 1) continue;
        reset_fixture(&fixture);
        fixture.wram[WRAM0(0xcf5f)] = mon_type;
        if (!run_guarded(&fixture)) return 1;
    }

    reset_fixture(&fixture);
    fixture.wram[WRAM0(0xc2dc)] = 1;
    if (!run_guarded(&fixture)) return 1;

    reset_fixture(&fixture);
    fixture.wram[WRAM0(0xcfc0)] = 1;
    if (!run_guarded(&fixture)) return 1;

    reset_fixture(&fixture);
    fixture.wram[WRAM1(0xd230)] = 1;
    if (crystal_challenge_trainer_level(&fixture.call) !=
            GB_NATIVE_STATUS_OK ||
        fixture.wram[WRAM1(0xd143)] != 8) {
        return 1;
    }

    reset_fixture(&fixture);
    fixture.wram[WRAM1(0xd230)] = 2;
    if (!run_guarded(&fixture)) return 1;

    reset_fixture(&fixture);
    fixture.wram[WRAM1(0xd22f)] = 0;
    if (!run_guarded(&fixture)) return 1;

    reset_fixture(&fixture);
    fixture.wram[WRAM1(0xd280)] = 6;
    if (!run_guarded(&fixture)) return 1;

    reset_fixture(&fixture);
    fixture.wram[WRAM1(0xd143)] = 0;
    if (crystal_challenge_trainer_level(&fixture.call) !=
            GB_NATIVE_STATUS_OK ||
        fixture.wram[WRAM1(0xd143)] != 0 ||
        fixture.context.semantic_transaction_sequence != 0) {
        return 1;
    }

    reset_fixture(&fixture);
    fixture.wram[WRAM1(0xd143)] = 101;
    if (crystal_challenge_trainer_level(&fixture.call) !=
            GB_NATIVE_STATUS_OK ||
        fixture.wram[WRAM1(0xd143)] != 101 ||
        fixture.context.semantic_transaction_sequence != 0) {
        return 1;
    }

    return 0;
}
