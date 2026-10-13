//
// rtw8852be_phy.h — Realtek RTL8852BE PHY & RF Subsystem Definitions
// Frequency bands, 2.4/5GHz channel mapping, regulatory power clamping (+18 dBm),
// and Baseband/RF control sequences for freestanding C++17 driver.
//
#pragma once

#include "linux_shim.h"
#include "rtw8852be_reg.h"
#include "rtw8852be_mmio.h"

#include <cstddef>
#include <cstdint>

// ============================================================================
// 1. Wi-Fi Bandwidth & Subchannel Definitions
// ============================================================================

enum rtw89_bandwidth : uint8_t {
    RTW89_CHANNEL_WIDTH_20 = 0,
    RTW89_CHANNEL_WIDTH_40 = 1,
    RTW89_CHANNEL_WIDTH_80 = 2,
    RTW89_CHANNEL_WIDTH_160 = 3,
    RTW89_CHANNEL_WIDTH_320 = 4,
    RTW89_CHANNEL_WIDTH_INVALID = 0xFF,
};

enum rtw89_band : uint8_t {
    RTW89_BAND_2G = 0,
    RTW89_BAND_5G = 1,
    RTW89_BAND_6G = 2,
    RTW89_BAND_INVALID = 0xFF,
};

enum rtw89_sc_offset : uint8_t {
    RTW89_SC_DONT_CARE = 0,
    RTW89_SC_20_UPPER  = 1,
    RTW89_SC_20_LOWER  = 2,
    RTW89_SC_20_UPMOST = 3,
    RTW89_SC_20_LOWEST = 4,
    RTW89_SC_20_UP2X   = 5,
    RTW89_SC_20_LOW2X  = 6,
    RTW89_SC_20_UP3X   = 7,
    RTW89_SC_20_LOW3X  = 8,
    RTW89_SC_40_UPPER  = 9,
    RTW89_SC_40_LOWER  = 10,
};

enum rtw89_rf_path : uint8_t {
    RF_PATH_A = 0,
    RF_PATH_B = 1,
    RTW89_RF_PATH_MAX = 2,
};

// ============================================================================
// 2. Hardware Registers for Baseband and RF Control (RTL8852BE)
// ============================================================================

// Baseband Path Band Selection
#ifndef R_PATH0_BAND_SEL_V1
#define R_PATH0_BAND_SEL_V1             0x4738
#define B_PATH0_BAND_SEL_MSK_V1         BIT(17)
#define R_PATH0_BT_SHARE_V1             0x4738
#endif

#ifndef R_PATH1_BAND_SEL_V1
#define R_PATH1_BAND_SEL_V1             0x4AA4
#define B_PATH1_BAND_SEL_MSK_V1         BIT(17)
#define R_PATH1_BT_SHARE_V1             0x4AA4
#endif

// Baseband Channel & Bandwidth Registers
#ifndef R_FC0_BW_V1
#define R_FC0_BW_V1                     0x49C0
#define B_FC0_BW_SET                    GENMASK(31, 30)
#define B_FC0_BW_INV                    GENMASK(6, 0)
#endif

#ifndef R_CHBW_MOD_V1
#define R_CHBW_MOD_V1                   0x49C4
#define B_BT_SHARE                      BIT(14)
#define B_CHBW_MOD_SBW                  GENMASK(13, 12)
#define B_CHBW_MOD_PRICH                GENMASK(11, 8)
#endif

// Baseband RF Mode (RX filter modes: 0x333 for 20/40M, 0xAAA for 80M)
#ifndef R_P0_RFMODE_ORI_RX
#define R_P0_RFMODE_ORI_RX              0x12AC
#define B_P0_RFMODE_ORI_RX_ALL          GENMASK(23, 12)
#endif

#ifndef R_P1_RFMODE_ORI_RX
#define R_P1_RFMODE_ORI_RX              0x32AC
#define B_P1_RFMODE_ORI_RX_ALL          GENMASK(23, 12)
#endif

// Subchannel Selection & MAC Pin Mapping
#ifndef R_RXSC
#define R_RXSC                          0x237C
#define B_RXSC_EN                       BIT(0)
#endif

#ifndef R_MAC_PIN_SEL
#define R_MAC_PIN_SEL                   0x0734
#define B_CH_IDX_SEG0                   GENMASK(23, 16)
#endif

// System Clock and AFE Control Bits
#ifndef B_AX_MAC_CLK_EN
#define B_AX_MAC_CLK_EN                 BIT(15)
#endif
#ifndef B_AX_AFE_EN
#define B_AX_AFE_EN                     BIT(0)
#endif

// ADC & Packet Control
#ifndef R_ADC_FIFO
#define R_ADC_FIFO                      0x20FC
#define B_ADC_FIFO_RST                  GENMASK(31, 24)
#endif

#ifndef R_PKT_CTRL
#define R_PKT_CTRL                      0x47D4
#define B_PKT_POP_EN                    BIT(8)
#endif

#ifndef R_SEG0R_PD
#define R_SEG0R_PD                      0x481C
#endif

