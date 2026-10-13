//
// rtw8852be_phy.cpp — Realtek RTL8852BE PHY & RF Subsystem Implementation
// Strictly integer fixed-point arithmetic (zero floating point, zero SSE/AVX)
//
#include "rtw8852be_phy.h"

#if !defined(KERNEL) && !defined(__APPLE_KEXT__)
#include <algorithm>
#include <cerrno>
#endif

// ============================================================================
// 1. Channel Frequency Conversion (Strictly Integer Math)
// ============================================================================

bool rtw89_is_2g_channel(uint8_t channel) {
    return channel >= RTW89_2G_CH_MIN && channel <= RTW89_2G_CH_MAX;
}

bool rtw89_is_5g_channel(uint8_t channel) {
    if (channel >= 36 && channel <= 64 && (channel % 4 == 0)) return true;
    if (channel >= 100 && channel <= 144 && (channel % 4 == 0)) return true;
    if (channel >= 149 && channel <= 165 && (channel % 4 == 1)) return true;
    return false;
}

bool rtw89_is_valid_channel(uint8_t channel) {
    return rtw89_is_2g_channel(channel) || rtw89_is_5g_channel(channel);
}

uint8_t rtw89_get_channel_band(uint8_t channel) {
    if (rtw89_is_2g_channel(channel)) {
        return RTW89_BAND_2G;
    }
    if (rtw89_is_5g_channel(channel)) {
        return RTW89_BAND_5G;
    }
    return RTW89_BAND_INVALID;
}

uint32_t rtw89_channel_to_freq_mhz(uint8_t channel) {
    if (channel >= 1 && channel <= 13) {
        return 2407 + static_cast<uint32_t>(channel) * 5;
    }
    if (channel == 14) {
        return 2484;
    }
    if (rtw89_is_5g_channel(channel)) {
        return 5000 + static_cast<uint32_t>(channel) * 5;
    }
    return 0;
}

uint8_t rtw89_freq_to_channel(uint32_t freq_mhz) {
    if (freq_mhz >= 2412 && freq_mhz <= 2472) {
        uint32_t diff = freq_mhz - 2407;
        if (diff % 5 == 0) {
            return static_cast<uint8_t>(diff / 5);
        }
    }
    if (freq_mhz == 2484) {
        return 14;
    }
    if (freq_mhz >= 5180 && freq_mhz <= 5825) {
        uint32_t diff = freq_mhz - 5000;
        if (diff % 5 == 0) {
            uint8_t ch = static_cast<uint8_t>(diff / 5);
            if (rtw89_is_5g_channel(ch)) {
                return ch;
            }
        }
    }
    return 0;
}

// ============================================================================
// 2. Fixed-Point Power Conversion (Strictly Integer Lookup Table)
// ============================================================================

// Precalculated mW table for dBm values 0 through 23
// 10^(dBm / 10) in milliwatts
static const uint32_t s_dbm_to_mw_table[24] = {
    1,   //  0 dBm = 1.00 mW
    1,   //  1 dBm = 1.25 mW
    1,   //  2 dBm = 1.58 mW
    2,   //  3 dBm = 1.99 mW
    2,   //  4 dBm = 2.51 mW
    3,   //  5 dBm = 3.16 mW
    4,   //  6 dBm = 3.98 mW
    5,   //  7 dBm = 5.01 mW
    6,   //  8 dBm = 6.30 mW
    8,   //  9 dBm = 7.94 mW
    10,  // 10 dBm = 10.00 mW
    12,  // 11 dBm = 12.58 mW
    15,  // 12 dBm = 15.84 mW
    20,  // 13 dBm = 19.95 mW
    25,  // 14 dBm = 25.11 mW
    31,  // 15 dBm = 31.62 mW
    40,  // 16 dBm = 39.81 mW
    50,  // 17 dBm = 50.11 mW
    63,  // 18 dBm = 63.09 mW (Target regulatory clamp)
    79,  // 19 dBm = 79.43 mW
    100, // 20 dBm = 100.00 mW
    125, // 21 dBm = 125.89 mW
    158, // 22 dBm = 158.48 mW
    200  // 23 dBm = 199.52 mW
};

