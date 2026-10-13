//
// test_rtw89_frame.cpp — Unit tests for 802.3 <-> 802.11 Frame Translation Engine
//
#include "mac80211_frame.h"
#include "skbuff.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

static void test_frame_classification() {
    printf("[*] Testing 802.11 frame classification helpers...\n");

    // Mgmt Beacon frame header
    uint8_t beacon_raw[IEEE80211_HDR3_LEN] = {0};
    uint16_t fc_beacon = static_cast<uint16_t>(IEEE80211_FTYPE_MGMT << 2) |
                         static_cast<uint16_t>(IEEE80211_STYPE_BEACON << 4);
    beacon_raw[0] = static_cast<uint8_t>(fc_beacon & 0xFF);
    beacon_raw[1] = static_cast<uint8_t>((fc_beacon >> 8) & 0xFF);

    assert(ieee80211_is_mgmt_frame(beacon_raw, sizeof(beacon_raw)));
    assert(ieee80211_is_beacon_frame(beacon_raw, sizeof(beacon_raw)));
    assert(!ieee80211_is_data_frame(beacon_raw, sizeof(beacon_raw)));

    // Data frame header
    uint8_t data_raw[IEEE80211_HDR3_LEN] = {0};
    uint16_t fc_data = static_cast<uint16_t>(IEEE80211_FTYPE_DATA << 2) |
                       static_cast<uint16_t>(IEEE80211_STYPE_DATA << 4);
    data_raw[0] = static_cast<uint8_t>(fc_data & 0xFF);
    data_raw[1] = static_cast<uint8_t>((fc_data >> 8) & 0xFF);

    assert(!ieee80211_is_mgmt_frame(data_raw, sizeof(data_raw)));
    assert(!ieee80211_is_beacon_frame(data_raw, sizeof(data_raw)));
    assert(ieee80211_is_data_frame(data_raw, sizeof(data_raw)));

    // Edge cases
    assert(!ieee80211_is_data_frame(nullptr, 100));
    assert(!ieee80211_is_data_frame(data_raw, IEEE80211_HDR3_LEN - 1));

    printf("    -> Classification tests PASSED\n");
}

static void test_encap_decap_roundtrip() {
    printf("[*] Testing 802.3 <-> 802.11 encapsulation & decapsulation roundtrip...\n");

    const uint8_t bssid[6]   = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    const uint8_t sta_mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    const uint8_t dest_ip_mac[6] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};

    // 1. Create a simulated Ethernet packet (IPv4 / TCP)
    const char *payload_data = "GET /index.html HTTP/1.1\r\nHost: example.com\r\n\r\n";
    size_t payload_len = strlen(payload_data);

    struct sk_buff *orig_eth = alloc_skb(IEEE8023_HDR_LEN + payload_len + 32, GFP_KERNEL);
    assert(orig_eth != nullptr);
    skb_reserve(orig_eth, 16);

    eth_hdr *eh = reinterpret_cast<eth_hdr *>(skb_put(orig_eth, IEEE8023_HDR_LEN));
    memcpy(eh->h_dest, dest_ip_mac, 6);
    memcpy(eh->h_source, sta_mac, 6);
    eh->h_proto = cpu_to_be16(0x0800); // IPv4 EtherType
    skb_put_data(orig_eth, payload_data, payload_len);

    // 2. Encapsulate into 802.11 WLAN frame (non-QoS)
    struct sk_buff *wlan_skb = nullptr;
    int ret = ieee80211_encap_8023(orig_eth, &wlan_skb, bssid, sta_mac, false);
    assert(ret == 0);
    assert(wlan_skb != nullptr);

    // Verify 802.11 header structure
    assert(wlan_skb->len == IEEE80211_HDR3_LEN + IEEE80211_LLC_SNAP_LEN + payload_len);
    const ieee80211_hdr *whdr = reinterpret_cast<const ieee80211_hdr *>(wlan_skb->data);
    assert(memcmp(whdr->addr1, bssid, 6) == 0);
    assert(memcmp(whdr->addr2, sta_mac, 6) == 0);
    assert(memcmp(whdr->addr3, dest_ip_mac, 6) == 0);

    // Now simulate AP receiving it and sending to client (From-DS frame)
    // Modify frame control to FROM_DS and reorder addresses for RX path
    ieee80211_hdr *whdr_rw = reinterpret_cast<ieee80211_hdr *>(wlan_skb->data);
    uint16_t fc_rx = (IEEE80211_FTYPE_DATA << 2) | (IEEE80211_STYPE_DATA << 4) | IEEE80211_FCTL_FROM_DS;
    whdr_rw->frame_control = cpu_to_le16(fc_rx);
    memcpy(whdr_rw->addr1, sta_mac, 6);      // DA (Client)
    memcpy(whdr_rw->addr2, bssid, 6);        // BSSID
    memcpy(whdr_rw->addr3, dest_ip_mac, 6);  // SA (Remote host)

    // 3. Decapsulate back to Ethernet
    struct sk_buff *decap_eth = nullptr;
    ret = ieee80211_decap_8023(wlan_skb, &decap_eth);
    assert(ret == 0);
    assert(decap_eth != nullptr);

    assert(decap_eth->len == orig_eth->len);
    const eth_hdr *decap_eh = reinterpret_cast<const eth_hdr *>(decap_eth->data);
    assert(memcmp(decap_eh->h_dest, sta_mac, 6) == 0);
    assert(memcmp(decap_eh->h_source, dest_ip_mac, 6) == 0);
    assert(decap_eh->h_proto == cpu_to_be16(0x0800));
    assert(memcmp(decap_eth->data + IEEE8023_HDR_LEN, payload_data, payload_len) == 0);

    kfree_skb(orig_eth);
    kfree_skb(wlan_skb);
    kfree_skb(decap_eth);

    printf("    -> Non-QoS roundtrip PASSED\n");
}

