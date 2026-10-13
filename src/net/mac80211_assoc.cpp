//
// mac80211_assoc.cpp — 802.11 Authentication & Association State Machine Implementation
//
#include "mac80211_assoc.h"

#include <cerrno>
#include <cstring>

AssocStateMachine::AssocStateMachine()
    : m_state(AssocState::IDLE),
      m_status(AssocStatus::SUCCESS),
      m_seq_num(1),
      m_aid(0),
      m_state_start_ms(0),
      m_tx_pending(false) {
    memset(m_sta_mac, 0, sizeof(m_sta_mac));
    memset(&m_target_ap, 0, sizeof(m_target_ap));
}

void AssocStateMachine::reset() {
    m_state = AssocState::IDLE;
    m_status = AssocStatus::SUCCESS;
    m_aid = 0;
    m_state_start_ms = 0;
    m_tx_pending = false;
    memset(&m_target_ap, 0, sizeof(m_target_ap));
}

int AssocStateMachine::start_association(const uint8_t *sta_mac, const bss_entry &target_ap, uint64_t current_time_ms) {
    if (!sta_mac) {
        return -EINVAL;
    }

    if (m_state != AssocState::IDLE && m_state != AssocState::FAILED) {
        return -EBUSY;
    }

    memcpy(m_sta_mac, sta_mac, 6);
    m_target_ap = target_ap;
    m_state = AssocState::AUTHENTICATING;
    m_status = AssocStatus::BUSY;
    m_aid = 0;
    m_state_start_ms = current_time_ms;
    m_tx_pending = true;

    return 0;
}

struct sk_buff *AssocStateMachine::build_auth_req() {
    size_t total_len = IEEE80211_HDR3_LEN + sizeof(ieee80211_auth_body);
    struct sk_buff *skb = alloc_skb(total_len + 64, GFP_KERNEL);
    if (!skb) return nullptr;
    skb_reserve(skb, 16);

    // 802.11 Management Header (Auth)
    ieee80211_hdr *hdr = reinterpret_cast<ieee80211_hdr *>(skb_put(skb, IEEE80211_HDR3_LEN));
    uint16_t fc = (IEEE80211_FTYPE_MGMT << 2) | (IEEE80211_STYPE_AUTH << 4);
    hdr->frame_control = cpu_to_le16(fc);
    hdr->duration_id = 0;
    memcpy(hdr->addr1, m_target_ap.bssid, 6); // DA (AP)
    memcpy(hdr->addr2, m_sta_mac, 6);         // SA (STA)
    memcpy(hdr->addr3, m_target_ap.bssid, 6); // BSSID (AP)
    hdr->seq_ctrl = cpu_to_le16(static_cast<uint16_t>((m_seq_num++ & 0x0FFF) << 4));

    // Auth Body (Open System, Seq 1, Status 0)
    ieee80211_auth_body *body = reinterpret_cast<ieee80211_auth_body *>(skb_put(skb, sizeof(ieee80211_auth_body)));
    body->auth_alg = cpu_to_le16(WLAN_AUTH_OPEN);
    body->auth_transaction = cpu_to_le16(1);
    body->status_code = cpu_to_le16(WLAN_STATUS_SUCCESS);

    return skb;
}

struct sk_buff *AssocStateMachine::build_assoc_req() {
    size_t total_len = IEEE80211_HDR3_LEN + 4 + // Fixed: capability (2) + listen interval (2)
                       2 + m_target_ap.ssid_len + // EID 0: SSID
                       2 + 4; // EID 1: Supported Rates (4 bytes)

    if (m_target_ap.wpa2) {
        total_len += 2 + 20; // EID 48: RSN IE
    }

    struct sk_buff *skb = alloc_skb(total_len + 64, GFP_KERNEL);
    if (!skb) return nullptr;
    skb_reserve(skb, 16);

    // 802.11 Management Header (Assoc Req)
    ieee80211_hdr *hdr = reinterpret_cast<ieee80211_hdr *>(skb_put(skb, IEEE80211_HDR3_LEN));
    uint16_t fc = (IEEE80211_FTYPE_MGMT << 2) | (IEEE80211_STYPE_ASSOC_REQ << 4);
    hdr->frame_control = cpu_to_le16(fc);
    hdr->duration_id = 0;
    memcpy(hdr->addr1, m_target_ap.bssid, 6); // DA (AP)
    memcpy(hdr->addr2, m_sta_mac, 6);         // SA (STA)
    memcpy(hdr->addr3, m_target_ap.bssid, 6); // BSSID (AP)
    hdr->seq_ctrl = cpu_to_le16(static_cast<uint16_t>((m_seq_num++ & 0x0FFF) << 4));

    // Fixed Fields: Capability + Listen Interval
    uint16_t cap = m_target_ap.capability;
    uint16_t listen_interval = 10;
    uint8_t *fixed = skb_put(skb, 4);
    fixed[0] = static_cast<uint8_t>(cap & 0xFF);
    fixed[1] = static_cast<uint8_t>((cap >> 8) & 0xFF);
    fixed[2] = static_cast<uint8_t>(listen_interval & 0xFF);
    fixed[3] = static_cast<uint8_t>((listen_interval >> 8) & 0xFF);

    // EID 0: SSID
    uint8_t *ie_ssid = skb_put(skb, 2 + m_target_ap.ssid_len);
    ie_ssid[0] = WLAN_EID_SSID;
    ie_ssid[1] = m_target_ap.ssid_len;
    if (m_target_ap.ssid_len > 0) {
        memcpy(&ie_ssid[2], m_target_ap.ssid, m_target_ap.ssid_len);
    }

