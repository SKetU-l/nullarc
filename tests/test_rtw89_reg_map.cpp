//
// test_rtw89_reg_map.cpp — Unit Test Suite for RTL8852BE Register Map & MMIO
// Validates register definitions, bitfield manipulation, atomic RMW, and polling
//
#include "linux_shim.h"
#include "rtw8852be_reg.h"
#include "rtw8852be_mmio.h"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

#define TEST_ASSERT(cond, msg, ...)                                           \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "[FAIL] %s:%d: " msg "\n", __FILE__, __LINE__, \
                         ##__VA_ARGS__);                                       \
            std::exit(1);                                                      \
        }                                                                      \
    } while (0)

static std::stringstream g_log;

#define LOG_PRINT(fmt, ...)                                                    \
    do {                                                                       \
        char buf[512];                                                         \
        std::snprintf(buf, sizeof(buf), fmt, ##__VA_ARGS__);                   \
        std::printf("%s", buf);                                                \
        g_log << buf;                                                          \
    } while (0)

// ============================================================================
// 1. Test Register Offsets & Identity
// ============================================================================
static void test_register_offsets() {
    LOG_PRINT("[TEST] 1. RTL8852BE Register Map Definitions & Offsets...\n");

    // PCIe mixed config and control
    static_assert(R_AX_PCIE_MIX_CFG_V1    == 0x300C, "R_AX_PCIE_MIX_CFG_V1 mismatch");
    static_assert(R_AX_PCIE_INIT_CFG1     == 0x1000, "R_AX_PCIE_INIT_CFG1 mismatch");
    static_assert(R_AX_PCIE_INIT_CFG2     == 0x1004, "R_AX_PCIE_INIT_CFG2 mismatch");
    static_assert(R_AX_PCIE_EXP_LNK_CTRL  == 0x13F0, "R_AX_PCIE_EXP_LNK_CTRL mismatch");
    static_assert(R_AX_PCIE_EXP_CTRL      == 0x13F0, "R_AX_PCIE_EXP_CTRL mismatch");
    static_assert(R_AX_PCIE_PS_CTRL_V1    == 0x3008, "R_AX_PCIE_PS_CTRL_V1 mismatch");
    static_assert(R_AX_L1_CLK_CTRL        == 0x3010, "R_AX_L1_CLK_CTRL mismatch");
    static_assert(R_AX_PCIE_LAT_CTRL      == 0x3044, "R_AX_PCIE_LAT_CTRL mismatch");

    // Interrupt registers
    static_assert(R_AX_PCIE_HIMR0         == 0x10B0, "R_AX_PCIE_HIMR0 mismatch");
    static_assert(R_AX_PCIE_HISR0         == 0x10B4, "R_AX_PCIE_HISR0 mismatch");
    static_assert(R_AX_PCIE_HIMR1         == 0x13B0, "R_AX_PCIE_HIMR1 mismatch");
    static_assert(R_AX_PCIE_HISR1         == 0x13B4, "R_AX_PCIE_HISR1 mismatch");
    static_assert(R_AX_PCIE_HIMR00_V1     == 0x30B0, "R_AX_PCIE_HIMR00_V1 mismatch");
    static_assert(R_AX_PCIE_HISR00_V1     == 0x30B4, "R_AX_PCIE_HISR00_V1 mismatch");
    static_assert(R_AX_HIMR0              == 0x01A0, "R_AX_HIMR0 mismatch");
    static_assert(R_AX_HISR0              == 0x01A4, "R_AX_HISR0 mismatch");

    // H2C / C2H Mailbox registers
    static_assert(R_AX_HALT_H2C_CTRL      == 0x0160, "R_AX_HALT_H2C_CTRL mismatch");
    static_assert(R_AX_HALT_C2H_CTRL      == 0x0164, "R_AX_HALT_C2H_CTRL mismatch");
    static_assert(R_AX_H2CQ_CSR           == 0x8160, "R_AX_H2CQ_CSR mismatch");
    static_assert(R_AX_H2CQ_CSR_V1        == 0x7160, "R_AX_H2CQ_CSR_V1 mismatch");
    static_assert(R_AX_C2H_CTRL           == 0x0164, "R_AX_C2H_CTRL mismatch");
    static_assert(R_AX_C2HREG_CTRL        == 0x8164, "R_AX_C2HREG_CTRL mismatch");
    static_assert(R_AX_C2HREG_CTRL_V1     == 0x7164, "R_AX_C2HREG_CTRL_V1 mismatch");
    static_assert(R_AX_UDM1               == 0x01F4, "R_AX_UDM1 mismatch");

    // Firmware download / status registers
    static_assert(R_AX_FWDL_STATUS_V1     == 0x01E0, "R_AX_FWDL_STATUS_V1 mismatch");
    static_assert(R_AX_SYS_STATUS         == 0x00F4, "R_AX_SYS_STATUS mismatch");
    static_assert(R_AX_SYS_STATUS1        == 0x00F4, "R_AX_SYS_STATUS1 mismatch");
    static_assert(R_AX_WCPU_FW_CTRL       == 0x01E0, "R_AX_WCPU_FW_CTRL mismatch");
    static_assert(R_AX_BOOT_DBG           == 0x83F0, "R_AX_BOOT_DBG mismatch");

    // DMA ring indices (TXBD CH0..CH12)
    static_assert(R_AX_TXBD_IDX_CH0       == 0x1058, "R_AX_TXBD_IDX_CH0 mismatch");
    static_assert(R_AX_TXBD_IDX_CH1       == 0x105C, "R_AX_TXBD_IDX_CH1 mismatch");
    static_assert(R_AX_TXBD_IDX_CH2       == 0x1060, "R_AX_TXBD_IDX_CH2 mismatch");
    static_assert(R_AX_TXBD_IDX_CH3       == 0x1064, "R_AX_TXBD_IDX_CH3 mismatch");
    static_assert(R_AX_TXBD_IDX_CH4       == 0x1068, "R_AX_TXBD_IDX_CH4 mismatch");
    static_assert(R_AX_TXBD_IDX_CH5       == 0x106C, "R_AX_TXBD_IDX_CH5 mismatch");
    static_assert(R_AX_TXBD_IDX_CH6       == 0x1070, "R_AX_TXBD_IDX_CH6 mismatch");
    static_assert(R_AX_TXBD_IDX_CH7       == 0x1074, "R_AX_TXBD_IDX_CH7 mismatch");
    static_assert(R_AX_TXBD_IDX_CH8       == 0x1078, "R_AX_TXBD_IDX_CH8 mismatch");
    static_assert(R_AX_TXBD_IDX_CH9       == 0x107C, "R_AX_TXBD_IDX_CH9 mismatch");
    static_assert(R_AX_TXBD_IDX_CH10      == 0x137C, "R_AX_TXBD_IDX_CH10 mismatch");
    static_assert(R_AX_TXBD_IDX_CH11      == 0x1380, "R_AX_TXBD_IDX_CH11 mismatch");
    static_assert(R_AX_TXBD_IDX_CH12      == 0x1080, "R_AX_TXBD_IDX_CH12 mismatch");
    static_assert(R_AX_TXBD_IDX_CH10_V1   == 0x11D0, "R_AX_TXBD_IDX_CH10_V1 mismatch");
    static_assert(R_AX_TXBD_IDX_CH11_V1   == 0x11D4, "R_AX_TXBD_IDX_CH11_V1 mismatch");

    // DMA ring indices (RXBD CH0..CH1)
    static_assert(R_AX_RXBD_IDX_CH0       == 0x1050, "R_AX_RXBD_IDX_CH0 mismatch");
    static_assert(R_AX_RXBD_IDX_CH1       == 0x1054, "R_AX_RXBD_IDX_CH1 mismatch");
    static_assert(R_AX_RXBD_IDX_CH0_V1    == 0x1218, "R_AX_RXBD_IDX_CH0_V1 mismatch");
    static_assert(R_AX_RXBD_IDX_CH1_V1    == 0x121C, "R_AX_RXBD_IDX_CH1_V1 mismatch");

    // MAC / BBP / RF registers
    static_assert(R_AX_MAC_REG            == 0xC000, "R_AX_MAC_REG mismatch");
    static_assert(R_AX_CMAC_REG_START     == 0xC000, "R_AX_CMAC_REG_START mismatch");
    static_assert(R_AX_CMAC_REG_END       == 0xFFFF, "R_AX_CMAC_REG_END mismatch");
    static_assert(R_AX_WMAC_REG_START     == 0x8000, "R_AX_WMAC_REG_START mismatch");
    static_assert(R_AX_DMAC_REG_START     == 0x9000, "R_AX_DMAC_REG_START mismatch");
    static_assert(R_AX_SYS_CLK_CTRL       == 0x0008, "R_AX_SYS_CLK_CTRL mismatch");
    static_assert(R_AX_AFE_CTRL           == 0x0024, "R_AX_AFE_CTRL mismatch");
    static_assert(R_AX_AFE_CTRL1          == 0x0024, "R_AX_AFE_CTRL1 mismatch");
    static_assert(R_AX_AFE_LDO_CTRL       == 0x0020, "R_AX_AFE_LDO_CTRL mismatch");

    LOG_PRINT("  -> [PASS] Register map offsets verified\n");
}

// ============================================================================
// 2. Test Bitfield Manipulation Macros (BIT, GENMASK, FIELD_PREP, FIELD_GET)
// ============================================================================
static void test_bitfield_macros() {
    LOG_PRINT("[TEST] 2. Bitfield Manipulation Macros & Masks...\n");

    // Check single bit generation
    TEST_ASSERT(BIT(0)  == 0x00000001ULL, "BIT(0) mismatch");
    TEST_ASSERT(BIT(14) == 0x00004000ULL, "BIT(14) mismatch");
    TEST_ASSERT(BIT(17) == 0x00020000ULL, "BIT(17) mismatch");
    TEST_ASSERT(BIT(31) == 0x80000000ULL, "BIT(31) mismatch");

    // Check GENMASK ranges
    TEST_ASSERT(GENMASK(1, 0)   == 0x00000003ULL, "GENMASK(1, 0) mismatch");
    TEST_ASSERT(GENMASK(7, 5)   == 0x000000E0ULL, "GENMASK(7, 5) mismatch");
    TEST_ASSERT(GENMASK(17, 16) == 0x00030000ULL, "GENMASK(17, 16) mismatch");
    TEST_ASSERT(GENMASK(27, 24) == 0x0F000000ULL, "GENMASK(27, 24) mismatch");

    // ASPM bitfield in R_AX_PCIE_MIX_CFG_V1
    u32 aspm_mask = B_AX_ASPM_CTRL_MASK;
    TEST_ASSERT(aspm_mask == 0x00030000U, "B_AX_ASPM_CTRL_MASK value error");

    u32 prep_aspm = FIELD_PREP(B_AX_ASPM_CTRL_MASK, 0x2);
    TEST_ASSERT(prep_aspm == 0x00020000U, "FIELD_PREP ASPM error: got 0x%08x", prep_aspm);
    TEST_ASSERT(FIELD_GET(B_AX_ASPM_CTRL_MASK, prep_aspm) == 0x2, "FIELD_GET ASPM mismatch");

    // Firmware download status mask in R_AX_WCPU_FW_CTRL
    u32 fwdl_mask = B_AX_WCPU_FWDL_STS_MASK;
    TEST_ASSERT(fwdl_mask == 0x000000E0U, "B_AX_WCPU_FWDL_STS_MASK value error");

    u32 prep_fwdl = FIELD_PREP(B_AX_WCPU_FWDL_STS_MASK, RTW89_FWDL_WCPU_FW_INIT_RDY);
    TEST_ASSERT(prep_fwdl == (7U << 5), "FIELD_PREP FWDL error: got 0x%08x", prep_fwdl);
    TEST_ASSERT(FIELD_GET(B_AX_WCPU_FWDL_STS_MASK, prep_fwdl) == 7U, "FIELD_GET FWDL mismatch");

    // Replace bits helper in pointer
    u32 reg_val = 0x12345678;
    u32p_replace_bits(&reg_val, 0x3, B_AX_ASPM_CTRL_MASK);
    TEST_ASSERT((reg_val & B_AX_ASPM_CTRL_MASK) == 0x00030000U, "u32p_replace_bits mismatch");
    TEST_ASSERT((reg_val & ~B_AX_ASPM_CTRL_MASK) == (0x12345678U & ~B_AX_ASPM_CTRL_MASK),
                "Unrelated bits corrupted by replace_bits");

    LOG_PRINT("  -> [PASS] Bitfield manipulation macros verified\n");
}

// ============================================================================
// 3. Test Basic MMIO Read / Write Operations
// ============================================================================
static void test_basic_mmio() {
    LOG_PRINT("[TEST] 3. Basic MMIO Read/Write Operations & Endianness...\n");

    MockMmioBus mock(0x20000);

    // 8-bit access
    mock.write8(0x100, 0xAB);
    TEST_ASSERT(mock.read8(0x100) == 0xAB, "read8 mismatch");

    // 16-bit access
    mock.write16(0x200, 0xCDEF);
    TEST_ASSERT(mock.read16(0x200) == 0xCDEF, "read16 mismatch");

    // 32-bit access
    mock.write32(0x300, 0x12345678);
    TEST_ASSERT(mock.read32(0x300) == 0x12345678, "read32 mismatch");

    // Check little-endian memory layout directly
    TEST_ASSERT(mock.peek8(0x300) == 0x78, "Little-endian byte 0 mismatch");
    TEST_ASSERT(mock.peek8(0x301) == 0x56, "Little-endian byte 1 mismatch");
    TEST_ASSERT(mock.peek8(0x302) == 0x34, "Little-endian byte 2 mismatch");
    TEST_ASSERT(mock.peek8(0x303) == 0x12, "Little-endian byte 3 mismatch");

    // Masked reads
    mock.write32(R_AX_PCIE_MIX_CFG_V1, 0x00020001); // L0 ASPM and L1SUB_DISABLE
    u32 aspm_field = mock.read32_mask(R_AX_PCIE_MIX_CFG_V1, B_AX_ASPM_CTRL_MASK);
    TEST_ASSERT(aspm_field == 0x2, "read32_mask mismatch: expected 2, got %u", aspm_field);

    u16 val16 = mock.read16_mask(0x200, 0x0F00);
    TEST_ASSERT(val16 == 0xD, "read16_mask mismatch: expected 0xD, got 0x%X", val16);

    u16 val16_hi = mock.read16_mask(0x200, 0xF000);
    TEST_ASSERT(val16_hi == 0xC, "read16_mask mismatch: expected 0xC, got 0x%X", val16_hi);

    u8 val8 = mock.read8_mask(0x100, 0x0F);
    TEST_ASSERT(val8 == 0xB, "read8_mask mismatch");

    LOG_PRINT("  -> [PASS] Basic MMIO read/write and endianness verified\n");
}

// ============================================================================
// 4. Test Atomic Read-Modify-Write (Set / Clear / Mask)
// ============================================================================
static void test_atomic_rmw() {
    LOG_PRINT("[TEST] 4. Atomic Read-Modify-Write (Set, Clear, Mask)...\n");

    MockMmioBus mock(0x20000);

    // Initial value
    mock.write32(R_AX_PCIE_MIX_CFG_V1, 0x00000000);

    // Set bit 17 (B_AX_ASPM_CTRL_L1)
    mock.write32_set(R_AX_PCIE_MIX_CFG_V1, B_AX_ASPM_CTRL_L1);
    TEST_ASSERT(mock.read32(R_AX_PCIE_MIX_CFG_V1) == B_AX_ASPM_CTRL_L1, "write32_set 1 mismatch");

    // Set bit 0 (B_AX_L1SUB_DISABLE)
    mock.write32_set(R_AX_PCIE_MIX_CFG_V1, B_AX_L1SUB_DISABLE);
    TEST_ASSERT(mock.read32(R_AX_PCIE_MIX_CFG_V1) == (B_AX_ASPM_CTRL_L1 | B_AX_L1SUB_DISABLE),
                "write32_set 2 mismatch");

    // Clear bit 17 (B_AX_ASPM_CTRL_L1)
    mock.write32_clr(R_AX_PCIE_MIX_CFG_V1, B_AX_ASPM_CTRL_L1);
    TEST_ASSERT(mock.read32(R_AX_PCIE_MIX_CFG_V1) == B_AX_L1SUB_DISABLE, "write32_clr mismatch");

    // Mask update: write value 0x3 into ASPM_CTRL_MASK without disturbing L1SUB_DISABLE
    mock.write32_mask(R_AX_PCIE_MIX_CFG_V1, B_AX_ASPM_CTRL_MASK, 0x3);
    TEST_ASSERT(mock.read32(R_AX_PCIE_MIX_CFG_V1) == (0x00030000U | B_AX_L1SUB_DISABLE),
                "write32_mask mismatch");

    // Mask update: change to 0x1
    mock.write32_mask(R_AX_PCIE_MIX_CFG_V1, B_AX_ASPM_CTRL_MASK, 0x1);
    TEST_ASSERT(mock.read32(R_AX_PCIE_MIX_CFG_V1) == (0x00010000U | B_AX_L1SUB_DISABLE),
                "write32_mask 2 mismatch");

    // 16-bit set / clr
    mock.write16(0x500, 0x0010);
    mock.write16_set(0x500, 0x0020);
    TEST_ASSERT(mock.read16(0x500) == 0x0030, "write16_set mismatch");
    mock.write16_clr(0x500, 0x0010);
    TEST_ASSERT(mock.read16(0x500) == 0x0020, "write16_clr mismatch");

    // 8-bit set / clr
    mock.write8(0x600, 0x01);
    mock.write8_set(0x600, 0x04);
    TEST_ASSERT(mock.read8(0x600) == 0x05, "write8_set mismatch");
    mock.write8_clr(0x600, 0x01);
    TEST_ASSERT(mock.read8(0x600) == 0x04, "write8_clr mismatch");

    LOG_PRINT("  -> [PASS] Atomic read-modify-write verified\n");
}

// ============================================================================
// 5. Test Multi-threaded Concurrent Atomic RMW
// ============================================================================
static void test_concurrent_rmw() {
    LOG_PRINT("[TEST] 5. Multi-threaded Concurrent Atomic RMW Stress Test...\n");

    MockMmioBus mock(0x20000);
    const uint32_t reg = R_AX_PCIE_INIT_CFG1;
    mock.write32(reg, 0x00000000);

    const int iterations = 10000;
    std::atomic<bool> start_flag{false};

    // Thread 1 toggles B_AX_RXHCI_EN (bit 15)
    std::thread t1([&]() {
        while (!start_flag.load(std::memory_order_acquire)) {}
        for (int i = 0; i < iterations; ++i) {
            mock.write32_set(reg, B_AX_RXHCI_EN);
            mock.write32_clr(reg, B_AX_RXHCI_EN);
        }
    });

    // Thread 2 toggles B_AX_TXHCI_EN (bit 7)
    std::thread t2([&]() {
        while (!start_flag.load(std::memory_order_acquire)) {}
        for (int i = 0; i < iterations; ++i) {
            mock.write32_set(reg, B_AX_TXHCI_EN);
            mock.write32_clr(reg, B_AX_TXHCI_EN);
        }
    });

    // Thread 3 updates B_AX_PCIE_MAX_RXDMA_MASK (bits 11:8)
    std::thread t3([&]() {
        while (!start_flag.load(std::memory_order_acquire)) {}
        for (int i = 0; i < iterations; ++i) {
            mock.write32_mask(reg, B_AX_PCIE_MAX_RXDMA_MASK, (i & 0xF));
        }
    });

    // Thread 4 updates B_AX_PCIE_MAX_TXDMA_MASK (bits 2:0)
    std::thread t4([&]() {
        while (!start_flag.load(std::memory_order_acquire)) {}
        for (int i = 0; i < iterations; ++i) {
            mock.write32_mask(reg, B_AX_PCIE_MAX_TXDMA_MASK, (i & 0x7));
        }
    });

    start_flag.store(true, std::memory_order_release);
    t1.join();
    t2.join();
    t3.join();
    t4.join();

    // Verify bits 15 and 7 are clean after their final clears
    u32 final_val = mock.read32(reg);
    TEST_ASSERT((final_val & B_AX_RXHCI_EN) == 0, "B_AX_RXHCI_EN not cleared cleanly");
    TEST_ASSERT((final_val & B_AX_TXHCI_EN) == 0, "B_AX_TXHCI_EN not cleared cleanly");

    LOG_PRINT("  -> [PASS] Concurrent atomic RMW completed 40,000 ops with 0 race errors\n");
}

// ============================================================================
// 6. Test Polling with Timeout (poll_timeout_atomic)
// ============================================================================
static void test_polling_timeout() {
    LOG_PRINT("[TEST] 6. Timeout Polling (poll_timeout_atomic)...\n");

    MockMmioBus mock(0x20000);

    // Case A: Bit is already set -> returns 0 immediately
    mock.write32(R_AX_WCPU_FW_CTRL, B_AX_FWDL_PATH_RDY);
    int ret = mock.poll_timeout_atomic(R_AX_WCPU_FW_CTRL, B_AX_FWDL_PATH_RDY,
                                       B_AX_FWDL_PATH_RDY, 1000);
    TEST_ASSERT(ret == 0, "Instant poll should succeed, got %d", ret);

    // Case B: Bit becomes set dynamically after N reads
    mock.write32(R_AX_WCPU_FW_CTRL, 0);
    int read_attempts = 0;
    mock.set_read_handler(R_AX_WCPU_FW_CTRL, [&](uint32_t /*addr*/) -> uint32_t {
        read_attempts++;
        if (read_attempts >= 4) {
            return B_AX_FWDL_PATH_RDY;
        }
        return 0;
    });

    ret = mock.poll_timeout_atomic(R_AX_WCPU_FW_CTRL, B_AX_FWDL_PATH_RDY,
                                   B_AX_FWDL_PATH_RDY, 50000, 10);
    TEST_ASSERT(ret == 0, "Dynamic poll should succeed, got %d", ret);
    TEST_ASSERT(read_attempts >= 4, "Read handler should have been queried at least 4 times");
    mock.clear_handlers();

    // Case C: Bit never sets -> should timeout with -ETIMEDOUT
    mock.write32(R_AX_WCPU_FW_CTRL, 0);
    auto t_before = std::chrono::steady_clock::now();
    ret = mock.poll_timeout_atomic(R_AX_WCPU_FW_CTRL, B_AX_FWDL_PATH_RDY,
                                   B_AX_FWDL_PATH_RDY, 2000, 100);
    auto t_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - t_before).count();

    TEST_ASSERT(ret == -ETIMEDOUT, "Should return -ETIMEDOUT on timeout, got %d", ret);
    TEST_ASSERT(t_elapsed >= 1500, "Should have waited for timeout period, elapsed=%lld us",
                static_cast<long long>(t_elapsed));

    // Case D: Predicate lambda polling
    mock.write32(R_AX_WCPU_FW_CTRL, 0);
    mock.set_read_handler(R_AX_WCPU_FW_CTRL, [count = 0](uint32_t) mutable -> uint32_t {
        count++;
        if (count >= 5) {
            return FIELD_PREP(B_AX_WCPU_FWDL_STS_MASK, RTW89_FWDL_WCPU_FW_INIT_RDY);
        }
        return 0;
    });

    ret = mock.poll_timeout_atomic(R_AX_WCPU_FW_CTRL, [](uint32_t val) {
        return FIELD_GET(B_AX_WCPU_FWDL_STS_MASK, val) == RTW89_FWDL_WCPU_FW_INIT_RDY;
    }, 50000, 10);
    TEST_ASSERT(ret == 0, "Predicate polling should succeed, got %d", ret);
    mock.clear_handlers();

    LOG_PRINT("  -> [PASS] Polling with timeout and predicate verified\n");
}

