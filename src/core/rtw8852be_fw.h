//
// rtw8852be_fw.h — RTL8852BE Firmware Parsing, Download & H2C/C2H Mailbox Protocol
// Realtek RTL8852BE macOS Driver / Freestanding C++17
//
#pragma once

#include "linux_shim.h"
#include "skbuff.h"
#include "rtw8852be_reg.h"
#include "rtw8852be_mmio.h"
#include "rtw8852b_fw_bin.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

// ============================================================================
// 1. MFW Directory & Firmware Binary Definitions
// ============================================================================

#define RTW89_MFW_SIG               0xFF
#define RTL8852_CHIP_ID             0x8852

enum rtw89_fw_type {
    RTW89_FW_NORMAL                 = 1,
    RTW89_FW_WOWLAN                 = 3,
    RTW89_FW_LOGFMT                 = 4,
    RTW89_FW_BBMCU0                 = 5,
    RTW89_FW_BBMCU1                 = 6,
};

// ============================================================================
// 2. Firmware Header & Section Layout Definitions
// ============================================================================

#define FWDL_SECTION_MAX_NUM        10
#define FWDL_SECTION_CHKSUM_LEN     8
#define FWDL_SECTION_PER_PKT_LEN    1024
#define RTW89_FWDL_CHUNK_SIZE       1024

#define FWDL_SECURITY_SECTION_TYPE  9
#define FWDL_SECURITY_SIGLEN        512
#define FWDL_SECURITY_CHKSUM_LEN    8

// Header word bitfield accessors
#define FW_HDR_W1_MAJOR_VERSION     GENMASK(7, 0)
#define FW_HDR_W1_MINOR_VERSION     GENMASK(15, 8)
#define FW_HDR_W1_SUBVERSION        GENMASK(23, 16)
#define FW_HDR_W1_SUBINDEX          GENMASK(31, 24)
#define FW_HDR_W2_COMMITID          GENMASK(31, 0)
#define FW_HDR_W3_LEN               GENMASK(23, 16)
#define FW_HDR_W3_HDR_VER           GENMASK(31, 24)
#define FW_HDR_W4_MONTH             GENMASK(7, 0)
#define FW_HDR_W4_DATE              GENMASK(15, 8)
#define FW_HDR_W4_HOUR              GENMASK(23, 16)
#define FW_HDR_W4_MIN               GENMASK(31, 24)
#define FW_HDR_W5_YEAR              GENMASK(31, 0)
#define FW_HDR_W6_SEC_NUM           GENMASK(15, 8)
#define FW_HDR_W7_PART_SIZE         GENMASK(15, 0)
#define FW_HDR_W7_DYN_HDR           BIT(16)
#define FW_HDR_W7_CMD_VERSERION     GENMASK(31, 24)

// Section word bitfield accessors
#define FWSECTION_HDR_W0_DL_ADDR    GENMASK(31, 0)
#define FWSECTION_HDR_W1_METADATA   GENMASK(31, 24)
#define FWSECTION_HDR_W1_SECTIONTYPE GENMASK(27, 24)
#define FWSECTION_HDR_W1_SEC_SIZE   GENMASK(23, 0)
#define FWSECTION_HDR_W1_CHECKSUM   BIT(28)
#define FWSECTION_HDR_W1_REDL       BIT(29)
#define FWSECTION_HDR_W2_MSSC       GENMASK(31, 0)

// ============================================================================
// 3. H2C Protocol & Header Definitions
// ============================================================================

#define H2C_HEADER_LEN              8
#define H2C_HDR_CAT                 GENMASK(1, 0)
#define H2C_HDR_CLASS               GENMASK(7, 2)
#define H2C_HDR_FUNC                GENMASK(15, 8)
#define H2C_HDR_DEL_TYPE            GENMASK(19, 16)
#define H2C_HDR_H2C_SEQ             GENMASK(31, 24)
#define H2C_HDR_TOTAL_LEN           GENMASK(13, 0)
#define H2C_HDR_REC_ACK             BIT(14)
#define H2C_HDR_DONE_ACK            BIT(15)

#define FWCMD_TYPE_H2C              0

#define H2C_CAT_TEST                0x0
#define H2C_CAT_MAC                 0x1
#define H2C_CAT_OUTSRC              0x2

#define H2C_CL_MAC_FW_INFO          0x0
#define H2C_CL_MAC_WOW              0x1
#define H2C_CL_MAC_PS               0x2
#define H2C_CL_MAC_FWDL             0x3
#define H2C_FUNC_MAC_FWHDR_DL       0x0

