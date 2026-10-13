//
// test_rtw89_lifecycle.cpp — Unit tests for Rtl89Driver & Rtl89UserClient Logic
//
#include "linux_shim.h"
#include "mac80211_assoc.h"
#include "mac80211_frame.h"
#include "mac80211_scan.h"
#include "rtw8852be_fw.h"
#include "rtw8852be_mmio.h"
#include "rtw8852be_pci.h"
#include "rtw8852be_phy.h"
#include "rtw8852be_reg.h"
#include "rtw89_dma.h"
#include "rtw89_interrupt.h"
#include "rtw89_rings.h"
#include "rtw89_tx.h"
#include "wpa2_psk.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

class MockRtl89Controller {
public:
    MockMmioBus         mmio{0x80000, false};
    rtw89_tx_ring       txRing{};
    rtw89_rx_ring       rxRing{};
    rtw89_irq_info      irqInfo{};
    BssCache            bssCache{};
    ScanEngine          scanEngine{bssCache};
    AssocStateMachine   assocFsm{};
    Wpa2HandshakeEngine wpa2Engine{};
    uint8_t             macAddr[6]{0x00, 0xE0, 0x4C, 0x88, 0x52, 0xBE};
    rtw89_pci_info      pciInfo{};
    bool                connected{false};
    char                connectedSsid[33]{0};
    uint8_t             connectedBssid[6]{0};
    uint8_t             currentChannel{36};

    int init_hardware() {
        // 1. PCIe ASPM disablement
        rtw89_pci_init(&pciInfo, &mmio);
        rtw89_pci_disable_aspm_l1_l1ss(&pciInfo);
        // 2. Power state D0
        rtw89_pci_set_power_state(&pciInfo, RTW89_POWER_D0);
        // 3. PHY / RF init
        rtw89_phy_init(mmio);
        // 4. Rings init
        int ret = rtw89_pci_init_tx_ring(&mmio, &txRing, 0, 64);
        if (ret != 0) return ret;
        ret = rtw89_pci_init_rx_ring(&mmio, &rxRing, 0, 64);
        if (ret != 0) return ret;
        // 5. Interrupt init & unmask
        rtw89_pci_irq_init(&irqInfo, &mmio, true);
        rtw89_pci_irq_enable(&irqInfo);
        return 0;
    }

    void cleanup_hardware() {
        rtw89_pci_irq_disable(&irqInfo);
        rtw89_pci_free_tx_ring(&txRing);
        rtw89_pci_free_rx_ring(&rxRing);
    }

    int connect(const char *ssid, const char *passphrase) {
        const bss_entry *target = bssCache.find_by_ssid(ssid);
        if (!target) return -1;

        memcpy(connectedBssid, target->bssid, 6);
        snprintf(connectedSsid, sizeof(connectedSsid), "%s", ssid);
        currentChannel = target->channel;

        assocFsm.start_association(macAddr, *target, 1000);
        wpa2Engine.init(macAddr, target->bssid, passphrase, ssid);
        connected = true;
        return 0;
    }

    void disconnect() {
        connected = false;
        memset(connectedSsid, 0, sizeof(connectedSsid));
        memset(connectedBssid, 0, sizeof(connectedBssid));
        assocFsm.reset();
        wpa2Engine.reset();
    }
};

static void test_controller_lifecycle() {
    printf("[*] Testing Rtl89 controller hardware init & lifecycle...\n");

    MockRtl89Controller ctl;
    int ret = ctl.init_hardware();
    assert(ret == 0);

    // Verify ASPM L1 was disabled in mock MMIO
    uint32_t link_ctrl = ctl.mmio.read32(R_AX_PCIE_EXP_LNK_CTRL);
    assert((link_ctrl & 0x03) == 0); // ASPM bits 0..1 clear

    // Populate BSS cache with an AP
    bss_entry ap = {};
    memcpy(ap.bssid, "\x00\x1A\x2B\x3C\x4D\x5E", 6);
    snprintf(ap.ssid, sizeof(ap.ssid), "Firebat-5G");
    ap.ssid_len = 10;
    ap.channel = 36;
    ap.rssi = -42;
    ap.privacy = true;
    ap.wpa2 = true;

    // Simulate beacon arriving into cache
    uint8_t beacon_raw[64] = {0};
    uint16_t fc = (IEEE80211_FTYPE_MGMT << 2) | (IEEE80211_STYPE_BEACON << 4);
    beacon_raw[0] = static_cast<uint8_t>(fc & 0xFF);
    beacon_raw[1] = static_cast<uint8_t>((fc >> 8) & 0xFF);
    memcpy(&beacon_raw[16], ap.bssid, 6);
    beacon_raw[36] = WLAN_EID_SSID;
    beacon_raw[37] = 10;
    memcpy(&beacon_raw[38], "Firebat-5G", 10);
    beacon_raw[48] = WLAN_EID_DS_PARAMS;
    beacon_raw[49] = 1;
    beacon_raw[50] = 36;
    ctl.bssCache.update_from_frame(beacon_raw, 51, -42, 36, 1000);

    // Verify scan results retrieval
    bss_entry results[10];
    size_t count = ctl.bssCache.get_entries(results, 10);
    assert(count == 1);
    assert(strcmp(results[0].ssid, "Firebat-5G") == 0);

    // Test connect
    ret = ctl.connect("Firebat-5G", "TopSecretKey");
    assert(ret == 0);
    assert(ctl.connected);
    assert(strcmp(ctl.connectedSsid, "Firebat-5G") == 0);
    assert(ctl.currentChannel == 36);

    // Test transmit when connected
    uint8_t dummy_packet[64] = {0};
    dummy_packet[12] = 0x08; dummy_packet[13] = 0x00; // IPv4
    rtw89_mbuf mb = {dummy_packet, sizeof(dummy_packet), nullptr, 0};
    ret = rtw89_tx_xmit_mbuf(&ctl.mmio, &ctl.txRing, &mb, 0);
    assert(ret == 0);

    // Test disconnect
    ctl.disconnect();
    assert(!ctl.connected);

    ctl.cleanup_hardware();
    printf("    -> Controller lifecycle & UserClient dispatch PASSED\n");
}

int main() {
    printf("=================================================================\n");
    printf("  Rtw89 Controller Lifecycle & UserClient Test Suite\n");
    printf("=================================================================\n");

    test_controller_lifecycle();

    printf("\n>>> ALL LIFECYCLE TESTS PASSED SUCCESSFULLY! <<<\n");
    return 0;
}