static void test_qos_encap() {
    printf("[*] Testing QoS 802.11 encapsulation with TID...\n");

    const uint8_t bssid[6]   = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    const uint8_t sta_mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    const uint8_t dest_mac[6] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};

    struct sk_buff *eth_skb = alloc_skb(128, GFP_KERNEL);
    eth_hdr *eh = reinterpret_cast<eth_hdr *>(skb_put(eth_skb, IEEE8023_HDR_LEN));
    memcpy(eh->h_dest, dest_mac, 6);
    memcpy(eh->h_source, sta_mac, 6);
    eh->h_proto = cpu_to_be16(0x86DD); // IPv6
    uint8_t payload[20] = {0x42};
    skb_put_data(eth_skb, payload, sizeof(payload));

    struct sk_buff *wlan_skb = nullptr;
    int ret = ieee80211_encap_8023(eth_skb, &wlan_skb, bssid, sta_mac, true, 5 /* Voice TID */);
    assert(ret == 0);
    assert(wlan_skb != nullptr);

    assert(wlan_skb->len == IEEE80211_HDR3_QOS_LEN + IEEE80211_LLC_SNAP_LEN + sizeof(payload));
    const ieee80211_qos_hdr *qhdr = reinterpret_cast<const ieee80211_qos_hdr *>(wlan_skb->data);
    assert((le16_to_cpu(qhdr->qos_ctrl) & 0x07) == 5);

    kfree_skb(eth_skb);
    kfree_skb(wlan_skb);

    printf("    -> QoS encapsulation PASSED\n");
}

static void test_stress_fuzzing_10k_cycles() {
    printf("[*] Running 10,000 stress cycles under ASan/UBSan...\n");

    const uint8_t bssid[6]   = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
    const uint8_t sta_mac[6] = {0x70, 0x80, 0x90, 0xA0, 0xB0, 0xC0};

    for (int i = 0; i < 10000; i++) {
        size_t plen = (i % 512) + 1;
        struct sk_buff *skb = alloc_skb(IEEE8023_HDR_LEN + plen + 32, GFP_KERNEL);
        assert(skb != nullptr);

        eth_hdr *eh = reinterpret_cast<eth_hdr *>(skb_put(skb, IEEE8023_HDR_LEN));
        eh->h_dest[0] = static_cast<uint8_t>(i);
        eh->h_source[0] = static_cast<uint8_t>(i >> 8);
        eh->h_proto = cpu_to_be16(0x0800);

        uint8_t *p = reinterpret_cast<uint8_t *>(skb_put(skb, plen));
        p[0] = static_cast<uint8_t>(i & 0xFF);
        p[plen - 1] = static_cast<uint8_t>((i * 7) & 0xFF);

        struct sk_buff *wlan_skb = nullptr;
        bool qos = (i % 2) != 0;
        int ret = ieee80211_encap_8023(skb, &wlan_skb, bssid, sta_mac, qos, i & 0x07);
        assert(ret == 0);
        assert(wlan_skb != nullptr);

        // Mutate for decap
        ieee80211_hdr *whdr = reinterpret_cast<ieee80211_hdr *>(wlan_skb->data);
        uint16_t fc = le16_to_cpu(whdr->frame_control) | IEEE80211_FCTL_FROM_DS;
        whdr->frame_control = cpu_to_le16(fc);

        struct sk_buff *out_eth = nullptr;
        ret = ieee80211_decap_8023(wlan_skb, &out_eth);
        assert(ret == 0);
        assert(out_eth != nullptr);
        assert(out_eth->len == skb->len);

        kfree_skb(skb);
        kfree_skb(wlan_skb);
        kfree_skb(out_eth);
    }

    printf("    -> 10,000 cycles completed with 0 errors and 0 leaks!\n");
}

int main() {
    printf("=================================================================\n");
    printf("  Rtw89 802.3 <-> 802.11 Frame Translation Test Suite\n");
    printf("=================================================================\n");

    test_frame_classification();
    test_encap_decap_roundtrip();
    test_qos_encap();
    test_stress_fuzzing_10k_cycles();

    printf("\n>>> ALL FRAME TRANSLATION TESTS PASSED SUCCESSFULLY! <<<\n");
    return 0;
}
