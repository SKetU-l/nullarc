//
// rtw89_interrupt.h — RTL8852BE MSI / Legacy Interrupt Dispatcher
// Top-half hardware IRQ filter and bottom-half workqueue drain engine
//
#pragma once

#include "linux_shim.h"
#include "rtw8852be_reg.h"
#include "rtw8852be_mmio.h"
#include "rtw89_rings.h"

#if defined(KERNEL) || defined(__APPLE_KEXT__)
#include <stdint.h>
#else
#include <atomic>
#include <cstdint>
#include <functional>
#endif

#define RTW89_IRQ_RX_BITS       (B_AX_HD0ISR_IND_INT_EN | B_AX_HD1ISR_IND_INT_EN)
#define RTW89_IRQ_TX_BITS       (B_AX_TXDMA_CH12_INT_EN | B_AX_TXDMA_CH9_INT_EN)
#define RTW89_IRQ_DEFAULT_MASK  (RTW89_IRQ_RX_BITS | RTW89_IRQ_TX_BITS | B_AX_HC00ISR_IND_INT_EN)

struct rtw89_irq_info {
    Rtw89Mmio *mmio{nullptr};
    bool msi_enabled{true};
    uint32_t himr0_reg{R_AX_HIMR0};
    uint32_t hisr0_reg{R_AX_HISR0};
    uint32_t enabled_mask{RTW89_IRQ_DEFAULT_MASK};

    std::atomic<bool> irq_enabled{false};
    std::atomic<bool> irq_masked{true};
    std::atomic<uint64_t> irq_count{0};
    std::atomic<uint64_t> spurious_count{0};
    std::atomic<uint64_t> rx_event_count{0};
    std::atomic<uint64_t> tx_event_count{0};

    // Callback invoked when top-half signals bottom-half work is needed
#if !defined(KERNEL) && !defined(__APPLE_KEXT__)
    std::function<void()> bottom_half_trigger{nullptr};
#else
    void (*bottom_half_trigger)(void *ctx){nullptr};
    void *bottom_half_ctx{nullptr};
#endif
};

// ============================================================================
// API Declarations
// ============================================================================

int  rtw89_pci_irq_init(rtw89_irq_info *irq, Rtw89Mmio *mmio, bool msi = true);
void rtw89_pci_irq_enable(rtw89_irq_info *irq);
void rtw89_pci_irq_disable(rtw89_irq_info *irq);
void rtw89_pci_irq_mask(rtw89_irq_info *irq);
void rtw89_pci_irq_unmask(rtw89_irq_info *irq);

// Top-half ISR (invoked in hardware / filter interrupt context)
// Returns true if bottom-half worker dispatch is needed, false if spurious
bool rtw89_pci_irq_top_half(rtw89_irq_info *irq);

// Bottom-half worker (invoked in asynchronous thread / workloop context)
// Drains RX packets, reaps TX descriptors, and re-enables IRQ mask
void rtw89_pci_irq_bottom_half(rtw89_irq_info *irq, rtw89_rx_ring *rx_ring, rtw89_tx_ring *tx_ring);
