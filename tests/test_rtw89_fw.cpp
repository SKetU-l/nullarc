//
// test_rtw89_fw.cpp — Unit tests for RTL8852BE firmware parser, downloader & H2C mailbox
//
#include "linux_shim.h"
#include "skbuff.h"
#include "rtw8852be_reg.h"
#include "rtw8852be_mmio.h"
#include "rtw8852be_fw.h"
#include "rtw8852b_fw_bin.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static void test_checksum_algorithms() {
    printf("[TEST] Running test_checksum_algorithms...\n");

    // Known CRC-32 vector: "123456789" (ASCII) = 0xCBF43926
    const char *test_str = "123456789";
    uint32_t crc = rtw89_calc_crc32(reinterpret_cast<const uint8_t *>(test_str), 9);
    assert(crc == 0xCBF43926U);

    // Empty buffer
    assert(rtw89_calc_crc32(nullptr, 0) == 0);
    assert(rtw89_calc_checksum32(nullptr, 0) == 0);

    // Simple 32-bit sum
    uint8_t data[7] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07 };
    uint32_t expected_sum = 0x04030201U + 0x00070605U;
    uint32_t sum = rtw89_calc_checksum32(data, 7);
    assert(sum == expected_sum);

    printf("[PASS] test_checksum_algorithms\n");
}

static void test_mfw_recognize() {
    printf("[TEST] Running test_mfw_recognize...\n");

    struct rtw89_dev dev{};
    dev.cv = 2; // CV2

    struct rtw89_fw_suit suit{};
    int ret = rtw89_mfw_recognize(&dev, RTW89_FW_NORMAL, &suit);
    assert(ret == 0);
    assert(suit.type == RTW89_FW_NORMAL);
    assert(suit.data != nullptr);
    assert(suit.size > 200000);
    assert(suit.major_ver == 0);
    assert(suit.minor_ver == 27);
    assert(suit.sub_ver == 32);
    assert(suit.sub_idex == 1);
    assert(suit.commitid != 0);

    // Test recognizing WOWLAN firmware
    struct rtw89_fw_suit wowlan_suit{};
    ret = rtw89_mfw_recognize(&dev, RTW89_FW_WOWLAN, &wowlan_suit);
    assert(ret == 0);
    assert(wowlan_suit.type == RTW89_FW_WOWLAN);
    assert(wowlan_suit.size > 200000);

    // Test invalid magic signature rejection
    uint8_t corrupt_bin[64];
    memcpy(corrupt_bin, rtw8852b_fw_bin, sizeof(corrupt_bin));
    corrupt_bin[0] = 0xAA; // Corrupt signature
    struct rtw89_fw_suit corrupt_suit{};
    ret = rtw89_mfw_recognize(&dev, RTW89_FW_NORMAL, &corrupt_suit, corrupt_bin, sizeof(corrupt_bin));
    assert(ret == -EINVAL);

    printf("[PASS] test_mfw_recognize\n");
}

static void test_fw_hdr_parser() {
    printf("[TEST] Running test_fw_hdr_parser...\n");

    struct rtw89_dev dev{};
    dev.cv = 2;

    struct rtw89_fw_suit suit{};
    int ret = rtw89_mfw_recognize(&dev, RTW89_FW_NORMAL, &suit);
    assert(ret == 0);

    struct rtw89_fw_bin_info info{};
    ret = rtw89_fw_hdr_parser(&dev, &suit, &info);
    assert(ret == 0);

    // RTL8852B Normal firmware has exactly 3 sections
    assert(info.section_num == 3);
    assert(info.hdr_len == 80); // 32 + 3 * 16
    assert(info.secure_section_exist == true);

    // Section 0: Main FW
    assert(info.section_info[0].dladdr == 0xB8970000U);
    assert(info.section_info[0].len > 250000);
    assert(info.section_info[0].type == 2);

    // Section 1: Data / RAM
    assert(info.section_info[1].dladdr == 0xB8E12000U);
    assert(info.section_info[1].len > 10000);
    assert(info.section_info[1].type == 1);

    // Section 2: Security block
    assert(info.section_info[2].dladdr == 0xB8E17C00U);
    assert(info.section_info[2].len == 2048);
    assert(info.section_info[2].type == FWDL_SECURITY_SECTION_TYPE);

    // Total size equals header + sections
    uint32_t total = info.hdr_len + info.section_info[0].len +
                     info.section_info[1].len + info.section_info[2].len;
    assert(total == suit.size);

    // Test rejection of truncated firmware
    struct rtw89_fw_bin_info trunc_info{};
    ret = rtw89_fw_hdr_parser(suit.data, 50, &trunc_info);
    assert(ret == -EINVAL);

    printf("[PASS] test_fw_hdr_parser\n");
}

