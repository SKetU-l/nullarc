//
// test_rtw89_aspm.cpp — Unit test suite for RTL8852BE PCIe ASPM & Power State Control
// Verifies ASPM L1/L1SS disablement, CLKREQ control, and D0 <-> D3hot transitions
//
#include "linux_shim.h"
#include "rtw8852be_reg.h"
#include "rtw8852be_mmio.h"
#include "rtw8852be_pci.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// ============================================================================
// Test Cases
// ============================================================================

static void test_initialization() {
    printf("[TEST] Running test_initialization...\n");
    MockMmioBus mmio(0x20000, false);
    rtw89_pci_info pci{};

    int ret = rtw89_pci_init(&pci, &mmio);
    assert(ret == 0);
    assert(pci.mmio == &mmio);
    assert(pci.power_state == RTW89_POWER_D0);
    assert(!pci.aspm_l1_enabled);
    assert(!pci.aspm_l1ss_enabled);
    assert(!pci.disable_aspm_l1);
    assert(!pci.disable_aspm_l1ss);

    // Null checks
    assert(rtw89_pci_init(nullptr, &mmio) == -EINVAL);
    assert(rtw89_pci_init(&pci, nullptr) == -EINVAL);
    printf("[PASS] test_initialization\n");
}

static void test_disable_aspm_l1_l1ss() {
    printf("[TEST] Running test_disable_aspm_l1_l1ss...\n");
    MockMmioBus mmio(0x20000, false);
    rtw89_pci_info pci{};
    rtw89_pci_init(&pci, &mmio);

    // Seed registers with ASPM L1 enabled state
    mmio.write32(pci.mix_cfg_reg, B_AX_ASPM_CTRL_L1);
    mmio.write32(pci.exp_lnk_ctrl_reg, PCI_EXP_LNKCTL_ASPM_L1 | PCI_EXP_LNKCTL_ASPM_L0S);

    // Simulated config space register
    uint16_t cfg_link_ctrl = PCI_EXP_LNKCTL_ASPM_L1 | PCI_EXP_LNKCTL_ASPM_L0S;
    pci.config_write16 = [&](uint16_t offset, uint16_t val) -> int {
        if (offset == PCI_EXP_LNKCTL) {
            cfg_link_ctrl = val;
            return 0;
        }
        return -EINVAL;
    };

    int ret = rtw89_pci_disable_aspm_l1_l1ss(&pci);
    assert(ret == 0);
    assert(pci.disable_aspm_l1 == true);
    assert(pci.disable_aspm_l1ss == true);
    assert(pci.aspm_l1_enabled == false);
    assert(pci.aspm_l1ss_enabled == false);

    // Check hardware MMIO registers
    uint32_t mix_val = mmio.read32(pci.mix_cfg_reg);
    assert((mix_val & B_AX_ASPM_CTRL_MASK) == 0);
    assert((mix_val & B_AX_L1SUB_DISABLE) != 0);

    uint32_t lnk_val = mmio.read32(pci.exp_lnk_ctrl_reg);
    assert((lnk_val & (PCI_EXP_LNKCTL_ASPM_L1 | PCI_EXP_LNKCTL_ASPM_L0S)) == 0);

    // Check PCIe config space write-through
    assert((cfg_link_ctrl & (PCI_EXP_LNKCTL_ASPM_L1 | PCI_EXP_LNKCTL_ASPM_L0S)) == 0);

    // Verify policy enforcement: attempting to enable ASPM L1 should be blocked
    ret = rtw89_pci_aspm_set(&pci, true);
    assert(ret == 0);
    assert(pci.aspm_l1_enabled == false);
    assert((mmio.read32(pci.mix_cfg_reg) & B_AX_ASPM_CTRL_L1) == 0);

    printf("[PASS] test_disable_aspm_l1_l1ss\n");
}

