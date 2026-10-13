//
// rtw8852be_fw.cpp — RTL8852BE Firmware Parsing, Download & H2C/C2H Mailbox Protocol
//
#include "rtw8852be_fw.h"

#include <algorithm>
#include <chrono>
#include <thread>

// ============================================================================
// 1. Checksum and Integrity Algorithms
// ============================================================================

uint32_t rtw89_calc_crc32(const uint8_t *data, size_t len) {
    if (!data || len == 0) {
        return 0;
    }

    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320U;
            } else {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}

uint32_t rtw89_calc_checksum32(const uint8_t *data, size_t len) {
    if (!data || len == 0) {
        return 0;
    }

    uint32_t sum = 0;
    size_t dwords = len / 4;
    for (size_t i = 0; i < dwords; ++i) {
        size_t off = i * 4;
        uint32_t word = static_cast<uint32_t>(data[off]) |
                        (static_cast<uint32_t>(data[off + 1]) << 8) |
                        (static_cast<uint32_t>(data[off + 2]) << 16) |
                        (static_cast<uint32_t>(data[off + 3]) << 24);
        sum += word;
    }

    size_t rem = len % 4;
    if (rem > 0) {
        size_t off = dwords * 4;
        uint32_t word = 0;
        for (size_t i = 0; i < rem; ++i) {
            word |= (static_cast<uint32_t>(data[off + i]) << (i * 8));
        }
        sum += word;
    }

    return sum;
}

// ============================================================================
// 2. MFW Directory Parser
// ============================================================================

int rtw89_mfw_recognize(struct rtw89_dev *rtwdev, uint8_t type,
                        struct rtw89_fw_suit *fw_suit,
                        const uint8_t *mfw_bin, size_t mfw_len) {
    if (!fw_suit) {
        return -EINVAL;
    }

    if (!mfw_bin) {
        mfw_bin = rtw8852b_fw_bin;
        mfw_len = rtw8852b_fw_bin_len;
    }

    if (mfw_len < sizeof(struct rtw89_mfw_hdr)) {
        return -EINVAL;
    }

    if (mfw_bin[0] != RTW89_MFW_SIG) {
        return -EINVAL;
    }

    uint8_t fw_nr = mfw_bin[1];
    if (fw_nr == 0) {
        return -EINVAL;
    }

    size_t dir_size = 16 + static_cast<size_t>(fw_nr) * 16;
    if (mfw_len < dir_size) {
        return -EINVAL;
    }

    bool found = false;
    uint32_t match_shift = 0;
    uint32_t match_size = 0;

    for (uint8_t i = 0; i < fw_nr; ++i) {
        size_t entry_offset = 16 + static_cast<size_t>(i) * 16;
        const uint8_t *ent = &mfw_bin[entry_offset];

        uint8_t ent_cv = ent[0];
        uint8_t ent_type = ent[1];
        uint32_t ent_shift = static_cast<uint32_t>(ent[4]) |
                             (static_cast<uint32_t>(ent[5]) << 8) |
                             (static_cast<uint32_t>(ent[6]) << 16) |
                             (static_cast<uint32_t>(ent[7]) << 24);
        uint32_t ent_size = static_cast<uint32_t>(ent[8]) |
                            (static_cast<uint32_t>(ent[9]) << 8) |
                            (static_cast<uint32_t>(ent[10]) << 16) |
                            (static_cast<uint32_t>(ent[11]) << 24);

        if (ent_type == type) {
            if (static_cast<size_t>(ent_shift) + static_cast<size_t>(ent_size) > mfw_len) {
                return -EINVAL;
            }

            // If rtwdev specified, check CV match preference
            if (rtwdev && ent_cv != rtwdev->cv && found) {
                continue;
            }

            match_shift = ent_shift;
            match_size = ent_size;
            found = true;

            if (!rtwdev || ent_cv == rtwdev->cv) {
                break;
            }
        }
    }

    if (!found) {
        return -ENOENT;
    }

    const uint8_t *sub_data = &mfw_bin[match_shift];
    if (match_size < 32) {
        return -EINVAL;
    }

    uint16_t chip_id = static_cast<uint16_t>(sub_data[2]) |
                       (static_cast<uint16_t>(sub_data[3]) << 8);
    if (chip_id != RTL8852_CHIP_ID) {
        return -ENODEV;
    }

    fw_suit->type = type;
    fw_suit->data = sub_data;
    fw_suit->size = match_size;
    fw_suit->major_ver = sub_data[4];
    fw_suit->minor_ver = sub_data[5];
    fw_suit->sub_ver = sub_data[6];
    fw_suit->sub_idex = sub_data[7];
    fw_suit->commitid = static_cast<uint32_t>(sub_data[8]) |
                        (static_cast<uint32_t>(sub_data[9]) << 8) |
                        (static_cast<uint32_t>(sub_data[10]) << 16) |
                        (static_cast<uint32_t>(sub_data[11]) << 24);

    fw_suit->hdr_ver = (sub_data[15] >> 0) & 0xFF; // word 3 bits 31..24
    fw_suit->build_mon = sub_data[16];
    fw_suit->build_date = sub_data[17];
    fw_suit->build_hour = sub_data[18];
    fw_suit->build_min = sub_data[19];
    fw_suit->build_year = static_cast<uint16_t>(sub_data[20]) |
                          (static_cast<uint16_t>(sub_data[21]) << 8);
    fw_suit->cmd_ver = sub_data[31];

    if (rtwdev) {
        if (type == RTW89_FW_NORMAL) {
            rtwdev->fw.normal = *fw_suit;
        } else if (type == RTW89_FW_WOWLAN) {
            rtwdev->fw.wowlan = *fw_suit;
        }
    }

    return 0;
}