#define H2C_CL_MAC_FR_EXCHG         0x5
#define H2C_CL_MAC_ADDR_CAM_UPDATE  0x6
#define H2C_CL_MAC_MEDIA_RPT        0x8
#define H2C_CL_MAC_FW_OFLD          0x9
#define H2C_CL_MAC_SEC_CAM          0xA
#define H2C_CL_BA_CAM               0xC
#define H2C_CL_MCC                  0xE
#define H2C_CL_MRC                  0x18

enum rtw89_fwdl_check_type {
    RTW89_FWDL_CHECK_WCPU_FWDL_DONE = 0,
    RTW89_FWDL_CHECK_FREERTOS_DONE  = 1,
    RTW89_FWDL_CHECK_BB0_FWDL_DONE  = 2,
    RTW89_FWDL_CHECK_BB1_FWDL_DONE  = 3,
};

// Driver state flags
#define RTW89_FLAG_POWERON          0
#define RTW89_FLAG_FW_RDY           1

// ============================================================================
// 4. Data Structures
// ============================================================================

// Binary section metadata in firmware header
struct rtw89_fw_hdr_section {
    __le32 w0;
    __le32 w1;
    __le32 w2;
    __le32 w3;
} __attribute__((packed));

// Binary firmware header (v0)
struct rtw89_fw_hdr {
    __le32 w0;
    __le32 w1;
    __le32 w2;
    __le32 w3;
    __le32 w4;
    __le32 w5;
    __le32 w6;
    __le32 w7;
    struct rtw89_fw_hdr_section sections[];
} __attribute__((packed));

// Parsed section information
struct rtw89_fw_hdr_section_info {
    uint8_t redl{0};
    const uint8_t *addr{nullptr};
    uint32_t len{0};
    uint32_t dladdr{0};
    uint32_t mssc{0};
    uint8_t type{0};
    bool ignore{false};
    const uint8_t *key_addr{nullptr};
    uint32_t key_len{0};
    uint32_t key_idx{0};
    uint32_t checksum{0};
};

// Parsed firmware binary structure
struct rtw89_fw_bin_info {
    uint8_t section_num{0};
    uint32_t hdr_len{0};
    bool dynamic_hdr_en{false};
    uint32_t dynamic_hdr_len{0};
    bool dsp_checksum{false};
    bool secure_section_exist{false};
    struct rtw89_fw_hdr_section_info section_info[FWDL_SECTION_MAX_NUM];
};

// MFW Directory Entry
struct rtw89_mfw_info {
    uint8_t cv;
    uint8_t type;
    uint8_t mp;
    uint8_t rsvd;
    __le32 shift;
    __le32 size;
} __attribute__((packed));

// MFW Header
struct rtw89_mfw_hdr {
    uint8_t sig;
    uint8_t fw_nr;
    uint8_t rsvd[14];
    struct rtw89_mfw_info info[];
} __attribute__((packed));

// Sub-firmware suit descriptor
struct rtw89_fw_suit {
    uint8_t type{0};
    const uint8_t *data{nullptr};
    uint32_t size{0};
    uint8_t major_ver{0};
    uint8_t minor_ver{0};
    uint8_t sub_ver{0};
    uint8_t sub_idex{0};
    uint16_t build_year{0};
    uint16_t build_mon{0};
    uint16_t build_date{0};
    uint16_t build_hour{0};
    uint16_t build_min{0};
    uint8_t cmd_ver{0};
    uint8_t hdr_ver{0};
    uint32_t commitid{0};
};

// On-the-wire H2C Command 8-byte header
struct fwcmd_hdr {
    __le32 hdr0;
    __le32 hdr1;
} __attribute__((packed));

// High-level H2C Command representation
struct rtw89_h2c_cmd {
    uint8_t type{FWCMD_TYPE_H2C};
    uint8_t category{0};
    uint8_t cmd_class{0};
    uint8_t function{0};
    uint8_t seq{0};
    bool req_ack{false};
    bool done_ack{false};
    uint32_t len{0};
    const uint8_t *payload{nullptr};
};

// Device chip information
struct rtw89_chip_info {
    uint16_t chip_id{RTL8852_CHIP_ID};
    uint32_t h2c_desc_size{0};
    uint8_t bbmcu_nr{0};
};

// Device firmware state
struct rtw89_fw_info {
    uint8_t h2c_seq{0};
    uint8_t rec_seq{0};
    uint8_t h2c_counter{0};
    uint8_t c2h_counter{0};
    struct rtw89_fw_suit normal{};
    struct rtw89_fw_suit wowlan{};
    struct rtw89_fw_bin_info bin_info{};
};