static void test_aspm_enable_disable() {
    printf("[TEST] Running test_aspm_enable_disable...\n");
    MockMmioBus mmio(0x20000, false);
    rtw89_pci_info pci{};
    rtw89_pci_init(&pci, &mmio);

    // ASPM L1 disable policy is false
    pci.disable_aspm_l1 = false;

    // Enable ASPM L1
    int ret = rtw89_pci_aspm_set(&pci, true);
    assert(ret == 0);
    assert(pci.aspm_l1_enabled == true);
    assert((mmio.read32(pci.mix_cfg_reg) & B_AX_ASPM_CTRL_L1) != 0);
    assert((mmio.read32(pci.mix_cfg_reg) & B_AX_L1SUB_DISABLE) == 0);
    assert((mmio.read32(pci.exp_lnk_ctrl_reg) & PCI_EXP_LNKCTL_ASPM_L1) != 0);

    // Disable ASPM L1
    ret = rtw89_pci_aspm_set(&pci, false);
    assert(ret == 0);
    assert(pci.aspm_l1_enabled == false);
    assert((mmio.read32(pci.mix_cfg_reg) & B_AX_ASPM_CTRL_L1) == 0);
    assert((mmio.read32(pci.mix_cfg_reg) & B_AX_L1SUB_DISABLE) != 0);
    assert((mmio.read32(pci.exp_lnk_ctrl_reg) & PCI_EXP_LNKCTL_ASPM_L1) == 0);

    printf("[PASS] test_aspm_enable_disable\n");
}

static void test_clkreq_and_l1ss() {
    printf("[TEST] Running test_clkreq_and_l1ss...\n");
    MockMmioBus mmio(0x20000, false);
    rtw89_pci_info pci{};
    rtw89_pci_init(&pci, &mmio);

    // CLKREQ enable
    int ret = rtw89_pci_clkreq_set(&pci, true);
    assert(ret == 0);
    assert(pci.clkreq_enabled == true);
    assert((mmio.read32(pci.lat_ctrl_reg) & (B_AX_CLK_REQ_SEL_OPT | B_AX_CLK_REQ_SEL)) != 0);
    assert((mmio.read32(pci.l1_clk_ctrl_reg) & B_AX_CLK_REQ_N) != 0);
    assert((mmio.read32(pci.exp_lnk_ctrl_reg) & PCI_EXP_LNKCTL_CLKREQ_EN) != 0);

    // CLKREQ disable
    ret = rtw89_pci_clkreq_set(&pci, false);
    assert(ret == 0);
    assert(pci.clkreq_enabled == false);
    assert((mmio.read32(pci.l1_clk_ctrl_reg) & B_AX_CLK_REQ_N) == 0);
    assert((mmio.read32(pci.exp_lnk_ctrl_reg) & PCI_EXP_LNKCTL_CLKREQ_EN) == 0);

    // L1SS enable
    ret = rtw89_pci_l1ss_set(&pci, true);
    assert(ret == 0);
    assert(pci.aspm_l1ss_enabled == true);
    assert((mmio.read32(pci.mix_cfg_reg) & B_AX_L1SUB_DISABLE) == 0);

    // L1SS disable
    ret = rtw89_pci_l1ss_set(&pci, false);
    assert(ret == 0);
    assert(pci.aspm_l1ss_enabled == false);
    assert((mmio.read32(pci.mix_cfg_reg) & B_AX_L1SUB_DISABLE) != 0);

    printf("[PASS] test_clkreq_and_l1ss\n");
}