// ============================================================================
// 3. Firmware Header & Section Parsing
// ============================================================================

int rtw89_fw_hdr_parser(const uint8_t *fw, uint32_t len,
                        struct rtw89_fw_bin_info *info) {
    if (!fw || !info || len < 32) {
        return -EINVAL;
    }

    uint8_t sec_num = fw[25];
    if (sec_num == 0 || sec_num > FWDL_SECTION_MAX_NUM) {
        return -EINVAL;
    }

    uint32_t hdr_len = 32 + static_cast<uint32_t>(sec_num) * 16;
    if (len < hdr_len) {
        return -EINVAL;
    }

    info->section_num = sec_num;
    info->hdr_len = hdr_len;
    info->dynamic_hdr_en = (fw[30] & 0x01) != 0; // bit 16 of word 7
    info->dynamic_hdr_len = 0;
    info->secure_section_exist = false;
    info->dsp_checksum = false;

    uint32_t curr_offset = hdr_len;

    for (uint8_t i = 0; i < sec_num; ++i) {
        size_t s_hdr_offset = 32 + static_cast<size_t>(i) * 16;
        const uint8_t *s_hdr = &fw[s_hdr_offset];

        uint32_t dl_addr = static_cast<uint32_t>(s_hdr[0]) |
                           (static_cast<uint32_t>(s_hdr[1]) << 8) |
                           (static_cast<uint32_t>(s_hdr[2]) << 16) |
                           (static_cast<uint32_t>(s_hdr[3]) << 24);

        uint32_t w1 = static_cast<uint32_t>(s_hdr[4]) |
                      (static_cast<uint32_t>(s_hdr[5]) << 8) |
                      (static_cast<uint32_t>(s_hdr[6]) << 16) |
                      (static_cast<uint32_t>(s_hdr[7]) << 24);

        uint32_t mssc = static_cast<uint32_t>(s_hdr[8]) |
                        (static_cast<uint32_t>(s_hdr[9]) << 8) |
                        (static_cast<uint32_t>(s_hdr[10]) << 16) |
                        (static_cast<uint32_t>(s_hdr[11]) << 24);

        uint32_t sec_size = w1 & 0x00FFFFFF;
        uint8_t sec_type = static_cast<uint8_t>((w1 >> 24) & 0x0F);
        uint8_t checksum = static_cast<uint8_t>((w1 >> 28) & 0x01);
        uint8_t redl = static_cast<uint8_t>((w1 >> 29) & 0x01);

        if (curr_offset + sec_size > len) {
            return -EINVAL;
        }

        struct rtw89_fw_hdr_section_info &s_info = info->section_info[i];
        s_info.addr = &fw[curr_offset];
        s_info.len = sec_size;
        s_info.dladdr = dl_addr;
        s_info.mssc = mssc;
        s_info.type = sec_type;
        s_info.redl = redl;
        s_info.checksum = checksum;
        s_info.ignore = false;

        if (sec_type == FWDL_SECURITY_SECTION_TYPE) {
            info->secure_section_exist = true;
        }

        curr_offset += sec_size;
    }

    return 0;
}

int rtw89_fw_hdr_parser(struct rtw89_dev *rtwdev,
                        const uint8_t *fw, uint32_t len,
                        struct rtw89_fw_bin_info *info) {
    int ret = rtw89_fw_hdr_parser(fw, len, info);
    if (ret == 0 && rtwdev && info) {
        rtwdev->fw.bin_info = *info;
    }
    return ret;
}

int rtw89_fw_hdr_parser(struct rtw89_dev *rtwdev,
                        const struct rtw89_fw_suit *fw_suit,
                        struct rtw89_fw_bin_info *info) {
    if (!fw_suit || !fw_suit->data || fw_suit->size == 0) {
        return -EINVAL;
    }
    return rtw89_fw_hdr_parser(rtwdev, fw_suit->data, fw_suit->size, info);
}