    // EID 1: Supported Rates (1, 2, 5.5, 11 Mbps)
    uint8_t *ie_rates = skb_put(skb, 6);
    ie_rates[0] = WLAN_EID_SUPP_RATES;
    ie_rates[1] = 4;
    ie_rates[2] = 0x82; ie_rates[3] = 0x84; ie_rates[4] = 0x8B; ie_rates[5] = 0x96;

    // EID 48: RSN (WPA2) if target is WPA2
    if (m_target_ap.wpa2) {
        uint8_t *ie_rsn = skb_put(skb, 2 + 20);
        ie_rsn[0] = WLAN_EID_RSN;
        ie_rsn[1] = 20; // Length
        ie_rsn[2] = 0x01; ie_rsn[3] = 0x00; // Version 1
        // Group Cipher CCMP
        ie_rsn[4] = 0x00; ie_rsn[5] = 0x0F; ie_rsn[6] = 0xAC; ie_rsn[7] = 0x04;
        // Pairwise Count 1
        ie_rsn[8] = 0x01; ie_rsn[9] = 0x00;
        // Pairwise Cipher CCMP
        ie_rsn[10] = 0x00; ie_rsn[11] = 0x0F; ie_rsn[12] = 0xAC; ie_rsn[13] = 0x04;
        // AKM Count 1
        ie_rsn[14] = 0x01; ie_rsn[15] = 0x00;
        // AKM PSK
        ie_rsn[16] = 0x00; ie_rsn[17] = 0x0F; ie_rsn[18] = 0xAC; ie_rsn[19] = 0x02;
        // Capabilities
        ie_rsn[20] = 0x00; ie_rsn[21] = 0x00;
    }

    return skb;
}

struct sk_buff *AssocStateMachine::generate_tx_frame() {
    if (!m_tx_pending) {
        return nullptr;
    }

    m_tx_pending = false;

    if (m_state == AssocState::AUTHENTICATING) {
        return build_auth_req();
    } else if (m_state == AssocState::ASSOCIATING) {
        return build_assoc_req();
    }

    return nullptr;
}

int AssocStateMachine::process_rx_frame(const uint8_t *frame, size_t len, uint64_t current_time_ms) {
    if (!frame || len < IEEE80211_HDR3_LEN) {
        return -EINVAL;
    }

    uint16_t fc = static_cast<uint16_t>(frame[0]) | (static_cast<uint16_t>(frame[1]) << 8);
    if (ieee80211_get_fc_type(fc) != IEEE80211_FTYPE_MGMT) {
        return 0; // Ignore non-management frames
    }

    const ieee80211_hdr *hdr = reinterpret_cast<const ieee80211_hdr *>(frame);

    // Verify destination is STA and source is target BSSID
    if (memcmp(hdr->addr1, m_sta_mac, 6) != 0 ||
        memcmp(hdr->addr2, m_target_ap.bssid, 6) != 0) {
        return 0; // Not addressed to us or not from our target AP
    }

    uint8_t subtype = ieee80211_get_fc_subtype(fc);

    if (m_state == AssocState::AUTHENTICATING) {
        if (subtype != IEEE80211_STYPE_AUTH) {
            return 0;
        }

        if (len < IEEE80211_HDR3_LEN + sizeof(ieee80211_auth_body)) {
            return -EINVAL;
        }

        const ieee80211_auth_body *body =
            reinterpret_cast<const ieee80211_auth_body *>(frame + IEEE80211_HDR3_LEN);

        uint16_t trans = le16_to_cpu(body->auth_transaction);
        uint16_t status = le16_to_cpu(body->status_code);

        if (trans == 2) { // Auth Response
            if (status == WLAN_STATUS_SUCCESS) {
                // Auth succeeded! Transition to Associating
                m_state = AssocState::ASSOCIATING;
                m_state_start_ms = current_time_ms;
                m_tx_pending = true;
                return 0;
            } else {
                // Auth rejected
                m_state = AssocState::FAILED;
                m_status = AssocStatus::AUTH_REJECTED;
                return -EACCES;
            }
        }
    } else if (m_state == AssocState::ASSOCIATING) {
        if (subtype != IEEE80211_STYPE_ASSOC_RESP) {
            return 0;
        }

        if (len < IEEE80211_HDR3_LEN + sizeof(ieee80211_assoc_resp_body)) {
            return -EINVAL;
        }

        const ieee80211_assoc_resp_body *body =
            reinterpret_cast<const ieee80211_assoc_resp_body *>(frame + IEEE80211_HDR3_LEN);

        uint16_t status = le16_to_cpu(body->status_code);
        if (status == WLAN_STATUS_SUCCESS) {
            // Assoc succeeded!
            m_aid = le16_to_cpu(body->aid) & 0x3FFF;
            m_state = AssocState::ASSOCIATED;
            m_status = AssocStatus::SUCCESS;
            return 0;
        } else {
            // Assoc rejected
            m_state = AssocState::FAILED;
            m_status = AssocStatus::ASSOC_REJECTED;
            return -EACCES;
        }
    }

    return 0;
}

void AssocStateMachine::check_timeout(uint64_t current_time_ms) {
    if (m_state == AssocState::AUTHENTICATING || m_state == AssocState::ASSOCIATING) {
        if (current_time_ms - m_state_start_ms >= TIMEOUT_MS) {
            m_state = AssocState::FAILED;
            m_status = AssocStatus::TIMEOUT;
            m_tx_pending = false;
        }
    }
}
