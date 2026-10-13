//
// rtw8852be_pci.h — RTL8852BE PCIe ASPM & Power State Control Engine
// Declarations for ASPM control, power states (D0/D3hot), and PCIe state tracking
//
#pragma once

#include "linux_shim.h"
#include "rtw8852be_reg.h"
#include "rtw8852be_mmio.h"

#include <cerrno>
#include <cstdint>
#include <functional>

// ============================================================================
// 1. Additional PCIe Register & Bitfield Definitions
// ============================================================================

#ifndef R_AX_SYS_SDIO_CTRL
#define R_AX_SYS_SDIO_CTRL              0x0070
#endif

#ifndef B_AX_PCIE_DIS_L2_CTRL_LDO_HCI
#define B_AX_PCIE_DIS_L2_CTRL_LDO_HCI   BIT(15)
#endif

#ifndef RTW89_PCIE_ASPM_CTRL
#define RTW89_PCIE_ASPM_CTRL            0x070F
#endif

#ifndef RTW89_PCIE_TIMER_CTRL
#define RTW89_PCIE_TIMER_CTRL           0x0718
#endif

#ifndef RTW89_PCIE_BIT_L1SUB
#define RTW89_PCIE_BIT_L1SUB            BIT(5)
#endif

#ifndef RTW89_PCIE_L1_CTRL
#define RTW89_PCIE_L1_CTRL              0x0719
#endif

#ifndef RTW89_PCIE_BIT_CLK
#define RTW89_PCIE_BIT_CLK              BIT(4)
#endif

#ifndef RTW89_PCIE_BIT_L1
#define RTW89_PCIE_BIT_L1               BIT(3)
#endif

// ============================================================================
// 2. Power State Enum (D0, D3hot)
// ============================================================================

enum rtw89_power_state {
    RTW89_POWER_D0    = 0,
    RTW89_POWER_D3HOT = 3,
};

// ============================================================================
// 3. PCIe State Tracking Structure: rtw89_pci_info
// ============================================================================

struct rtw89_pci_info {
    Rtw89Mmio *mmio{nullptr};

    // Current device power state
    rtw89_power_state power_state{RTW89_POWER_D0};

    // ASPM & L1 Substate Tracking
    bool aspm_l1_enabled{false};
    bool aspm_l1ss_enabled{false};
    bool disable_aspm_l1{false};       // Force disable ASPM L1 (AMD Phoenix APU safety quirk)
    bool disable_aspm_l1ss{false};     // Force disable L1 substates
    uint32_t backup_aspm{0};           // Preserved original R_AX_PCIE_MIX_CFG_V1

    // PCIe Link Control & Clock State
    uint16_t link_ctrl{0};             // Cached PCI_EXP_LNKCTL register value
    bool clkreq_enabled{false};        // CLKREQ# state

    // Register Offsets
    uint32_t mix_cfg_reg{R_AX_PCIE_MIX_CFG_V1};        // 0x300C
    uint32_t exp_lnk_ctrl_reg{R_AX_PCIE_EXP_LNK_CTRL};  // 0x13F0
    uint32_t init_cfg_reg{R_AX_PCIE_INIT_CFG1};        // 0x1000
    uint32_t ps_ctrl_reg{R_AX_PCIE_PS_CTRL_V1};         // 0x3008
    uint32_t l1_clk_ctrl_reg{R_AX_L1_CLK_CTRL};         // 0x3010
    uint32_t lat_ctrl_reg{R_AX_PCIE_LAT_CTRL};          // 0x3044
    uint32_t rsv_ctrl_reg{R_AX_RSV_CTRL};              // 0x001C
    uint32_t sys_sdio_ctrl_reg{R_AX_SYS_SDIO_CTRL};    // 0x0070

    // Timing & Delay Configuration (in microseconds)
    uint32_t transition_delay_us{10};

    // Register Preservation Store across D3hot transitions
    struct SavedRegisters {
        uint32_t mix_cfg{0};
        uint32_t init_cfg1{0};
        uint32_t himr0{0};
        uint32_t himr1{0};
        uint32_t ps_ctrl{0};
        uint32_t lat_ctrl{0};
        uint16_t link_ctrl{0};
        bool valid{false};
    } saved_regs;

    // Optional PCIe Configuration Space hooks
    using ConfigRead16 = std::function<int(uint16_t offset, uint16_t *val)>;
    using ConfigWrite16 = std::function<int(uint16_t offset, uint16_t val)>;
    ConfigRead16 config_read16{nullptr};
    ConfigWrite16 config_write16{nullptr};
};

// ============================================================================
// 4. API Declarations
// ============================================================================

// Initialize PCIe tracking info
int rtw89_pci_init(rtw89_pci_info *pci, Rtw89Mmio *mmio);

// ASPM control functions
int rtw89_pci_aspm_set(rtw89_pci_info *pci, bool enable);
int rtw89_pci_aspm_set(Rtw89Mmio &mmio, rtw89_pci_info &pci, bool enable);

int rtw89_pci_disable_aspm_l1_l1ss(rtw89_pci_info *pci);
int rtw89_pci_disable_aspm_l1_l1ss(Rtw89Mmio &mmio, rtw89_pci_info &pci);

// Clock request (CLKREQ#) and L1 substate (L1SS) control
int rtw89_pci_clkreq_set(rtw89_pci_info *pci, bool enable);
int rtw89_pci_clkreq_set(Rtw89Mmio &mmio, rtw89_pci_info &pci, bool enable);

int rtw89_pci_l1ss_set(rtw89_pci_info *pci, bool enable);
int rtw89_pci_l1ss_set(Rtw89Mmio &mmio, rtw89_pci_info &pci, bool enable);

// Power state management state machine (D0 <-> D3hot)
int rtw89_pci_set_power_state(rtw89_pci_info *pci, rtw89_power_state state);
int rtw89_pci_set_power_state(Rtw89Mmio &mmio, rtw89_pci_info &pci, rtw89_power_state state);
