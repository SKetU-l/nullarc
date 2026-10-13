//
// wpa2_psk.cpp — WPA2-PSK 4-Way Handshake & Key Derivation Implementation
//
#include "wpa2_psk.h"

#include <cerrno>
#include <cstring>

Wpa2HandshakeEngine::Wpa2HandshakeEngine()
    : m_state(HandshakeState::IDLE) {
    reset();
}

Wpa2HandshakeEngine::~Wpa2HandshakeEngine() {
    reset();
}

void Wpa2HandshakeEngine::reset() {
    m_state = HandshakeState::IDLE;
    memset(m_sta_mac, 0, sizeof(m_sta_mac));
    memset(m_bssid, 0, sizeof(m_bssid));
    memset(m_pmk, 0, sizeof(m_pmk));
    memset(m_anonce, 0, sizeof(m_anonce));
    memset(m_snonce, 0, sizeof(m_snonce));
    memset(m_replay_counter, 0, sizeof(m_replay_counter));
    memset(&m_ptk, 0, sizeof(m_ptk));
}

int Wpa2HandshakeEngine::init(const uint8_t *sta_mac, const uint8_t *bssid,
                             const char *passphrase, const char *ssid) {
    if (!sta_mac || !bssid || !passphrase || !ssid) {
        return -EINVAL;
    }

    reset();

    memcpy(m_sta_mac, sta_mac, 6);
    memcpy(m_bssid, bssid, 6);

    // Derive PMK: PBKDF2-SHA1(passphrase, ssid, 4096, pmk)
    size_t pass_len = strlen(passphrase);
    size_t ssid_len = strlen(ssid);
    pbkdf2_sha1(passphrase, pass_len, reinterpret_cast<const uint8_t *>(ssid), ssid_len, 4096, m_pmk, 32);

    m_state = HandshakeState::WAIT_MSG1;
    return 0;
}

void Wpa2HandshakeEngine::prf512(const uint8_t pmk[32], const char *prefix,
                                const uint8_t *data, size_t data_len, uint8_t *out_buf) {
    size_t prefix_len = strlen(prefix);
    // Buffer: prefix + 1 (null byte) + data + 1 (counter)
    size_t total_buf_len = prefix_len + 1 + data_len + 1;
    uint8_t input_buf[256];
    if (total_buf_len > sizeof(input_buf)) return;

    memcpy(input_buf, prefix, prefix_len);
    input_buf[prefix_len] = 0x00;
    memcpy(&input_buf[prefix_len + 1], data, data_len);

    uint8_t digest[20];
    for (uint8_t i = 0; i < 4; i++) {
        input_buf[prefix_len + 1 + data_len] = i;
        hmac_sha1(pmk, 32, input_buf, total_buf_len, digest);

        size_t copy_len = (i == 3) ? 4 : 20; // 20 + 20 + 20 + 4 = 64 bytes
        memcpy(&out_buf[i * 20], digest, copy_len);
    }
}

void Wpa2HandshakeEngine::derive_ptk() {
    // IEEE 802.11i PRF-512 input:
    // min(AA, SPA) || max(AA, SPA) || min(ANonce, SNonce) || max(ANonce, SNonce)
    uint8_t data[76];
    size_t off = 0;

    // MAC addresses (6 + 6 = 12 bytes)
    if (memcmp(m_bssid, m_sta_mac, 6) < 0) {
        memcpy(&data[off], m_bssid, 6); off += 6;
        memcpy(&data[off], m_sta_mac, 6); off += 6;
    } else {
        memcpy(&data[off], m_sta_mac, 6); off += 6;
        memcpy(&data[off], m_bssid, 6); off += 6;
    }

    // Nonces (32 + 32 = 64 bytes)
    if (memcmp(m_anonce, m_snonce, 32) < 0) {
        memcpy(&data[off], m_anonce, 32); off += 32;
        memcpy(&data[off], m_snonce, 32); off += 32;
    } else {
        memcpy(&data[off], m_snonce, 32); off += 32;
        memcpy(&data[off], m_anonce, 32); off += 32;
    }

    uint8_t ptk_raw[64];
    prf512(m_pmk, "Pairwise key expansion", data, 76, ptk_raw);

    memcpy(m_ptk.kck, &ptk_raw[0], 16);
    memcpy(m_ptk.kek, &ptk_raw[16], 16);
    memcpy(m_ptk.tk, &ptk_raw[32], 16);
    memcpy(m_ptk.mic_rx, &ptk_raw[48], 8);
    memcpy(m_ptk.mic_tx, &ptk_raw[56], 8);
}

