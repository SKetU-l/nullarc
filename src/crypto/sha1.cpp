//
// sha1.cpp — Freestanding SHA-1, HMAC-SHA1, and PBKDF2-SHA1 Implementation
//
#include "sha1.h"

#include <cstring>

static inline uint32_t rol32(uint32_t value, uint32_t bits) {
    return (value << bits) | (value >> (32 - bits));
}

static inline uint32_t get_be32(const uint8_t *p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
           (static_cast<uint32_t>(p[3]));
}

static inline void put_be32(uint8_t *p, uint32_t v) {
    p[0] = static_cast<uint8_t>((v >> 24) & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 16) & 0xFF);
    p[2] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[3] = static_cast<uint8_t>(v & 0xFF);
}

static void sha1_transform(uint32_t state[5], const uint8_t buffer[64]) {
    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t e = state[4];
    uint32_t w[80];

    for (int i = 0; i < 16; i++) {
        w[i] = get_be32(&buffer[i * 4]);
    }
    for (int i = 16; i < 80; i++) {
        w[i] = rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    // Round 1 (0..19)
    for (int i = 0; i < 20; i++) {
        uint32_t f = (b & c) | ((~b) & d);
        uint32_t temp = rol32(a, 5) + f + e + 0x5A827999U + w[i];
        e = d;
        d = c;
        c = rol32(b, 30);
        b = a;
        a = temp;
    }

    // Round 2 (20..39)
    for (int i = 20; i < 40; i++) {
        uint32_t f = b ^ c ^ d;
        uint32_t temp = rol32(a, 5) + f + e + 0x6ED9EBA1U + w[i];
        e = d;
        d = c;
        c = rol32(b, 30);
        b = a;
        a = temp;
    }

    // Round 3 (40..59)
    for (int i = 40; i < 60; i++) {
        uint32_t f = (b & c) | (b & d) | (c & d);
        uint32_t temp = rol32(a, 5) + f + e + 0x8F1BBCDCU + w[i];
        e = d;
        d = c;
        c = rol32(b, 30);
        b = a;
        a = temp;
    }

    // Round 4 (60..79)
    for (int i = 60; i < 80; i++) {
        uint32_t f = b ^ c ^ d;
        uint32_t temp = rol32(a, 5) + f + e + 0xCA62C1D6U + w[i];
        e = d;
        d = c;
        c = rol32(b, 30);
        b = a;
        a = temp;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

void sha1_init(sha1_ctx *ctx) {
    ctx->state[0] = 0x67452301U;
    ctx->state[1] = 0xEFCDAB89U;
    ctx->state[2] = 0x98BADCFEU;
    ctx->state[3] = 0x10325476U;
    ctx->state[4] = 0xC3D2E1F0U;
    ctx->count[0] = 0;
    ctx->count[1] = 0;
    memset(ctx->buffer, 0, sizeof(ctx->buffer));
}

void sha1_update(sha1_ctx *ctx, const uint8_t *data, size_t len) {
    size_t i = 0;
    size_t j = (ctx->count[0] >> 3) & 63;
    if ((ctx->count[0] += static_cast<uint32_t>(len << 3)) < static_cast<uint32_t>(len << 3)) {
        ctx->count[1]++;
    }
    ctx->count[1] += static_cast<uint32_t>(len >> 29);

    if ((j + len) > 63) {
        memcpy(&ctx->buffer[j], data, (i = 64 - j));
        sha1_transform(ctx->state, ctx->buffer);
        for (; i + 63 < len; i += 64) {
            sha1_transform(ctx->state, &data[i]);
        }
        j = 0;
    }
    memcpy(&ctx->buffer[j], &data[i], len - i);
}

void sha1_final(sha1_ctx *ctx, uint8_t digest[20]) {
    uint8_t finalcount[8];
    for (int i = 0; i < 8; i++) {
        finalcount[i] = static_cast<uint8_t>((ctx->count[(i >= 4 ? 0 : 1)] >> ((3 - (i & 3)) * 8)) & 255);
    }
    uint8_t c = 0x80;
    sha1_update(ctx, &c, 1);
    while ((ctx->count[0] & 504) != 448) {
        c = 0x00;
        sha1_update(ctx, &c, 1);
    }
    sha1_update(ctx, finalcount, 8);
    for (int i = 0; i < 20; i++) {
        digest[i] = static_cast<uint8_t>((ctx->state[i >> 2] >> ((3 - (i & 3)) * 8)) & 255);
    }
}

void sha1_calc(const uint8_t *data, size_t len, uint8_t digest[20]) {
    sha1_ctx ctx;
    sha1_init(&ctx);
    sha1_update(&ctx, data, len);
    sha1_final(&ctx, digest);
}

void hmac_sha1(const uint8_t *key, size_t key_len,
               const uint8_t *data, size_t data_len,
               uint8_t digest[20]) {
    uint8_t k_pad[64];
    uint8_t key_hashed[20];

    if (key_len > 64) {
        sha1_calc(key, key_len, key_hashed);
        key = key_hashed;
        key_len = 20;
    }

    // Inner pad: key ^ 0x36
    memset(k_pad, 0x36, 64);
    for (size_t i = 0; i < key_len; i++) {
        k_pad[i] ^= key[i];
    }

    sha1_ctx ctx;
    sha1_init(&ctx);
    sha1_update(&ctx, k_pad, 64);
    sha1_update(&ctx, data, data_len);
    uint8_t inner_hash[20];
    sha1_final(&ctx, inner_hash);

    // Outer pad: key ^ 0x5c
    memset(k_pad, 0x5C, 64);
    for (size_t i = 0; i < key_len; i++) {
        k_pad[i] ^= key[i];
    }

    sha1_init(&ctx);
    sha1_update(&ctx, k_pad, 64);
    sha1_update(&ctx, inner_hash, 20);
    sha1_final(&ctx, digest);
}

void pbkdf2_sha1(const char *passphrase, size_t pass_len,
                 const uint8_t *salt, size_t salt_len,
                 uint32_t iterations,
                 uint8_t *out_key, size_t out_len) {
    uint8_t asalt[128];
    if (salt_len + 4 > sizeof(asalt)) return;

    memcpy(asalt, salt, salt_len);
    size_t key_idx = 0;
    uint32_t block_num = 1;

    while (key_idx < out_len) {
        put_be32(&asalt[salt_len], block_num);

        uint8_t u_curr[20];
        uint8_t u_xor[20];

        // U_1 = PRF(P, S || INT(i))
        hmac_sha1(reinterpret_cast<const uint8_t *>(passphrase), pass_len,
                  asalt, salt_len + 4, u_curr);
        memcpy(u_xor, u_curr, 20);

        // U_2 ... U_c
        for (uint32_t iter = 1; iter < iterations; iter++) {
            hmac_sha1(reinterpret_cast<const uint8_t *>(passphrase), pass_len,
                      u_curr, 20, u_curr);
            for (int k = 0; k < 20; k++) {
                u_xor[k] ^= u_curr[k];
            }
        }

        size_t to_copy = (out_len - key_idx < 20) ? (out_len - key_idx) : 20;
        memcpy(&out_key[key_idx], u_xor, to_copy);
        key_idx += to_copy;
        block_num++;
    }
}
