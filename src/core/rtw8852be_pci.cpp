//
// rtw8852be_pci.cpp — RTL8852BE PCIe ASPM & Power State Control Engine
//
#include "rtw8852be_pci.h"

#if !defined(KERNEL) && !defined(__APPLE_KEXT__)
#include <chrono>
#include <thread>
#endif

// ============================================================================
// 1. Initialization
// ============================================================================

int rtw89_pci_init(rtw89_pci_info *pci, Rtw89Mmio *mmio) {
    if (!pci || !mmio) {
        return -EINVAL;
    }

    pci->mmio = mmio;
    pci->power_state = RTW89_POWER_D0;
    pci->aspm_l1_enabled = false;
    pci->aspm_l1ss_enabled = false;
    pci->disable_aspm_l1 = false;
    pci->disable_aspm_l1ss = false;
    pci->backup_aspm = 0;
    pci->link_ctrl = 0;
    pci->clkreq_enabled = false;
    pci->transition_delay_us = 10;
    pci->saved_regs = {};

    return 0;
}

// ============================================================================
// 2. ASPM Control Implementation
// ============================================================================

int rtw89_pci_disable_aspm_l1_l1ss(Rtw89Mmio &mmio, rtw89_pci_info &pci) {
    // 1. Save original ASPM register configuration if not already preserved
    uint32_t mix_cfg = mmio.read32(pci.mix_cfg_reg);
    if (pci.backup_aspm == 0) {
        pci.backup_aspm = mix_cfg;
    }

    // 2. Clear ASPM L1 and L0 control bits in R_AX_PCIE_MIX_CFG_V1
    mmio.write32_clr(pci.mix_cfg_reg, B_AX_ASPM_CTRL_MASK);

    // 3. Explicitly disable L1 Substates in hardware via B_AX_L1SUB_DISABLE
    mmio.write32_set(pci.mix_cfg_reg, B_AX_L1SUB_DISABLE);

    // 4. Clear ASPM L1 and L0s bits in PCIe Link Control MMIO register
    mmio.write32_clr(pci.exp_lnk_ctrl_reg,
                     PCI_EXP_LNKCTL_ASPM_L1 | PCI_EXP_LNKCTL_ASPM_L0S);

    // 5. Update cached PCIe Link Control value
    pci.link_ctrl &= static_cast<uint16_t>(~(PCI_EXP_LNKCTL_ASPM_L1 | PCI_EXP_LNKCTL_ASPM_L0S));

    // 6. Write through to PCIe configuration space if hook is present
    if (pci.config_write16) {
        int ret = pci.config_write16(PCI_EXP_LNKCTL, pci.link_ctrl);
        if (ret != 0) {
            return -EIO;
        }
    }

    // 7. Enforce bus memory barrier
    Rtw89Mmio::mb();

    // 8. Update tracking state
    pci.aspm_l1_enabled = false;
    pci.aspm_l1ss_enabled = false;
    pci.disable_aspm_l1 = true;
    pci.disable_aspm_l1ss = true;

    return 0;
}

int rtw89_pci_disable_aspm_l1_l1ss(rtw89_pci_info *pci) {
    if (!pci || !pci->mmio) {
        return -EINVAL;
    }
    return rtw89_pci_disable_aspm_l1_l1ss(*pci->mmio, *pci);
}