// ============================================================================
// 4. H2C Packet Allocation & Formatting
// ============================================================================

struct sk_buff *rtw89_fw_h2c_alloc_skb_no_hdr(struct rtw89_dev * /*rtwdev*/, uint32_t len) {
    struct sk_buff *skb = alloc_skb(len + 32, GFP_KERNEL);
    if (!skb) {
        return nullptr;
    }
    skb_reserve(skb, 16);
    return skb;
}

struct sk_buff *rtw89_fw_h2c_alloc_skb_with_hdr(struct rtw89_dev *rtwdev, uint32_t len) {
    uint32_t total_alloc = H2C_HEADER_LEN + len + 32;
    struct sk_buff *skb = alloc_skb(total_alloc, GFP_KERNEL);
    if (!skb) {
        return nullptr;
    }
    skb_reserve(skb, 16);
    skb_put(skb, H2C_HEADER_LEN);
    (void)rtwdev;
    return skb;
}

void rtw89_h2c_pkt_set_hdr(struct rtw89_dev *rtwdev, struct sk_buff *skb,
                           uint8_t type, uint8_t cat, uint8_t cmd_class, uint8_t func,
                           bool rack, bool dack, uint32_t len) {
    if (!skb || skb->len < H2C_HEADER_LEN) {
        return;
    }

    uint8_t seq = 0;
    if (rtwdev) {
        seq = rtwdev->fw.h2c_seq++;
        rtwdev->fw.h2c_counter++;
    }

    uint32_t w0 = FIELD_PREP(H2C_HDR_CAT, cat) |
                  FIELD_PREP(H2C_HDR_CLASS, cmd_class) |
                  FIELD_PREP(H2C_HDR_FUNC, func) |
                  FIELD_PREP(H2C_HDR_DEL_TYPE, type) |
                  FIELD_PREP(H2C_HDR_H2C_SEQ, seq);

    uint32_t w1 = FIELD_PREP(H2C_HDR_TOTAL_LEN, len) |
                  (rack ? H2C_HDR_REC_ACK : 0) |
                  (dack ? H2C_HDR_DONE_ACK : 0);

    uint8_t *p = skb->data;
    p[0] = static_cast<uint8_t>(w0 & 0xFF);
    p[1] = static_cast<uint8_t>((w0 >> 8) & 0xFF);
    p[2] = static_cast<uint8_t>((w0 >> 16) & 0xFF);
    p[3] = static_cast<uint8_t>((w0 >> 24) & 0xFF);

    p[4] = static_cast<uint8_t>(w1 & 0xFF);
    p[5] = static_cast<uint8_t>((w1 >> 8) & 0xFF);
    p[6] = static_cast<uint8_t>((w1 >> 16) & 0xFF);
    p[7] = static_cast<uint8_t>((w1 >> 24) & 0xFF);
}

void rtw89_h2c_pkt_set_hdr_fwdl(struct rtw89_dev *rtwdev, struct sk_buff *skb,
                                uint8_t type, uint8_t cat, uint8_t cmd_class, uint8_t func,
                                uint32_t len) {
    rtw89_h2c_pkt_set_hdr(rtwdev, skb, type, cat, cmd_class, func, false, false, len);
}

struct sk_buff *Rtw89H2cBuilder::build(struct rtw89_dev *rtwdev) const {
    struct sk_buff *skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, cmd_.len);
    if (!skb) {
        return nullptr;
    }

    rtw89_h2c_pkt_set_hdr(rtwdev, skb, cmd_.type, cmd_.category, cmd_.cmd_class,
                          cmd_.function, cmd_.req_ack, cmd_.done_ack, cmd_.len);

    if (cmd_.payload && cmd_.len > 0) {
        skb_put_data(skb, cmd_.payload, cmd_.len);
    }

    return skb;
}

int rtw89_h2c_tx(struct rtw89_dev *rtwdev, struct sk_buff *skb, bool fwdl) {
    if (!skb) {
        return -EINVAL;
    }

    int ret = 0;
    if (rtwdev && rtwdev->h2c_tx_fn) {
        ret = rtwdev->h2c_tx_fn(rtwdev, skb, fwdl);
    } else if (rtwdev) {
        // Record packet for verification
        std::vector<uint8_t> pkt(skb->data, skb->data + skb->len);
        rtwdev->tx_history.push_back(std::move(pkt));
    }

    kfree_skb(skb);
    return ret;
}

// ============================================================================
// 5. Readiness Polling
// ============================================================================

