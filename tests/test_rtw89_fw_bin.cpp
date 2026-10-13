//
// tests/test_rtw89_fw_bin.cpp
//
// Validation test for embedded Realtek RTL8852B firmware binary (rtw8852b_fw_bin).
// Verifies array accessibility, length requirements (> 200 KB), Realtek MFW signature,
// sub-firmware directory entries, and RTL8852 chip ID.
//
// Outputs evidence to .omo/evidence/rtw89-driver/task-3-fw-extraction.log and stdout.
//

#include "rtw8852b_fw_bin.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

#define RTW89_MFW_SIG 0xFF
#define RTL8852_CHIP_ID 0x8852

struct MfwInfoEntry {
    uint8_t cv;
    uint8_t type;
    uint8_t mp;
    uint8_t rsvd;
    uint32_t shift;
    uint32_t size;
};

static uint32_t read_le32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

static uint16_t read_le16(const uint8_t *p) {
    return static_cast<uint16_t>(p[0]) |
           (static_cast<uint16_t>(p[1]) << 8);
}

int main() {
    std::ostringstream log;
    log << "======================================================================\n";
    log << "  Realtek RTL8852B Embedded Firmware Verification (Task 3)\n";
    log << "======================================================================\n\n";

    // 1. Accessibility and non-empty check
    log << "[CHECK 1] Array pointer accessibility and address check:\n";
    const uint8_t *fw_ptr = rtw8852b_fw_bin;
    assert(fw_ptr != nullptr);
    log << "  - Pointer address: " << static_cast<const void *>(fw_ptr) << " (Valid)\n";
    log << "  - Status: PASS\n\n";

    // 2. Length check (> 200 KB requirement)
    log << "[CHECK 2] Firmware length check:\n";
    const size_t fw_len = rtw8852b_fw_bin_len;
    const size_t min_required = 200 * 1024;
    log << "  - Array length: " << fw_len << " bytes (" << (fw_len / 1024) << " KB)\n";
    log << "  - Required minimum: " << min_required << " bytes (200 KB)\n";
    assert(fw_len > min_required);
    log << "  - Status: PASS (length > 200 KB)\n\n";

    // 3. Realtek MFW Magic / Signature check
    log << "[CHECK 3] Realtek MFW Signature (RTW89_MFW_SIG):\n";
    uint8_t sig = rtw8852b_fw_bin[0];
    log << "  - Signature byte: 0x" << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<int>(sig) << std::dec << "\n";
    log << "  - Expected signature: 0x" << std::hex << std::setw(2) << std::setfill('0')
        << RTW89_MFW_SIG << std::dec << "\n";
    assert(sig == RTW89_MFW_SIG);
    log << "  - Status: PASS (valid RTW89_MFW_SIG)\n\n";

    // 4. Subfirmware count
    uint8_t fw_nr = rtw8852b_fw_bin[1];
    log << "[CHECK 4] Sub-firmware component count:\n";
    log << "  - Count: " << static_cast<int>(fw_nr) << " sub-firmwares\n";
    assert(fw_nr >= 1);
    log << "  - Status: PASS (fw_nr >= 1)\n\n";

    // 5. Parse and validate MFW Directory Entries
    log << "[CHECK 5] MFW Directory table validation:\n";
    size_t table_header_size = 16 + static_cast<size_t>(fw_nr) * 16;
    assert(table_header_size <= fw_len);

    std::vector<MfwInfoEntry> entries;
    for (uint8_t i = 0; i < fw_nr; ++i) {
        size_t entry_offset = 16 + static_cast<size_t>(i) * 16;
        const uint8_t *entry_ptr = &rtw8852b_fw_bin[entry_offset];

        MfwInfoEntry ent = {};
        ent.cv    = entry_ptr[0];
        ent.type  = entry_ptr[1];
        ent.mp    = entry_ptr[2];
        ent.rsvd  = entry_ptr[3];
        ent.shift = read_le32(&entry_ptr[4]);
        ent.size  = read_le32(&entry_ptr[8]);
        entries.push_back(ent);

        log << "  - SubFW " << static_cast<int>(i) << ": "
            << "cv=" << static_cast<int>(ent.cv) << ", "
            << "type=" << static_cast<int>(ent.type) << ", "
            << "shift=0x" << std::hex << ent.shift << ", "
            << "size=0x" << ent.size << std::dec << " (" << ent.size << " bytes)\n";

        assert(static_cast<size_t>(ent.shift) + static_cast<size_t>(ent.size) <= fw_len);
        assert(ent.size > 0);
    }
    log << "  - Status: PASS (all section shifts and sizes bounds-checked within binary)\n\n";

    // 6. Subfirmware 0 Header & Chip ID validation
    log << "[CHECK 6] Subfirmware 0 Header (RTL8852 chip ID verification):\n";
    const uint32_t sub0_shift = entries[0].shift;
    const uint8_t *sub0_ptr = &rtw8852b_fw_bin[sub0_shift];

    // Read word 0: [0]=0x03, [1]=0x01, [2..3]=chip_id
    uint16_t chip_id = read_le16(&sub0_ptr[2]);
    log << "  - Sub0 offset: 0x" << std::hex << sub0_shift << std::dec << "\n";
    log << "  - Detected Chip ID: 0x" << std::hex << chip_id << std::dec << "\n";
    log << "  - Expected Chip ID: 0x" << std::hex << RTL8852_CHIP_ID << std::dec << " (RTL8852B)\n";
    assert(chip_id == RTL8852_CHIP_ID);
    log << "  - Status: PASS (RTL8852 confirmed)\n\n";

    // 7. Extract build metadata
    uint8_t fw_major = sub0_ptr[4];
    uint8_t fw_minor = sub0_ptr[5];
    uint8_t fw_sub   = sub0_ptr[6];
    uint8_t fw_idx   = sub0_ptr[7];
    uint32_t commit_id = read_le32(&sub0_ptr[8]);

    uint8_t month  = sub0_ptr[16];
    uint8_t day    = sub0_ptr[17];
    uint8_t hour   = sub0_ptr[18];
    uint8_t minute = sub0_ptr[19];
    uint32_t year  = read_le32(&sub0_ptr[20]);

    uint8_t sec_num = sub0_ptr[25];

    log << "[METADATA] RTL8852B Firmware Details:\n";
    log << "  - Firmware Version: "
        << static_cast<int>(fw_major) << "."
        << static_cast<int>(fw_minor) << "."
        << static_cast<int>(fw_sub) << "."
        << static_cast<int>(fw_idx) << "\n";
    log << "  - Git Commit ID: 0x" << std::hex << commit_id << std::dec << "\n";
    log << "  - Build Timestamp: "
        << std::setw(4) << std::setfill('0') << year << "-"
        << std::setw(2) << std::setfill('0') << static_cast<int>(month) << "-"
        << std::setw(2) << std::setfill('0') << static_cast<int>(day) << " "
        << std::setw(2) << std::setfill('0') << static_cast<int>(hour) << ":"
        << std::setw(2) << std::setfill('0') << static_cast<int>(minute) << "\n";
    log << "  - Firmware Sections: " << static_cast<int>(sec_num) << "\n\n";

    log << "======================================================================\n";
    log << "  VERDICT: [PASS] All RTL8852B firmware tests passed successfully.\n";
    log << "======================================================================\n";

    // Print to stdout
    std::cout << log.str();

    // Write evidence to .omo/evidence/rtw89-driver/task-3-fw-extraction.log
    fs::path evidence_dir = ".omo/evidence/rtw89-driver";
    try {
        fs::create_directories(evidence_dir);
        fs::path log_path = evidence_dir / "task-3-fw-extraction.log";
        std::ofstream log_file(log_path);
        if (log_file.is_open()) {
            log_file << log.str();
            log_file.close();
            std::cout << "\n[EVIDENCE] Written to: " << log_path.string() << "\n";
        } else {
            std::cerr << "[-] Warning: Could not open evidence log file: " << log_path << "\n";
            return 1;
        }
    } catch (const std::exception &e) {
        std::cerr << "[-] Error writing evidence file: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
