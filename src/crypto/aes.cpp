//
// aes.cpp — Freestanding AES-128 and AES-CCMP Implementation
//
#include "aes.h"

#if defined(KERNEL) || defined(__APPLE_KEXT__)
#include <sys/errno.h>
#include <string.h>
#else
#include <cerrno>
#include <cstring>
#endif

static const uint8_t sbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5e, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16
};

static const uint32_t rcon[10] = {
    0x01000000, 0x02000000, 0x04000000, 0x08000000,
    0x10000000, 0x20000000, 0x40000000, 0x80000000,
    0x1B000000, 0x36000000
};

static inline uint32_t sub_word(uint32_t w) {
    return (static_cast<uint32_t>(sbox[(w >> 24) & 0xFF]) << 24) |
           (static_cast<uint32_t>(sbox[(w >> 16) & 0xFF]) << 16) |
           (static_cast<uint32_t>(sbox[(w >> 8)  & 0xFF]) << 8)  |
           (static_cast<uint32_t>(sbox[w & 0xFF]));
}

static inline uint32_t rot_word(uint32_t w) {
    return (w << 8) | (w >> 24);
}

void aes128_set_encrypt_key(aes_key_ctx *ctx, const uint8_t key[16]) {
    for (int i = 0; i < 4; i++) {
        ctx->round_keys[i] = (static_cast<uint32_t>(key[4 * i]) << 24) |
                             (static_cast<uint32_t>(key[4 * i + 1]) << 16) |
                             (static_cast<uint32_t>(key[4 * i + 2]) << 8) |
                             (static_cast<uint32_t>(key[4 * i + 3]));
    }
    for (int i = 4; i < 44; i++) {
        uint32_t temp = ctx->round_keys[i - 1];
        if (i % 4 == 0) {
            temp = sub_word(rot_word(temp)) ^ rcon[i / 4 - 1];
        }
        ctx->round_keys[i] = ctx->round_keys[i - 4] ^ temp;
    }
}

static inline uint8_t xtime(uint8_t x) {
    return static_cast<uint8_t>((x << 1) ^ (((x >> 7) & 1) * 0x1B));
}

void aes128_encrypt_block(const aes_key_ctx *ctx, const uint8_t in[16], uint8_t out[16]) {
    uint8_t state[4][4];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            state[j][i] = in[i * 4 + j];
        }
    }

    // Round 0: AddRoundKey
    for (int i = 0; i < 4; i++) {
        uint32_t rk = ctx->round_keys[i];
        state[0][i] ^= static_cast<uint8_t>((rk >> 24) & 0xFF);
        state[1][i] ^= static_cast<uint8_t>((rk >> 16) & 0xFF);
        state[2][i] ^= static_cast<uint8_t>((rk >> 8) & 0xFF);
        state[3][i] ^= static_cast<uint8_t>(rk & 0xFF);
    }

    // Rounds 1..9
    for (int round = 1; round <= 9; round++) {
        // SubBytes
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                state[i][j] = sbox[state[i][j]];
            }
        }
        // ShiftRows
        uint8_t t;
        // row 1: rot 1
        t = state[1][0]; state[1][0] = state[1][1]; state[1][1] = state[1][2]; state[1][2] = state[1][3]; state[1][3] = t;
        // row 2: rot 2
        t = state[2][0]; state[2][0] = state[2][2]; state[2][2] = t;
        t = state[2][1]; state[2][1] = state[2][3]; state[2][3] = t;
        // row 3: rot 3
        t = state[3][3]; state[3][3] = state[3][2]; state[3][2] = state[3][1]; state[3][1] = state[3][0]; state[3][0] = t;

        // MixColumns
        for (int i = 0; i < 4; i++) {
            uint8_t a = state[0][i], b = state[1][i], c = state[2][i], d = state[3][i];
            state[0][i] = xtime(a ^ b) ^ b ^ c ^ d;
            state[1][i] = xtime(b ^ c) ^ c ^ d ^ a;
            state[2][i] = xtime(c ^ d) ^ d ^ a ^ b;
            state[3][i] = xtime(d ^ a) ^ a ^ b ^ c;
        }

        // AddRoundKey
        for (int i = 0; i < 4; i++) {
            uint32_t rk = ctx->round_keys[round * 4 + i];
            state[0][i] ^= static_cast<uint8_t>((rk >> 24) & 0xFF);
            state[1][i] ^= static_cast<uint8_t>((rk >> 16) & 0xFF);
            state[2][i] ^= static_cast<uint8_t>((rk >> 8) & 0xFF);
            state[3][i] ^= static_cast<uint8_t>(rk & 0xFF);
        }
    }

    // Round 10 (Final, no MixColumns)
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            state[i][j] = sbox[state[i][j]];
        }
    }
    uint8_t t;
    t = state[1][0]; state[1][0] = state[1][1]; state[1][1] = state[1][2]; state[1][2] = state[1][3]; state[1][3] = t;
    t = state[2][0]; state[2][0] = state[2][2]; state[2][2] = t;
    t = state[2][1]; state[2][1] = state[2][3]; state[2][3] = t;
    t = state[3][3]; state[3][3] = state[3][2]; state[3][2] = state[3][1]; state[3][1] = state[3][0]; state[3][0] = t;

    for (int i = 0; i < 4; i++) {
        uint32_t rk = ctx->round_keys[40 + i];
        state[0][i] ^= static_cast<uint8_t>((rk >> 24) & 0xFF);
        state[1][i] ^= static_cast<uint8_t>((rk >> 16) & 0xFF);
        state[2][i] ^= static_cast<uint8_t>((rk >> 8) & 0xFF);
        state[3][i] ^= static_cast<uint8_t>(rk & 0xFF);
    }

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            out[i * 4 + j] = state[j][i];
        }
    }
}