uint32_t rtw89_dbm_to_mw_fixed(int8_t dbm) {
    if (dbm <= 0) {
        return 1;
    }
    if (dbm >= 23) {
        return s_dbm_to_mw_table[23];
    }
    return s_dbm_to_mw_table[dbm];
}

uint32_t rtw89_half_dbm_to_mw_fixed(int16_t half_dbm) {
    int8_t dbm = static_cast<int8_t>(half_dbm / 2);
    return rtw89_dbm_to_mw_fixed(dbm);
}

// ============================================================================
// 3. Regulatory Power Tables
// ============================================================================

static rtw89_regulatory_table s_default_regulatory_table;
static bool s_regulatory_table_initialized = false;

static void init_default_regulatory_table() {
    if (s_regulatory_table_initialized) {
        return;
    }

    size_t idx = 0;

    // 2.4 GHz channels 1..14 (FCC/ETSI default 20 dBm raw, clamped to +18 dBm)
    for (uint8_t ch = 1; ch <= 14 && idx < rtw89_regulatory_table::MAX_CHANNELS; ++ch) {
        rtw89_power_entry &e = s_default_regulatory_table.entries[idx++];
        e.channel = ch;
        e.band = RTW89_BAND_2G;
        e.freq_mhz = static_cast<uint16_t>(rtw89_channel_to_freq_mhz(ch));
        e.raw_limit_dbm = 20;
        e.clamped_power_dbm = RTW89_REGULATORY_POWER_CLAMP_DBM; // 18
        e.power_half_dbm = RTW89_REGULATORY_POWER_CLAMP_HALF_DBM; // 36
        e.power_quarter_dbm = RTW89_REGULATORY_POWER_CLAMP_QUARTER_DBM; // 72
        e.power_mw = RTW89_REGULATORY_POWER_CLAMP_MW; // 63
    }

    // 5 GHz UNII-1 (36..48)
    const uint8_t unii1[] = { 36, 40, 44, 48 };
    for (uint8_t ch : unii1) {
        if (idx >= rtw89_regulatory_table::MAX_CHANNELS) break;
        rtw89_power_entry &e = s_default_regulatory_table.entries[idx++];
        e.channel = ch;
        e.band = RTW89_BAND_5G;
        e.freq_mhz = static_cast<uint16_t>(rtw89_channel_to_freq_mhz(ch));
        e.raw_limit_dbm = 23;
        e.clamped_power_dbm = RTW89_REGULATORY_POWER_CLAMP_DBM;
        e.power_half_dbm = RTW89_REGULATORY_POWER_CLAMP_HALF_DBM;
        e.power_quarter_dbm = RTW89_REGULATORY_POWER_CLAMP_QUARTER_DBM;
        e.power_mw = RTW89_REGULATORY_POWER_CLAMP_MW;
    }

    // 5 GHz UNII-2A (52..64)
    const uint8_t unii2a[] = { 52, 56, 60, 64 };
    for (uint8_t ch : unii2a) {
        if (idx >= rtw89_regulatory_table::MAX_CHANNELS) break;
        rtw89_power_entry &e = s_default_regulatory_table.entries[idx++];
        e.channel = ch;
        e.band = RTW89_BAND_5G;
        e.freq_mhz = static_cast<uint16_t>(rtw89_channel_to_freq_mhz(ch));
        e.raw_limit_dbm = 23;
        e.clamped_power_dbm = RTW89_REGULATORY_POWER_CLAMP_DBM;
        e.power_half_dbm = RTW89_REGULATORY_POWER_CLAMP_HALF_DBM;
        e.power_quarter_dbm = RTW89_REGULATORY_POWER_CLAMP_QUARTER_DBM;
        e.power_mw = RTW89_REGULATORY_POWER_CLAMP_MW;
    }

    // 5 GHz UNII-2C (100..144)
    const uint8_t unii2c[] = { 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144 };
    for (uint8_t ch : unii2c) {
        if (idx >= rtw89_regulatory_table::MAX_CHANNELS) break;
        rtw89_power_entry &e = s_default_regulatory_table.entries[idx++];
        e.channel = ch;
        e.band = RTW89_BAND_5G;
        e.freq_mhz = static_cast<uint16_t>(rtw89_channel_to_freq_mhz(ch));
        e.raw_limit_dbm = 23;
        e.clamped_power_dbm = RTW89_REGULATORY_POWER_CLAMP_DBM;
        e.power_half_dbm = RTW89_REGULATORY_POWER_CLAMP_HALF_DBM;
        e.power_quarter_dbm = RTW89_REGULATORY_POWER_CLAMP_QUARTER_DBM;
        e.power_mw = RTW89_REGULATORY_POWER_CLAMP_MW;
    }

    // 5 GHz UNII-3 (149..165)
    const uint8_t unii3[] = { 149, 153, 157, 161, 165 };
    for (uint8_t ch : unii3) {
        if (idx >= rtw89_regulatory_table::MAX_CHANNELS) break;
        rtw89_power_entry &e = s_default_regulatory_table.entries[idx++];
        e.channel = ch;
        e.band = RTW89_BAND_5G;
        e.freq_mhz = static_cast<uint16_t>(rtw89_channel_to_freq_mhz(ch));
        e.raw_limit_dbm = 23;
        e.clamped_power_dbm = RTW89_REGULATORY_POWER_CLAMP_DBM;
        e.power_half_dbm = RTW89_REGULATORY_POWER_CLAMP_HALF_DBM;
        e.power_quarter_dbm = RTW89_REGULATORY_POWER_CLAMP_QUARTER_DBM;
        e.power_mw = RTW89_REGULATORY_POWER_CLAMP_MW;
    }

    s_default_regulatory_table.count = idx;
    s_regulatory_table_initialized = true;
}