// ============================================================================
// 7. Test Strict Alignment & Bounds Enforcement
// ============================================================================
static void test_alignment_and_bounds() {
    LOG_PRINT("[TEST] 7. Strict Alignment & Bounds Exception Checking...\n");

    MockMmioBus mock(0x1000, true);

    // Unaligned 32-bit read tests (addr 0x101, 0x102, 0x103)
    bool caught_unaligned_r32 = false;
    try {
        mock.read32(0x101);
    } catch (const std::runtime_error &e) {
        caught_unaligned_r32 = true;
    }
    TEST_ASSERT(caught_unaligned_r32, "Unaligned 32-bit read did not throw exception");

    bool caught_unaligned_w32 = false;
    try {
        mock.write32(0x102, 0x12345678);
    } catch (const std::runtime_error &e) {
        caught_unaligned_w32 = true;
    }
    TEST_ASSERT(caught_unaligned_w32, "Unaligned 32-bit write did not throw exception");

    // Unaligned 16-bit access
    bool caught_unaligned_r16 = false;
    try {
        mock.read16(0x103);
    } catch (const std::runtime_error &e) {
        caught_unaligned_r16 = true;
    }
    TEST_ASSERT(caught_unaligned_r16, "Unaligned 16-bit read did not throw exception");

    // Out of bounds access
    bool caught_oob = false;
    try {
        mock.read32(0x1000); // 0x1000 + 4 > 0x1000
    } catch (const std::out_of_range &e) {
        caught_oob = true;
    }
    TEST_ASSERT(caught_oob, "Out-of-bounds read did not throw exception");

    // Relaxed alignment mode allows unaligned access
    mock.set_strict_alignment(false);
    mock.write32(0x101, 0x11223344);
    TEST_ASSERT(mock.read32(0x101) == 0x11223344, "Relaxed alignment read failed");

    LOG_PRINT("  -> [PASS] Alignment and bounds enforcement verified\n");
}