// AES-CCMP Helper: Encrypt payload and append 8-byte MIC
int aes_ccmp_encrypt(const uint8_t tk[16], uint64_t pn,
                     const uint8_t *hdr, size_t hdr_len,
                     const uint8_t *payload, size_t payload_len,
                     uint8_t *out_encrypted, size_t &out_encrypted_len) {
    if (!tk || !hdr || hdr_len < 24) {
        return -EINVAL;
    }

    aes_key_ctx ctx;
    aes128_set_encrypt_key(&ctx, tk);

    // Build CCMP Header (8 bytes):
    // PN0, PN1, Resv, KeyID (bit 5 ExtKey=1), PN2, PN3, PN4, PN5
    uint8_t ccmp_hdr[8];
    ccmp_hdr[0] = static_cast<uint8_t>(pn & 0xFF);
    ccmp_hdr[1] = static_cast<uint8_t>((pn >> 8) & 0xFF);
    ccmp_hdr[2] = 0x00;
    ccmp_hdr[3] = 0x20; // ExtIV bit set
    ccmp_hdr[4] = static_cast<uint8_t>((pn >> 16) & 0xFF);
    ccmp_hdr[5] = static_cast<uint8_t>((pn >> 24) & 0xFF);
    ccmp_hdr[6] = static_cast<uint8_t>((pn >> 32) & 0xFF);
    ccmp_hdr[7] = static_cast<uint8_t>((pn >> 40) & 0xFF);

    // Build Nonce (13 bytes): Priority (1) + Addr2 (6) + PN (6)
    uint8_t nonce[13];
    nonce[0] = 0x00; // Normal data priority
    memcpy(&nonce[1], &hdr[10], 6); // Addr2 (TA)
    nonce[7] = ccmp_hdr[7];
    nonce[8] = ccmp_hdr[6];
    nonce[9] = ccmp_hdr[5];
    nonce[10] = ccmp_hdr[4];
    nonce[11] = ccmp_hdr[1];
    nonce[12] = ccmp_hdr[0];

    // Compute CBC-MAC for MIC (8 bytes)
    uint8_t b0[16] = {0};
    b0[0] = 0x59; // Flags: Adata=1, M'=1 (8 bytes MIC), L'=1 (2 octets)
    memcpy(&b0[1], nonce, 13);
    b0[14] = static_cast<uint8_t>((payload_len >> 8) & 0xFF);
    b0[15] = static_cast<uint8_t>(payload_len & 0xFF);

    uint8_t mac[16];
    aes128_encrypt_block(&ctx, b0, mac);

    // Adata: AAD length (2 bytes) + masked 802.11 header
    uint8_t aad_block[32] = {0};
    size_t aad_len = 22; // FC(masked) + Addr1 + Addr2 + Addr3 + SC(masked)
    aad_block[0] = 0x00;
    aad_block[1] = static_cast<uint8_t>(aad_len);
    // Masked FC: zero retry, pwr_mgt, more_data, protected
    aad_block[2] = hdr[0] & 0x8F;
    aad_block[3] = hdr[1] & 0xC7;
    memcpy(&aad_block[4], &hdr[4], 18); // Addr1, Addr2, Addr3
    aad_block[22] = 0; // SC masked
    aad_block[23] = 0;

    for (int i = 0; i < 16; i++) mac[i] ^= aad_block[i];
    aes128_encrypt_block(&ctx, mac, mac);
    for (int i = 0; i < 16; i++) mac[i] ^= aad_block[16 + i];
    aes128_encrypt_block(&ctx, mac, mac);

    // Payload blocks into CBC-MAC
    size_t pos = 0;
    while (pos < payload_len) {
        size_t chunk = (payload_len - pos < 16) ? (payload_len - pos) : 16;
        for (size_t i = 0; i < chunk; i++) {
            mac[i] ^= payload[pos + i];
        }
        aes128_encrypt_block(&ctx, mac, mac);
        pos += chunk;
    }

    // CTR mode encryption
    // S_0 block for MIC encryption
    uint8_t a0[16] = {0};
    a0[0] = 0x01; // Flags: L'=1
    memcpy(&a0[1], nonce, 13);
    a0[14] = 0x00;
    a0[15] = 0x00;
    uint8_t s0[16];
    aes128_encrypt_block(&ctx, a0, s0);
    uint8_t mic[8];
    for (int i = 0; i < 8; i++) {
        mic[i] = mac[i] ^ s0[i];
    }

    // Encrypt payload with CTR
    size_t out_pos = 0;
    memcpy(&out_encrypted[out_pos], ccmp_hdr, 8);
    out_pos += 8;

    uint16_t ctr = 1;
    pos = 0;
    while (pos < payload_len) {
        uint8_t ai[16] = {0};
        ai[0] = 0x01;
        memcpy(&ai[1], nonce, 13);
        ai[14] = static_cast<uint8_t>((ctr >> 8) & 0xFF);
        ai[15] = static_cast<uint8_t>(ctr & 0xFF);
        uint8_t si[16];
        aes128_encrypt_block(&ctx, ai, si);

        size_t chunk = (payload_len - pos < 16) ? (payload_len - pos) : 16;
        for (size_t i = 0; i < chunk; i++) {
            out_encrypted[out_pos + i] = payload[pos + i] ^ si[i];
        }
        out_pos += chunk;
        pos += chunk;
        ctr++;
    }

    // Append encrypted MIC
    memcpy(&out_encrypted[out_pos], mic, 8);
    out_pos += 8;

    out_encrypted_len = out_pos;
    return 0;
}