const rtw89_regulatory_table& rtw89_get_default_regulatory_table() {
    if (!s_regulatory_table_initialized) {
        init_default_regulatory_table();
    }
    return s_default_regulatory_table;
}

const rtw89_power_entry* rtw89_get_power_entry(uint8_t channel) {
    const auto &table = rtw89_get_default_regulatory_table();
    for (size_t i = 0; i < table.count; ++i) {
        if (table.entries[i].channel == channel) {
            return &table.entries[i];
        }
    }
    return nullptr;
}

// ============================================================================
// 4. Power Clamping Functions
// ============================================================================

int8_t rtw89_phy_clamp_power(int8_t power_dbm, int8_t max_limit_dbm) {
    return (power_dbm < max_limit_dbm) ? power_dbm : max_limit_dbm;
}

int16_t rtw89_phy_clamp_power_half_dbm(int16_t power_half_dbm, int16_t max_limit) {
    return (power_half_dbm < max_limit) ? power_half_dbm : max_limit;
}

int16_t rtw89_phy_clamp_power_quarter_dbm(int16_t power_quarter_dbm, int16_t max_limit) {
    return (power_quarter_dbm < max_limit) ? power_quarter_dbm : max_limit;
}

void rtw89_phy_clamp_power(rtw89_regulatory_table &table, int8_t max_limit_dbm) {
    for (size_t i = 0; i < table.count; ++i) {
        int8_t raw = table.entries[i].raw_limit_dbm;
        table.entries[i].clamped_power_dbm = (raw < max_limit_dbm) ? raw : max_limit_dbm;
        table.entries[i].power_half_dbm = static_cast<int16_t>(table.entries[i].clamped_power_dbm) * 2;
        table.entries[i].power_quarter_dbm = static_cast<int16_t>(table.entries[i].clamped_power_dbm) * 4;
        table.entries[i].power_mw = rtw89_dbm_to_mw_fixed(table.entries[i].clamped_power_dbm);
    }
}

