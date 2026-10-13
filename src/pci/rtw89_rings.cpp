//
// rtw89_rings.cpp — RTL8852BE PCIe TX/RX Descriptor Ring Management
//
#include "rtw89_rings.h"

#include <algorithm>
#include <cerrno>

uint32_t rtw89_pci_get_tx_idx_reg(uint8_t ch) {
    switch (ch) {
    case 0:  return R_AX_ACH0_TXBD_IDX;
    case 1:  return R_AX_ACH1_TXBD_IDX;
    case 2:  return R_AX_ACH2_TXBD_IDX;
    case 3:  return R_AX_ACH3_TXBD_IDX;
    case 4:  return R_AX_ACH4_TXBD_IDX;
    case 5:  return R_AX_ACH5_TXBD_IDX;
    case 6:  return R_AX_ACH6_TXBD_IDX;
    case 7:  return R_AX_ACH7_TXBD_IDX;
    case 8:  return R_AX_CH8_TXBD_IDX;
    case 9:  return R_AX_CH9_TXBD_IDX;
    case 10: return R_AX_CH10_TXBD_IDX;
    case 11: return R_AX_CH11_TXBD_IDX;
    case 12: return R_AX_CH12_TXBD_IDX;
    default: return R_AX_ACH0_TXBD_IDX;
    }
}

uint32_t rtw89_pci_get_rx_idx_reg(uint8_t ch) {
    switch (ch) {
    case 0:  return R_AX_RXBD_IDX_CH0;
    case 1:  return R_AX_RXBD_IDX_CH1;
    default: return R_AX_RXBD_IDX_CH0;
    }
}

static size_t round_up_pow2(size_t v) {
    size_t p = 1;
    while (p < v) {
        p <<= 1;
    }
    return p;
}

// ============================================================================
// TX Ring Management
// ============================================================================

int rtw89_pci_init_tx_ring(Rtw89Mmio *mmio, rtw89_tx_ring *ring, uint8_t ch, size_t count) {
    if (!ring || count < 4) {
        return -EINVAL;
    }

    count = round_up_pow2(count);

    ring->ch = ch;
    ring->size = count;
    ring->mask = count - 1;
    ring->wp = 0;
    ring->rp = 0;
    ring->dma_idx_reg = rtw89_pci_get_tx_idx_reg(ch);

    // Allocate descriptor ring DMA memory
    size_t ring_bytes = count * sizeof(rtw89_tx_desc);
    int ret = rtw89_dma_alloc(&ring->bd_ring, ring_bytes, RTW89_DMA_DEFAULT_MASK, 128);
    if (ret != 0) {
        return ret;
    }

    ring->skbs.assign(count, nullptr);
    ring->dma_addrs.assign(count, 0);

    // Initialize hardware ring pointer to 0
    if (mmio) {
        mmio->write32(ring->dma_idx_reg, 0);
    }

    return 0;
}

void rtw89_pci_free_tx_ring(rtw89_tx_ring *ring) {
    if (!ring) {
        return;
    }

    // Free any pending skbs
    for (size_t i = 0; i < ring->skbs.size(); ++i) {
        if (ring->skbs[i]) {
            dev_kfree_skb_any(ring->skbs[i]);
            ring->skbs[i] = nullptr;
        }
    }

    ring->skbs.clear();
    ring->dma_addrs.clear();

    rtw89_dma_free(&ring->bd_ring);
    ring->wp = 0;
    ring->rp = 0;
    ring->size = 0;
    ring->mask = 0;
}

int rtw89_pci_tx_enqueue(Rtw89Mmio *mmio, rtw89_tx_ring *ring, struct sk_buff *skb, uint64_t dma_addr, uint32_t len) {
    if (!ring || !skb || len == 0) {
        return -EINVAL;
    }

    if (ring->is_full()) {
        return -ENOSPC;
    }

    if (!rtw89_dma_verify_32bit_mask(dma_addr)) {
        return -EFAULT;
    }

    uint32_t idx = ring->wp & ring->mask;
    rtw89_tx_desc *desc_base = static_cast<rtw89_tx_desc *>(ring->bd_ring.vaddr);
    rtw89_tx_desc *desc = &desc_base[idx];

    // Populate hardware descriptor
    desc->dma_addr_l = cpu_to_le32(static_cast<uint32_t>(dma_addr & 0xFFFFFFFFU));
    desc->dma_addr_h = 0;
    desc->len = cpu_to_le32(len);

    uint32_t flags = BIT(31); // Ownership flag: device owns descriptor
    if (idx == ring->mask) {
        flags |= BIT(30);     // Ring wrap-around bit
    }
    desc->flags = cpu_to_le32(flags);

    ring->skbs[idx] = skb;
    ring->dma_addrs[idx] = dma_addr;

    // Advance write pointer
    ring->wp++;

    rtw89_dma_sync(&ring->bd_ring, RTW89_DMA_TO_DEVICE);

    // Update hardware DMA index
    if (mmio) {
        mmio->write32(ring->dma_idx_reg, ring->wp & ring->mask);
    }

    return 0;
}

