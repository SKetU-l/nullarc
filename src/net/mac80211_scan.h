//
// mac80211_scan.h — 802.11 Station Scanning & Beacon / Probe Parser
//
#pragma once

#include "linux_shim.h"
#include "mac80211_frame.h"

#if defined(KERNEL) || defined(__APPLE_KEXT__)
#include <stddef.h>
#include <stdint.h>
#else
#include <cstddef>
#include <cstdint>
#endif

// Cipher Suites (OUI 00:0F:AC)
#define WLAN_CIPHER_SUITE_NONE          0
#define WLAN_CIPHER_SUITE_WEP40         BIT(0)
#define WLAN_CIPHER_SUITE_TKIP          BIT(1)
#define WLAN_CIPHER_SUITE_CCMP          BIT(2)
#define WLAN_CIPHER_SUITE_WEP104        BIT(3)
#define WLAN_CIPHER_SUITE_GCMP          BIT(4)

// AKM Suites (OUI 00:0F:AC)
#define WLAN_AKM_SUITE_8021X            BIT(0)
#define WLAN_AKM_SUITE_PSK              BIT(1)
#define WLAN_AKM_SUITE_FT_8021X         BIT(2)
#define WLAN_AKM_SUITE_FT_PSK           BIT(3)
#define WLAN_AKM_SUITE_SAE              BIT(4)

// 802.11 Capability Information bits
#define WLAN_CAPABILITY_ESS             0x0001
#define WLAN_CAPABILITY_IBSS            0x0002
#define WLAN_CAPABILITY_PRIVACY         0x0010
#define WLAN_CAPABILITY_SHORT_PREAMBLE  0x0020
#define WLAN_CAPABILITY_SHORT_SLOT_TIME 0x0400

// Information Element Tags
#define WLAN_EID_SSID                   0
#define WLAN_EID_SUPP_RATES             1
#define WLAN_EID_DS_PARAMS              3
#define WLAN_EID_RSN                    48
#define WLAN_EID_EXT_SUPP_RATES         50
#define WLAN_EID_VENDOR_SPECIFIC        221

// Discovered BSS Entry structure
struct bss_entry {
    uint8_t  bssid[6];
    char     ssid[33];                   // 32 chars + null terminator
    uint8_t  ssid_len;
    uint8_t  channel;
    int8_t   rssi;
    uint16_t beacon_interval;
    uint16_t capability;
    bool     privacy;
    bool     wpa2;
    uint32_t pairwise_ciphers;           // Bitmask of WLAN_CIPHER_SUITE_*
    uint32_t akm_suites;                 // Bitmask of WLAN_AKM_SUITE_*
    uint64_t last_seen_ms;
};

// Scan State Machine
enum class ScanState {
    IDLE = 0,
    SCANNING,
    COMPLETED,
    ABORTED
};

// Scan Configuration
struct ScanConfig {
    const uint8_t *channels;
    size_t         num_channels;
    uint32_t       dwell_time_ms;
    bool           passive;
};

class BssCache {
public:
    static constexpr size_t MAX_BSS_ENTRIES = 128;

    BssCache();
    ~BssCache() = default;

    // Parse incoming beacon or probe response frame and insert/update BSS cache
    int  update_from_frame(const uint8_t *frame, size_t len, int8_t rssi,
                           uint8_t rx_channel, uint64_t current_time_ms);

    // Lookups
    const bss_entry *find_by_bssid(const uint8_t *bssid) const;
    const bss_entry *find_by_ssid(const char *ssid) const;
    const bss_entry *get_entry_at(size_t index) const;

    // Cache management
    int    update_or_add(const bss_entry &entry);
    size_t get_entries(bss_entry *out_entries, size_t max_count) const;
    void   age_out(uint64_t current_time_ms, uint64_t max_age_ms);
    void   clear();
    size_t count() const { return m_count; }

private:
    bss_entry m_entries[MAX_BSS_ENTRIES];
    size_t    m_count;
};

class ScanEngine {
public:
    ScanEngine(BssCache &cache);
    ~ScanEngine() = default;

    int  start_scan(const ScanConfig &config);
    void step_channel(uint64_t current_time_ms, uint8_t &next_channel, bool &scan_done);
    void abort();

    ScanState state() const { return m_state; }
    uint8_t   current_channel() const;
    BssCache       &cache() { return m_cache; }
    const BssCache &cache() const { return m_cache; }

private:
    BssCache   &m_cache;
    ScanConfig  m_config;
    ScanState   m_state;
    size_t      m_current_channel_idx;
    uint64_t    m_channel_start_ms;
};

// IE Parser Helper
int ieee80211_parse_rsn_ie(const uint8_t *ie_data, size_t ie_len,
                           uint32_t &pairwise_ciphers, uint32_t &akm_suites);