int rtw89_phy_clamp_power(Rtw89Mmio &mmio, uint8_t channel, int8_t target_dbm, int8_t max_limit_dbm) {
    if (!rtw89_is_valid_channel(channel)) {
        return -EINVAL;
    }

    int8_t clamped = rtw89_phy_clamp_power(target_dbm, max_limit_dbm);
    uint32_t val = static_cast<uint8_t>(clamped & 0xFF);

    // Write to hardware TX power control and per-path limits
    mmio.write32_mask(R_AX_TXPWR_CTRL, B_AX_TXPWR_VAL_MASK, val);
    mmio.write32_mask(R_P0_TXPWR_LIMIT, B_P0_TXPWR_LIMIT_MASK, val);
    mmio.write32_mask(R_P1_TXPWR_LIMIT, B_P1_TXPWR_LIMIT_MASK, val);

    Rtw89Mmio::mb();
    return 0;
}

int rtw89_phy_clamp_power(Rtw89Mmio &mmio, int8_t target_dbm) {
    return rtw89_phy_clamp_power(mmio, 1, target_dbm, RTW89_REGULATORY_POWER_CLAMP_DBM);
}

// ============================================================================
// 5. Baseband & RF Hardware Configuration Sequences
// ============================================================================

int rtw89_phy_init(Rtw89Mmio &mmio, rtw89_phy_state &state) {
    // 1. Enable MAC clock and baseband clocks
    mmio.write32_set(R_AX_SYS_CLK_CTRL, B_AX_CPU_CLK_EN | B_AX_MAC_CLK_EN);
    mmio.write32_set(R_AX_AFE_CTRL, B_AX_AFE_EN);

    // 2. Configure default RF mode (0x333 for 20MHz on both Path A and Path B)
    mmio.write32_mask(R_P0_RFMODE_ORI_RX, B_P0_RFMODE_ORI_RX_ALL, 0x333U);
    mmio.write32_mask(R_P1_RFMODE_ORI_RX, B_P1_RFMODE_ORI_RX_ALL, 0x333U);

    // 3. Set default channel: Channel 1 (2.4 GHz, 20 MHz bandwidth)
    int ret = rtw89_phy_set_channel(mmio, state, 1, RTW89_CHANNEL_WIDTH_20, RTW89_SC_DONT_CARE);
    if (ret != 0) {
        return ret;
    }

    // 4. Apply default power clamp (+18 dBm)
    ret = rtw89_phy_clamp_power(mmio, 1, RTW89_REGULATORY_POWER_CLAMP_DBM, RTW89_REGULATORY_POWER_CLAMP_DBM);
    if (ret != 0) {
        return ret;
    }

    state.initialized = true;
    Rtw89Mmio::mb();
    return 0;
}

int rtw89_phy_init(Rtw89Mmio &mmio) {
    rtw89_phy_state dummy{};
    return rtw89_phy_init(mmio, dummy);
}