// SCO / CCK Threshold Registers for 2.4 GHz
#ifndef R_RXSCOBC
#define R_RXSCOBC                       0x23B0
#define B_RXSCOBC_TH                    GENMASK(18, 0)
#endif

#ifndef R_RXSCOCCK
#define R_RXSCOCCK                      0x23B4
#define B_RXSCOCCK_TH                   GENMASK(18, 0)
#endif

// Transmit Power Control Registers
#ifndef R_AX_TXPWR_CTRL
#define R_AX_TXPWR_CTRL                 0x1400
#define B_AX_TXPWR_VAL_MASK             GENMASK(7, 0)
#endif

#ifndef R_P0_TXPWR_LIMIT
#define R_P0_TXPWR_LIMIT                0x4200
#define B_P0_TXPWR_LIMIT_MASK           GENMASK(7, 0)
#endif

#ifndef R_P1_TXPWR_LIMIT
#define R_P1_TXPWR_LIMIT                0x4204
#define B_P1_TXPWR_LIMIT_MASK           GENMASK(7, 0)
#endif

// RF Synthesizer Direct Access (Base Addresses)
#define RTW89_RF_PATH_A_BASE            0xE000
#define RTW89_RF_PATH_B_BASE            0xF000
#define RTW89_RFREG_MASK                0x000FFFFFU

// RF Registers (relative to path base)
#define RR_CFGCH                        0x18
#define RR_CFGCH_V1                     0x19
#define RR_CFGCH_BAND1                  GENMASK(17, 16)
#define CFGCH_BAND1_2G                  0
#define CFGCH_BAND1_5G                  1
#define RR_CFGCH_POW_LCK                BIT(15)
#define RR_CFGCH_TRX_AH                 BIT(14)
#define RR_CFGCH_BCN                    BIT(13)
#define RR_CFGCH_BW2                    BIT(12)
#define RR_CFGCH_BW                     GENMASK(11, 10)
#define CFGCH_BW_80M                    1
#define CFGCH_BW_40M                    2
#define CFGCH_BW_20M                    3
#define RR_CFGCH_BAND0                  GENMASK(9, 8)
#define CFGCH_BAND0_2G                  0
#define CFGCH_BAND0_5G                  1
#define RR_CFGCH_CH                     GENMASK(7, 0)

#define RR_LUTWD0                       0x3F
#define RR_LUTWD0_LB                    GENMASK(5, 0)

#define RR_LCKST                        0xCF
#define RR_LCKST_BIN                    BIT(0)

// ============================================================================
// 3. Frequency Band & Regulatory Power Constants
// ============================================================================

// Regulatory Power Limits
#define RTW89_REGULATORY_POWER_CLAMP_DBM        18      // +18 dBm maximum regulatory clamp
#define RTW89_REGULATORY_POWER_CLAMP_HALF_DBM   36      // 36 * 0.5 dBm = +18.0 dBm
#define RTW89_REGULATORY_POWER_CLAMP_QUARTER_DBM 72     // 72 * 0.25 dBm = +18.00 dBm
#define RTW89_REGULATORY_POWER_CLAMP_MW         63      // 10^(1.8) ≈ 63.0957 mW -> 63 mW integer

// 2.4 GHz Band Limits (Channels 1-14)
#define RTW89_2G_CH_MIN                 1
#define RTW89_2G_CH_MAX                 14
#define RTW89_2G_FREQ_MIN_MHZ           2412
#define RTW89_2G_FREQ_MAX_MHZ           2484

// 5 GHz Band Limits (Channels 36-165)
#define RTW89_5G_CH_MIN                 36
#define RTW89_5G_CH_MAX                 165
#define RTW89_5G_FREQ_MIN_MHZ           5180
#define RTW89_5G_FREQ_MAX_MHZ           5825

// ============================================================================
// 4. Data Structures for Channel & Regulatory Information
// ============================================================================

struct rtw89_power_entry {
    uint8_t  channel;             // Channel index (1-14 for 2.4G, 36-165 for 5G)
    uint8_t  band;                // RTW89_BAND_2G or RTW89_BAND_5G
    uint16_t freq_mhz;            // Center frequency in MHz
    int8_t   raw_limit_dbm;       // Original regulatory limit before clamp (dBm)
    int8_t   clamped_power_dbm;   // Clamped power (<= +18 dBm)
    int16_t  power_half_dbm;      // Clamped power in 0.5 dBm units (<= 36)
    int16_t  power_quarter_dbm;   // Clamped power in 0.25 dBm units (<= 72)
    uint32_t power_mw;            // Approximate power in milliwatts (<= 63 mW)
};

struct rtw89_regulatory_table {
    static constexpr size_t MAX_CHANNELS = 64;
    rtw89_power_entry entries[MAX_CHANNELS];
    size_t count{0};
};

struct rtw89_channel_params {
    uint8_t  channel{1};
    uint8_t  band_type{RTW89_BAND_2G};
    uint8_t  band_width{RTW89_CHANNEL_WIDTH_20};
    uint8_t  primary_ch_offset{RTW89_SC_DONT_CARE};
    uint32_t freq_mhz{2412};
    int8_t   tx_power_dbm{18};
};

