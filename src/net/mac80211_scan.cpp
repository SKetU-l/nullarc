//
// mac80211_scan.cpp — 802.11 Station Scanning & Beacon / Probe Parser Implementation
//
#include "mac80211_scan.h"

#if defined(KERNEL) || defined(__APPLE_KEXT__)
#include <sys/errno.h>
#include <string.h>
#else
#include <cerrno>
#include <cstring>
#endif

// OUI prefixes for 802.11i / WPA2
static const uint8_t WPA2_OUI[3] = {0x00, 0x0F, 0xAC};

int ieee80211_parse_rsn_ie(const uint8_t *ie_data, size_t ie_len,
                           uint32_t &pairwise_ciphers, uint32_t &akm_suites) {
    pairwise_ciphers = WLAN_CIPHER_SUITE_NONE;
    akm_suites = 0;

    if (!ie_data || ie_len < 8) {
        return -EINVAL;
    }

    size_t off = 0;

    // 1. Version (2 bytes, LE, should be 1)
    uint16_t version = static_cast<uint16_t>(ie_data[off]) |
                       (static_cast<uint16_t>(ie_data[off + 1]) << 8);
    off += 2;
    if (version != 1) {
        return -EINVAL;
    }

    // 2. Group Data Cipher Suite (4 bytes: 3 OUI + 1 Suite Type)
    if (off + 4 > ie_len) return -EINVAL;
    // We skip group cipher detail for now or record it if needed
    off += 4;

    // 3. Pairwise Cipher Suite Count (2 bytes, LE)
    if (off + 2 > ie_len) return -EINVAL;
    uint16_t pairwise_count = static_cast<uint16_t>(ie_data[off]) |
                              (static_cast<uint16_t>(ie_data[off + 1]) << 8);
    off += 2;

    if (off + pairwise_count * 4 > ie_len) return -EINVAL;
    for (uint16_t i = 0; i < pairwise_count; i++) {
        if (memcmp(&ie_data[off], WPA2_OUI, 3) == 0) {
            uint8_t suite_type = ie_data[off + 3];
            switch (suite_type) {
                case 2: pairwise_ciphers |= WLAN_CIPHER_SUITE_TKIP; break;
                case 4: pairwise_ciphers |= WLAN_CIPHER_SUITE_CCMP; break;
                case 8: pairwise_ciphers |= WLAN_CIPHER_SUITE_GCMP; break;
                default: break;
            }
        }
        off += 4;
    }

    // 4. AKM Suite Count (2 bytes, LE)
    if (off + 2 > ie_len) {
        // AKM list optional in truncated/minimal RSN
        return 0;
    }
    uint16_t akm_count = static_cast<uint16_t>(ie_data[off]) |
                         (static_cast<uint16_t>(ie_data[off + 1]) << 8);
    off += 2;

    if (off + akm_count * 4 > ie_len) return -EINVAL;
    for (uint16_t i = 0; i < akm_count; i++) {
        if (memcmp(&ie_data[off], WPA2_OUI, 3) == 0) {
            uint8_t akm_type = ie_data[off + 3];
            switch (akm_type) {
                case 1: akm_suites |= WLAN_AKM_SUITE_8021X; break;
                case 2: akm_suites |= WLAN_AKM_SUITE_PSK; break;
                case 3: akm_suites |= WLAN_AKM_SUITE_FT_8021X; break;
                case 4: akm_suites |= WLAN_AKM_SUITE_FT_PSK; break;
                case 8: akm_suites |= WLAN_AKM_SUITE_SAE; break;
                default: break;
            }
        }
        off += 4;
    }

    return 0;
}

BssCache::BssCache() : m_count(0) {
    memset(m_entries, 0, sizeof(m_entries));
}

