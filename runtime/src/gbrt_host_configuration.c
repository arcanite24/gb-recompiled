#include "gbrt_host_configuration.h"

#include "gbrt_hash.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

static int portable_id(const char* value) {
    size_t index;
    const size_t size = value == NULL ? 0u : strlen(value);
    if (size == 0u || size >= GB_HOST_CONFIGURATION_ID_CAPACITY) return 0;
    for (index = 0; index < size; ++index) {
        const unsigned char ch = (unsigned char)value[index];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' ||
              ch == '-')) {
            return 0;
        }
    }
    return 1;
}

static void hash_hex(const uint8_t* bytes, size_t size, char output[65]) {
    static const char digits[] = "0123456789abcdef";
    uint8_t digest[32];
    size_t index;
    gbrt_sha256(bytes, size, digest);
    for (index = 0; index < sizeof(digest); ++index) {
        output[index * 2u] = digits[digest[index] >> 4u];
        output[index * 2u + 1u] = digits[digest[index] & 0x0fu];
    }
    output[64] = '\0';
}

GBHostConfigurationStatus gbrt_host_configuration_serialize(
    const GBHostConfiguration* configuration,
    char* output,
    size_t capacity,
    size_t* output_size) {
    int written;
    if (configuration == NULL || output == NULL || output_size == NULL ||
        configuration->abi_version != GB_HOST_CONFIGURATION_ABI_VERSION ||
        !portable_id(configuration->schema) ||
        !portable_id(configuration->policy_id) ||
        (configuration->applied != 0u && configuration->applied != 1u) ||
        (configuration->enabled != 0u && configuration->enabled != 1u)) {
        return GB_HOST_CONFIGURATION_ABI_MISMATCH;
    }
    written = snprintf(
        output,
        capacity,
        "{\"schema\":\"%s\",\"version\":%u,\"policy_id\":\"%s\","
        "\"applied\":%s,\"enabled\":%s,\"offset\":%d,\"minimum\":%u,"
        "\"maximum\":%u}\n",
        configuration->schema,
        configuration->schema_version,
        configuration->policy_id,
        configuration->applied ? "true" : "false",
        configuration->enabled ? "true" : "false",
        configuration->offset,
        configuration->minimum,
        configuration->maximum);
    if (written < 0 || (size_t)written >= capacity) {
        return GB_HOST_CONFIGURATION_TOO_LARGE;
    }
    *output_size = (size_t)written;
    return GB_HOST_CONFIGURATION_OK;
}

GBHostConfigurationStatus gbrt_host_configuration_parse(
    const uint8_t* bytes,
    size_t size,
    const GBHostConfigurationContract* contract,
    GBHostConfiguration* output) {
    GBHostConfiguration parsed = {0};
    char text[GB_HOST_CONFIGURATION_CANONICAL_CAPACITY];
    char applied[6] = {0};
    char enabled[6] = {0};
    char canonical[GB_HOST_CONFIGURATION_CANONICAL_CAPACITY];
    size_t canonical_size = 0;
    int consumed = 0;
    int matched;
    if (output == NULL) return GB_HOST_CONFIGURATION_MALFORMED;
    memset(output, 0, sizeof(*output));
    if (bytes == NULL || contract == NULL || contract->schema == NULL ||
        contract->policy_id == NULL) {
        return GB_HOST_CONFIGURATION_MALFORMED;
    }
    if (contract->abi_version != GB_HOST_CONFIGURATION_ABI_VERSION) {
        return GB_HOST_CONFIGURATION_ABI_MISMATCH;
    }
    if (size == 0u || size >= sizeof(text)) {
        return size == 0u ? GB_HOST_CONFIGURATION_MALFORMED
                          : GB_HOST_CONFIGURATION_TOO_LARGE;
    }
    memcpy(text, bytes, size);
    text[size] = '\0';
    parsed.abi_version = GB_HOST_CONFIGURATION_ABI_VERSION;
    matched = sscanf(
        text,
        "{\"schema\":\"%63[^\"]\",\"version\":%u,"
        "\"policy_id\":\"%63[^\"]\",\"applied\":%5[a-z],"
        "\"enabled\":%5[a-z],\"offset\":%d,\"minimum\":%u,"
        "\"maximum\":%u}\n%n",
        parsed.schema,
        &parsed.schema_version,
        parsed.policy_id,
        applied,
        enabled,
        &parsed.offset,
        &parsed.minimum,
        &parsed.maximum,
        &consumed);
    if (matched != 8 || consumed != (int)size ||
        (strcmp(applied, "true") != 0 && strcmp(applied, "false") != 0) ||
        (strcmp(enabled, "true") != 0 && strcmp(enabled, "false") != 0) ||
        !portable_id(parsed.schema) || !portable_id(parsed.policy_id)) {
        return GB_HOST_CONFIGURATION_MALFORMED;
    }
    parsed.applied = (uint8_t)(strcmp(applied, "true") == 0);
    parsed.enabled = (uint8_t)(strcmp(enabled, "true") == 0);
    if (gbrt_host_configuration_serialize(
            &parsed,
            canonical,
            sizeof(canonical),
            &canonical_size) != GB_HOST_CONFIGURATION_OK ||
        canonical_size != size || memcmp(canonical, bytes, size) != 0) {
        return GB_HOST_CONFIGURATION_NON_CANONICAL;
    }
    if (strcmp(parsed.schema, contract->schema) != 0 ||
        parsed.schema_version != contract->schema_version) {
        return GB_HOST_CONFIGURATION_SCHEMA_MISMATCH;
    }
    if (strcmp(parsed.policy_id, contract->policy_id) != 0) {
        return GB_HOST_CONFIGURATION_POLICY_MISMATCH;
    }
    if (parsed.offset < contract->offset_minimum ||
        parsed.offset > contract->offset_maximum ||
        parsed.minimum < contract->value_minimum ||
        parsed.maximum > contract->value_maximum ||
        parsed.minimum > parsed.maximum) {
        return GB_HOST_CONFIGURATION_OUT_OF_RANGE;
    }
    parsed.present = 1u;
    hash_hex(bytes, size, parsed.sha256);
    *output = parsed;
    return GB_HOST_CONFIGURATION_OK;
}