int aes_ccmp_decrypt(const uint8_t tk[16], const uint8_t *frame, size_t frame_len,
                     uint8_t *out_payload, size_t &out_payload_len) {
    if (!tk || !frame || frame_len < 24 + 8 + 8) { // 802.11 hdr + CCMP hdr + 8-byte MIC
        return -EINVAL;
    }

    const uint8_t *hdr = frame;
    size_t hdr_len = 24;
    const uint8_t *ccmp_hdr = frame + hdr_len;
    size_t cipher_len = frame_len - hdr_len - 8 - 8;
    const uint8_t *ciphertext = ccmp_hdr + 8;
    const uint8_t *rx_mic = ciphertext + cipher_len;

    // Nonce from CCMP header
    uint8_t nonce[13];
    nonce[0] = 0x00;
    memcpy(&nonce[1], &hdr[10], 6); // Addr2 (TA)
    nonce[7] = ccmp_hdr[7];
    nonce[8] = ccmp_hdr[6];
    nonce[9] = ccmp_hdr[5];
    nonce[10] = ccmp_hdr[4];
    nonce[11] = ccmp_hdr[1];
    nonce[12] = ccmp_hdr[0];

    aes_key_ctx ctx;
    aes128_set_encrypt_key(&ctx, tk);

    // Decrypt payload with CTR
    uint16_t ctr = 1;
    size_t pos = 0;
    while (pos < cipher_len) {
        uint8_t ai[16] = {0};
        ai[0] = 0x01;
        memcpy(&ai[1], nonce, 13);
        ai[14] = static_cast<uint8_t>((ctr >> 8) & 0xFF);
        ai[15] = static_cast<uint8_t>(ctr & 0xFF);
        uint8_t si[16];
        aes128_encrypt_block(&ctx, ai, si);

        size_t chunk = (cipher_len - pos < 16) ? (cipher_len - pos) : 16;
        for (size_t i = 0; i < chunk; i++) {
            out_payload[pos + i] = ciphertext[pos + i] ^ si[i];
        }
        pos += chunk;
        ctr++;
    }
    out_payload_len = cipher_len;

    // Verify MIC with CBC-MAC
    uint8_t b0[16] = {0};
    b0[0] = 0x59;
    memcpy(&b0[1], nonce, 13);
    b0[14] = static_cast<uint8_t>((cipher_len >> 8) & 0xFF);
    b0[15] = static_cast<uint8_t>(cipher_len & 0xFF);

    uint8_t mac[16];
    aes128_encrypt_block(&ctx, b0, mac);

    uint8_t aad_block[32] = {0};
    size_t aad_len = 22;
    aad_block[0] = 0x00;
    aad_block[1] = static_cast<uint8_t>(aad_len);
    aad_block[2] = hdr[0] & 0x8F;
    aad_block[3] = hdr[1] & 0xC7;
    memcpy(&aad_block[4], &hdr[4], 18);

    for (int i = 0; i < 16; i++) mac[i] ^= aad_block[i];
    aes128_encrypt_block(&ctx, mac, mac);
    for (int i = 0; i < 16; i++) mac[i] ^= aad_block[16 + i];
    aes128_encrypt_block(&ctx, mac, mac);

    pos = 0;
    while (pos < cipher_len) {
        size_t chunk = (cipher_len - pos < 16) ? (cipher_len - pos) : 16;
        for (size_t i = 0; i < chunk; i++) {
            mac[i] ^= out_payload[pos + i];
        }
        aes128_encrypt_block(&ctx, mac, mac);
        pos += chunk;
    }

    uint8_t a0[16] = {0};
    a0[0] = 0x01;
    memcpy(&a0[1], nonce, 13);
    uint8_t s0[16];
    aes128_encrypt_block(&ctx, a0, s0);
    uint8_t calculated_mic[8];
    for (int i = 0; i < 8; i++) {
        calculated_mic[i] = mac[i] ^ s0[i];
    }

    if (memcmp(calculated_mic, rx_mic, 8) != 0) {
        return -EBADMSG; // MIC verification failed!
    }

    return 0;
}