int rtw89_pci_tx_reap(Rtw89Mmio *mmio, rtw89_tx_ring *ring, size_t max_reap) {
    if (!ring || ring->is_empty()) {
        return 0;
    }

    size_t reaped = 0;
    rtw89_tx_desc *desc_base = static_cast<rtw89_tx_desc *>(ring->bd_ring.vaddr);

    while (ring->rp != ring->wp && reaped < max_reap) {
        uint32_t idx = ring->rp & ring->mask;
        rtw89_tx_desc *desc = &desc_base[idx];

        // In simulation / hardware completion:
        // Clear ownership bit in descriptor
        desc->flags &= cpu_to_le32(~static_cast<uint32_t>(BIT(31)));

        if (ring->skbs[idx]) {
            dev_kfree_skb_any(ring->skbs[idx]);
            ring->skbs[idx] = nullptr;
            ring->dma_addrs[idx] = 0;
        }

        ring->rp++;
        reaped++;
    }

    rtw89_dma_sync(&ring->bd_ring, RTW89_DMA_BIDIRECTIONAL);

    (void)mmio;
    return static_cast<int>(reaped);
}

// ============================================================================
// RX Ring Management
// ============================================================================

int rtw89_pci_init_rx_ring(Rtw89Mmio *mmio, rtw89_rx_ring *ring, uint8_t ch, size_t count, size_t buf_sz) {
    if (!ring || count < 4 || buf_sz == 0) {
        return -EINVAL;
    }

    count = round_up_pow2(count);

    ring->ch = ch;
    ring->size = count;
    ring->mask = count - 1;
    ring->wp = 0;
    ring->rp = 0;
    ring->buf_sz = buf_sz;
    ring->dma_idx_reg = rtw89_pci_get_rx_idx_reg(ch);

    size_t ring_bytes = count * sizeof(rtw89_rx_desc);
    int ret = rtw89_dma_alloc(&ring->bd_ring, ring_bytes, RTW89_DMA_DEFAULT_MASK, 128);
    if (ret != 0) {
        return ret;
    }

    ring->skbs.assign(count, nullptr);
    ring->dma_addrs.assign(count, 0);

    // Pre-populate descriptors with sk_buffs
    ret = rtw89_pci_rx_refill(mmio, ring);
    if (ret < 0) {
        rtw89_pci_free_rx_ring(ring);
        return ret;
    }

    return 0;
}

void rtw89_pci_free_rx_ring(rtw89_rx_ring *ring) {
    if (!ring) {
        return;
    }

    for (size_t i = 0; i < ring->skbs.size(); ++i) {
        if (ring->skbs[i]) {
            dev_kfree_skb_any(ring->skbs[i]);
            ring->skbs[i] = nullptr;
        }
    }

    ring->skbs.clear();
    ring->dma_addrs.clear();

    rtw89_dma_free(&ring->bd_ring);
    ring->wp = 0;
    ring->rp = 0;
    ring->size = 0;
    ring->mask = 0;
}

int rtw89_pci_rx_refill(Rtw89Mmio *mmio, rtw89_rx_ring *ring) {
    if (!ring || !ring->bd_ring.vaddr) {
        return -EINVAL;
    }

    int refilled = 0;
    rtw89_rx_desc *desc_base = static_cast<rtw89_rx_desc *>(ring->bd_ring.vaddr);

    while (ring->count() < ring->size) {
        uint32_t idx = ring->wp & ring->mask;
        struct sk_buff *skb = dev_alloc_skb(ring->buf_sz);
        if (!skb) {
            return refilled > 0 ? refilled : -ENOMEM;
        }

        uintptr_t v_int = reinterpret_cast<uintptr_t>(skb->data);
        uint64_t paddr = 0x30000000ULL | (v_int & 0x0FFFFFFFULL);

        rtw89_rx_desc *desc = &desc_base[idx];
        desc->dma_addr_l = cpu_to_le32(static_cast<uint32_t>(paddr & 0xFFFFFFFFU));
        desc->dma_addr_h = 0;
        desc->len = cpu_to_le32(static_cast<uint32_t>(ring->buf_sz));

        uint32_t flags = BIT(31); // Device owns buffer
        if (idx == ring->mask) {
            flags |= BIT(30);     // Wrap bit
        }
        desc->flags = cpu_to_le32(flags);

        ring->skbs[idx] = skb;
        ring->dma_addrs[idx] = paddr;
        ring->wp++;
        refilled++;
    }

    rtw89_dma_sync(&ring->bd_ring, RTW89_DMA_FROM_DEVICE);

    if (mmio && ring->wp > 0) {
        mmio->write32(ring->dma_idx_reg, (ring->wp - 1) & ring->mask);
    }

    return refilled;
}

struct sk_buff *rtw89_pci_rx_dequeue(Rtw89Mmio *mmio, rtw89_rx_ring *ring) {
    if (!ring || ring->is_empty()) {
        return nullptr;
    }

    uint32_t idx = ring->rp & ring->mask;
    struct sk_buff *skb = ring->skbs[idx];
    if (!skb) {
        return nullptr;
    }

    ring->skbs[idx] = nullptr;
    ring->dma_addrs[idx] = 0;

    rtw89_rx_desc *desc_base = static_cast<rtw89_rx_desc *>(ring->bd_ring.vaddr);
    desc_base[idx].flags &= cpu_to_le32(~static_cast<uint32_t>(BIT(31))); // Host now owns buffer

    ring->rp++;

    (void)mmio;
    return skb;
}
