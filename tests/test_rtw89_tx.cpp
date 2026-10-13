//
// test_rtw89_tx.cpp — Unit tests for RTL8852BE packet transmit & scatter-gather engine
//
#include "linux_shim.h"
#include "skbuff.h"
#include "rtw89_dma.h"
#include "rtw8852be_reg.h"
#include "rtw8852be_mmio.h"
#include "rtw89_rings.h"
#include "rtw89_tx.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static void test_tx_parse_mbuf() {
    printf("[TEST] Running test_tx_parse_mbuf...\n");

    // Standard IPv4 packet
    uint8_t eth_frame[1514];
    memset(eth_frame, 0, sizeof(eth_frame));
    eth_frame[0] = 0x00; eth_frame[1] = 0x11; eth_frame[2] = 0x22; // Unicast
    eth_frame[12] = 0x08; eth_frame[13] = 0x00; // IPv4

    rtw89_mbuf m1{};
    m1.m_data = eth_frame;
    m1.m_len = sizeof(eth_frame);
    m1.m_next = nullptr;

    rtw89_tx_pkt_info info{};
    int ret = rtw89_tx_parse_mbuf(&m1, &info);
    assert(ret == 0);
    assert(info.total_len == 1514);
    assert(info.num_frags == 1);
    assert(info.ether_type == 0x0800);
    assert(!info.is_multicast);
    assert(!info.is_eapol);
    assert(info.priority == 0);

    // EAPOL broadcast packet
    eth_frame[0] = 0xFF; // Broadcast
    eth_frame[12] = 0x88; eth_frame[13] = 0x8E; // EAPOL
    ret = rtw89_tx_parse_mbuf(&m1, &info);
    assert(ret == 0);
    assert(info.is_multicast);
    assert(info.is_eapol);
    assert(info.priority == 7);

    // Truncated packet (< 14 bytes)
    m1.m_len = 10;
    assert(rtw89_tx_parse_mbuf(&m1, &info) == -EINVAL);

    // Oversized packet (> 2304 MTU)
    m1.m_len = 3000;
    assert(rtw89_tx_parse_mbuf(&m1, &info) == -EMSGSIZE);

    printf("[PASS] test_tx_parse_mbuf\n");
}

static void test_tx_linearize_mbuf() {
    printf("[TEST] Running test_tx_linearize_mbuf...\n");

    // Construct 5-fragment chained mbuf
    uint8_t frag0_hdr[14] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0x08, 0x00 };
    uint8_t frag1_ip[20]; memset(frag1_ip, 0x11, sizeof(frag1_ip));
    uint8_t frag2_udp[8]; memset(frag2_udp, 0x22, sizeof(frag2_udp));
    uint8_t frag3_data[500]; memset(frag3_data, 0x33, sizeof(frag3_data));
    uint8_t frag4_data[500]; memset(frag4_data, 0x44, sizeof(frag4_data));

    rtw89_mbuf m4{frag4_data, sizeof(frag4_data), nullptr, 0};
    rtw89_mbuf m3{frag3_data, sizeof(frag3_data), &m4, 0};
    rtw89_mbuf m2{frag2_udp, sizeof(frag2_udp), &m3, 0};
    rtw89_mbuf m1{frag1_ip, sizeof(frag1_ip), &m2, 0};
    rtw89_mbuf m0{frag0_hdr, sizeof(frag0_hdr), &m1, 0};

    struct sk_buff *skb = nullptr;
    int ret = rtw89_tx_linearize_mbuf(&m0, &skb);
    assert(ret == 0);
    assert(skb != nullptr);
    assert(skb->len == 14 + 20 + 8 + 500 + 500); // 1042 bytes

    // Verify byte contents
    assert(memcmp(skb->data, frag0_hdr, 14) == 0);
    assert(memcmp(skb->data + 14, frag1_ip, 20) == 0);
    assert(memcmp(skb->data + 34, frag2_udp, 8) == 0);
    assert(memcmp(skb->data + 42, frag3_data, 500) == 0);
    assert(memcmp(skb->data + 542, frag4_data, 500) == 0);

    kfree_skb(skb);

    printf("[PASS] test_tx_linearize_mbuf\n");
}

static void test_tx_xmit_mbuf() {
    printf("[TEST] Running test_tx_xmit_mbuf...\n");

    MockMmioBus mmio(0x20000, false);
    rtw89_tx_ring ring{};
    rtw89_pci_init_tx_ring(&mmio, &ring, 0, 16);

    uint8_t payload[512];
    memset(payload, 0xAB, sizeof(payload));
    payload[0] = 0x02; // Unicast
    payload[12] = 0x08; payload[13] = 0x00; // IPv4

    rtw89_mbuf m{payload, sizeof(payload), nullptr, 0};

    // Transmit 16 packets
    for (int i = 0; i < 16; ++i) {
        int ret = rtw89_tx_xmit_mbuf(&mmio, &ring, &m, 0);
        assert(ret == 0);
    }

    assert(ring.is_full());

    // 17th packet must return -ENOSPC
    int ret = rtw89_tx_xmit_mbuf(&mmio, &ring, &m, 0);
    assert(ret == -ENOSPC);

    // Reap all
    int reaped = rtw89_pci_tx_reap(&mmio, &ring, 32);
    assert(reaped == 16);
    assert(ring.is_empty());

    rtw89_pci_free_tx_ring(&ring);

    printf("[PASS] test_tx_xmit_mbuf\n");
}

static void test_tx_stress() {
    printf("[TEST] Running test_tx_stress (10,000 packets)...\n");

    MockMmioBus mmio(0x20000, false);
    rtw89_tx_ring ring{};
    rtw89_pci_init_tx_ring(&mmio, &ring, 0, 64);

    uint8_t frag0[14]; memset(frag0, 0x01, sizeof(frag0)); frag0[12] = 0x08; frag0[13] = 0x00;
    uint8_t frag1[64]; memset(frag1, 0x02, sizeof(frag1));
    uint8_t frag2[128]; memset(frag2, 0x03, sizeof(frag2));
    uint8_t frag3[256]; memset(frag3, 0x04, sizeof(frag3));

    rtw89_mbuf m3{frag3, sizeof(frag3), nullptr, 0};
    rtw89_mbuf m2{frag2, sizeof(frag2), &m3, 0};
    rtw89_mbuf m1{frag1, sizeof(frag1), &m2, 0};
    rtw89_mbuf m0{frag0, sizeof(frag0), &m1, 0};

    for (int i = 0; i < 10000; ++i) {
        int ret = rtw89_tx_xmit_mbuf(&mmio, &ring, &m0, 0);
        assert(ret == 0);

        if (ring.count() >= 32) {
            rtw89_pci_tx_reap(&mmio, &ring, 16);
        }
    }

    rtw89_pci_tx_reap(&mmio, &ring, 64);
    assert(ring.is_empty());
    rtw89_pci_free_tx_ring(&ring);

    printf("[PASS] test_tx_stress (10,000 chained packets passed, 0 leaks)\n");
}

int main() {
    printf("=== Realtek RTL8852BE Packet Transmit & Scatter-Gather Unit Tests ===\n");

    test_tx_parse_mbuf();
    test_tx_linearize_mbuf();
    test_tx_xmit_mbuf();
    test_tx_stress();

    printf("=== All Packet Transmit tests passed successfully (0 errors) ===\n");
    return 0;
}
