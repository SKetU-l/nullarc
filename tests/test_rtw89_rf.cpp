//
// test_rtw89_rf.cpp — Unit tests for RTL8852BE PHY/RF subsystem & power clamping
//
#include "linux_shim.h"
#include "rtw8852be_reg.h"
#include "rtw8852be_mmio.h"
#include "rtw8852be_phy.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void test_channel_frequency_mapping() {
    printf("[TEST] Running test_channel_frequency_mapping...\n");

    // 2.4 GHz Band tests
    assert(rtw89_is_2g_channel(1));
    assert(rtw89_is_2g_channel(6));
    assert(rtw89_is_2g_channel(11));
    assert(rtw89_is_2g_channel(14));
    assert(!rtw89_is_2g_channel(0));
    assert(!rtw89_is_2g_channel(15));
    assert(!rtw89_is_2g_channel(36));

    assert(rtw89_channel_to_freq_mhz(1) == 2412);
    assert(rtw89_channel_to_freq_mhz(6) == 2437);
    assert(rtw89_channel_to_freq_mhz(11) == 2462);
    assert(rtw89_channel_to_freq_mhz(14) == 2484);

    assert(rtw89_freq_to_channel(2412) == 1);
    assert(rtw89_freq_to_channel(2437) == 6);
    assert(rtw89_freq_to_channel(2462) == 11);
    assert(rtw89_freq_to_channel(2484) == 14);

    // 5 GHz Band tests
    assert(rtw89_is_5g_channel(36));
    assert(rtw89_is_5g_channel(48));
    assert(rtw89_is_5g_channel(149));
    assert(rtw89_is_5g_channel(165));
    assert(!rtw89_is_5g_channel(1));
    assert(!rtw89_is_5g_channel(37));
    assert(!rtw89_is_5g_channel(170));

    assert(rtw89_channel_to_freq_mhz(36) == 5180);
    assert(rtw89_channel_to_freq_mhz(48) == 5240);
    assert(rtw89_channel_to_freq_mhz(149) == 5745);
    assert(rtw89_channel_to_freq_mhz(165) == 5825);

    assert(rtw89_freq_to_channel(5180) == 36);
    assert(rtw89_freq_to_channel(5240) == 48);
    assert(rtw89_freq_to_channel(5745) == 149);
    assert(rtw89_freq_to_channel(5825) == 165);

    // Band identification
    assert(rtw89_get_channel_band(1) == RTW89_BAND_2G);
    assert(rtw89_get_channel_band(36) == RTW89_BAND_5G);
    assert(rtw89_get_channel_band(200) == RTW89_BAND_INVALID);

    printf("[PASS] test_channel_frequency_mapping\n");
}

static void test_fixed_point_power_and_clamping() {
    printf("[TEST] Running test_fixed_point_power_and_clamping...\n");

    // Power conversion table checks
    assert(rtw89_dbm_to_mw_fixed(0) == 1);
    assert(rtw89_dbm_to_mw_fixed(10) == 10);
    assert(rtw89_dbm_to_mw_fixed(13) == 20);
    assert(rtw89_dbm_to_mw_fixed(18) == 63); // Clamped power
    assert(rtw89_half_dbm_to_mw_fixed(36) == 63);

    // Clamping helpers
    assert(rtw89_phy_clamp_power(23, 18) == 18);
    assert(rtw89_phy_clamp_power(15, 18) == 15);
    assert(rtw89_phy_clamp_power_half_dbm(46, 36) == 36);
    assert(rtw89_phy_clamp_power_quarter_dbm(92, 72) == 72);

    // Regulatory table inspection
    const auto &reg_table = rtw89_get_default_regulatory_table();
    assert(reg_table.count > 20);
    for (size_t i = 0; i < reg_table.count; ++i) {
        const auto &e = reg_table.entries[i];
        assert(e.clamped_power_dbm <= RTW89_REGULATORY_POWER_CLAMP_DBM);
        assert(e.power_half_dbm <= RTW89_REGULATORY_POWER_CLAMP_HALF_DBM);
        assert(e.power_quarter_dbm <= RTW89_REGULATORY_POWER_CLAMP_QUARTER_DBM);
        assert(e.power_mw <= RTW89_REGULATORY_POWER_CLAMP_MW);
    }

    const auto *entry_ch1 = rtw89_get_power_entry(1);
    assert(entry_ch1 != nullptr);
    assert(entry_ch1->freq_mhz == 2412);
    assert(entry_ch1->clamped_power_dbm == 18);

    const auto *entry_ch36 = rtw89_get_power_entry(36);
    assert(entry_ch36 != nullptr);
    assert(entry_ch36->freq_mhz == 5180);
    assert(entry_ch36->clamped_power_dbm == 18);

    // Hardware power clamp MMIO write
    MockMmioBus mmio(0x20000, false);
    int ret = rtw89_phy_clamp_power(mmio, 1, 23, 18);
    assert(ret == 0);
    assert((mmio.read32(R_AX_TXPWR_CTRL) & B_AX_TXPWR_VAL_MASK) == 18);
    assert((mmio.read32(R_P0_TXPWR_LIMIT) & B_P0_TXPWR_LIMIT_MASK) == 18);
    assert((mmio.read32(R_P1_TXPWR_LIMIT) & B_P1_TXPWR_LIMIT_MASK) == 18);

    printf("[PASS] test_fixed_point_power_and_clamping\n");
}

