//
// sha1.h — Freestanding SHA-1 and HMAC-SHA1 Implementation
//
#pragma once

#include <cstddef>
#include <cstdint>

struct sha1_ctx {
    uint32_t state[5];
    uint32_t count[2];
    uint8_t  buffer[64];
};

void sha1_init(sha1_ctx *ctx);
void sha1_update(sha1_ctx *ctx, const uint8_t *data, size_t len);
void sha1_final(sha1_ctx *ctx, uint8_t digest[20]);

// Direct one-shot SHA1
void sha1_calc(const uint8_t *data, size_t len, uint8_t digest[20]);

// HMAC-SHA1
void hmac_sha1(const uint8_t *key, size_t key_len,
               const uint8_t *data, size_t data_len,
               uint8_t digest[20]);

// PBKDF2-SHA1 (for PMK generation in WPA/WPA2)
void pbkdf2_sha1(const char *passphrase, size_t pass_len,
                 const uint8_t *salt, size_t salt_len,
                 uint32_t iterations,
                 uint8_t *out_key, size_t out_len);