static void test_h2c_builder_and_tx() {
    printf("[TEST] Running test_h2c_builder_and_tx...\n");

    struct rtw89_dev dev{};
    dev.fw.h2c_seq = 42;

    const uint8_t test_payload[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x11, 0x22, 0x33, 0x44 };
    Rtw89H2cBuilder builder(H2C_CAT_MAC, H2C_CL_MAC_PS, 0x05);
    builder.req_ack(true).done_ack(false).payload(test_payload, sizeof(test_payload));

    struct sk_buff *skb = builder.build(&dev);
    assert(skb != nullptr);
    assert(skb->len == H2C_HEADER_LEN + sizeof(test_payload));

    // Verify H2C header contents
    uint32_t w0 = static_cast<uint32_t>(skb->data[0]) |
                  (static_cast<uint32_t>(skb->data[1]) << 8) |
                  (static_cast<uint32_t>(skb->data[2]) << 16) |
                  (static_cast<uint32_t>(skb->data[3]) << 24);
    assert(FIELD_GET(H2C_HDR_CAT, w0) == H2C_CAT_MAC);
    assert(FIELD_GET(H2C_HDR_CLASS, w0) == H2C_CL_MAC_PS);
    assert(FIELD_GET(H2C_HDR_FUNC, w0) == 0x05);
    assert(FIELD_GET(H2C_HDR_H2C_SEQ, w0) == 42);

    uint32_t w1 = static_cast<uint32_t>(skb->data[4]) |
                  (static_cast<uint32_t>(skb->data[5]) << 8) |
                  (static_cast<uint32_t>(skb->data[6]) << 16) |
                  (static_cast<uint32_t>(skb->data[7]) << 24);
    assert(FIELD_GET(H2C_HDR_TOTAL_LEN, w1) == sizeof(test_payload));
    assert((w1 & H2C_HDR_REC_ACK) != 0);
    assert((w1 & H2C_HDR_DONE_ACK) == 0);

    // Verify payload bytes
    assert(memcmp(skb->data + H2C_HEADER_LEN, test_payload, sizeof(test_payload)) == 0);

    // Transmit into device history
    int ret = rtw89_h2c_tx(&dev, skb, false);
    assert(ret == 0);
    assert(dev.tx_history.size() == 1);
    assert(dev.tx_history[0].size() == H2C_HEADER_LEN + sizeof(test_payload));

    printf("[PASS] test_h2c_builder_and_tx\n");
}

static void test_fw_check_rdy() {
    printf("[TEST] Running test_fw_check_rdy...\n");

    MockMmioBus mmio(0x20000, false);
    struct rtw89_dev dev{};
    dev.mmio = &mmio;

    // Initially 0 -> timeout
    int ret = rtw89_fw_check_rdy(&dev, 100);
    assert(ret == -ETIMEDOUT);

    // Set INIT_RDY -> success
    mmio.write32_mask(R_AX_FWDL_STATUS_V1, B_AX_WCPU_FWDL_STS_MASK, RTW89_FWDL_WCPU_FW_INIT_RDY);
    ret = rtw89_fw_check_rdy(&dev, 100);
    assert(ret == 0);
    assert((dev.flags[0] & BIT(RTW89_FLAG_FW_RDY)) != 0);

    // Set Checksum fail -> EIO
    mmio.write32_mask(R_AX_FWDL_STATUS_V1, B_AX_WCPU_FWDL_STS_MASK, RTW89_FWDL_CHECKSUM_FAIL);
    ret = rtw89_fw_check_rdy(&dev, 100);
    assert(ret == -EIO);

    printf("[PASS] test_fw_check_rdy\n");
}

static void test_fw_download_full() {
    printf("[TEST] Running test_fw_download_full...\n");

    MockMmioBus mmio(0x20000, false);
    struct rtw89_dev dev{};
    dev.mmio = &mmio;
    dev.cv = 2;

    size_t total_tx_bytes = 0;
    size_t packet_count = 0;

    dev.h2c_tx_fn = [&](struct rtw89_dev *rtwdev, struct sk_buff *skb, bool fwdl) -> int {
        (void)rtwdev;
        assert(fwdl == true);
        assert(skb->len <= H2C_HEADER_LEN + RTW89_FWDL_CHUNK_SIZE);
        total_tx_bytes += (skb->len - H2C_HEADER_LEN);
        packet_count++;
        return 0;
    };

    int ret = rtw89_fw_download(&dev);
    assert(ret == 0);

    // In RTL8852B Normal firmware:
    // Header (80 bytes) + Section 0 (255120) + Section 1 (11800) + Section 2 (2048) = 269048 bytes
    assert(total_tx_bytes == 269048);
    // Packet count: 1 (header) + ceil(255120/1024) [250] + ceil(11800/1024) [12] + ceil(2048/1024) [2] = 265 packets
    assert(packet_count == 265);
    assert((dev.flags[0] & BIT(RTW89_FLAG_FW_RDY)) != 0);

    printf("[PASS] test_fw_download_full (265 packets, 269,048 bytes verified)\n");
}

static void test_fw_stress() {
    printf("[TEST] Running test_fw_stress (100 cycles)...\n");

    for (int cycle = 0; cycle < 100; ++cycle) {
        MockMmioBus mmio(0x20000, false);
        struct rtw89_dev dev{};
        dev.mmio = &mmio;
        dev.cv = 2;

        int ret = rtw89_fw_download(&dev);
        assert(ret == 0);
        assert((dev.flags[0] & BIT(RTW89_FLAG_FW_RDY)) != 0);
    }

    printf("[PASS] test_fw_stress (100 cycles passed)\n");
}

int main() {
    printf("=== Realtek RTL8852BE Firmware Download & Mailbox Unit Tests ===\n");

    test_checksum_algorithms();
    test_mfw_recognize();
    test_fw_hdr_parser();
    test_h2c_builder_and_tx();
    test_fw_check_rdy();
    test_fw_download_full();
    test_fw_stress();

    printf("=== All Firmware Download & Mailbox tests passed successfully (0 errors) ===\n");
    return 0;
}