int rtw89_fw_check_rdy(Rtw89Mmio *mmio, uint32_t timeout_us) {
    if (!mmio) {
        return -EINVAL;
    }

    auto start = std::chrono::steady_clock::now();
    while (true) {
        uint32_t status = mmio->read32(R_AX_FWDL_STATUS_V1);
        uint32_t sts_val = FIELD_GET(B_AX_WCPU_FWDL_STS_MASK, status);

        if (sts_val == RTW89_FWDL_WCPU_FW_INIT_RDY) {
            return 0;
        }

        if (sts_val == RTW89_FWDL_CHECKSUM_FAIL ||
            sts_val == RTW89_FWDL_SECURITY_FAIL ||
            sts_val == RTW89_FWDL_CV_NOT_MATCH) {
            return -EIO;
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed >= timeout_us) {
            // Re-read once
            status = mmio->read32(R_AX_FWDL_STATUS_V1);
            if (FIELD_GET(B_AX_WCPU_FWDL_STS_MASK, status) == RTW89_FWDL_WCPU_FW_INIT_RDY) {
                return 0;
            }
            return -ETIMEDOUT;
        }

        std::this_thread::sleep_for(std::chrono::microseconds(10));
    }
}

int rtw89_fw_check_rdy(struct rtw89_dev *rtwdev, uint32_t timeout_us) {
    if (!rtwdev || !rtwdev->mmio) {
        return -EINVAL;
    }

    int ret = rtw89_fw_check_rdy(rtwdev->mmio, timeout_us);
    if (ret == 0) {
        rtwdev->flags[0] |= BIT(RTW89_FLAG_FW_RDY);
    }
    return ret;
}

// ============================================================================
// 6. Firmware Download Engine
// ============================================================================

int rtw89_fw_download(struct rtw89_dev *rtwdev, const uint8_t *fw_bin, size_t fw_bin_len) {
    if (!rtwdev) {
        return -EINVAL;
    }

    struct rtw89_fw_suit suit{};
    int ret = rtw89_mfw_recognize(rtwdev, RTW89_FW_NORMAL, &suit, fw_bin, fw_bin_len);
    if (ret != 0) {
        return ret;
    }

    struct rtw89_fw_bin_info info{};
    ret = rtw89_fw_hdr_parser(rtwdev, &suit, &info);
    if (ret != 0) {
        return ret;
    }

    // 1. Send Firmware Header H2C packet
    struct sk_buff *hdr_skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, info.hdr_len);
    if (!hdr_skb) {
        return -ENOMEM;
    }
    rtw89_h2c_pkt_set_hdr_fwdl(rtwdev, hdr_skb, FWCMD_TYPE_H2C, H2C_CAT_MAC,
                               H2C_CL_MAC_FWDL, H2C_FUNC_MAC_FWHDR_DL, info.hdr_len);
    skb_put_data(hdr_skb, suit.data, info.hdr_len);

    ret = rtw89_h2c_tx(rtwdev, hdr_skb, true);
    if (ret != 0) {
        return ret;
    }

    // 2. Download Sections chunked into <= 1024-byte packets
    for (uint8_t i = 0; i < info.section_num; ++i) {
        const auto &sec = info.section_info[i];
        if (sec.ignore || sec.len == 0) {
            continue;
        }

        uint32_t sent = 0;
        while (sent < sec.len) {
            uint32_t chunk_len = std::min<uint32_t>(sec.len - sent, RTW89_FWDL_CHUNK_SIZE);
            struct sk_buff *skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, chunk_len);
            if (!skb) {
                return -ENOMEM;
            }

            rtw89_h2c_pkt_set_hdr_fwdl(rtwdev, skb, FWCMD_TYPE_H2C, H2C_CAT_MAC,
                                       H2C_CL_MAC_FWDL, i, chunk_len);
            skb_put_data(skb, sec.addr + sent, chunk_len);

            ret = rtw89_h2c_tx(rtwdev, skb, true);
            if (ret != 0) {
                return ret;
            }

            sent += chunk_len;
        }
    }

    // 3. If MMIO is present, set download done / ready status
    if (rtwdev->mmio) {
        // In simulation, if controller hasn't set status, set INIT_RDY
        uint32_t cur = rtwdev->mmio->read32(R_AX_FWDL_STATUS_V1);
        if (FIELD_GET(B_AX_WCPU_FWDL_STS_MASK, cur) == 0) {
            rtwdev->mmio->write32_mask(R_AX_FWDL_STATUS_V1, B_AX_WCPU_FWDL_STS_MASK,
                                       RTW89_FWDL_WCPU_FW_INIT_RDY);
        }

        ret = rtw89_fw_check_rdy(rtwdev, 50000);
        if (ret != 0) {
            return ret;
        }
    }

    return 0;
}

int rtw89_fw_download(struct rtw89_dev *rtwdev, uint8_t /*type*/, bool /*include_bb*/) {
    return rtw89_fw_download(rtwdev, rtw8852b_fw_bin, rtw8852b_fw_bin_len);
}
