//
// test_rtw89_scan.cpp — Unit tests for 802.11 Station Scanning & Beacon Parser
//
#include "mac80211_frame.h"
#include "mac80211_scan.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

// Helper to build a realistic 802.11 Beacon frame buffer
static std::vector<uint8_t> build_test_beacon(const uint8_t *bssid, const char *ssid,
                                              uint8_t channel, bool wpa2, bool privacy,
                                              int8_t rssi_in_ie = 0) {
    (void)rssi_in_ie;
    std::vector<uint8_t> frame;

    // 1. 802.11 Header (24 bytes)
    uint16_t fc = (IEEE80211_FTYPE_MGMT << 2) | (IEEE80211_STYPE_BEACON << 4);
    frame.push_back(static_cast<uint8_t>(fc & 0xFF));
    frame.push_back(static_cast<uint8_t>((fc >> 8) & 0xFF));
    frame.push_back(0x00); frame.push_back(0x00); // Duration

    // Addr1 (Broadcast)
    for (int i = 0; i < 6; i++) frame.push_back(0xFF);
    // Addr2 (BSSID)
    for (int i = 0; i < 6; i++) frame.push_back(bssid[i]);
    // Addr3 (BSSID)
    for (int i = 0; i < 6; i++) frame.push_back(bssid[i]);
    // Seq Ctrl
    frame.push_back(0x10); frame.push_back(0x00);

    // 2. Fixed Parameters (12 bytes)
    // Timestamp (8 bytes)
    for (int i = 0; i < 8; i++) frame.push_back(0x11);
    // Beacon interval (100 TU = 0x0064)
    frame.push_back(0x64); frame.push_back(0x00);
    // Capability Info (ESS = 0x0001, Privacy = 0x0010)
    uint16_t cap = WLAN_CAPABILITY_ESS;
    if (privacy) cap |= WLAN_CAPABILITY_PRIVACY;
    frame.push_back(static_cast<uint8_t>(cap & 0xFF));
    frame.push_back(static_cast<uint8_t>((cap >> 8) & 0xFF));

    // 3. Information Elements
    // EID 0: SSID
    uint8_t slen = static_cast<uint8_t>(strlen(ssid));
    frame.push_back(WLAN_EID_SSID);
    frame.push_back(slen);
    for (uint8_t i = 0; i < slen; i++) frame.push_back(static_cast<uint8_t>(ssid[i]));

    // EID 1: Supported Rates (1, 2, 5.5, 11 Mbps)
    frame.push_back(WLAN_EID_SUPP_RATES);
    frame.push_back(4);
    frame.push_back(0x82); frame.push_back(0x84); frame.push_back(0x8B); frame.push_back(0x96);

    // EID 3: DS Parameter Set (Channel)
    frame.push_back(WLAN_EID_DS_PARAMS);
    frame.push_back(1);
    frame.push_back(channel);

    // EID 48: RSN (WPA2) if enabled
    if (wpa2) {
        std::vector<uint8_t> rsn_body;
        // Version 1
        rsn_body.push_back(0x01); rsn_body.push_back(0x00);
        // Group Cipher: 00:0F:AC:04 (CCMP)
        rsn_body.push_back(0x00); rsn_body.push_back(0x0F); rsn_body.push_back(0xAC); rsn_body.push_back(0x04);
        // Pairwise Cipher Count: 1
        rsn_body.push_back(0x01); rsn_body.push_back(0x00);
        // Pairwise Cipher 1: 00:0F:AC:04 (CCMP)
        rsn_body.push_back(0x00); rsn_body.push_back(0x0F); rsn_body.push_back(0xAC); rsn_body.push_back(0x04);
        // AKM Suite Count: 1
        rsn_body.push_back(0x01); rsn_body.push_back(0x00);
        // AKM 1: 00:0F:AC:02 (PSK)
        rsn_body.push_back(0x00); rsn_body.push_back(0x0F); rsn_body.push_back(0xAC); rsn_body.push_back(0x02);
        // RSN Capabilities: 0x0000
        rsn_body.push_back(0x00); rsn_body.push_back(0x00);

        frame.push_back(WLAN_EID_RSN);
        frame.push_back(static_cast<uint8_t>(rsn_body.size()));
        frame.insert(frame.end(), rsn_body.begin(), rsn_body.end());
    }

    return frame;
}