int rtw89_pci_aspm_set(Rtw89Mmio &mmio, rtw89_pci_info &pci, bool enable) {
    if (enable) {
        // If driver policy forces ASPM L1 disablement (e.g. AMD APU stability quirk),
        // disallow enabling ASPM L1
        if (pci.disable_aspm_l1) {
            return 0;
        }

        uint32_t mix_cfg = mmio.read32(pci.mix_cfg_reg);
        if (pci.backup_aspm == 0) {
            pci.backup_aspm = mix_cfg;
        }

        // Enable ASPM L1 and clear substate disable bit in hardware
        mmio.write32_set(pci.mix_cfg_reg, B_AX_ASPM_CTRL_L1);
        mmio.write32_clr(pci.mix_cfg_reg, B_AX_L1SUB_DISABLE);

        // Update Link Control register
        mmio.write32_set(pci.exp_lnk_ctrl_reg, PCI_EXP_LNKCTL_ASPM_L1);
        pci.link_ctrl |= PCI_EXP_LNKCTL_ASPM_L1;

        if (pci.config_write16) {
            int ret = pci.config_write16(PCI_EXP_LNKCTL, pci.link_ctrl);
            if (ret != 0) {
                return -EIO;
            }
        }

        Rtw89Mmio::mb();
        pci.aspm_l1_enabled = true;
    } else {
        // Clear ASPM L1 and set substate disable bit in hardware
        mmio.write32_clr(pci.mix_cfg_reg, B_AX_ASPM_CTRL_L1);
        mmio.write32_set(pci.mix_cfg_reg, B_AX_L1SUB_DISABLE);

        mmio.write32_clr(pci.exp_lnk_ctrl_reg, PCI_EXP_LNKCTL_ASPM_L1);
        pci.link_ctrl &= static_cast<uint16_t>(~PCI_EXP_LNKCTL_ASPM_L1);

        if (pci.config_write16) {
            int ret = pci.config_write16(PCI_EXP_LNKCTL, pci.link_ctrl);
            if (ret != 0) {
                return -EIO;
            }
        }

        Rtw89Mmio::mb();
        pci.aspm_l1_enabled = false;
    }

    return 0;
}

int rtw89_pci_aspm_set(rtw89_pci_info *pci, bool enable) {
    if (!pci || !pci->mmio) {
        return -EINVAL;
    }
    return rtw89_pci_aspm_set(*pci->mmio, *pci, enable);
}

// ============================================================================
// 3. CLKREQ and L1SS Controls
// ============================================================================

int rtw89_pci_clkreq_set(Rtw89Mmio &mmio, rtw89_pci_info &pci, bool enable) {
    if (enable) {
        mmio.write32_set(pci.lat_ctrl_reg, B_AX_CLK_REQ_SEL_OPT | B_AX_CLK_REQ_SEL);
        mmio.write32_set(pci.l1_clk_ctrl_reg, B_AX_CLK_REQ_N);
        mmio.write32_set(pci.exp_lnk_ctrl_reg, PCI_EXP_LNKCTL_CLKREQ_EN);
        pci.link_ctrl |= PCI_EXP_LNKCTL_CLKREQ_EN;

        if (pci.config_write16) {
            int ret = pci.config_write16(PCI_EXP_LNKCTL, pci.link_ctrl);
            if (ret != 0) {
                return -EIO;
            }
        }
        pci.clkreq_enabled = true;
    } else {
        mmio.write32_clr(pci.l1_clk_ctrl_reg, B_AX_CLK_REQ_N);
        mmio.write32_clr(pci.exp_lnk_ctrl_reg, PCI_EXP_LNKCTL_CLKREQ_EN);
        pci.link_ctrl &= static_cast<uint16_t>(~PCI_EXP_LNKCTL_CLKREQ_EN);

        if (pci.config_write16) {
            int ret = pci.config_write16(PCI_EXP_LNKCTL, pci.link_ctrl);
            if (ret != 0) {
                return -EIO;
            }
        }
        pci.clkreq_enabled = false;
    }

    Rtw89Mmio::mb();
    return 0;
}

int rtw89_pci_clkreq_set(rtw89_pci_info *pci, bool enable) {
    if (!pci || !pci->mmio) {
        return -EINVAL;
    }
    return rtw89_pci_clkreq_set(*pci->mmio, *pci, enable);
}

int rtw89_pci_l1ss_set(Rtw89Mmio &mmio, rtw89_pci_info &pci, bool enable) {
    if (pci.disable_aspm_l1ss && enable) {
        return 0;
    }

    if (enable) {
        mmio.write32_clr(pci.mix_cfg_reg, B_AX_L1SUB_DISABLE);
        pci.aspm_l1ss_enabled = true;
    } else {
        mmio.write32_set(pci.mix_cfg_reg, B_AX_L1SUB_DISABLE);
        pci.aspm_l1ss_enabled = false;
    }

    Rtw89Mmio::mb();
    return 0;
}