int rtw89_phy_set_channel(Rtw89Mmio &mmio, rtw89_phy_state &state,
                          uint8_t channel, uint8_t bandwidth, uint8_t primary_offset) {
    if (!rtw89_is_valid_channel(channel)) {
        return -EINVAL;
    }

    if (bandwidth > RTW89_CHANNEL_WIDTH_80) {
        return -EOPNOTSUPP; // RTL8852BE max supported bandwidth in basic mode is 80MHz
    }

    uint8_t band = rtw89_get_channel_band(channel);
    if (band == RTW89_BAND_INVALID) {
        return -EINVAL;
    }

    // 80 MHz is only supported in 5 GHz band
    if (band == RTW89_BAND_2G && bandwidth == RTW89_CHANNEL_WIDTH_80) {
        return -EINVAL;
    }

    // 1. Configure Baseband Path Band Selection
    if (band == RTW89_BAND_5G) {
        mmio.write32_set(R_PATH0_BAND_SEL_V1, B_PATH0_BAND_SEL_MSK_V1);
        mmio.write32_set(R_PATH1_BAND_SEL_V1, B_PATH1_BAND_SEL_MSK_V1);
    } else {
        mmio.write32_clr(R_PATH0_BAND_SEL_V1, B_PATH0_BAND_SEL_MSK_V1);
        mmio.write32_clr(R_PATH1_BAND_SEL_V1, B_PATH1_BAND_SEL_MSK_V1);
    }

    // 2. Configure Bandwidth in Baseband (R_FC0_BW_V1)
    uint32_t bw_set = 0;
    if (bandwidth == RTW89_CHANNEL_WIDTH_40) {
        bw_set = 1;
    } else if (bandwidth == RTW89_CHANNEL_WIDTH_80) {
        bw_set = 2;
    }
    mmio.write32_mask(R_FC0_BW_V1, B_FC0_BW_SET, bw_set);

    // 3. Configure Subband & Primary Channel Offset (R_CHBW_MOD_V1)
    uint32_t sbw_val = 0;
    if (bandwidth == RTW89_CHANNEL_WIDTH_40) {
        sbw_val = (primary_offset == RTW89_SC_20_LOWER) ? 1 : 2;
    } else if (bandwidth == RTW89_CHANNEL_WIDTH_80) {
        sbw_val = 3;
    }
    mmio.write32_mask(R_CHBW_MOD_V1, B_CHBW_MOD_SBW, sbw_val);
    mmio.write32_mask(R_CHBW_MOD_V1, B_CHBW_MOD_PRICH, static_cast<uint32_t>(channel & 0x0F));

    // 4. Configure RF Synthesizer Direct Access (Path A & Path B)
    // Channel tuning register RR_CFGCH
    uint32_t rf_bw = CFGCH_BW_20M;
    if (bandwidth == RTW89_CHANNEL_WIDTH_40) {
        rf_bw = CFGCH_BW_40M;
    } else if (bandwidth == RTW89_CHANNEL_WIDTH_80) {
        rf_bw = CFGCH_BW_80M;
    }

    uint32_t rf_band = (band == RTW89_BAND_5G) ? CFGCH_BAND0_5G : CFGCH_BAND0_2G;
    uint32_t cfgch = FIELD_PREP(RR_CFGCH_BW, rf_bw) |
                     FIELD_PREP(RR_CFGCH_BAND0, rf_band) |
                     FIELD_PREP(RR_CFGCH_CH, channel);

    mmio.write32(RTW89_RF_PATH_A_BASE + RR_CFGCH, cfgch);
    mmio.write32(RTW89_RF_PATH_B_BASE + RR_CFGCH, cfgch);

    // 5. Update state
    state.current_channel = channel;
    state.current_band = band;
    state.current_bandwidth = bandwidth;
    state.current_offset = primary_offset;
    state.current_freq_mhz = rtw89_channel_to_freq_mhz(channel);
    state.current_power_dbm = RTW89_REGULATORY_POWER_CLAMP_DBM;
    state.current_power_half_dbm = RTW89_REGULATORY_POWER_CLAMP_HALF_DBM;
    state.current_power_mw = RTW89_REGULATORY_POWER_CLAMP_MW;

    Rtw89Mmio::mb();
    return 0;
}

int rtw89_phy_set_channel(Rtw89Mmio &mmio, uint8_t channel, uint8_t bandwidth, uint8_t primary_offset) {
    rtw89_phy_state dummy{};
    return rtw89_phy_set_channel(mmio, dummy, channel, bandwidth, primary_offset);
}

int rtw89_phy_set_channel(Rtw89Mmio &mmio, const rtw89_channel_params &params) {
    rtw89_phy_state dummy{};
    return rtw89_phy_set_channel(mmio, dummy, params.channel, params.band_width, params.primary_ch_offset);
}
