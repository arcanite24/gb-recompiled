#ifndef GBRT_HASH_H
#define GBRT_HASH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void gbrt_sha256(const uint8_t* data, size_t size, uint8_t digest[32]);
int gbrt_sha256_matches_hex(
    const uint8_t* data,
    size_t size,
    const char* expected_hex);

#ifdef __cplusplus
}
#endif

#endif