int BssCache::update_from_frame(const uint8_t *frame, size_t len, int8_t rssi,
                               uint8_t rx_channel, uint64_t current_time_ms) {
    if (!frame || len < IEEE80211_HDR3_LEN + 12) {
        return -EINVAL;
    }

    // Verify 802.11 Management Frame
    uint16_t fc = static_cast<uint16_t>(frame[0]) | (static_cast<uint16_t>(frame[1]) << 8);
    if (ieee80211_get_fc_type(fc) != IEEE80211_FTYPE_MGMT) {
        return -EINVAL;
    }

    uint8_t subtype = ieee80211_get_fc_subtype(fc);
    if (subtype != IEEE80211_STYPE_BEACON && subtype != IEEE80211_STYPE_PROBE_RESP) {
        return -EINVAL;
    }

    const ieee80211_hdr *hdr = reinterpret_cast<const ieee80211_hdr *>(frame);
    const uint8_t *bssid = hdr->addr3; // BSSID is in addr3 for Beacons/Probes

    // Parse Fixed Management Parameters
    size_t off = IEEE80211_HDR3_LEN;
    // timestamp: 8 bytes
    off += 8;
    uint16_t beacon_int = static_cast<uint16_t>(frame[off]) | (static_cast<uint16_t>(frame[off + 1]) << 8);
    off += 2;
    uint16_t capability = static_cast<uint16_t>(frame[off]) | (static_cast<uint16_t>(frame[off + 1]) << 8);
    off += 2;

    char ssid[33] = {0};
    uint8_t ssid_len = 0;
    uint8_t channel = rx_channel;
    bool wpa2 = false;
    uint32_t pairwise_ciphers = 0;
    uint32_t akm_suites = 0;

    // Parse Variable Information Elements (IEs)
    while (off + 2 <= len) {
        uint8_t eid = frame[off];
        uint8_t elen = frame[off + 1];
        off += 2;

        if (off + elen > len) {
            break; // Truncated IE
        }

        const uint8_t *edata = &frame[off];

        switch (eid) {
            case WLAN_EID_SSID:
                ssid_len = (elen > 32) ? 32 : elen;
                memcpy(ssid, edata, ssid_len);
                ssid[ssid_len] = '\0';
                break;

            case WLAN_EID_DS_PARAMS:
                if (elen >= 1) {
                    channel = edata[0];
                }
                break;

            case WLAN_EID_RSN:
                wpa2 = true;
                ieee80211_parse_rsn_ie(edata, elen, pairwise_ciphers, akm_suites);
                break;

            default:
                break;
        }

        off += elen;
    }

    // Find existing entry in cache
    for (size_t i = 0; i < m_count; i++) {
        if (memcmp(m_entries[i].bssid, bssid, 6) == 0) {
            // Update existing entry
            m_entries[i].rssi = rssi;
            m_entries[i].channel = channel;
            m_entries[i].beacon_interval = beacon_int;
            m_entries[i].capability = capability;
            m_entries[i].privacy = (capability & WLAN_CAPABILITY_PRIVACY) != 0;
            m_entries[i].wpa2 = wpa2;
            m_entries[i].pairwise_ciphers = pairwise_ciphers;
            m_entries[i].akm_suites = akm_suites;
            m_entries[i].last_seen_ms = current_time_ms;
            if (ssid_len > 0) {
                memcpy(m_entries[i].ssid, ssid, ssid_len + 1);
                m_entries[i].ssid_len = ssid_len;
            }
            return 0;
        }
    }

    // New entry
    size_t target_idx = 0;
    if (m_count < MAX_BSS_ENTRIES) {
        target_idx = m_count++;
    } else {
        // Cache full: find oldest entry by last_seen_ms
        uint64_t oldest_time = m_entries[0].last_seen_ms;
        target_idx = 0;
        for (size_t i = 1; i < MAX_BSS_ENTRIES; i++) {
            if (m_entries[i].last_seen_ms < oldest_time) {
                oldest_time = m_entries[i].last_seen_ms;
                target_idx = i;
            }
        }
    }

    bss_entry &e = m_entries[target_idx];
    memcpy(e.bssid, bssid, 6);
    memcpy(e.ssid, ssid, ssid_len + 1);
    e.ssid_len = ssid_len;
    e.channel = channel;
    e.rssi = rssi;
    e.beacon_interval = beacon_int;
    e.capability = capability;
    e.privacy = (capability & WLAN_CAPABILITY_PRIVACY) != 0;
    e.wpa2 = wpa2;
    e.pairwise_ciphers = pairwise_ciphers;
    e.akm_suites = akm_suites;
    e.last_seen_ms = current_time_ms;

    return 0;
}

