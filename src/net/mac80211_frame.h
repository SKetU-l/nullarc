//
// mac80211_frame.h — 802.3 Ethernet <-> 802.11 WLAN Frame Translation Engine
//
#pragma once

#include "linux_shim.h"
#include "skbuff.h"

#if defined(KERNEL) || defined(__APPLE_KEXT__)
#include <stddef.h>
#include <stdint.h>
#else
#include <cstddef>
#include <cstdint>
#endif

// 802.11 Frame Control Type definitions
#define IEEE80211_FTYPE_MGMT            0x00
#define IEEE80211_FTYPE_CTL             0x01
#define IEEE80211_FTYPE_DATA            0x02

// 802.11 Management Subtypes
#define IEEE80211_STYPE_ASSOC_REQ       0x00
#define IEEE80211_STYPE_ASSOC_RESP      0x01
#define IEEE80211_STYPE_PROBE_REQ       0x04
#define IEEE80211_STYPE_PROBE_RESP      0x05
#define IEEE80211_STYPE_BEACON          0x08
#define IEEE80211_STYPE_DISASSOC        0x0A
#define IEEE80211_STYPE_AUTH            0x0B
#define IEEE80211_STYPE_DEAUTH          0x0C
#define IEEE80211_STYPE_ACTION          0x0D

// 802.11 Data Subtypes
#define IEEE80211_STYPE_DATA            0x00
#define IEEE80211_STYPE_DATA_CFACK      0x01
#define IEEE80211_STYPE_DATA_CFPOLL     0x02
#define IEEE80211_STYPE_DATA_CFACKPOLL  0x03
#define IEEE80211_STYPE_NULLFUNC        0x04
#define IEEE80211_STYPE_QOS_DATA        0x08

// Frame Control Flags
#define IEEE80211_FCTL_TO_DS            0x0100
#define IEEE80211_FCTL_FROM_DS          0x0200
#define IEEE80211_FCTL_MORE_FRAGS       0x0400
#define IEEE80211_FCTL_RETRY            0x0800
#define IEEE80211_FCTL_PWR_MGT          0x1000
#define IEEE80211_FCTL_MORE_DATA        0x2000
#define IEEE80211_FCTL_PROTECTED        0x4000
#define IEEE80211_FCTL_ORDER            0x8000

// Standard Header Lengths
#define IEEE80211_HDR3_LEN              24      // Standard 3-address data header
#define IEEE80211_HDR3_QOS_LEN          26      // 3-address header with QoS control
#define IEEE80211_LLC_SNAP_LEN          8       // LLC / SNAP RFC 1042 header
#define IEEE8023_HDR_LEN                14      // Destination (6) + Source (6) + EtherType (2)

// Standard 3-Address 802.11 MAC Header
struct ieee80211_hdr {
    __le16 frame_control;
    __le16 duration_id;
    uint8_t addr1[6];                   // RA / BSSID
    uint8_t addr2[6];                   // TA / SA
    uint8_t addr3[6];                   // DA / BSSID
    __le16 seq_ctrl;
} __attribute__((packed));

// Standard 3-Address 802.11 QoS MAC Header
struct ieee80211_qos_hdr {
    struct ieee80211_hdr base;
    __le16 qos_ctrl;
} __attribute__((packed));

// LLC / SNAP Header (RFC 1042)
struct ieee80211_llc_snap {
    uint8_t dsap;                       // 0xAA
    uint8_t ssap;                       // 0xAA
    uint8_t ctrl;                       // 0x03
    uint8_t oui[3];                     // 0x00, 0x00, 0x00
    __be16  proto_id;                   // EtherType in network byte order
} __attribute__((packed));

// 802.3 Ethernet Header
struct eth_hdr {
    uint8_t h_dest[6];
    uint8_t h_source[6];
    __be16  h_proto;
} __attribute__((packed));

// ============================================================================
// API Declarations
// ============================================================================

uint8_t  ieee80211_get_fc_type(uint16_t fc);
uint8_t  ieee80211_get_fc_subtype(uint16_t fc);
bool     ieee80211_is_data_frame(const uint8_t *data, size_t len);
bool     ieee80211_is_mgmt_frame(const uint8_t *data, size_t len);
bool     ieee80211_is_beacon_frame(const uint8_t *data, size_t len);

// Encapsulate standard 802.3 Ethernet packet into 802.11 Data frame (To-DS)
int  ieee80211_encap_8023(struct sk_buff *eth_skb, struct sk_buff **out_wlan_skb,
                          const uint8_t *bssid, const uint8_t *sta_mac,
                          bool qos = false, uint8_t tid = 0);

// Decapsulate incoming 802.11 Data frame (From-DS) into standard 802.3 Ethernet packet
int  ieee80211_decap_8023(struct sk_buff *wlan_skb, struct sk_buff **out_eth_skb);