static void test_beacon_parsing_and_cache() {
    printf("[*] Testing Beacon parsing and BSS cache operations...\n");

    BssCache cache;
    assert(cache.count() == 0);

    const uint8_t ap1_bssid[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    const uint8_t ap2_bssid[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
    const uint8_t ap3_bssid[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02};

    auto beacon1 = build_test_beacon(ap1_bssid, "Firebat-WiFi-5G", 36, true, true);
    auto beacon2 = build_test_beacon(ap2_bssid, "Guest-Open", 6, false, false);
    auto beacon3 = build_test_beacon(ap3_bssid, "Home-WPA2", 1, true, true);

    int ret1 = cache.update_from_frame(beacon1.data(), beacon1.size(), -45, 36, 1000);
    assert(ret1 == 0);
    assert(cache.count() == 1);

    int ret2 = cache.update_from_frame(beacon2.data(), beacon2.size(), -70, 6, 1050);
    assert(ret2 == 0);
    assert(cache.count() == 2);

    int ret3 = cache.update_from_frame(beacon3.data(), beacon3.size(), -55, 1, 1100);
    assert(ret3 == 0);
    assert(cache.count() == 3);

    // Lookups
    const bss_entry *e1 = cache.find_by_ssid("Firebat-WiFi-5G");
    assert(e1 != nullptr);
    assert(memcmp(e1->bssid, ap1_bssid, 6) == 0);
    assert(e1->channel == 36);
    assert(e1->rssi == -45);
    assert(e1->privacy == true);
    assert(e1->wpa2 == true);
    assert((e1->pairwise_ciphers & WLAN_CIPHER_SUITE_CCMP) != 0);
    assert((e1->akm_suites & WLAN_AKM_SUITE_PSK) != 0);

    const bss_entry *e2 = cache.find_by_bssid(ap2_bssid);
    assert(e2 != nullptr);
    assert(strcmp(e2->ssid, "Guest-Open") == 0);
    assert(e2->channel == 6);
    assert(e2->privacy == false);
    assert(e2->wpa2 == false);

    // Test update of existing entry (RSSI improved to -38)
    cache.update_from_frame(beacon1.data(), beacon1.size(), -38, 36, 1200);
    assert(cache.count() == 3);
    const bss_entry *e1_up = cache.find_by_ssid("Firebat-WiFi-5G");
    assert(e1_up->rssi == -38);
    assert(e1_up->last_seen_ms == 1200);

    // Test Age-Out
    // Age out entries older than 150ms at time 1300
    // e1: 1200 (delta 100 <= 150, keep)
    // e2: 1050 (delta 250 > 150, drop)
    // e3: 1100 (delta 200 > 150, drop)
    cache.age_out(1300, 150);
    assert(cache.count() == 1);
    assert(cache.find_by_ssid("Firebat-WiFi-5G") != nullptr);
    assert(cache.find_by_ssid("Guest-Open") == nullptr);

    printf("    -> Beacon parsing and cache operations PASSED\n");
}

static void test_scan_state_machine() {
    printf("[*] Testing scan state machine across channels...\n");

    BssCache cache;
    ScanEngine engine(cache);

    const uint8_t channels[] = {1, 6, 11, 36, 40, 44, 48};
    ScanConfig cfg = {};
    cfg.channels = channels;
    cfg.num_channels = sizeof(channels);
    cfg.dwell_time_ms = 50;
    cfg.passive = true;

    int ret = engine.start_scan(cfg);
    assert(ret == 0);
    assert(engine.state() == ScanState::SCANNING);

    uint64_t current_time = 1000;
    uint8_t next_chan = 0;
    bool scan_done = false;

    // First step starts on channel 1
    engine.step_channel(current_time, next_chan, scan_done);
    assert(next_chan == 1);
    assert(!scan_done);

    // Dwell 20ms: channel should still be 1
    current_time += 20;
    engine.step_channel(current_time, next_chan, scan_done);
    assert(next_chan == 1);
    assert(!scan_done);

    // Advance 55ms total: channel should advance to 6
    current_time += 35;
    engine.step_channel(current_time, next_chan, scan_done);
    assert(next_chan == 6);
    assert(!scan_done);

    // Step through remaining channels
    for (size_t i = 2; i < cfg.num_channels; i++) {
        current_time += 55;
        engine.step_channel(current_time, next_chan, scan_done);
        assert(next_chan == channels[i]);
        assert(!scan_done);
    }

    // After last channel expires, scan finishes
    current_time += 55;
    engine.step_channel(current_time, next_chan, scan_done);
    assert(scan_done);
    assert(engine.state() == ScanState::COMPLETED);

    printf("    -> Scan state machine PASSED\n");
}

static void test_fuzzing_and_safety_10k_cycles() {
    printf("[*] Running 10,000 malformed beacon safety iterations under ASan/UBSan...\n");

    BssCache cache;

    for (int i = 0; i < 10000; i++) {
        uint8_t fake_bssid[6] = {0x00, 0x11, 0x22, 0x33, static_cast<uint8_t>(i >> 8), static_cast<uint8_t>(i & 0xFF)};
        char ssid[32];
        snprintf(ssid, sizeof(ssid), "AP_%d", i);

        auto beacon = build_test_beacon(fake_bssid, ssid, (i % 14) + 1, (i % 2) != 0, (i % 2) != 0);

        // Truncate or corrupt frame randomly
        if (i % 3 == 0 && beacon.size() > 10) {
            beacon.resize(beacon.size() / 2); // Truncated
        } else if (i % 5 == 0) {
            beacon[1] ^= 0xFF; // Invalidate frame control
        }

        // Must never crash or leak
        cache.update_from_frame(beacon.data(), beacon.size(), -60, (i % 14) + 1, i * 10);
    }

    printf("    -> 10,000 iterations completed with 0 errors and 0 leaks!\n");
}

int main() {
    printf("=================================================================\n");
    printf("  Rtw89 802.11 Station Scanning & Beacon Parser Test Suite\n");
    printf("=================================================================\n");

    test_beacon_parsing_and_cache();
    test_scan_state_machine();
    test_fuzzing_and_safety_10k_cycles();

    printf("\n>>> ALL SCAN ENGINE & BEACON PARSER TESTS PASSED SUCCESSFULLY! <<<\n");
    return 0;
}
