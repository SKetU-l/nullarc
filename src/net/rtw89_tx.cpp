//
// rtw89_tx.cpp — RTL8852BE Packet Transmit & Scatter-Gather Engine Implementation
//
#include "rtw89_tx.h"

#if defined(KERNEL) || defined(__APPLE_KEXT__)
#include <sys/errno.h>
#include <string.h>
#else
#include <cerrno>
#include <cstring>
#endif

int rtw89_tx_parse_mbuf(rtw89_mbuf_t m, rtw89_tx_pkt_info *info) {
    if (!m || !info) {
        return -EINVAL;
    }

    size_t total = 0;
    size_t frags = 0;
    uint8_t hdr_bytes[RTW89_ETHER_HDR_LEN] = {0};
    size_t copied_hdr = 0;

#if defined(KERNEL) && defined(__APPLE__)
    for (mbuf_t cur = m; cur != nullptr; cur = mbuf_next(cur)) {
        size_t len = mbuf_len(cur);
        uint8_t *data = static_cast<uint8_t *>(mbuf_data(cur));
        if (copied_hdr < RTW89_ETHER_HDR_LEN && len > 0) {
            size_t rem = RTW89_ETHER_HDR_LEN - copied_hdr;
            size_t to_copy = (len < rem) ? len : rem;
            memcpy(hdr_bytes + copied_hdr, data, to_copy);
            copied_hdr += to_copy;
        }
        total += len;
        frags++;
    }
#else
    for (rtw89_mbuf *cur = m; cur != nullptr; cur = cur->m_next) {
        size_t len = cur->m_len;
        uint8_t *data = cur->m_data;
        if (copied_hdr < RTW89_ETHER_HDR_LEN && len > 0 && data) {
            size_t rem = RTW89_ETHER_HDR_LEN - copied_hdr;
            size_t to_copy = (len < rem) ? len : rem;
            memcpy(hdr_bytes + copied_hdr, data, to_copy);
            copied_hdr += to_copy;
        }
        total += len;
        frags++;
    }
#endif

    if (total < RTW89_ETHER_HDR_LEN) {
        return -EINVAL;
    }

    if (total > RTW89_MAX_MTU) {
        return -EMSGSIZE;
    }

    if (frags > RTW89_MAX_FRAGS) {
        return -EMSGSIZE;
    }

    uint16_t ether_type = (static_cast<uint16_t>(hdr_bytes[12]) << 8) |
                           static_cast<uint16_t>(hdr_bytes[13]);
    bool is_mcast = (hdr_bytes[0] & 1) != 0;
    bool is_eapol = (ether_type == 0x888E);

    info->ether_type = ether_type;
    info->total_len = total;
    info->num_frags = frags;
    info->is_multicast = is_mcast;
    info->is_eapol = is_eapol;
    info->priority = is_eapol ? 7 : 0; // High priority for EAPOL
    info->ch = 0;

    return 0;
}

int rtw89_tx_linearize_mbuf(rtw89_mbuf_t m, struct sk_buff **out_skb) {
    if (!m || !out_skb) {
        return -EINVAL;
    }

    rtw89_tx_pkt_info info{};
    int ret = rtw89_tx_parse_mbuf(m, &info);
    if (ret != 0) {
        return ret;
    }

    struct sk_buff *skb = alloc_skb(info.total_len + 64, GFP_KERNEL);
    if (!skb) {
        return -ENOMEM;
    }
    skb_reserve(skb, 32);

#if defined(KERNEL) && defined(__APPLE__)
    for (mbuf_t cur = m; cur != nullptr; cur = mbuf_next(cur)) {
        size_t len = mbuf_len(cur);
        void *data = mbuf_data(cur);
        if (len > 0 && data) {
            skb_put_data(skb, data, len);
        }
    }
#else
    for (rtw89_mbuf *cur = m; cur != nullptr; cur = cur->m_next) {
        size_t len = cur->m_len;
        void *data = cur->m_data;
        if (len > 0 && data) {
            skb_put_data(skb, data, len);
        }
    }
#endif

    *out_skb = skb;
    return 0;
}

int rtw89_tx_xmit_mbuf(Rtw89Mmio *mmio, rtw89_tx_ring *ring, rtw89_mbuf_t m, uint8_t ch) {
    if (!ring || !m) {
        return -EINVAL;
    }

    if (ring->is_full()) {
        return -ENOSPC;
    }

    struct sk_buff *skb = nullptr;
    int ret = rtw89_tx_linearize_mbuf(m, &skb);
    if (ret != 0) {
        return ret;
    }

    // Determine simulated DMA physical address (< 4GB)
    uintptr_t v_int = reinterpret_cast<uintptr_t>(skb->data);
    uint64_t paddr = 0x20000000ULL | (v_int & 0x0FFFFFFFULL);

    ret = rtw89_pci_tx_enqueue(mmio, ring, skb, paddr, static_cast<uint32_t>(skb->len));
    if (ret != 0) {
        kfree_skb(skb);
        return ret;
    }

    (void)ch;
    return 0;
}