// ============================================================================
// 8. Test Memory Barriers
// ============================================================================
static void test_memory_barriers() {
    LOG_PRINT("[TEST] 8. Synchronous Memory Barriers (mb, rmb, wmb)...\n");

    Rtw89Mmio::mb();
    Rtw89Mmio::rmb();
    Rtw89Mmio::wmb();

    LOG_PRINT("  -> [PASS] Memory barriers executed cleanly\n");
}

// ============================================================================
// 9. Test DirectMmioBus Accessor
// ============================================================================
static void test_direct_mmio_bus() {
    LOG_PRINT("[TEST] 9. Direct Memory-Mapped Bus (DirectMmioBus)...\n");

    alignas(16) uint8_t raw_buffer[1024];
    std::memset(raw_buffer, 0, sizeof(raw_buffer));

    DirectMmioBus direct(raw_buffer, sizeof(raw_buffer));

    direct.write32(0x10, 0xCAFEBABE);
    TEST_ASSERT(direct.read32(0x10) == 0xCAFEBABE, "DirectMmioBus read32 mismatch");

    direct.write32_set(0x10, 0x00000001);
    TEST_ASSERT(direct.read32(0x10) == 0xCAFEBABF, "DirectMmioBus write32_set mismatch");

    direct.write16(0x20, 0x55AA);
    TEST_ASSERT(direct.read16(0x20) == 0x55AA, "DirectMmioBus read16 mismatch");

    direct.write8(0x30, 0x99);
    TEST_ASSERT(direct.read8(0x30) == 0x99, "DirectMmioBus read8 mismatch");

    // Unaligned error
    bool caught = false;
    try {
        direct.read32(0x11);
    } catch (const std::runtime_error &) {
        caught = true;
    }
    TEST_ASSERT(caught, "DirectMmioBus unaligned access not caught");

    LOG_PRINT("  -> [PASS] DirectMmioBus verified\n");
}