GBHostConfigurationStatus gbrt_host_configuration_load_file(
    const char* path,
    const GBHostConfigurationContract* contract,
    GBHostConfiguration* output) {
    FILE* file;
    uint8_t bytes[GB_HOST_CONFIGURATION_CANONICAL_CAPACITY];
    size_t size;
    int trailing;
    if (output == NULL) return GB_HOST_CONFIGURATION_MALFORMED;
    memset(output, 0, sizeof(*output));
    if (path == NULL || path[0] == '\0') return GB_HOST_CONFIGURATION_MISSING;
    errno = 0;
    file = fopen(path, "rb");
    if (file == NULL) {
        return errno == ENOENT ? GB_HOST_CONFIGURATION_MISSING
                               : GB_HOST_CONFIGURATION_IO_ERROR;
    }
    size = fread(bytes, 1, sizeof(bytes), file);
    trailing = fgetc(file);
    if (ferror(file)) {
        fclose(file);
        return GB_HOST_CONFIGURATION_IO_ERROR;
    }
    fclose(file);
    if (size == sizeof(bytes) || trailing != EOF) {
        return GB_HOST_CONFIGURATION_TOO_LARGE;
    }
    return gbrt_host_configuration_parse(bytes, size, contract, output);
}

GBHostConfigurationStatus gbrt_host_configuration_write_file(
    const char* path,
    const GBHostConfiguration* configuration) {
    char canonical[GB_HOST_CONFIGURATION_CANONICAL_CAPACITY];
    char temporary[4096];
    size_t size = 0;
    FILE* file;
    int descriptor;
    GBHostConfigurationStatus status = gbrt_host_configuration_serialize(
        configuration, canonical, sizeof(canonical), &size);
    if (status != GB_HOST_CONFIGURATION_OK || path == NULL || path[0] == '\0' ||
        snprintf(temporary, sizeof(temporary), "%s.tmp-v1", path) < 0 ||
        strlen(temporary) >= sizeof(temporary) - 1u) {
        return GB_HOST_CONFIGURATION_WRITE_ERROR;
    }
    file = fopen(temporary, "wb");
    if (file == NULL) return GB_HOST_CONFIGURATION_WRITE_ERROR;
    if (fwrite(canonical, 1, size, file) != size || fflush(file) != 0) {
        fclose(file);
        remove(temporary);
        return GB_HOST_CONFIGURATION_WRITE_ERROR;
    }
#ifdef _WIN32
    descriptor = _fileno(file);
    if (descriptor < 0 || _commit(descriptor) != 0) {
#else
    descriptor = fileno(file);
    if (descriptor < 0 || fsync(descriptor) != 0) {
#endif
        fclose(file);
        remove(temporary);
        return GB_HOST_CONFIGURATION_WRITE_ERROR;
    }
    if (fclose(file) != 0) {
        remove(temporary);
        return GB_HOST_CONFIGURATION_WRITE_ERROR;
    }
#ifdef _WIN32
    if (!MoveFileExA(
            temporary,
            path,
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
#else
    if (rename(temporary, path) != 0) {
#endif
        remove(temporary);
        return GB_HOST_CONFIGURATION_WRITE_ERROR;
    }
    return GB_HOST_CONFIGURATION_OK;
}

const char* gbrt_host_configuration_status_string(
    GBHostConfigurationStatus status) {
    switch (status) {
        case GB_HOST_CONFIGURATION_OK: return "ok";
        case GB_HOST_CONFIGURATION_MISSING: return "missing";
        case GB_HOST_CONFIGURATION_IO_ERROR: return "io-error";
        case GB_HOST_CONFIGURATION_TOO_LARGE: return "too-large";
        case GB_HOST_CONFIGURATION_MALFORMED: return "malformed";
        case GB_HOST_CONFIGURATION_NON_CANONICAL: return "non-canonical";
        case GB_HOST_CONFIGURATION_ABI_MISMATCH: return "abi-mismatch";
        case GB_HOST_CONFIGURATION_SCHEMA_MISMATCH: return "schema-mismatch";
        case GB_HOST_CONFIGURATION_POLICY_MISMATCH: return "policy-mismatch";
        case GB_HOST_CONFIGURATION_OUT_OF_RANGE: return "out-of-range";
        case GB_HOST_CONFIGURATION_WRITE_ERROR: return "write-error";
        default: return "unknown";
    }
}
