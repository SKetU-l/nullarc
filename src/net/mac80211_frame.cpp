//
// mac80211_frame.cpp — 802.3 Ethernet <-> 802.11 WLAN Frame Translation Implementation
//
#include "mac80211_frame.h"

#if defined(KERNEL) || defined(__APPLE_KEXT__)
#include <sys/errno.h>
#include <string.h>
#else
#include <cerrno>
#include <cstring>
#endif

uint8_t ieee80211_get_fc_type(uint16_t fc) {
    return static_cast<uint8_t>((fc >> 2) & 0x03);
}

uint8_t ieee80211_get_fc_subtype(uint16_t fc) {
    return static_cast<uint8_t>((fc >> 4) & 0x0F);
}

bool ieee80211_is_data_frame(const uint8_t *data, size_t len) {
    if (!data || len < IEEE80211_HDR3_LEN) {
        return false;
    }
    uint16_t fc = static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
    return ieee80211_get_fc_type(fc) == IEEE80211_FTYPE_DATA;
}

bool ieee80211_is_mgmt_frame(const uint8_t *data, size_t len) {
    if (!data || len < IEEE80211_HDR3_LEN) {
        return false;
    }
    uint16_t fc = static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
    return ieee80211_get_fc_type(fc) == IEEE80211_FTYPE_MGMT;
}

bool ieee80211_is_beacon_frame(const uint8_t *data, size_t len) {
    if (!ieee80211_is_mgmt_frame(data, len)) {
        return false;
    }
    uint16_t fc = static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
    return ieee80211_get_fc_subtype(fc) == IEEE80211_STYPE_BEACON;
}

int ieee80211_encap_8023(struct sk_buff *eth_skb, struct sk_buff **out_wlan_skb,
                         const uint8_t *bssid, const uint8_t *sta_mac,
                         bool qos, uint8_t tid) {
    if (!eth_skb || !out_wlan_skb || !bssid || !sta_mac) {
        return -EINVAL;
    }

    if (eth_skb->len < IEEE8023_HDR_LEN) {
        return -EINVAL;
    }

    const eth_hdr *eh = reinterpret_cast<const eth_hdr *>(eth_skb->data);
    size_t payload_len = eth_skb->len - IEEE8023_HDR_LEN;
    const uint8_t *payload = eth_skb->data + IEEE8023_HDR_LEN;

    size_t hdr_len = qos ? IEEE80211_HDR3_QOS_LEN : IEEE80211_HDR3_LEN;
    size_t total_wlan_len = hdr_len + IEEE80211_LLC_SNAP_LEN + payload_len;

    struct sk_buff *wlan_skb = alloc_skb(total_wlan_len + 64, GFP_KERNEL);
    if (!wlan_skb) {
        return -ENOMEM;
    }
    skb_reserve(wlan_skb, 32);

    // 1. Build 802.11 MAC Header (To-DS)
    uint16_t fc = static_cast<uint16_t>(IEEE80211_FTYPE_DATA << 2) |
                  static_cast<uint16_t>((qos ? IEEE80211_STYPE_QOS_DATA : IEEE80211_STYPE_DATA) << 4) |
                  IEEE80211_FCTL_TO_DS;

    if (qos) {
        ieee80211_qos_hdr *qhdr = reinterpret_cast<ieee80211_qos_hdr *>(skb_put(wlan_skb, sizeof(ieee80211_qos_hdr)));
        qhdr->base.frame_control = cpu_to_le16(fc);
        qhdr->base.duration_id = 0;
        memcpy(qhdr->base.addr1, bssid, 6);       // BSSID / Receiver
        memcpy(qhdr->base.addr2, sta_mac, 6);     // Source STA
        memcpy(qhdr->base.addr3, eh->h_dest, 6);  // Final Destination
        qhdr->base.seq_ctrl = 0;
        qhdr->qos_ctrl = cpu_to_le16(tid & 0x07);
    } else {
        ieee80211_hdr *hdr = reinterpret_cast<ieee80211_hdr *>(skb_put(wlan_skb, sizeof(ieee80211_hdr)));
        hdr->frame_control = cpu_to_le16(fc);
        hdr->duration_id = 0;
        memcpy(hdr->addr1, bssid, 6);             // BSSID / Receiver
        memcpy(hdr->addr2, sta_mac, 6);           // Source STA
        memcpy(hdr->addr3, eh->h_dest, 6);        // Final Destination
        hdr->seq_ctrl = 0;
    }

    // 2. Build RFC 1042 LLC/SNAP Header
    ieee80211_llc_snap *llc = reinterpret_cast<ieee80211_llc_snap *>(skb_put(wlan_skb, sizeof(ieee80211_llc_snap)));
    llc->dsap = 0xAA;
    llc->ssap = 0xAA;
    llc->ctrl = 0x03;
    llc->oui[0] = 0x00;
    llc->oui[1] = 0x00;
    llc->oui[2] = 0x00;
    llc->proto_id = eh->h_proto;

    // 3. Append Payload
    if (payload_len > 0) {
        skb_put_data(wlan_skb, payload, payload_len);
    }

    *out_wlan_skb = wlan_skb;
    return 0;
}