// Driver device context
struct rtw89_dev {
    Rtw89Mmio *mmio{nullptr};
    struct rtw89_chip_info chip{RTL8852_CHIP_ID, 0, 0};
    struct rtw89_fw_info fw{};
    unsigned long flags[2]{0, 0};
    uint8_t cv{2};

    // Callback hook for custom H2C transmit handling if set
    std::function<int(struct rtw89_dev *dev, struct sk_buff *skb, bool fwdl)> h2c_tx_fn;

    // Transmitted packet history for inspection and unit testing
    std::vector<std::vector<uint8_t>> tx_history;
};

// ============================================================================
// 5. H2C Command Builders
// ============================================================================

class Rtw89H2cBuilder {
public:
    Rtw89H2cBuilder() = default;
    Rtw89H2cBuilder(uint8_t cat, uint8_t cl, uint8_t func) {
        cmd_.category = cat;
        cmd_.cmd_class = cl;
        cmd_.function = func;
    }

    Rtw89H2cBuilder &category(uint8_t cat) { cmd_.category = cat; return *this; }
    Rtw89H2cBuilder &cmd_class(uint8_t cl) { cmd_.cmd_class = cl; return *this; }
    Rtw89H2cBuilder &function(uint8_t func) { cmd_.function = func; return *this; }
    Rtw89H2cBuilder &seq(uint8_t s) { cmd_.seq = s; return *this; }
    Rtw89H2cBuilder &req_ack(bool rack) { cmd_.req_ack = rack; return *this; }
    Rtw89H2cBuilder &done_ack(bool dack) { cmd_.done_ack = dack; return *this; }
    Rtw89H2cBuilder &payload(const void *data, uint32_t len) {
        cmd_.payload = static_cast<const uint8_t *>(data);
        cmd_.len = len;
        return *this;
    }

    struct sk_buff *build(struct rtw89_dev *rtwdev) const;
    const rtw89_h2c_cmd &cmd() const { return cmd_; }

private:
    rtw89_h2c_cmd cmd_{};
};

// ============================================================================
// 6. Declarations for Firmware Parsing, Downloading & Mailbox
// ============================================================================

// Integrity computation helpers
uint32_t rtw89_calc_crc32(const uint8_t *data, size_t len);
uint32_t rtw89_calc_checksum32(const uint8_t *data, size_t len);

// Multi-firmware (MFW) unpacker
int rtw89_mfw_recognize(struct rtw89_dev *rtwdev, uint8_t type,
                        struct rtw89_fw_suit *fw_suit,
                        const uint8_t *mfw_bin = nullptr, size_t mfw_len = 0);

// Firmware header parsing
int rtw89_fw_hdr_parser(struct rtw89_dev *rtwdev,
                        const struct rtw89_fw_suit *fw_suit,
                        struct rtw89_fw_bin_info *info);

int rtw89_fw_hdr_parser(struct rtw89_dev *rtwdev,
                        const uint8_t *fw, uint32_t len,
                        struct rtw89_fw_bin_info *info);

int rtw89_fw_hdr_parser(const uint8_t *fw, uint32_t len,
                        struct rtw89_fw_bin_info *info);

// H2C packet allocation & formatting
struct sk_buff *rtw89_fw_h2c_alloc_skb_no_hdr(struct rtw89_dev *rtwdev, uint32_t len);
struct sk_buff *rtw89_fw_h2c_alloc_skb_with_hdr(struct rtw89_dev *rtwdev, uint32_t len);

void rtw89_h2c_pkt_set_hdr(struct rtw89_dev *rtwdev, struct sk_buff *skb,
                           uint8_t type, uint8_t cat, uint8_t cmd_class, uint8_t func,
                           bool rack, bool dack, uint32_t len);

void rtw89_h2c_pkt_set_hdr_fwdl(struct rtw89_dev *rtwdev, struct sk_buff *skb,
                                uint8_t type, uint8_t cat, uint8_t cmd_class, uint8_t func,
                                uint32_t len);

// H2C transmit
int rtw89_h2c_tx(struct rtw89_dev *rtwdev, struct sk_buff *skb, bool fwdl);

// Readiness verification
int rtw89_fw_check_rdy(struct rtw89_dev *rtwdev, uint32_t timeout_us = 100000);
int rtw89_fw_check_rdy(Rtw89Mmio *mmio, uint32_t timeout_us = 100000);

// Firmware download main entry points
int rtw89_fw_download(struct rtw89_dev *rtwdev, uint8_t type = RTW89_FW_NORMAL, bool include_bb = false);
int rtw89_fw_download(struct rtw89_dev *rtwdev, const uint8_t *fw_bin, size_t fw_bin_len);
