#include "gbrt_hash.h"

#include <stdlib.h>
#include <string.h>

static uint32_t rotate_right(uint32_t value, unsigned count) {
    return (value >> count) | (value << (32u - count));
}

void gbrt_sha256(const uint8_t* data, size_t size, uint8_t digest[32]) {
    static const uint32_t k[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
        0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
        0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
        0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988daU,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
        0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
        0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
        0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
        0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
        0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
    };
    uint32_t h[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
    };
    if (digest == NULL || (data == NULL && size != 0)) {
        return;
    }
    const uint64_t bit_size = (uint64_t)size * 8u;
    const size_t padded_size = ((size + 9u + 63u) / 64u) * 64u;
    uint8_t* padded = (uint8_t*)calloc(1, padded_size);
    if (padded == NULL) {
        memset(digest, 0, 32u);
        return;
    }
    if (size != 0) memcpy(padded, data, size);
    padded[size] = 0x80u;
    for (size_t index = 0; index < 8u; ++index) {
        padded[padded_size - 1u - index] =
            (uint8_t)(bit_size >> (index * 8u));
    }

    for (size_t offset = 0; offset < padded_size; offset += 64u) {
        uint32_t w[64] = {0};
        for (size_t index = 0; index < 16u; ++index) {
            const size_t position = offset + index * 4u;
            w[index] = ((uint32_t)padded[position] << 24u) |
                       ((uint32_t)padded[position + 1u] << 16u) |
                       ((uint32_t)padded[position + 2u] << 8u) |
                       (uint32_t)padded[position + 3u];
        }
        for (size_t index = 16u; index < 64u; ++index) {
            const uint32_t s0 =
                rotate_right(w[index - 15u], 7u) ^
                rotate_right(w[index - 15u], 18u) ^
                (w[index - 15u] >> 3u);
            const uint32_t s1 =
                rotate_right(w[index - 2u], 17u) ^
                rotate_right(w[index - 2u], 19u) ^
                (w[index - 2u] >> 10u);
            w[index] =
                w[index - 16u] + s0 + w[index - 7u] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
        for (size_t index = 0; index < 64u; ++index) {
            const uint32_t s1 =
                rotate_right(e, 6u) ^ rotate_right(e, 11u) ^
                rotate_right(e, 25u);
            const uint32_t choose = (e & f) ^ ((~e) & g);
            const uint32_t temp1 =
                hh + s1 + choose + k[index] + w[index];
            const uint32_t s0 =
                rotate_right(a, 2u) ^ rotate_right(a, 13u) ^
                rotate_right(a, 22u);
            const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = s0 + majority;
            hh = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += hh;
    }
    free(padded);
    for (size_t index = 0; index < 8u; ++index) {
        digest[index * 4u] = (uint8_t)(h[index] >> 24u);
        digest[index * 4u + 1u] = (uint8_t)(h[index] >> 16u);
        digest[index * 4u + 2u] = (uint8_t)(h[index] >> 8u);
        digest[index * 4u + 3u] = (uint8_t)h[index];
    }
}

static int hex_value(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

int gbrt_sha256_matches_hex(
    const uint8_t* data,
    size_t size,
    const char* expected_hex) {
    uint8_t expected[32];
    uint8_t actual[32];
    if (expected_hex == NULL || strlen(expected_hex) != 64u) return 0;
    for (size_t index = 0; index < 32u; ++index) {
        const int high = hex_value(expected_hex[index * 2u]);
        const int low = hex_value(expected_hex[index * 2u + 1u]);
        if (high < 0 || low < 0) return 0;
        expected[index] = (uint8_t)((high << 4) | low);
    }
    gbrt_sha256(data, size, actual);
    return memcmp(actual, expected, sizeof(actual)) == 0;
}
