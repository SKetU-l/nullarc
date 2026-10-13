//
// aes.h — Freestanding AES-128 and AES-CCMP Primitives
//
#pragma once

#if defined(KERNEL) || defined(__APPLE_KEXT__)
#include <stddef.h>
#include <stdint.h>
#else
#include <cstddef>
#include <cstdint>
#endif

struct aes_key_ctx {
    uint32_t round_keys[44]; // 10 rounds for AES-128
};

void aes128_set_encrypt_key(aes_key_ctx *ctx, const uint8_t key[16]);
void aes128_encrypt_block(const aes_key_ctx *ctx, const uint8_t in[16], uint8_t out[16]);

// AES-CCMP 802.11 Data Decryption / Verification
int aes_ccmp_decrypt(const uint8_t tk[16], const uint8_t *frame, size_t frame_len,
                     uint8_t *out_payload, size_t &out_payload_len);

// AES-CCMP 802.11 Data Encryption / MIC Generation
int aes_ccmp_encrypt(const uint8_t tk[16], uint64_t pn,
                     const uint8_t *hdr, size_t hdr_len,
                     const uint8_t *payload, size_t payload_len,
                     uint8_t *out_encrypted, size_t &out_encrypted_len);
