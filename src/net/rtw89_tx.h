//
// rtw89_tx.h — RTL8852BE Packet Transmit & Scatter-Gather Engine
// Translates BSD mbuf_t chains into DMA descriptor scatter-gather rings
//
#pragma once

#include "linux_shim.h"
#include "skbuff.h"
#include "rtw89_dma.h"
#include "rtw89_rings.h"
#include "rtw8852be_reg.h"
#include "rtw8852be_mmio.h"

#if defined(KERNEL) || defined(__APPLE_KEXT__)
#include <stddef.h>
#include <stdint.h>
#else
#include <cstddef>
#include <cstdint>
#endif

#define RTW89_MAX_MTU                   2304
#define RTW89_MAX_FRAGS                 8
#define RTW89_ETHER_HDR_LEN             14

// Freestanding BSD mbuf representation for unit testing and freestanding compilation
#if defined(KERNEL) && defined(__APPLE__)
#include <sys/mbuf.h>
typedef mbuf_t rtw89_mbuf_t;
#else
struct rtw89_mbuf {
    uint8_t *m_data{nullptr};
    size_t   m_len{0};
    rtw89_mbuf *m_next{nullptr};
    uint32_t m_flags{0};
};
typedef rtw89_mbuf* rtw89_mbuf_t;
#endif

// TX Packet Information Structure
struct rtw89_tx_pkt_info {
    uint16_t ether_type{0};
    uint8_t  priority{0};
    uint8_t  ch{0};
    bool     is_eapol{false};
    bool     is_multicast{false};
    size_t   total_len{0};
    size_t   num_frags{0};
};

// ============================================================================
// API Declarations
// ============================================================================

// Inspect and validate mbuf chain
int  rtw89_tx_parse_mbuf(rtw89_mbuf_t m, rtw89_tx_pkt_info *info);

// Flatten / coalesce scattered mbuf fragments into sk_buff
int  rtw89_tx_linearize_mbuf(rtw89_mbuf_t m, struct sk_buff **out_skb);

// Main transmit entry point for IOEthernetController::outputPacket
int  rtw89_tx_xmit_mbuf(Rtw89Mmio *mmio, rtw89_tx_ring *ring, rtw89_mbuf_t m, uint8_t ch = 0);