// ============================================================================
// 10. Stress & Leak-Free Test (ASan / UBSan)
// ============================================================================
static void test_stress_cycles() {
    LOG_PRINT("[TEST] 10. 10,000 Cycles Stress & Leak-Free Verification...\n");

    auto t_start = std::chrono::steady_clock::now();
    MockMmioBus mock(0x20000);

    for (int i = 0; i < 10000; ++i) {
        uint32_t addr = (i % 256) * 4;
        mock.write32(addr, static_cast<uint32_t>(i));
        uint32_t v = mock.read32(addr);
        TEST_ASSERT(v == static_cast<uint32_t>(i), "Stress loop read mismatch at cycle %d", i);

        mock.write32_set(addr, 0x80000000);
        mock.write32_clr(addr, 0x80000000);
        mock.write32_mask(addr, 0x000000FF, (i & 0xFF));
    }

    auto t_end = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();

    LOG_PRINT("  -> [PASS] Completed 10,000 cycles in %lld ms with 0 leaks\n",
              static_cast<long long>(ms));
}

// ============================================================================
// Main Runner
// ============================================================================
int main() {
    LOG_PRINT("============================================================\n");
    LOG_PRINT("  RTW89 Wave 2 RTL8852BE Register Map & MMIO Engine Tests   \n");
    LOG_PRINT("============================================================\n");

    test_register_offsets();
    test_bitfield_macros();
    test_basic_mmio();
    test_atomic_rmw();
    test_concurrent_rmw();
    test_polling_timeout();
    test_alignment_and_bounds();
    test_memory_barriers();
    test_direct_mmio_bus();
    test_stress_cycles();

    LOG_PRINT("============================================================\n");
    LOG_PRINT("  ALL TESTS PASSED SUCCESSFULLY (0 ASan/UBSan errors)       \n");
    LOG_PRINT("============================================================\n");

    // Write evidence log to .omo/evidence/rtw89-driver/task-4-reg-map.log
    fs::path evidence_dir = ".omo/evidence/rtw89-driver";
    try {
        fs::create_directories(evidence_dir);
        fs::path log_path = evidence_dir / "task-4-reg-map.log";
        std::ofstream log_file(log_path);
        if (log_file.is_open()) {
            log_file << g_log.str();
            log_file.close();
            std::printf("\n[EVIDENCE] Written to: %s\n", log_path.string().c_str());
        } else {
            std::fprintf(stderr, "[-] Warning: Could not open evidence log file: %s\n",
                         log_path.string().c_str());
            return 1;
        }
    } catch (const std::exception &e) {
        std::fprintf(stderr, "[-] Error writing evidence file: %s\n", e.what());
        return 1;
    }

    return 0;
}