void Wpa2HandshakeEngine::compute_eapol_mic(const uint8_t *eapol_data, size_t len, uint8_t mic[16]) {
    // Clone frame into temporary buffer with MIC zeroed out
    uint8_t buf[512];
    if (len > sizeof(buf)) return;
    memcpy(buf, eapol_data, len);

    // Zero out MIC field in clone
    eapol_key_hdr *key_hdr = reinterpret_cast<eapol_key_hdr *>(buf);
    memset(key_hdr->key_mic, 0, sizeof(key_hdr->key_mic));

    uint8_t digest[20];
    hmac_sha1(m_ptk.kck, 16, buf, len, digest);
    memcpy(mic, digest, 16);
}

bool Wpa2HandshakeEngine::verify_eapol_mic(const uint8_t *eapol_data, size_t len, const uint8_t expected_mic[16]) {
    uint8_t calculated[16];
    compute_eapol_mic(eapol_data, len, calculated);
    return memcmp(calculated, expected_mic, 16) == 0;
}

int Wpa2HandshakeEngine::process_eapol_frame(const uint8_t *frame, size_t len, struct sk_buff **out_reply_skb) {
    if (!frame || len < IEEE8023_HDR_LEN + sizeof(eapol_key_hdr)) {
        return -EINVAL;
    }

    const eth_hdr *eh = reinterpret_cast<const eth_hdr *>(frame);
    if (be16_to_cpu(eh->h_proto) != ETH_P_EAPOL) {
        return -EINVAL;
    }

    const eapol_key_hdr *key_hdr = reinterpret_cast<const eapol_key_hdr *>(frame + IEEE8023_HDR_LEN);
    if (key_hdr->eapol.type != EAPOL_TYPE_KEY || key_hdr->desc_type != 2 /* RSN */) {
        return -EINVAL;
    }

    uint16_t key_info = be16_to_cpu(key_hdr->key_info);

    if (m_state == HandshakeState::WAIT_MSG1) {
        // Message 1 from AP: ACK=1, MIC=0
        if ((key_info & WPA_KEY_INFO_ACK) && !(key_info & WPA_KEY_INFO_MIC)) {
            return handle_msg1(key_hdr, len - IEEE8023_HDR_LEN, out_reply_skb);
        }
    } else if (m_state == HandshakeState::WAIT_MSG3) {
        // Message 3 from AP: ACK=1, MIC=1
        if ((key_info & WPA_KEY_INFO_ACK) && (key_info & WPA_KEY_INFO_MIC)) {
            return handle_msg3(key_hdr, len - IEEE8023_HDR_LEN, out_reply_skb);
        }
    }

    return 0;
}