int ieee80211_decap_8023(struct sk_buff *wlan_skb, struct sk_buff **out_eth_skb) {
    if (!wlan_skb || !out_eth_skb) {
        return -EINVAL;
    }

    if (wlan_skb->len < IEEE80211_HDR3_LEN) {
        return -EINVAL;
    }

    uint16_t fc = static_cast<uint16_t>(wlan_skb->data[0]) |
                  (static_cast<uint16_t>(wlan_skb->data[1]) << 8);

    if (ieee80211_get_fc_type(fc) != IEEE80211_FTYPE_DATA) {
        return -EPROTO; // Not a data frame (e.g. management/control frame)
    }

    uint8_t subtype = ieee80211_get_fc_subtype(fc);
    bool is_qos = (subtype == IEEE80211_STYPE_QOS_DATA);
    size_t hdr_len = is_qos ? IEEE80211_HDR3_QOS_LEN : IEEE80211_HDR3_LEN;

    if (wlan_skb->len < hdr_len + IEEE80211_LLC_SNAP_LEN) {
        return -EINVAL;
    }

    const ieee80211_hdr *hdr = reinterpret_cast<const ieee80211_hdr *>(wlan_skb->data);
    const uint8_t *da = nullptr;
    const uint8_t *sa = nullptr;

    // Check From-DS bit
    if (fc & IEEE80211_FCTL_FROM_DS) {
        da = hdr->addr1; // Client MAC (Receiver)
        sa = hdr->addr3; // Original Source MAC
    } else {
        da = hdr->addr3;
        sa = hdr->addr2;
    }

    // Inspect LLC/SNAP Header
    const ieee80211_llc_snap *llc = reinterpret_cast<const ieee80211_llc_snap *>(wlan_skb->data + hdr_len);
    if (llc->dsap != 0xAA || llc->ssap != 0xAA || llc->ctrl != 0x03) {
        return -EPROTO; // Malformed LLC/SNAP
    }

    __be16 ether_type = llc->proto_id;

    size_t payload_offset = hdr_len + IEEE80211_LLC_SNAP_LEN;
    size_t payload_len = wlan_skb->len - payload_offset;

    struct sk_buff *eth_skb = alloc_skb(IEEE8023_HDR_LEN + payload_len + 64, GFP_KERNEL);
    if (!eth_skb) {
        return -ENOMEM;
    }
    skb_reserve(eth_skb, 16);

    // Build Ethernet Header
    eth_hdr *eh = reinterpret_cast<eth_hdr *>(skb_put(eth_skb, IEEE8023_HDR_LEN));
    memcpy(eh->h_dest, da, 6);
    memcpy(eh->h_source, sa, 6);
    eh->h_proto = ether_type;

    // Append Payload
    if (payload_len > 0) {
        skb_put_data(eth_skb, wlan_skb->data + payload_offset, payload_len);
    }

    *out_eth_skb = eth_skb;
    return 0;
}