static void test_power_state_transitions() {
    printf("[TEST] Running test_power_state_transitions...\n");
    MockMmioBus mmio(0x20000, false);
    rtw89_pci_info pci{};
    rtw89_pci_init(&pci, &mmio);
    pci.transition_delay_us = 0; // fast for unit testing

    // Pre-populate registers to test preservation
    const uint32_t orig_himr0 = 0x12345678;
    const uint32_t orig_himr1 = 0x87654321;
    const uint32_t orig_init_cfg1 = 0xDEADBEEF;
    const uint32_t orig_mix_cfg = 0xCAFEBABE;
    mmio.write32(R_AX_PCIE_HIMR0, orig_himr0);
    mmio.write32(R_AX_PCIE_HIMR1, orig_himr1);
    mmio.write32(pci.init_cfg_reg, orig_init_cfg1);
    mmio.write32(pci.mix_cfg_reg, orig_mix_cfg);

    // Enforce AMD APU ASPM disable policy
    rtw89_pci_disable_aspm_l1_l1ss(&pci);

    // Transition D0 -> D3hot
    int ret = rtw89_pci_set_power_state(&pci, RTW89_POWER_D3HOT);
    assert(ret == 0);
    assert(pci.power_state == RTW89_POWER_D3HOT);
    assert(pci.saved_regs.valid == true);
    assert(pci.saved_regs.himr0 == orig_himr0);
    assert(pci.saved_regs.himr1 == orig_himr1);

    // Verify D3hot register states: enter L1 requested, clock gated
    assert((mmio.read32(pci.ps_ctrl_reg) & B_AX_SEL_REQ_ENTR_L1) != 0);
    assert((mmio.read32(pci.lat_ctrl_reg) & (B_AX_CLK_REQ_SEL_OPT | B_AX_CLK_REQ_SEL)) == 0);
    assert((mmio.read32(pci.l1_clk_ctrl_reg) & B_AX_CLK_REQ_N) == 0);

    // Corrupt registers in low power state to test restoration
    mmio.write32(R_AX_PCIE_HIMR0, 0);
    mmio.write32(R_AX_PCIE_HIMR1, 0);
    mmio.write32(pci.init_cfg_reg, 0);

    // Transition D3hot -> D0
    ret = rtw89_pci_set_power_state(&pci, RTW89_POWER_D0);
    assert(ret == 0);
    assert(pci.power_state == RTW89_POWER_D0);

    // Verify restored registers
    assert(mmio.read32(R_AX_PCIE_HIMR0) == orig_himr0);
    assert(mmio.read32(R_AX_PCIE_HIMR1) == orig_himr1);
    assert(mmio.read32(pci.init_cfg_reg) == orig_init_cfg1);

    // Verify AMD APU ASPM disable policy is strictly re-enforced upon wake
    assert(pci.aspm_l1_enabled == false);
    assert((mmio.read32(pci.mix_cfg_reg) & B_AX_ASPM_CTRL_MASK) == 0);
    assert((mmio.read32(pci.mix_cfg_reg) & B_AX_L1SUB_DISABLE) != 0);

    printf("[PASS] test_power_state_transitions\n");
}

static void test_power_state_stress() {
    printf("[TEST] Running test_power_state_stress (10,000 cycles)...\n");
    MockMmioBus mmio(0x20000, false);
    rtw89_pci_info pci{};
    rtw89_pci_init(&pci, &mmio);
    pci.transition_delay_us = 0;

    for (int i = 0; i < 10000; ++i) {
        int ret = rtw89_pci_set_power_state(&pci, RTW89_POWER_D3HOT);
        assert(ret == 0);
        assert(pci.power_state == RTW89_POWER_D3HOT);

        ret = rtw89_pci_set_power_state(&pci, RTW89_POWER_D0);
        assert(ret == 0);
        assert(pci.power_state == RTW89_POWER_D0);
    }

    printf("[PASS] test_power_state_stress (10,000 cycles passed)\n");
}

int main() {
    printf("=== Realtek RTL8852BE PCIe ASPM & Power State Control Unit Tests ===\n");

    test_initialization();
    test_disable_aspm_l1_l1ss();
    test_aspm_enable_disable();
    test_clkreq_and_l1ss();
    test_power_state_transitions();
    test_power_state_stress();

    printf("=== All ASPM & Power State tests passed successfully (0 errors) ===\n");
    return 0;
}
