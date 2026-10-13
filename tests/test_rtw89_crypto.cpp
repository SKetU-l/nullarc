//
// test_rtw89_crypto.cpp — Unit tests for SHA1, AES-CCMP, and WPA2-PSK 4-Way Handshake
//
#include "aes.h"
#include "sha1.h"
#include "skbuff.h"
#include "wpa2_psk.h"

#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <vector>

static void test_sha1_and_hmac() {
    printf("[*] Testing SHA-1 and HMAC-SHA1 RFC test vectors...\n");

    // NIST FIPS 180-1: sha1("abc") == a9993e364706816aba3e25717850c26c9cd0d89d
    const uint8_t nist_sha1_expected[20] = {
        0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a, 0xba, 0x3e,
        0x25, 0x71, 0x78, 0x50, 0xc2, 0x6c, 0x9c, 0xd0, 0xd8, 0x9d
    };
    uint8_t digest[20];
    sha1_calc(reinterpret_cast<const uint8_t *>("abc"), 3, digest);
    assert(memcmp(digest, nist_sha1_expected, 20) == 0);

    // RFC 2202 Test Case 1: key = 20 bytes of 0x0b, data = "Hi There"
    // Digest: b617318655057264e28bc0b6fb378c8ef146be00
    const uint8_t rfc2202_expected[20] = {
        0xb6, 0x17, 0x31, 0x86, 0x55, 0x05, 0x72, 0x64, 0xe2, 0x8b,
        0xc0, 0xb6, 0xfb, 0x37, 0x8c, 0x8e, 0xf1, 0x46, 0xbe, 0x00
    };
    uint8_t hmac_key[20];
    memset(hmac_key, 0x0b, 20);
    const char *hmac_data = "Hi There";
    hmac_sha1(hmac_key, 20, reinterpret_cast<const uint8_t *>(hmac_data), strlen(hmac_data), digest);
    assert(memcmp(digest, rfc2202_expected, 20) == 0);

    printf("    -> SHA-1 and HMAC-SHA1 PASSED\n");
}

static void test_pbkdf2_sha1_ieee() {
    printf("[*] Testing PBKDF2-SHA1 IEEE 802.11i test vector (4096 iterations)...\n");

    // IEEE 802.11i Appendix I-1:
    // Passphrase = "password", SSID = "IEEE", iterations = 4096
    // Expected PMK:
    // f42c6fc52df0ebef9ebb4b90b38a5f902e83fe1b135a70e23aed762e9710a12e
    const uint8_t ieee_pmk_expected[32] = {
        0xf4, 0x2c, 0x6f, 0xc5, 0x2d, 0xf0, 0xeb, 0xef,
        0x9e, 0xbb, 0x4b, 0x90, 0xb3, 0x8a, 0x5f, 0x90,
        0x2e, 0x83, 0xfe, 0x1b, 0x13, 0x5a, 0x70, 0xe2,
        0x3a, 0xed, 0x76, 0x2e, 0x97, 0x10, 0xa1, 0x2e
    };

    uint8_t pmk[32];
    pbkdf2_sha1("password", 8, reinterpret_cast<const uint8_t *>("IEEE"), 4, 4096, pmk, 32);
    assert(memcmp(pmk, ieee_pmk_expected, 32) == 0);

    printf("    -> IEEE 802.11i PBKDF2-SHA1 PMK matches exactly!\n");
}

