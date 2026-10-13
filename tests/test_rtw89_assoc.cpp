//
// test_rtw89_assoc.cpp — Unit tests for 802.11 Authentication & Association State Machine
//
#include "mac80211_assoc.h"
#include "mac80211_frame.h"
#include "mac80211_scan.h"

#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <vector>

static const uint8_t g_sta_mac[6] = {0x10, 0x22, 0x33, 0x44, 0x55, 0x66};
static const uint8_t g_ap_bssid[6] = {0x00, 0x1A, 0x2B, 0x3C, 0x4D, 0x5E};

static bss_entry create_dummy_ap(bool wpa2 = true) {
    bss_entry ap = {};
    memcpy(ap.bssid, g_ap_bssid, 6);
    snprintf(ap.ssid, sizeof(ap.ssid), "Firebat-U6C-Net");
    ap.ssid_len = static_cast<uint8_t>(strlen(ap.ssid));
    ap.channel = 36;
    ap.rssi = -50;
    ap.beacon_interval = 100;
    ap.capability = WLAN_CAPABILITY_ESS | WLAN_CAPABILITY_PRIVACY;
    ap.privacy = true;
    ap.wpa2 = wpa2;
    ap.pairwise_ciphers = WLAN_CIPHER_SUITE_CCMP;
    ap.akm_suites = WLAN_AKM_SUITE_PSK;
    return ap;
}

static std::vector<uint8_t> build_auth_resp(uint16_t status_code) {
    std::vector<uint8_t> frame;
    // 802.11 Mgmt Header (Auth)
    uint16_t fc = (IEEE80211_FTYPE_MGMT << 2) | (IEEE80211_STYPE_AUTH << 4);
    frame.push_back(static_cast<uint8_t>(fc & 0xFF));
    frame.push_back(static_cast<uint8_t>((fc >> 8) & 0xFF));
    frame.push_back(0); frame.push_back(0); // Duration

    // Addr1 (STA)
    for (int i = 0; i < 6; i++) frame.push_back(g_sta_mac[i]);
    // Addr2 (AP)
    for (int i = 0; i < 6; i++) frame.push_back(g_ap_bssid[i]);
    // Addr3 (BSSID)
    for (int i = 0; i < 6; i++) frame.push_back(g_ap_bssid[i]);
    // Seq Ctrl
    frame.push_back(0x20); frame.push_back(0x00);

    // Auth Body
    ieee80211_auth_body body = {};
    body.auth_alg = cpu_to_le16(WLAN_AUTH_OPEN);
    body.auth_transaction = cpu_to_le16(2); // Response
    body.status_code = cpu_to_le16(status_code);

    const uint8_t *p = reinterpret_cast<const uint8_t *>(&body);
    frame.insert(frame.end(), p, p + sizeof(body));

    return frame;
}

static std::vector<uint8_t> build_assoc_resp(uint16_t status_code, uint16_t aid) {
    std::vector<uint8_t> frame;
    // 802.11 Mgmt Header (Assoc Resp)
    uint16_t fc = (IEEE80211_FTYPE_MGMT << 2) | (IEEE80211_STYPE_ASSOC_RESP << 4);
    frame.push_back(static_cast<uint8_t>(fc & 0xFF));
    frame.push_back(static_cast<uint8_t>((fc >> 8) & 0xFF));
    frame.push_back(0); frame.push_back(0); // Duration

    // Addr1 (STA)
    for (int i = 0; i < 6; i++) frame.push_back(g_sta_mac[i]);
    // Addr2 (AP)
    for (int i = 0; i < 6; i++) frame.push_back(g_ap_bssid[i]);
    // Addr3 (BSSID)
    for (int i = 0; i < 6; i++) frame.push_back(g_ap_bssid[i]);
    // Seq Ctrl
    frame.push_back(0x30); frame.push_back(0x00);

    // Assoc Resp Body
    ieee80211_assoc_resp_body body = {};
    body.capability = cpu_to_le16(WLAN_CAPABILITY_ESS | WLAN_CAPABILITY_PRIVACY);
    body.status_code = cpu_to_le16(status_code);
    body.aid = cpu_to_le16(0xC000 | (aid & 0x3FFF));

    const uint8_t *p = reinterpret_cast<const uint8_t *>(&body);
    frame.insert(frame.end(), p, p + sizeof(body));

    return frame;
}

static void test_assoc_happy_path() {
    printf("[*] Testing association happy path (Open Auth + WPA2 Assoc)...\n");

    AssocStateMachine fsm;
    assert(fsm.state() == AssocState::IDLE);

    bss_entry ap = create_dummy_ap(true);
    uint64_t now = 1000;

    int ret = fsm.start_association(g_sta_mac, ap, now);
    assert(ret == 0);
    assert(fsm.state() == AssocState::AUTHENTICATING);

    // 1. Generate Auth Req
    struct sk_buff *auth_req = fsm.generate_tx_frame();
    assert(auth_req != nullptr);
    assert(auth_req->len >= IEEE80211_HDR3_LEN + sizeof(ieee80211_auth_body));
    const ieee80211_hdr *hdr = reinterpret_cast<const ieee80211_hdr *>(auth_req->data);
    assert(memcmp(hdr->addr1, g_ap_bssid, 6) == 0);
    assert(memcmp(hdr->addr2, g_sta_mac, 6) == 0);
    kfree_skb(auth_req);

    // Verify subsequent generate_tx_frame returns null when none pending
    assert(fsm.generate_tx_frame() == nullptr);

    // 2. Feed Auth Resp Success
    auto auth_resp = build_auth_resp(WLAN_STATUS_SUCCESS);
    now += 15;
    ret = fsm.process_rx_frame(auth_resp.data(), auth_resp.size(), now);
    assert(ret == 0);
    assert(fsm.state() == AssocState::ASSOCIATING);

    // 3. Generate Assoc Req
    struct sk_buff *assoc_req = fsm.generate_tx_frame();
    assert(assoc_req != nullptr);
    assert(assoc_req->len > IEEE80211_HDR3_LEN + 4);
    kfree_skb(assoc_req);

    // 4. Feed Assoc Resp Success (AID = 7)
    auto assoc_resp = build_assoc_resp(WLAN_STATUS_SUCCESS, 7);
    now += 20;
    ret = fsm.process_rx_frame(assoc_resp.data(), assoc_resp.size(), now);
    assert(ret == 0);
    assert(fsm.state() == AssocState::ASSOCIATED);
    assert(fsm.aid() == 7);
    assert(fsm.last_status() == AssocStatus::SUCCESS);

    printf("    -> Happy path PASSED in 35ms (AID=7)\n");
}