struct rtw89_phy_state {
    bool     initialized{false};
    uint8_t  current_channel{1};
    uint8_t  current_band{RTW89_BAND_2G};
    uint8_t  current_bandwidth{RTW89_CHANNEL_WIDTH_20};
    uint8_t  current_offset{RTW89_SC_DONT_CARE};
    uint32_t current_freq_mhz{2412};
    int8_t   current_power_dbm{18};
    int16_t  current_power_half_dbm{36};
    uint32_t current_power_mw{63};
};

// ============================================================================
// 5. Declarations: Frequency, Channels, and Regulatory Calculations
// ============================================================================

// Channel Frequency & Validation (strictly integer arithmetic)
uint32_t rtw89_channel_to_freq_mhz(uint8_t channel);
uint8_t  rtw89_freq_to_channel(uint32_t freq_mhz);
bool     rtw89_is_valid_channel(uint8_t channel);
bool     rtw89_is_2g_channel(uint8_t channel);
bool     rtw89_is_5g_channel(uint8_t channel);
uint8_t  rtw89_get_channel_band(uint8_t channel);

// Fixed-Point Power Conversion (dBm <-> mW, strictly integer fixed-point)
uint32_t rtw89_dbm_to_mw_fixed(int8_t dbm);
uint32_t rtw89_half_dbm_to_mw_fixed(int16_t half_dbm);

// Regulatory Power Tables Access
const rtw89_regulatory_table& rtw89_get_default_regulatory_table();
const rtw89_power_entry*      rtw89_get_power_entry(uint8_t channel);

// ============================================================================
// 6. Declarations: Core PHY & RF Engine Functions
// ============================================================================

// 6.1 Power Limit Clamping Functions
int8_t  rtw89_phy_clamp_power(int8_t power_dbm, int8_t max_limit_dbm = RTW89_REGULATORY_POWER_CLAMP_DBM);
int16_t rtw89_phy_clamp_power_half_dbm(int16_t power_half_dbm, int16_t max_limit = RTW89_REGULATORY_POWER_CLAMP_HALF_DBM);
int16_t rtw89_phy_clamp_power_quarter_dbm(int16_t power_quarter_dbm, int16_t max_limit = RTW89_REGULATORY_POWER_CLAMP_QUARTER_DBM);
void    rtw89_phy_clamp_power(rtw89_regulatory_table &table, int8_t max_limit_dbm = RTW89_REGULATORY_POWER_CLAMP_DBM);
int     rtw89_phy_clamp_power(Rtw89Mmio &mmio, uint8_t channel, int8_t target_dbm, int8_t max_limit_dbm = RTW89_REGULATORY_POWER_CLAMP_DBM);
int     rtw89_phy_clamp_power(Rtw89Mmio &mmio, int8_t target_dbm);

// 6.2 PHY and RF Initialization
int rtw89_phy_init(Rtw89Mmio &mmio);
int rtw89_phy_init(Rtw89Mmio &mmio, rtw89_phy_state &state);

// 6.3 Channel Tuning & Configuration
int rtw89_phy_set_channel(Rtw89Mmio &mmio, uint8_t channel,
                          uint8_t bandwidth = RTW89_CHANNEL_WIDTH_20,
                          uint8_t primary_offset = RTW89_SC_DONT_CARE);
int rtw89_phy_set_channel(Rtw89Mmio &mmio, const rtw89_channel_params &params);
int rtw89_phy_set_channel(Rtw89Mmio &mmio, rtw89_phy_state &state,
                          uint8_t channel,
                          uint8_t bandwidth = RTW89_CHANNEL_WIDTH_20,
                          uint8_t primary_offset = RTW89_SC_DONT_CARE);

// Inline convenience overloads taking pointers
inline int rtw89_phy_init(Rtw89Mmio *mmio) {
    return mmio ? rtw89_phy_init(*mmio) : -EINVAL;
}

inline int rtw89_phy_init(Rtw89Mmio *mmio, rtw89_phy_state &state) {
    return mmio ? rtw89_phy_init(*mmio, state) : -EINVAL;
}

inline int rtw89_phy_set_channel(Rtw89Mmio *mmio, uint8_t channel,
                                 uint8_t bandwidth = RTW89_CHANNEL_WIDTH_20,
                                 uint8_t primary_offset = RTW89_SC_DONT_CARE) {
    return mmio ? rtw89_phy_set_channel(*mmio, channel, bandwidth, primary_offset) : -EINVAL;
}

inline int rtw89_phy_clamp_power(Rtw89Mmio *mmio, uint8_t channel, int8_t target_dbm,
                                 int8_t max_limit_dbm = RTW89_REGULATORY_POWER_CLAMP_DBM) {
    return mmio ? rtw89_phy_clamp_power(*mmio, channel, target_dbm, max_limit_dbm) : -EINVAL;
}

inline int rtw89_phy_clamp_power(Rtw89Mmio *mmio, int8_t target_dbm) {
    return mmio ? rtw89_phy_clamp_power(*mmio, target_dbm) : -EINVAL;
}