static void test_aes128_and_ccmp() {
    printf("[*] Testing AES-128 NIST SP 800-38A and AES-CCMP roundtrip...\n");

    // NIST FIPS 197 Appendix C.1 test vector:
    // Key: 2b7e1516 28aed2a6 abf71588 09cf4f3c
    // Plain: 3243f6a8 885a308d 313198a2 e0370734
    // Cipher: 3925841d 02dc09fb dc118597 196a0b32
    const uint8_t nist_key[16] = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
    };
    const uint8_t nist_plain[16] = {
        0x32, 0x43, 0xf6, 0xa8, 0x88, 0x5a, 0x30, 0x8d,
        0x31, 0x31, 0x98, 0xa2, 0xe0, 0x37, 0x07, 0x34
    };
    const uint8_t nist_cipher_expected[16] = {
        0x39, 0x25, 0x84, 0x1d, 0x02, 0xdc, 0x09, 0xfb,
        0xdc, 0x11, 0x85, 0x97, 0x19, 0x6a, 0x0b, 0x32
    };

    aes_key_ctx ctx;
    aes128_set_encrypt_key(&ctx, nist_key);
    uint8_t cipher_out[16];
    aes128_encrypt_block(&ctx, nist_plain, cipher_out);
    assert(memcmp(cipher_out, nist_cipher_expected, 16) == 0);

    // CCMP Test: Encrypt and Decrypt
    uint8_t tk[16] = {
        0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80,
        0x90, 0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0, 0x01
    };
    uint8_t hdr[24] = {
        0x08, 0x41, // Data, Protected=1, ToDS=1
        0x00, 0x00, // Duration
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, // Addr1 (RA)
        0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, // Addr2 (TA)
        0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, // Addr3 (DA)
        0x00, 0x00  // Seq Ctrl
    };

    const char *payload_text = "Realtek RTL8852BE WiFi 6 Freestanding CCMP Payload";
    size_t payload_len = strlen(payload_text);

    uint8_t encrypted[256];
    size_t encrypted_len = 0;
    int ret = aes_ccmp_encrypt(tk, 1 /* PN */, hdr, sizeof(hdr),
                               reinterpret_cast<const uint8_t *>(payload_text), payload_len,
                               encrypted, encrypted_len);
    assert(ret == 0);
    assert(encrypted_len == 8 /* CCMP hdr */ + payload_len + 8 /* MIC */);

    // Assemble full frame for decrypt: hdr + encrypted
    std::vector<uint8_t> full_frame;
    full_frame.insert(full_frame.end(), hdr, hdr + 24);
    full_frame.insert(full_frame.end(), encrypted, encrypted + encrypted_len);

    uint8_t decrypted[256];
    size_t decrypted_len = 0;
    ret = aes_ccmp_decrypt(tk, full_frame.data(), full_frame.size(), decrypted, decrypted_len);
    assert(ret == 0);
    assert(decrypted_len == payload_len);
    assert(memcmp(decrypted, payload_text, payload_len) == 0);

    // Tamper with MIC -> verify decryption failure
    full_frame.back() ^= 0xFF;
    ret = aes_ccmp_decrypt(tk, full_frame.data(), full_frame.size(), decrypted, decrypted_len);
    assert(ret == -EBADMSG);

    printf("    -> AES-128 and CCMP encryption/decryption PASSED\n");
}

