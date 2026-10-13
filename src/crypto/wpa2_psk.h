//
// wpa2_psk.h — WPA2-PSK 4-Way Handshake & Key Derivation Engine
//
#pragma once

#include "aes.h"
#include "linux_shim.h"
#include "mac80211_frame.h"
#include "sha1.h"
#include "skbuff.h"

#include <cstddef>
#include <cstdint>

// EAPOL Protocol definitions
#define ETH_P_EAPOL                     0x888E
#define EAPOL_VERSION                   2
#define EAPOL_TYPE_KEY                  3

// EAPOL-Key Descriptor Types
#define WPA_KEY_INFO_TYPE_MASK          0x0007
#define WPA_KEY_INFO_TYPE_HMAC_MD5_RC4  0x0001
#define WPA_KEY_INFO_TYPE_HMAC_SHA1_AES 0x0002
#define WPA_KEY_INFO_KEY_INDEX_MASK     0x0030
#define WPA_KEY_INFO_KEY_INDEX_SHIFT    4
#define WPA_KEY_INFO_INSTALL            BIT(6)
#define WPA_KEY_INFO_ACK                BIT(7)
#define WPA_KEY_INFO_MIC                BIT(8)
#define WPA_KEY_INFO_SECURE             BIT(9)
#define WPA_KEY_INFO_ERROR              BIT(10)
#define WPA_KEY_INFO_REQUEST            BIT(11)
#define WPA_KEY_INFO_ENCR_KEY_DATA      BIT(12)

// EAPOL Header (RFC 3580 / 802.1X)
struct eapol_hdr {
    uint8_t version;                    // 1 or 2
    uint8_t type;                       // 3 = EAPOL-Key
    __be16  length;                     // Packet body length
} __attribute__((packed));

// 802.11i EAPOL-Key Frame
struct eapol_key_hdr {
    struct eapol_hdr eapol;
    uint8_t  desc_type;                 // 2 = RSN (WPA2)
    __be16   key_info;
    __be16   key_length;
    uint8_t  replay_counter[8];
    uint8_t  key_nonce[32];
    uint8_t  key_iv[16];
    uint8_t  key_rsc[8];
    uint8_t  key_id[8];
    uint8_t  key_mic[16];
    __be16   key_data_length;
} __attribute__((packed));

// PTK Layout for CCMP (64 bytes)
struct wpa_ptk {
    uint8_t kck[16];                    // Key Confirmation Key (EAPOL MIC)
    uint8_t kek[16];                    // Key Encryption Key (EAPOL Key Data decrypt)
    uint8_t tk[16];                     // Temporal Key (AES-CCMP 802.11 Data encryption)
    uint8_t mic_rx[8];                  // TKIP MIC
    uint8_t mic_tx[8];                  // TKIP MIC
};

enum class HandshakeState {
    IDLE = 0,
    WAIT_MSG1,
    WAIT_MSG3,
    ESTABLISHED,
    FAILED
};

class Wpa2HandshakeEngine {
public:
    Wpa2HandshakeEngine();
    ~Wpa2HandshakeEngine();

    // Initialize with Station MAC, AP BSSID, Passphrase, and SSID
    int init(const uint8_t *sta_mac, const uint8_t *bssid,
             const char *passphrase, const char *ssid);

    // Process incoming EAPOL frame from AP
    int process_eapol_frame(const uint8_t *frame, size_t len, struct sk_buff **out_reply_skb);

    // Key status
    HandshakeState state() const { return m_state; }
    const wpa_ptk &ptk() const { return m_ptk; }
    const uint8_t *temporal_key() const { return m_ptk.tk; }
    bool is_established() const { return m_state == HandshakeState::ESTABLISHED; }

    void reset();

    // IEEE 802.11i PRF-512 calculation
    static void prf512(const uint8_t pmk[32], const char *prefix,
                       const uint8_t *data, size_t data_len, uint8_t *out_buf);

private:
    HandshakeState m_state;
    uint8_t        m_sta_mac[6];
    uint8_t        m_bssid[6];
    uint8_t        m_pmk[32];
    uint8_t        m_anonce[32];
    uint8_t        m_snonce[32];
    uint8_t        m_replay_counter[8];
    wpa_ptk        m_ptk;

    int handle_msg1(const eapol_key_hdr *key_hdr, size_t total_len, struct sk_buff **out_reply_skb);
    int handle_msg3(const eapol_key_hdr *key_hdr, size_t total_len, struct sk_buff **out_reply_skb);
    void derive_ptk();
    void compute_eapol_mic(const uint8_t *eapol_data, size_t len, uint8_t mic[16]);
    bool verify_eapol_mic(const uint8_t *eapol_data, size_t len, const uint8_t expected_mic[16]);
};
