#ifndef GBRT_HOST_CONFIGURATION_H
#define GBRT_HOST_CONFIGURATION_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GB_HOST_CONFIGURATION_ABI_VERSION 1u
#define GB_HOST_CONFIGURATION_ID_CAPACITY 64u
#define GB_HOST_CONFIGURATION_HASH_CAPACITY 65u
#define GB_HOST_CONFIGURATION_CANONICAL_CAPACITY 512u

typedef enum GBHostConfigurationStatus {
    GB_HOST_CONFIGURATION_OK = 0,
    GB_HOST_CONFIGURATION_MISSING,
    GB_HOST_CONFIGURATION_IO_ERROR,
    GB_HOST_CONFIGURATION_TOO_LARGE,
    GB_HOST_CONFIGURATION_MALFORMED,
    GB_HOST_CONFIGURATION_NON_CANONICAL,
    GB_HOST_CONFIGURATION_ABI_MISMATCH,
    GB_HOST_CONFIGURATION_SCHEMA_MISMATCH,
    GB_HOST_CONFIGURATION_POLICY_MISMATCH,
    GB_HOST_CONFIGURATION_OUT_OF_RANGE,
    GB_HOST_CONFIGURATION_WRITE_ERROR,
} GBHostConfigurationStatus;

typedef struct GBHostConfigurationContract {
    uint32_t abi_version;
    const char* schema;
    uint32_t schema_version;
    const char* policy_id;
    int32_t offset_minimum;
    int32_t offset_maximum;
    uint32_t value_minimum;
    uint32_t value_maximum;
} GBHostConfigurationContract;

typedef struct GBHostConfiguration {
    uint32_t abi_version;
    uint8_t present;
    uint8_t applied;
    uint8_t enabled;
    char schema[GB_HOST_CONFIGURATION_ID_CAPACITY];
    uint32_t schema_version;
    char policy_id[GB_HOST_CONFIGURATION_ID_CAPACITY];
    int32_t offset;
    uint32_t minimum;
    uint32_t maximum;
    char sha256[GB_HOST_CONFIGURATION_HASH_CAPACITY];
} GBHostConfiguration;

GBHostConfigurationStatus gbrt_host_configuration_parse(
    const uint8_t* bytes,
    size_t size,
    const GBHostConfigurationContract* contract,
    GBHostConfiguration* output);

GBHostConfigurationStatus gbrt_host_configuration_load_file(
    const char* path,
    const GBHostConfigurationContract* contract,
    GBHostConfiguration* output);

GBHostConfigurationStatus gbrt_host_configuration_write_file(
    const char* path,
    const GBHostConfiguration* configuration);

GBHostConfigurationStatus gbrt_host_configuration_serialize(
    const GBHostConfiguration* configuration,
    char* output,
    size_t capacity,
    size_t* output_size);

const char* gbrt_host_configuration_status_string(
    GBHostConfigurationStatus status);

#ifdef __cplusplus
}
#endif

#endif