const bss_entry *BssCache::find_by_bssid(const uint8_t *bssid) const {
    if (!bssid) return nullptr;
    for (size_t i = 0; i < m_count; i++) {
        if (memcmp(m_entries[i].bssid, bssid, 6) == 0) {
            return &m_entries[i];
        }
    }
    return nullptr;
}

const bss_entry *BssCache::find_by_ssid(const char *ssid) const {
    if (!ssid) return nullptr;
    for (size_t i = 0; i < m_count; i++) {
        if (strcmp(m_entries[i].ssid, ssid) == 0) {
            return &m_entries[i];
        }
    }
    return nullptr;
}

const bss_entry *BssCache::get_entry_at(size_t index) const {
    if (index >= m_count) return nullptr;
    return &m_entries[index];
}

size_t BssCache::get_entries(bss_entry *out_entries, size_t max_count) const {
    if (!out_entries || max_count == 0) return 0;
    size_t to_copy = (m_count < max_count) ? m_count : max_count;
    memcpy(out_entries, m_entries, to_copy * sizeof(bss_entry));
    return to_copy;
}

void BssCache::age_out(uint64_t current_time_ms, uint64_t max_age_ms) {
    size_t write_idx = 0;
    for (size_t read_idx = 0; read_idx < m_count; read_idx++) {
        if (current_time_ms >= m_entries[read_idx].last_seen_ms &&
            (current_time_ms - m_entries[read_idx].last_seen_ms) <= max_age_ms) {
            if (write_idx != read_idx) {
                m_entries[write_idx] = m_entries[read_idx];
            }
            write_idx++;
        }
    }
    m_count = write_idx;
}

void BssCache::clear() {
    m_count = 0;
    memset(m_entries, 0, sizeof(m_entries));
}

ScanEngine::ScanEngine(BssCache &cache)
    : m_cache(cache),
      m_state(ScanState::IDLE),
      m_current_channel_idx(0),
      m_channel_start_ms(0) {
    memset(&m_config, 0, sizeof(m_config));
}

int ScanEngine::start_scan(const ScanConfig &config) {
    if (config.num_channels == 0 || !config.channels) {
        return -EINVAL;
    }
    m_config = config;
    m_state = ScanState::SCANNING;
    m_current_channel_idx = 0;
    m_channel_start_ms = 0;
    return 0;
}

void ScanEngine::step_channel(uint64_t current_time_ms, uint8_t &next_channel, bool &scan_done) {
    scan_done = false;

    if (m_state != ScanState::SCANNING) {
        scan_done = true;
        return;
    }

    if (m_channel_start_ms == 0) {
        // Start first channel
        m_channel_start_ms = current_time_ms;
        next_channel = m_config.channels[m_current_channel_idx];
        return;
    }

    if (current_time_ms - m_channel_start_ms >= m_config.dwell_time_ms) {
        m_current_channel_idx++;
        if (m_current_channel_idx >= m_config.num_channels) {
            m_state = ScanState::COMPLETED;
            scan_done = true;
            return;
        }
        m_channel_start_ms = current_time_ms;
        next_channel = m_config.channels[m_current_channel_idx];
    } else {
        next_channel = m_config.channels[m_current_channel_idx];
    }
}

void ScanEngine::abort() {
    m_state = ScanState::ABORTED;
}

uint8_t ScanEngine::current_channel() const {
    if (m_state != ScanState::SCANNING || m_current_channel_idx >= m_config.num_channels) {
        return 0;
    }
    return m_config.channels[m_current_channel_idx];
}