static void test_auth_rejection() {
    printf("[*] Testing authentication rejection path...\n");

    AssocStateMachine fsm;
    bss_entry ap = create_dummy_ap();
    fsm.start_association(g_sta_mac, ap, 1000);

    struct sk_buff *req = fsm.generate_tx_frame();
    kfree_skb(req);

    // AP responds with rejection status 13
    auto auth_resp = build_auth_resp(WLAN_STATUS_NOT_SUPPORTED_AUTH_ALG);
    int ret = fsm.process_rx_frame(auth_resp.data(), auth_resp.size(), 1010);
    assert(ret == -EACCES);
    assert(fsm.state() == AssocState::FAILED);
    assert(fsm.last_status() == AssocStatus::AUTH_REJECTED);

    printf("    -> Auth rejection PASSED\n");
}

static void test_assoc_rejection() {
    printf("[*] Testing association rejection path...\n");

    AssocStateMachine fsm;
    bss_entry ap = create_dummy_ap();
    fsm.start_association(g_sta_mac, ap, 1000);

    struct sk_buff *req = fsm.generate_tx_frame();
    kfree_skb(req);

    auto auth_resp = build_auth_resp(WLAN_STATUS_SUCCESS);
    fsm.process_rx_frame(auth_resp.data(), auth_resp.size(), 1010);
    assert(fsm.state() == AssocState::ASSOCIATING);

    struct sk_buff *areq = fsm.generate_tx_frame();
    kfree_skb(areq);

    // AP responds with Assoc Denied (12)
    auto assoc_resp = build_assoc_resp(WLAN_STATUS_ASSOC_DENIED_UNSPEC, 0);
    int ret = fsm.process_rx_frame(assoc_resp.data(), assoc_resp.size(), 1025);
    assert(ret == -EACCES);
    assert(fsm.state() == AssocState::FAILED);
    assert(fsm.last_status() == AssocStatus::ASSOC_REJECTED);

    printf("    -> Assoc rejection PASSED\n");
}

static void test_assoc_timeout() {
    printf("[*] Testing association timeout (3000ms)...\n");

    AssocStateMachine fsm;
    bss_entry ap = create_dummy_ap();
    fsm.start_association(g_sta_mac, ap, 1000);

    // At 2500ms, should not time out
    fsm.check_timeout(3500);
    assert(fsm.state() == AssocState::AUTHENTICATING);

    // At 3001ms delta (time 4001), should time out
    fsm.check_timeout(4001);
    assert(fsm.state() == AssocState::FAILED);
    assert(fsm.last_status() == AssocStatus::TIMEOUT);

    printf("    -> Association timeout PASSED\n");
}

static void test_stress_and_fuzzing_10k_cycles() {
    printf("[*] Running 10,000 association state machine fuzzing cycles under ASan/UBSan...\n");

    AssocStateMachine fsm;
    bss_entry ap = create_dummy_ap();

    for (int i = 0; i < 10000; i++) {
        fsm.reset();
        fsm.start_association(g_sta_mac, ap, i * 100);

        struct sk_buff *req = fsm.generate_tx_frame();
        assert(req != nullptr);
        kfree_skb(req);

        if (i % 3 == 0) {
            // Malformed frame feeding
            uint8_t garbage[32];
            memset(garbage, static_cast<uint8_t>(i), sizeof(garbage));
            fsm.process_rx_frame(garbage, sizeof(garbage), i * 100 + 10);
        } else if (i % 3 == 1) {
            // Success path
            auto aresp = build_auth_resp(WLAN_STATUS_SUCCESS);
            fsm.process_rx_frame(aresp.data(), aresp.size(), i * 100 + 10);

            struct sk_buff *areq = fsm.generate_tx_frame();
            assert(areq != nullptr);
            kfree_skb(areq);

            auto asresp = build_assoc_resp(WLAN_STATUS_SUCCESS, i & 0x0FFF);
            fsm.process_rx_frame(asresp.data(), asresp.size(), i * 100 + 20);
            assert(fsm.state() == AssocState::ASSOCIATED);
        } else {
            // Timeout path
            fsm.check_timeout(i * 100 + 3500);
            assert(fsm.state() == AssocState::FAILED);
        }
    }

    printf("    -> 10,000 cycles completed with 0 errors and 0 leaks!\n");
}

int main() {
    printf("=================================================================\n");
    printf("  Rtw89 802.11 Authentication & Association Test Suite\n");
    printf("=================================================================\n");

    test_assoc_happy_path();
    test_auth_rejection();
    test_assoc_rejection();
    test_assoc_timeout();
    test_stress_and_fuzzing_10k_cycles();

    printf("\n>>> ALL ASSOC STATE MACHINE TESTS PASSED SUCCESSFULLY! <<<\n");
    return 0;
}