static void test_wpa2_4way_handshake() {
    printf("[*] Testing WPA2-PSK 4-Way Handshake full flow...\n");

    const uint8_t sta_mac[6] = {0x00, 0xE0, 0x4C, 0x88, 0x52, 0xBE};
    const uint8_t bssid[6]   = {0x00, 0x1A, 0x2B, 0x3C, 0x4D, 0x5E};
    const char *passphrase   = "SuperSecretWiFiKey123";
    const char *ssid         = "Firebat_U6C_AP";

    Wpa2HandshakeEngine sta;
    int ret = sta.init(sta_mac, bssid, passphrase, ssid);
    assert(ret == 0);
    assert(sta.state() == HandshakeState::WAIT_MSG1);

    // AP side: also derive PMK to create valid Message 1 and 3
    uint8_t ap_pmk[32];
    pbkdf2_sha1(passphrase, strlen(passphrase), reinterpret_cast<const uint8_t *>(ssid), strlen(ssid), 4096, ap_pmk, 32);

    // 1. AP sends Message 1 (ANonce, ACK=1, MIC=0)
    uint8_t ap_anonce[32];
    for (int i = 0; i < 32; i++) ap_anonce[i] = static_cast<uint8_t>(0x30 + i);

    std::vector<uint8_t> msg1_frame(IEEE8023_HDR_LEN + sizeof(eapol_key_hdr), 0);
    eth_hdr *eh1 = reinterpret_cast<eth_hdr *>(msg1_frame.data());
    memcpy(eh1->h_dest, sta_mac, 6);
    memcpy(eh1->h_source, bssid, 6);
    eh1->h_proto = cpu_to_be16(ETH_P_EAPOL);

    eapol_key_hdr *kh1 = reinterpret_cast<eapol_key_hdr *>(msg1_frame.data() + IEEE8023_HDR_LEN);
    kh1->eapol.version = EAPOL_VERSION;
    kh1->eapol.type = EAPOL_TYPE_KEY;
    kh1->eapol.length = cpu_to_be16(sizeof(eapol_key_hdr) - sizeof(eapol_hdr));
    kh1->desc_type = 2; // RSN
    kh1->key_info = cpu_to_be16(WPA_KEY_INFO_TYPE_HMAC_SHA1_AES | WPA_KEY_INFO_ACK);
    kh1->key_length = cpu_to_be16(16);
    kh1->replay_counter[7] = 1;
    memcpy(kh1->key_nonce, ap_anonce, 32);

    // 2. STA processes Message 1 -> generates Message 2
    struct sk_buff *msg2_skb = nullptr;
    ret = sta.process_eapol_frame(msg1_frame.data(), msg1_frame.size(), &msg2_skb);
    assert(ret == 0);
    assert(msg2_skb != nullptr);
    assert(sta.state() == HandshakeState::WAIT_MSG3);

    // AP receives Message 2 -> extracts SNonce, derives PTK
    const eapol_key_hdr *kh2 = reinterpret_cast<const eapol_key_hdr *>(msg2_skb->data + IEEE8023_HDR_LEN);
    const uint8_t *ap_snonce = kh2->key_nonce;

    // AP derives PTK
    uint8_t ap_data[76];
    if (memcmp(bssid, sta_mac, 6) < 0) {
        memcpy(&ap_data[0], bssid, 6); memcpy(&ap_data[6], sta_mac, 6);
    } else {
        memcpy(&ap_data[0], sta_mac, 6); memcpy(&ap_data[6], bssid, 6);
    }
    if (memcmp(ap_anonce, ap_snonce, 32) < 0) {
        memcpy(&ap_data[12], ap_anonce, 32); memcpy(&ap_data[44], ap_snonce, 32);
    } else {
        memcpy(&ap_data[12], ap_snonce, 32); memcpy(&ap_data[44], ap_anonce, 32);
    }
    uint8_t ap_ptk[64];
    Wpa2HandshakeEngine::prf512(ap_pmk, "Pairwise key expansion", ap_data, 76, ap_ptk);

    // AP verifies Message 2 MIC
    uint8_t check_mic[16];
    uint8_t msg2_copy[sizeof(eapol_key_hdr)];
    memcpy(msg2_copy, kh2, sizeof(msg2_copy));
    eapol_key_hdr *kh2_copy = reinterpret_cast<eapol_key_hdr *>(msg2_copy);
    memset(kh2_copy->key_mic, 0, 16);
    uint8_t digest[20];
    hmac_sha1(ap_ptk, 16, msg2_copy, sizeof(msg2_copy), digest);
    memcpy(check_mic, digest, 16);
    assert(memcmp(check_mic, kh2->key_mic, 16) == 0); // AP verified STA's MIC!

    kfree_skb(msg2_skb);

    // 3. AP generates Message 3 (ACK=1, MIC=1, INSTALL=1)
    std::vector<uint8_t> msg3_frame(IEEE8023_HDR_LEN + sizeof(eapol_key_hdr), 0);
    eth_hdr *eh3 = reinterpret_cast<eth_hdr *>(msg3_frame.data());
    memcpy(eh3->h_dest, sta_mac, 6);
    memcpy(eh3->h_source, bssid, 6);
    eh3->h_proto = cpu_to_be16(ETH_P_EAPOL);

    eapol_key_hdr *kh3 = reinterpret_cast<eapol_key_hdr *>(msg3_frame.data() + IEEE8023_HDR_LEN);
    kh3->eapol.version = EAPOL_VERSION;
    kh3->eapol.type = EAPOL_TYPE_KEY;
    kh3->eapol.length = cpu_to_be16(sizeof(eapol_key_hdr) - sizeof(eapol_hdr));
    kh3->desc_type = 2; // RSN
    kh3->key_info = cpu_to_be16(WPA_KEY_INFO_TYPE_HMAC_SHA1_AES | WPA_KEY_INFO_ACK | WPA_KEY_INFO_MIC | WPA_KEY_INFO_INSTALL);
    kh3->key_length = cpu_to_be16(16);
    kh3->replay_counter[7] = 2; // Incremented replay counter
    memcpy(kh3->key_nonce, ap_anonce, 32);

    // Compute AP MIC over Message 3
    hmac_sha1(ap_ptk, 16, reinterpret_cast<const uint8_t *>(kh3), sizeof(eapol_key_hdr), digest);
    memcpy(kh3->key_mic, digest, 16);

    // 4. STA processes Message 3 -> generates Message 4
    struct sk_buff *msg4_skb = nullptr;
    ret = sta.process_eapol_frame(msg3_frame.data(), msg3_frame.size(), &msg4_skb);
    assert(ret == 0);
    assert(msg4_skb != nullptr);
    assert(sta.is_established());
    assert(sta.state() == HandshakeState::ESTABLISHED);

    // Verify Temporal Key derived on STA matches AP!
    assert(memcmp(sta.temporal_key(), &ap_ptk[32], 16) == 0);

    kfree_skb(msg4_skb);

    printf("    -> 4-Way Handshake successfully ESTABLISHED (TK verified)!\n");
}