int rtw89_pci_l1ss_set(rtw89_pci_info *pci, bool enable) {
    if (!pci || !pci->mmio) {
        return -EINVAL;
    }
    return rtw89_pci_l1ss_set(*pci->mmio, *pci, enable);
}

// ============================================================================
// 4. Power State Management (D0 <-> D3hot)
// ============================================================================

int rtw89_pci_set_power_state(Rtw89Mmio &mmio, rtw89_pci_info &pci, rtw89_power_state target_state) {
    if (target_state != RTW89_POWER_D0 && target_state != RTW89_POWER_D3HOT) {
        return -EINVAL;
    }

    if (pci.power_state == target_state) {
        return 0; // Already in desired power state
    }

    if (target_state == RTW89_POWER_D3HOT) {
        // Transition D0 -> D3hot
        // 1. Preserve critical registers before low-power transition
        pci.saved_regs.mix_cfg   = mmio.read32(pci.mix_cfg_reg);
        pci.saved_regs.init_cfg1 = mmio.read32(pci.init_cfg_reg);
        pci.saved_regs.himr0     = mmio.read32(R_AX_PCIE_HIMR0);
        pci.saved_regs.himr1     = mmio.read32(R_AX_PCIE_HIMR1);
        pci.saved_regs.ps_ctrl   = mmio.read32(pci.ps_ctrl_reg);
        pci.saved_regs.lat_ctrl  = mmio.read32(pci.lat_ctrl_reg);
        pci.saved_regs.link_ctrl = pci.link_ctrl;
        pci.saved_regs.valid     = true;

        Rtw89Mmio::wmb();

        // 2. Unlock RSV_CTRL and enable PERST power disconnect
        mmio.write32_set(pci.rsv_ctrl_reg, B_AX_WLOCK_1C_BIT6);
        mmio.write32_set(pci.rsv_ctrl_reg, B_AX_R_DIS_PRST);
        mmio.write32_clr(pci.rsv_ctrl_reg, B_AX_WLOCK_1C_BIT6);

        // 3. Release L2 HCI LDO control in SYS_SDIO_CTRL
        mmio.write32_clr(pci.sys_sdio_ctrl_reg, B_AX_PCIE_DIS_L2_CTRL_LDO_HCI);

        // 4. Ensure PERST / PCIe link training registers remain preserved
        mmio.write32_set(pci.init_cfg_reg,
                         B_AX_PCIE_PERST_KEEP_REG | B_AX_PCIE_TRAIN_KEEP_REG);

        // 5. PCIe Power State Control: disable exit L1, request enter L1
        mmio.write32_clr(pci.ps_ctrl_reg,
                         B_AX_CMAC_EXIT_L1_EN | B_AX_DMAC0_EXIT_L1_EN);
        mmio.write32_set(pci.ps_ctrl_reg, B_AX_SEL_REQ_ENTR_L1);

        // 6. Clock gating sequence: gate reference clock and request
        mmio.write32_clr(pci.lat_ctrl_reg, B_AX_CLK_REQ_SEL_OPT | B_AX_CLK_REQ_SEL);
        mmio.write32_clr(pci.l1_clk_ctrl_reg, B_AX_CLK_REQ_N);

        // 7. Settling delay sequence for clock gating and power down
        if (pci.transition_delay_us > 0) {
#if !defined(KERNEL) && !defined(__APPLE_KEXT__)
            std::this_thread::sleep_for(std::chrono::microseconds(pci.transition_delay_us));
#else
            IODelay(pci.transition_delay_us);
#endif
        }

        Rtw89Mmio::mb();
        pci.power_state = RTW89_POWER_D3HOT;
        return 0;

    } else { // target_state == RTW89_POWER_D0
        // Transition D3hot -> D0
        // 1. Power rail stabilization delay sequence
        if (pci.transition_delay_us > 0) {
#if !defined(KERNEL) && !defined(__APPLE_KEXT__)
            std::this_thread::sleep_for(std::chrono::microseconds(pci.transition_delay_us));
#else
            IODelay(pci.transition_delay_us);
#endif
        }

        // 2. Unlock RSV_CTRL and restore normal PERST connection
        mmio.write32_set(pci.rsv_ctrl_reg, B_AX_WLOCK_1C_BIT6);
        mmio.write32_clr(pci.rsv_ctrl_reg, B_AX_R_DIS_PRST);
        mmio.write32_clr(pci.rsv_ctrl_reg, B_AX_WLOCK_1C_BIT6);

        // 3. Assert normal L2 LDO mode
        mmio.write32_set(pci.sys_sdio_ctrl_reg, B_AX_PCIE_DIS_L2_CTRL_LDO_HCI);

        // 4. Clear PERST keep flags
        mmio.write32_clr(pci.init_cfg_reg,
                         B_AX_PCIE_PERST_KEEP_REG | B_AX_PCIE_TRAIN_KEEP_REG);

        // 5. PCIe Power State Control: enable exit L1, clear request enter L1
        mmio.write32_set(pci.ps_ctrl_reg,
                         B_AX_CMAC_EXIT_L1_EN | B_AX_DMAC0_EXIT_L1_EN);
        mmio.write32_clr(pci.ps_ctrl_reg, B_AX_SEL_REQ_ENTR_L1);

        // 6. Clock un-gating sequence: re-enable clock request and latency opt
        mmio.write32_set(pci.lat_ctrl_reg, B_AX_CLK_REQ_SEL_OPT | B_AX_CLK_REQ_SEL);
        mmio.write32_set(pci.l1_clk_ctrl_reg, B_AX_CLK_REQ_N);

        // 7. Settling delay sequence for clock & PLL stabilization
        if (pci.transition_delay_us > 0) {
#if !defined(KERNEL) && !defined(__APPLE_KEXT__)
            std::this_thread::sleep_for(std::chrono::microseconds(pci.transition_delay_us));
#else
            IODelay(pci.transition_delay_us);
#endif
        }

        // 8. Restore preserved registers across power transition
        if (pci.saved_regs.valid) {
            mmio.write32(R_AX_PCIE_HIMR0, pci.saved_regs.himr0);
            mmio.write32(R_AX_PCIE_HIMR1, pci.saved_regs.himr1);
            mmio.write32(pci.init_cfg_reg, pci.saved_regs.init_cfg1);
            pci.link_ctrl = pci.saved_regs.link_ctrl;

            if (pci.config_write16) {
                int ret = pci.config_write16(PCI_EXP_LNKCTL, pci.link_ctrl);
                if (ret != 0) {
                    return -EIO;
                }
            }
        }

        // 9. Re-enforce ASPM L1/L1SS disablement policy if active (AMD Phoenix APU stability)
        if (pci.disable_aspm_l1) {
            mmio.write32_clr(pci.mix_cfg_reg, B_AX_ASPM_CTRL_MASK);
            mmio.write32_set(pci.mix_cfg_reg, B_AX_L1SUB_DISABLE);
            mmio.write32_clr(pci.exp_lnk_ctrl_reg,
                             PCI_EXP_LNKCTL_ASPM_L1 | PCI_EXP_LNKCTL_ASPM_L0S);
            pci.link_ctrl &= static_cast<uint16_t>(~(PCI_EXP_LNKCTL_ASPM_L1 | PCI_EXP_LNKCTL_ASPM_L0S));

            if (pci.config_write16) {
                int ret = pci.config_write16(PCI_EXP_LNKCTL, pci.link_ctrl);
                if (ret != 0) {
                    return -EIO;
                }
            }
            pci.aspm_l1_enabled = false;
            pci.aspm_l1ss_enabled = false;
        }

        Rtw89Mmio::mb();
        pci.power_state = RTW89_POWER_D0;
        return 0;
    }
}

int rtw89_pci_set_power_state(rtw89_pci_info *pci, rtw89_power_state state) {
    if (!pci || !pci->mmio) {
        return -EINVAL;
    }
    return rtw89_pci_set_power_state(*pci->mmio, *pci, state);
}