static void test_phy_init_and_channel_tuning() {
    printf("[TEST] Running test_phy_init_and_channel_tuning...\n");

    MockMmioBus mmio(0x20000, false);
    rtw89_phy_state state{};

    // 1. Initialization
    int ret = rtw89_phy_init(mmio, state);
    assert(ret == 0);
    assert(state.initialized == true);
    assert(state.current_channel == 1);
    assert(state.current_band == RTW89_BAND_2G);
    assert(state.current_bandwidth == RTW89_CHANNEL_WIDTH_20);
    assert(state.current_freq_mhz == 2412);
    assert(state.current_power_dbm == 18);

    // Verify clock and AFE registers
    assert((mmio.read32(R_AX_SYS_CLK_CTRL) & (B_AX_CPU_CLK_EN | B_AX_MAC_CLK_EN)) != 0);
    assert((mmio.read32(R_AX_AFE_CTRL) & B_AX_AFE_EN) != 0);

    // 2. Tune to 5 GHz channel 36 (20 MHz)
    ret = rtw89_phy_set_channel(mmio, state, 36, RTW89_CHANNEL_WIDTH_20, RTW89_SC_DONT_CARE);
    assert(ret == 0);
    assert(state.current_channel == 36);
    assert(state.current_band == RTW89_BAND_5G);
    assert(state.current_freq_mhz == 5180);
    // Path band selection should have 5G bit set
    assert((mmio.read32(R_PATH0_BAND_SEL_V1) & B_PATH0_BAND_SEL_MSK_V1) != 0);
    assert((mmio.read32(R_PATH1_BAND_SEL_V1) & B_PATH1_BAND_SEL_MSK_V1) != 0);

    // 3. Tune to 5 GHz channel 36 (40 MHz)
    ret = rtw89_phy_set_channel(mmio, state, 36, RTW89_CHANNEL_WIDTH_40, RTW89_SC_20_LOWER);
    assert(ret == 0);
    assert(state.current_bandwidth == RTW89_CHANNEL_WIDTH_40);
    assert((mmio.read32(R_FC0_BW_V1) & B_FC0_BW_SET) == (1U << 30));

    // 4. Tune to 5 GHz channel 36 (80 MHz)
    ret = rtw89_phy_set_channel(mmio, state, 36, RTW89_CHANNEL_WIDTH_80, RTW89_SC_DONT_CARE);
    assert(ret == 0);
    assert(state.current_bandwidth == RTW89_CHANNEL_WIDTH_80);
    assert((mmio.read32(R_FC0_BW_V1) & B_FC0_BW_SET) == (2U << 30));

    // 5. Reject 80 MHz in 2.4 GHz band
    ret = rtw89_phy_set_channel(mmio, state, 1, RTW89_CHANNEL_WIDTH_80, RTW89_SC_DONT_CARE);
    assert(ret == -EINVAL);

    // 6. Reject invalid channel
    ret = rtw89_phy_set_channel(mmio, state, 99, RTW89_CHANNEL_WIDTH_20, RTW89_SC_DONT_CARE);
    assert(ret == -EINVAL);

    printf("[PASS] test_phy_init_and_channel_tuning\n");
}

static void test_phy_stress() {
    printf("[TEST] Running test_phy_stress (10,000 hops)...\n");

    MockMmioBus mmio(0x20000, false);
    rtw89_phy_state state{};
    rtw89_phy_init(mmio, state);

    const uint8_t test_channels[] = { 1, 6, 11, 14, 36, 40, 44, 48, 149, 153, 157, 161, 165 };
    const size_t num_channels = sizeof(test_channels) / sizeof(test_channels[0]);

    for (int i = 0; i < 10000; ++i) {
        uint8_t ch = test_channels[i % num_channels];
        uint8_t bw = (ch > 14 && (i % 2 == 1)) ? RTW89_CHANNEL_WIDTH_40 : RTW89_CHANNEL_WIDTH_20;
        int ret = rtw89_phy_set_channel(mmio, state, ch, bw, RTW89_SC_DONT_CARE);
        assert(ret == 0);
        assert(state.current_channel == ch);
    }

    printf("[PASS] test_phy_stress (10,000 hops passed)\n");
}

int main() {
    printf("=== Realtek RTL8852BE PHY Subsystem & RF Calibration Unit Tests ===\n");

    test_channel_frequency_mapping();
    test_fixed_point_power_and_clamping();
    test_phy_init_and_channel_tuning();
    test_phy_stress();

    printf("=== All PHY Subsystem & RF Calibration tests passed successfully (0 errors) ===\n");
    return 0;
}