static void test_handshake_corrupted_mic() {
    printf("[*] Testing corrupted MIC in Message 3 rejected with -EBADMSG...\n");

    const uint8_t sta_mac[6] = {0x00, 0xE0, 0x4C, 0x88, 0x52, 0xBE};
    const uint8_t bssid[6]   = {0x00, 0x1A, 0x2B, 0x3C, 0x4D, 0x5E};

    Wpa2HandshakeEngine sta;
    sta.init(sta_mac, bssid, "password123", "TestSSID");

    // Message 1
    std::vector<uint8_t> msg1(IEEE8023_HDR_LEN + sizeof(eapol_key_hdr), 0);
    eth_hdr *eh1 = reinterpret_cast<eth_hdr *>(msg1.data());
    memcpy(eh1->h_dest, sta_mac, 6);
    eh1->h_proto = cpu_to_be16(ETH_P_EAPOL);
    eapol_key_hdr *kh1 = reinterpret_cast<eapol_key_hdr *>(msg1.data() + IEEE8023_HDR_LEN);
    kh1->eapol.type = EAPOL_TYPE_KEY;
    kh1->desc_type = 2;
    kh1->key_info = cpu_to_be16(WPA_KEY_INFO_TYPE_HMAC_SHA1_AES | WPA_KEY_INFO_ACK);
    struct sk_buff *reply = nullptr;
    sta.process_eapol_frame(msg1.data(), msg1.size(), &reply);
    kfree_skb(reply);

    // Message 3 with corrupted MIC
    std::vector<uint8_t> msg3(IEEE8023_HDR_LEN + sizeof(eapol_key_hdr), 0);
    eth_hdr *eh3 = reinterpret_cast<eth_hdr *>(msg3.data());
    memcpy(eh3->h_dest, sta_mac, 6);
    eh3->h_proto = cpu_to_be16(ETH_P_EAPOL);
    eapol_key_hdr *kh3 = reinterpret_cast<eapol_key_hdr *>(msg3.data() + IEEE8023_HDR_LEN);
    kh3->eapol.type = EAPOL_TYPE_KEY;
    kh3->desc_type = 2;
    kh3->key_info = cpu_to_be16(WPA_KEY_INFO_TYPE_HMAC_SHA1_AES | WPA_KEY_INFO_ACK | WPA_KEY_INFO_MIC);
    memset(kh3->key_mic, 0xDD, 16); // Bogus MIC

    int ret = sta.process_eapol_frame(msg3.data(), msg3.size(), &reply);
    assert(ret == -EBADMSG);
    assert(sta.state() == HandshakeState::FAILED);

    printf("    -> Corrupted MIC correctly rejected with -EBADMSG\n");
}

int main() {
    printf("=================================================================\n");
    printf("  Rtw89 SHA1, AES-CCMP & WPA2-PSK 4-Way Handshake Test Suite\n");
    printf("=================================================================\n");

    test_sha1_and_hmac();
    test_pbkdf2_sha1_ieee();
    test_aes128_and_ccmp();
    test_wpa2_4way_handshake();
    test_handshake_corrupted_mic();

    printf("\n>>> ALL CRYPTO & WPA2-PSK HANDSHAKE TESTS PASSED SUCCESSFULLY! <<<\n");
    return 0;
}
