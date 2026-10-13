//
// rtw89_rings.h — RTL8852BE PCIe TX/RX Descriptor Ring Management
//
#pragma once

#include "linux_shim.h"
#include "skbuff.h"
#include "rtw89_dma.h"
#include "rtw8852be_reg.h"
#include "rtw8852be_mmio.h"

#if defined(KERNEL) || defined(__APPLE_KEXT__)
#include <stddef.h>
#include <stdint.h>
#else
#include <cstddef>
#include <cstdint>
#include <vector>
#endif

#define RTW89_TX_RING_DEFAULT_SIZE      256
#define RTW89_RX_RING_DEFAULT_SIZE      512
#define RTW89_RX_BUF_SIZE               4096

// Hardware TX Buffer Descriptor (16 bytes)
struct rtw89_tx_desc {
    __le32 dma_addr_l;                  // Physical address bits 31..0
    __le32 dma_addr_h;                  // Physical address bits 63..32 (always 0)
    __le32 flags;                       // Ownership, wrap, end-of-packet, aggregation
    __le32 len;                         // Buffer byte length (bits 15..0)
} __attribute__((packed));

// Hardware RX Buffer Descriptor (16 bytes)
struct rtw89_rx_desc {
    __le32 dma_addr_l;                  // Physical address bits 31..0
    __le32 dma_addr_h;                  // Physical address bits 63..32 (always 0)
    __le32 flags;                       // Ownership, status, packet ready
    __le32 len;                         // Buffer byte length
} __attribute__((packed));

// TX Descriptor Ring Structure
struct rtw89_tx_ring {
    uint8_t ch{0};                      // Channel number (0..11 data, 12 H2C)
    size_t size{RTW89_TX_RING_DEFAULT_SIZE}; // Total descriptors (must be power of 2)
    size_t mask{RTW89_TX_RING_DEFAULT_SIZE - 1};
    uint32_t wp{0};                     // Host Write Pointer (index where host adds)
    uint32_t rp{0};                     // Device Read Pointer (index where device read)
    uint32_t dma_idx_reg{0};            // Hardware index register offset

    rtw89_dma_buf bd_ring{};            // Contiguous DMA buffer for descriptors
#if defined(KERNEL) || defined(__APPLE_KEXT__)
    struct sk_buff *skbs[RTW89_TX_RING_DEFAULT_SIZE]{}; // Associated packet buffers
    uint64_t dma_addrs[RTW89_TX_RING_DEFAULT_SIZE]{};    // Associated packet DMA addresses
#else
    std::vector<struct sk_buff *> skbs; // Associated packet buffers
    std::vector<uint64_t> dma_addrs;    // Associated packet DMA addresses
#endif

    inline size_t count() const {
        return wp - rp;
    }

    inline size_t space() const {
        return size - (wp - rp);
    }

    inline bool is_full() const {
        return (wp - rp) >= size;
    }

    inline bool is_empty() const {
        return wp == rp;
    }
};

// RX Descriptor Ring Structure
struct rtw89_rx_ring {
    uint8_t ch{0};                      // RX Channel number (0 data, 1 report)
    size_t size{RTW89_RX_RING_DEFAULT_SIZE};
    size_t mask{RTW89_RX_RING_DEFAULT_SIZE - 1};
    uint32_t wp{0};                     // Device Write Pointer
    uint32_t rp{0};                     // Host Read Pointer (index where host consumes)
    uint32_t dma_idx_reg{0};
    size_t buf_sz{RTW89_RX_BUF_SIZE};

    rtw89_dma_buf bd_ring{};            // Contiguous DMA buffer for descriptors
#if defined(KERNEL) || defined(__APPLE_KEXT__)
    struct sk_buff *skbs[RTW89_RX_RING_DEFAULT_SIZE]{}; // Associated packet buffers
    uint64_t dma_addrs[RTW89_RX_RING_DEFAULT_SIZE]{};
#else
    std::vector<struct sk_buff *> skbs; // Associated packet buffers
    std::vector<uint64_t> dma_addrs;
#endif

    inline size_t count() const {
        return wp - rp;
    }

    inline bool is_empty() const {
        return wp == rp;
    }

    inline bool is_full() const {
        return (wp - rp) >= size;
    }
};

// ============================================================================
// API Declarations
// ============================================================================

uint32_t rtw89_pci_get_tx_idx_reg(uint8_t ch);
uint32_t rtw89_pci_get_rx_idx_reg(uint8_t ch);

// TX Ring Functions
int  rtw89_pci_init_tx_ring(Rtw89Mmio *mmio, rtw89_tx_ring *ring, uint8_t ch, size_t count = RTW89_TX_RING_DEFAULT_SIZE);
void rtw89_pci_free_tx_ring(rtw89_tx_ring *ring);
int  rtw89_pci_tx_enqueue(Rtw89Mmio *mmio, rtw89_tx_ring *ring, struct sk_buff *skb, uint64_t dma_addr, uint32_t len);
int  rtw89_pci_tx_reap(Rtw89Mmio *mmio, rtw89_tx_ring *ring, size_t max_reap = 64);

// RX Ring Functions
int  rtw89_pci_init_rx_ring(Rtw89Mmio *mmio, rtw89_rx_ring *ring, uint8_t ch, size_t count = RTW89_RX_RING_DEFAULT_SIZE, size_t buf_sz = RTW89_RX_BUF_SIZE);
void rtw89_pci_free_rx_ring(rtw89_rx_ring *ring);
struct sk_buff *rtw89_pci_rx_dequeue(Rtw89Mmio *mmio, rtw89_rx_ring *ring);
int  rtw89_pci_rx_refill(Rtw89Mmio *mmio, rtw89_rx_ring *ring);
