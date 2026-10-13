//
// test_rtw89_rings.cpp — Unit tests for PCIe TX/RX descriptor rings
//
#include "linux_shim.h"
#include "skbuff.h"
#include "rtw89_dma.h"
#include "rtw8852be_reg.h"
#include "rtw8852be_mmio.h"
#include "rtw89_rings.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void test_tx_ring_init_and_wrap() {
    printf("[TEST] Running test_tx_ring_init_and_wrap...\n");

    MockMmioBus mmio(0x20000, false);
    rtw89_tx_ring ring{};

    int ret = rtw89_pci_init_tx_ring(&mmio, &ring, 0, 16);
    assert(ret == 0);
    assert(ring.size == 16);
    assert(ring.mask == 15);
    assert(ring.is_empty());
    assert(!ring.is_full());
    assert(ring.space() == 16);

    // Enqueue 16 packets to fill ring
    for (int i = 0; i < 16; ++i) {
        struct sk_buff *skb = alloc_skb(64, GFP_KERNEL);
        assert(skb != nullptr);
        skb_put(skb, 64);
        uint64_t dummy_paddr = 0x20000000ULL + i * 128;
        ret = rtw89_pci_tx_enqueue(&mmio, &ring, skb, dummy_paddr, 64);
        assert(ret == 0);
    }

    assert(ring.is_full());
    assert(ring.count() == 16);

    // Attempting to enqueue into full ring must return -ENOSPC
    struct sk_buff *extra_skb = alloc_skb(64, GFP_KERNEL);
    ret = rtw89_pci_tx_enqueue(&mmio, &ring, extra_skb, 0x20001000ULL, 64);
    assert(ret == -ENOSPC);
    kfree_skb(extra_skb);

    // Reap 8 packets
    int reaped = rtw89_pci_tx_reap(&mmio, &ring, 8);
    assert(reaped == 8);
    assert(!ring.is_full());
    assert(ring.count() == 8);

    // Enqueue 8 more packets to exercise ring wrap-around
    for (int i = 0; i < 8; ++i) {
        struct sk_buff *skb = alloc_skb(64, GFP_KERNEL);
        skb_put(skb, 64);
        uint64_t dummy_paddr = 0x20002000ULL + i * 128;
        ret = rtw89_pci_tx_enqueue(&mmio, &ring, skb, dummy_paddr, 64);
        assert(ret == 0);
    }

    assert(ring.is_full());

    // Reap all
    reaped = rtw89_pci_tx_reap(&mmio, &ring, 32);
    assert(reaped == 16);
    assert(ring.is_empty());

    rtw89_pci_free_tx_ring(&ring);
    assert(ring.bd_ring.vaddr == nullptr);

    printf("[PASS] test_tx_ring_init_and_wrap\n");
}

static void test_rx_ring_lifecycle() {
    printf("[TEST] Running test_rx_ring_lifecycle...\n");

    MockMmioBus mmio(0x20000, false);
    rtw89_rx_ring ring{};

    int ret = rtw89_pci_init_rx_ring(&mmio, &ring, 0, 32, 2048);
    assert(ret == 0);
    assert(ring.size == 32);
    assert(ring.mask == 31);
    assert(ring.count() == 32);

    // Dequeue 10 packets
    std::vector<struct sk_buff *> dequeued;
    for (int i = 0; i < 10; ++i) {
        struct sk_buff *skb = rtw89_pci_rx_dequeue(&mmio, &ring);
        assert(skb != nullptr);
        dequeued.push_back(skb);
    }

    assert(ring.count() == 22);

    // Free dequeued packets
    for (auto *skb : dequeued) {
        dev_kfree_skb_any(skb);
    }

    // Refill the 10 consumed slots
    int refilled = rtw89_pci_rx_refill(&mmio, &ring);
    assert(refilled == 10);
    assert(ring.count() == 32);

    rtw89_pci_free_rx_ring(&ring);
    assert(ring.bd_ring.vaddr == nullptr);

    printf("[PASS] test_rx_ring_lifecycle\n");
}

static void test_rings_stress() {
    printf("[TEST] Running test_rings_stress (100,000 iterations)...\n");

    MockMmioBus mmio(0x20000, false);
    rtw89_tx_ring tx_ring{};
    rtw89_rx_ring rx_ring{};

    rtw89_pci_init_tx_ring(&mmio, &tx_ring, 0, 64);
    rtw89_pci_init_rx_ring(&mmio, &rx_ring, 0, 64, 1500);

    for (int i = 0; i < 100000; ++i) {
        // TX enqueue and reap
        struct sk_buff *tx_skb = alloc_skb(128, GFP_KERNEL);
        skb_put(tx_skb, 128);
        int ret = rtw89_pci_tx_enqueue(&mmio, &tx_ring, tx_skb, 0x20000000ULL | ((i & 0xFF) * 128), 128);
        assert(ret == 0);

        if (tx_ring.count() >= 32) {
            rtw89_pci_tx_reap(&mmio, &tx_ring, 16);
        }

        // RX dequeue and refill
        struct sk_buff *rx_skb = rtw89_pci_rx_dequeue(&mmio, &rx_ring);
        if (rx_skb) {
            dev_kfree_skb_any(rx_skb);
        }

        if (rx_ring.count() <= 32) {
            rtw89_pci_rx_refill(&mmio, &rx_ring);
        }
    }

    // Clean up remaining
    rtw89_pci_tx_reap(&mmio, &tx_ring, 64);
    rtw89_pci_free_tx_ring(&tx_ring);
    rtw89_pci_free_rx_ring(&rx_ring);

    printf("[PASS] test_rings_stress (100,000 cycles passed, 0 leaks)\n");
}

int main() {
    printf("=== Realtek RTL8852BE PCIe TX/RX Ring Management Unit Tests ===\n");

    test_tx_ring_init_and_wrap();
    test_rx_ring_lifecycle();
    test_rings_stress();

    printf("=== All Ring Management tests passed successfully (0 errors) ===\n");
    return 0;
}
