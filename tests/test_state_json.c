#include "gbrt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_HRAM_SIZE 0x7Fu
#define TEST_ERAM_PREFIX_SIZE 0x100u

static GBContext* make_context(void) {
    GBConfig config;
    memset(&config, 0, sizeof(config));

    GBContext* ctx = gb_context_create(&config);
    if (!ctx) {
        return NULL;
    }

    uint8_t rom[32u * 1024u];
    memset(rom, 0, sizeof(rom));
    rom[0x147] = 0x03; /* MBC1 + RAM + battery */
    rom[0x149] = 0x02; /* 8 KiB external RAM */
    if (!gb_context_load_rom(ctx, rom, sizeof(rom))) {
        gb_context_destroy(ctx);
        return NULL;
    }
    gb_context_reset(ctx, true);
    return ctx;
}

int main(void) {
    GBContext* ctx = make_context();
    if (!ctx) {
        return 2;
    }
    for (size_t i = 0; i < TEST_HRAM_SIZE; ++i) {
        ctx->hram[i] = (uint8_t)i;
    }
    for (size_t i = 0; i < TEST_ERAM_PREFIX_SIZE; ++i) {
        ctx->eram[i] = (uint8_t)(i ^ 0xA5u);
    }
    ctx->semantic_transaction_sequence = 7;
    ctx->semantic_transaction_outcome = GB_SEMANTIC_TRANSACTION_COMMITTED;
    ctx->semantic_transaction_dirty_count = 1;
    ctx->semantic_transaction_dirty[0] =
        (GBSemanticTransactionRangeMetadata){
            .space = 1,
            .bank = 1,
            .address = 0xA865,
            .width = 428,
        };
    ctx->config.host_configuration.present = 1;
    ctx->config.host_configuration.applied = 1;
    ctx->config.host_configuration.enabled = 1;
    strcpy(ctx->config.host_configuration.policy_id, "challenge-v1");
    strcpy(
        ctx->config.host_configuration.sha256,
        "f9ee2131f80d8194535bc7cd50fc26593516afb948506869350a14b5a5353f30");

    const char* path = "test_state_json_output.json";
    if (!gb_context_write_state_json(ctx, path)) {
        fputs("failed to write state JSON\n", stderr);
        gb_context_destroy(ctx);
        return 1;
    }

    FILE* file = fopen(path, "rb");
    if (!file) {
        fputs("failed to reopen state JSON\n", stderr);
        gb_context_destroy(ctx);
        return 1;
    }
    char buffer[65536];
    const size_t length = fread(buffer, 1, sizeof(buffer) - 1, file);
    fclose(file);
    remove(path);
    buffer[length] = '\0';

    char* cursor = strstr(buffer, "\"hram_ff80_fffe\": [");
    if (!cursor) {
        fputs("state JSON omitted the full HRAM field\n", stderr);
        gb_context_destroy(ctx);
        return 1;
    }
    cursor = strchr(cursor, '[') + 1;
    for (unsigned expected = 0; expected < TEST_HRAM_SIZE; ++expected) {
        char* end = NULL;
        const unsigned long actual = strtoul(cursor, &end, 10);
        if (end == cursor || actual != expected) {
            fprintf(stderr, "full HRAM field stopped or diverged at byte %u\n",
                    expected);
            gb_context_destroy(ctx);
            return 1;
        }
        cursor = end;
        while (*cursor == ' ' || *cursor == ',') {
            cursor++;
        }
    }
    if (*cursor != ']') {
        fputs("full HRAM field contains an unexpected byte count\n", stderr);
        gb_context_destroy(ctx);
        return 1;
    }

    cursor = strstr(buffer, "\"eram_a000_a0ff\": [");
    if (!cursor) {
        fputs("state JSON omitted the Blargg external-RAM prefix\n", stderr);
        gb_context_destroy(ctx);
        return 1;
    }
    cursor = strchr(cursor, '[') + 1;
    for (unsigned i = 0; i < TEST_ERAM_PREFIX_SIZE; ++i) {
        char* end = NULL;
        const unsigned long actual = strtoul(cursor, &end, 10);
        const unsigned expected = i ^ 0xA5u;
        if (end == cursor || actual != expected) {
            fprintf(stderr,
                    "external-RAM state prefix stopped or diverged at byte %u\n",
                    i);
            gb_context_destroy(ctx);
            return 1;
        }
        cursor = end;
        while (*cursor == ' ' || *cursor == ',') {
            cursor++;
        }
    }
    if (*cursor != ']') {
        fputs("external-RAM state prefix has an unexpected byte count\n", stderr);
        gb_context_destroy(ctx);
        return 1;
    }

    if (!strstr(buffer, "\"wram_bank_0_c000_cfff\": [")) {
        fputs("state JSON omitted fixed WRAM bank 0\n", stderr);
        gb_context_destroy(ctx);
        return 1;
    }
    if (!strstr(buffer, "\"wram_bank_1_d000_dfff\": [")) {
        fputs("state JSON omitted switchable WRAM bank 1\n", stderr);
        gb_context_destroy(ctx);
        return 1;
    }
    if (!strstr(
            buffer,
            "\"host_configuration\": {\"present\": true, "
            "\"applied\": true, \"enabled\": true, "
            "\"policy_id\": \"challenge-v1\", "
            "\"sha256\": \"f9ee2131f80d8194535bc7cd50fc2659")) {
        fputs("state JSON omitted path-free host configuration identity\n", stderr);
        gb_context_destroy(ctx);
        return 1;
    }
    if (!strstr(
            buffer,
            "\"semantic_transaction\": {\"sequence\": 7, "
            "\"outcome\": \"committed\"") ||
        !strstr(
            buffer,
            "{\"space\": 1, \"bank\": 1, \"address\": 43109, "
            "\"width\": 428}")) {
        fputs("state JSON omitted semantic transaction metadata\n", stderr);
        gb_context_destroy(ctx);
        return 1;
    }

    gb_context_destroy(ctx);
    return 0;
}