int Wpa2HandshakeEngine::handle_msg1(const eapol_key_hdr *key_hdr, size_t total_len, struct sk_buff **out_reply_skb) {
    (void)total_len;
    // Extract ANonce and Replay Counter
    memcpy(m_anonce, key_hdr->key_nonce, 32);
    memcpy(m_replay_counter, key_hdr->replay_counter, 8);

    // Generate deterministic/pseudo-random SNonce
    for (int i = 0; i < 32; i++) {
        m_snonce[i] = static_cast<uint8_t>(0xA5 ^ (i * 7) ^ m_anonce[i]);
    }

    // Derive PTK (KCK, KEK, TK)
    derive_ptk();

    // Build Message 2 (STA -> AP)
    size_t eapol_body_len = sizeof(eapol_key_hdr) - sizeof(eapol_hdr);
    size_t total_eapol_len = sizeof(eapol_key_hdr);
    size_t total_frame_len = IEEE8023_HDR_LEN + total_eapol_len;

    struct sk_buff *skb = alloc_skb(total_frame_len + 64, GFP_KERNEL);
    if (!skb) return -ENOMEM;
    skb_reserve(skb, 16);

    // Ethernet Header
    eth_hdr *eh = reinterpret_cast<eth_hdr *>(skb_put(skb, IEEE8023_HDR_LEN));
    memcpy(eh->h_dest, m_bssid, 6);
    memcpy(eh->h_source, m_sta_mac, 6);
    eh->h_proto = cpu_to_be16(ETH_P_EAPOL);

    // EAPOL Key Frame
    eapol_key_hdr *resp = reinterpret_cast<eapol_key_hdr *>(skb_put(skb, total_eapol_len));
    resp->eapol.version = EAPOL_VERSION;
    resp->eapol.type = EAPOL_TYPE_KEY;
    resp->eapol.length = cpu_to_be16(static_cast<uint16_t>(eapol_body_len));

    resp->desc_type = 2; // RSN
    resp->key_info = cpu_to_be16(WPA_KEY_INFO_TYPE_HMAC_SHA1_AES | WPA_KEY_INFO_MIC);
    resp->key_length = cpu_to_be16(16);
    memcpy(resp->replay_counter, m_replay_counter, 8);
    memcpy(resp->key_nonce, m_snonce, 32);
    memset(resp->key_iv, 0, 16);
    memset(resp->key_rsc, 0, 8);
    memset(resp->key_id, 0, 8);
    memset(resp->key_mic, 0, 16);
    resp->key_data_length = 0;

    // Compute MIC over EAPOL frame
    uint8_t mic[16];
    compute_eapol_mic(reinterpret_cast<const uint8_t *>(resp), total_eapol_len, mic);
    memcpy(resp->key_mic, mic, 16);

    m_state = HandshakeState::WAIT_MSG3;
    *out_reply_skb = skb;
    return 0;
}

int Wpa2HandshakeEngine::handle_msg3(const eapol_key_hdr *key_hdr, size_t total_len, struct sk_buff **out_reply_skb) {
    // 1. Verify MIC from AP using KCK
    if (!verify_eapol_mic(reinterpret_cast<const uint8_t *>(key_hdr), total_len, key_hdr->key_mic)) {
        m_state = HandshakeState::FAILED;
        return -EBADMSG;
    }

    // Update replay counter
    memcpy(m_replay_counter, key_hdr->replay_counter, 8);

    // Build Message 4 (STA -> AP)
    size_t eapol_body_len = sizeof(eapol_key_hdr) - sizeof(eapol_hdr);
    size_t total_eapol_len = sizeof(eapol_key_hdr);
    size_t total_frame_len = IEEE8023_HDR_LEN + total_eapol_len;

    struct sk_buff *skb = alloc_skb(total_frame_len + 64, GFP_KERNEL);
    if (!skb) return -ENOMEM;
    skb_reserve(skb, 16);

    eth_hdr *eh = reinterpret_cast<eth_hdr *>(skb_put(skb, IEEE8023_HDR_LEN));
    memcpy(eh->h_dest, m_bssid, 6);
    memcpy(eh->h_source, m_sta_mac, 6);
    eh->h_proto = cpu_to_be16(ETH_P_EAPOL);

    eapol_key_hdr *resp = reinterpret_cast<eapol_key_hdr *>(skb_put(skb, total_eapol_len));
    resp->eapol.version = EAPOL_VERSION;
    resp->eapol.type = EAPOL_TYPE_KEY;
    resp->eapol.length = cpu_to_be16(static_cast<uint16_t>(eapol_body_len));

    resp->desc_type = 2; // RSN
    resp->key_info = cpu_to_be16(WPA_KEY_INFO_TYPE_HMAC_SHA1_AES | WPA_KEY_INFO_MIC | WPA_KEY_INFO_SECURE);
    resp->key_length = cpu_to_be16(16);
    memcpy(resp->replay_counter, m_replay_counter, 8);
    memset(resp->key_nonce, 0, 32);
    memset(resp->key_iv, 0, 16);
    memset(resp->key_rsc, 0, 8);
    memset(resp->key_id, 0, 8);
    memset(resp->key_mic, 0, 16);
    resp->key_data_length = 0;

    // Compute MIC over EAPOL frame
    uint8_t mic[16];
    compute_eapol_mic(reinterpret_cast<const uint8_t *>(resp), total_eapol_len, mic);
    memcpy(resp->key_mic, mic, 16);

    // Keys are now installed!
    m_state = HandshakeState::ESTABLISHED;
    *out_reply_skb = skb;
    return 0;
}
