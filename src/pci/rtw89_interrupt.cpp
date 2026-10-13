//
// rtw89_interrupt.cpp — RTL8852BE MSI / Legacy Interrupt Dispatcher Implementation
//
#include "rtw89_interrupt.h"

#if !defined(KERNEL) && !defined(__APPLE_KEXT__)
#include <cerrno>
#endif

int rtw89_pci_irq_init(rtw89_irq_info *irq, Rtw89Mmio *mmio, bool msi) {
    if (!irq || !mmio) {
        return -EINVAL;
    }

    irq->mmio = mmio;
    irq->msi_enabled = msi;
    irq->himr0_reg = R_AX_HIMR0;
    irq->hisr0_reg = R_AX_HISR0;
    irq->enabled_mask = RTW89_IRQ_DEFAULT_MASK;

    irq->irq_enabled = false;
    irq->irq_masked = true;
    irq->irq_count = 0;
    irq->spurious_count = 0;
    irq->rx_event_count = 0;
    irq->tx_event_count = 0;
    irq->bottom_half_trigger = nullptr;

    // Mask hardware interrupts on initialization
    irq->mmio->write32(irq->himr0_reg, 0);
    Rtw89Mmio::mb();

    return 0;
}

void rtw89_pci_irq_mask(rtw89_irq_info *irq) {
    if (!irq || !irq->mmio) {
        return;
    }
    irq->mmio->write32(irq->himr0_reg, 0);
    Rtw89Mmio::mb();
    irq->irq_masked = true;
}

void rtw89_pci_irq_unmask(rtw89_irq_info *irq) {
    if (!irq || !irq->mmio) {
        return;
    }
    irq->mmio->write32(irq->himr0_reg, irq->enabled_mask);
    Rtw89Mmio::mb();
    irq->irq_masked = false;
}

void rtw89_pci_irq_enable(rtw89_irq_info *irq) {
    if (!irq) {
        return;
    }
    irq->irq_enabled = true;
    rtw89_pci_irq_unmask(irq);
}

void rtw89_pci_irq_disable(rtw89_irq_info *irq) {
    if (!irq) {
        return;
    }
    irq->irq_enabled = false;
    rtw89_pci_irq_mask(irq);
}

bool rtw89_pci_irq_top_half(rtw89_irq_info *irq) {
    if (!irq || !irq->mmio || !irq->irq_enabled.load(std::memory_order_acquire)) {
        return false;
    }

    uint32_t isr = irq->mmio->read32(irq->hisr0_reg);
    uint32_t active = isr & irq->enabled_mask;

    if (active == 0) {
        irq->spurious_count.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    // Top-half immediate action: disable interrupt mask in hardware
    // This stops further IRQs while bottom-half worker runs
    irq->mmio->write32(irq->himr0_reg, 0);
    Rtw89Mmio::mb();
    irq->irq_masked.store(true, std::memory_order_release);
    irq->irq_count.fetch_add(1, std::memory_order_relaxed);

    // Acknowledge active interrupt flags (write-1-to-clear)
    irq->mmio->write32(irq->hisr0_reg, active);

    if (active & RTW89_IRQ_RX_BITS) {
        irq->rx_event_count.fetch_add(1, std::memory_order_relaxed);
    }
    if (active & RTW89_IRQ_TX_BITS) {
        irq->tx_event_count.fetch_add(1, std::memory_order_relaxed);
    }

#if !defined(KERNEL) && !defined(__APPLE_KEXT__)
    if (irq->bottom_half_trigger) {
        irq->bottom_half_trigger();
    }
#else
    if (irq->bottom_half_trigger) {
        irq->bottom_half_trigger(irq->bottom_half_ctx);
    }
#endif

    return true;
}

void rtw89_pci_irq_bottom_half(rtw89_irq_info *irq, rtw89_rx_ring *rx_ring, rtw89_tx_ring *tx_ring) {
    if (!irq || !irq->mmio) {
        return;
    }

    // 1. Drain RX ring packets
    if (rx_ring) {
        size_t drained = 0;
        while (!rx_ring->is_empty() && drained < 64) {
            struct sk_buff *skb = rtw89_pci_rx_dequeue(irq->mmio, rx_ring);
            if (skb) {
                dev_kfree_skb_any(skb);
                drained++;
            } else {
                break;
            }
        }
        if (drained > 0) {
            rtw89_pci_rx_refill(irq->mmio, rx_ring);
        }
    }

    // 2. Reap finished TX ring descriptors
    if (tx_ring) {
        rtw89_pci_tx_reap(irq->mmio, tx_ring, 64);
    }

    // 3. Re-enable hardware interrupt mask if driver remains enabled
    if (irq->irq_enabled.load(std::memory_order_acquire)) {
        rtw89_pci_irq_unmask(irq);
    }
}
