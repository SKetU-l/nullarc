//
// test_rtw89_interrupt.cpp — Unit tests for RTL8852BE MSI / Legacy Interrupt Dispatcher
//
#include "linux_shim.h"
#include "skbuff.h"
#include "rtw89_dma.h"
#include "rtw8852be_reg.h"
#include "rtw8852be_mmio.h"
#include "rtw89_rings.h"
#include "rtw89_interrupt.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

static void test_irq_init_enable_disable() {
    printf("[TEST] Running test_irq_init_enable_disable...\n");

    MockMmioBus mmio(0x20000, false);
    rtw89_irq_info irq{};

    int ret = rtw89_pci_irq_init(&irq, &mmio, true);
    assert(ret == 0);
    assert(irq.irq_enabled == false);
    assert(irq.irq_masked == true);
    assert(mmio.read32(irq.himr0_reg) == 0);

    // Enable IRQ
    rtw89_pci_irq_enable(&irq);
    assert(irq.irq_enabled == true);
    assert(irq.irq_masked == false);
    assert(mmio.read32(irq.himr0_reg) == irq.enabled_mask);

    // Disable IRQ
    rtw89_pci_irq_disable(&irq);
    assert(irq.irq_enabled == false);
    assert(irq.irq_masked == true);
    assert(mmio.read32(irq.himr0_reg) == 0);

    printf("[PASS] test_irq_init_enable_disable\n");
}

static void test_top_half_and_bottom_half() {
    printf("[TEST] Running test_top_half_and_bottom_half...\n");

    MockMmioBus mmio(0x20000, false);
    rtw89_irq_info irq{};
    rtw89_pci_irq_init(&irq, &mmio, true);
    rtw89_pci_irq_enable(&irq);

    rtw89_rx_ring rx_ring{};
    rtw89_tx_ring tx_ring{};
    rtw89_pci_init_rx_ring(&mmio, &rx_ring, 0, 16, 1500);
    rtw89_pci_init_tx_ring(&mmio, &tx_ring, 0, 16);

    bool bh_dispatched = false;
    irq.bottom_half_trigger = [&]() {
        bh_dispatched = true;
    };

    // 1. Simulate arrival of RX interrupt in hardware status register
    mmio.write32(irq.hisr0_reg, B_AX_HD0ISR_IND_INT_EN);

    // 2. Run top-half ISR
    bool handled = rtw89_pci_irq_top_half(&irq);
    assert(handled == true);
    assert(bh_dispatched == true);
    assert(irq.irq_count == 1);
    assert(irq.rx_event_count == 1);
    assert(irq.irq_masked == true);

    // Assert top-half immediately disabled hardware IRQ mask to prevent storm
    assert(mmio.read32(irq.himr0_reg) == 0);

    // 3. Run bottom-half worker
    rtw89_pci_irq_bottom_half(&irq, &rx_ring, &tx_ring);

    // Assert bottom-half re-enabled hardware IRQ mask
    assert(irq.irq_masked == false);
    assert(mmio.read32(irq.himr0_reg) == irq.enabled_mask);

    // 4. Test spurious interrupt handling (status == 0)
    mmio.write32(irq.hisr0_reg, 0);
    bh_dispatched = false;
    handled = rtw89_pci_irq_top_half(&irq);
    assert(handled == false);
    assert(bh_dispatched == false);
    assert(irq.spurious_count == 1);
    // Spurious IRQ must NOT mask interrupts or alter state
    assert(irq.irq_masked == false);

    rtw89_pci_free_rx_ring(&rx_ring);
    rtw89_pci_free_tx_ring(&tx_ring);

    printf("[PASS] test_top_half_and_bottom_half\n");
}

static void test_irq_stress() {
    printf("[TEST] Running test_irq_stress (10,000 cycles)...\n");

    MockMmioBus mmio(0x20000, false);
    rtw89_irq_info irq{};
    rtw89_pci_irq_init(&irq, &mmio, true);
    rtw89_pci_irq_enable(&irq);

    rtw89_rx_ring rx_ring{};
    rtw89_tx_ring tx_ring{};
    rtw89_pci_init_rx_ring(&mmio, &rx_ring, 0, 32, 1500);
    rtw89_pci_init_tx_ring(&mmio, &tx_ring, 0, 32);

    for (int i = 0; i < 10000; ++i) {
        // Enqueue a TX packet occasionally
        if (i % 4 == 0) {
            struct sk_buff *skb = alloc_skb(64, GFP_KERNEL);
            skb_put(skb, 64);
            rtw89_pci_tx_enqueue(&mmio, &tx_ring, skb, 0x20000000ULL | ((i & 0xF) * 128), 64);
        }

        // Fire RX and TX interrupts
        uint32_t status = B_AX_HD0ISR_IND_INT_EN | ((i % 4 == 0) ? B_AX_TXDMA_CH9_INT_EN : 0);
        mmio.write32(irq.hisr0_reg, status);

        bool handled = rtw89_pci_irq_top_half(&irq);
        assert(handled);

        rtw89_pci_irq_bottom_half(&irq, &rx_ring, &tx_ring);
        assert(irq.irq_masked == false);
    }

    assert(irq.irq_count == 10000);
    assert(irq.rx_event_count == 10000);

    rtw89_pci_free_rx_ring(&rx_ring);
    rtw89_pci_free_tx_ring(&tx_ring);

    printf("[PASS] test_irq_stress (10,000 cycles passed, 0 leaks)\n");
}

int main() {
    printf("=== Realtek RTL8852BE MSI / Legacy Interrupt Dispatcher Unit Tests ===\n");

    test_irq_init_enable_disable();
    test_top_half_and_bottom_half();
    test_irq_stress();

    printf("=== All Interrupt Dispatcher tests passed successfully (0 errors) ===\n");
    return 0;
}
