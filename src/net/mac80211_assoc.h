//
// mac80211_assoc.h — 802.11 Authentication & Association State Machine
//
#pragma once

#include "linux_shim.h"
#include "mac80211_frame.h"
#include "mac80211_scan.h"
#include "skbuff.h"

#include <cstddef>
#include <cstdint>

// 802.11 Authentication Algorithm Numbers
#define WLAN_AUTH_OPEN                  0
#define WLAN_AUTH_SHARED_KEY            1
#define WLAN_AUTH_FT                    2
#define WLAN_AUTH_SAE                   3

// 802.11 Status Codes
#define WLAN_STATUS_SUCCESS             0
#define WLAN_STATUS_UNSPECIFIED_FAILURE 1
#define WLAN_STATUS_NOT_SUPPORTED_AUTH_ALG 13
#define WLAN_STATUS_AUTH_TIMEOUT        16
#define WLAN_STATUS_ASSOC_DENIED_UNSPEC 12
#define WLAN_STATUS_CAPS_UNSUPPORTED    10

// Assoc State Machine States
enum class AssocState {
    IDLE = 0,
    AUTHENTICATING,
    ASSOCIATING,
    ASSOCIATED,
    FAILED
};

// Internal error / status code
enum class AssocStatus {
    SUCCESS = 0,
    TIMEOUT = 1,
    AUTH_REJECTED = 2,
    ASSOC_REJECTED = 3,
    INVALID_FRAME = 4,
    BUSY = 5
};

// 802.11 Authentication Frame Body
struct ieee80211_auth_body {
    __le16 auth_alg;
    __le16 auth_transaction;
    __le16 status_code;
} __attribute__((packed));

// 802.11 Association Response Body Fixed Fields
struct ieee80211_assoc_resp_body {
    __le16 capability;
    __le16 status_code;
    __le16 aid;
} __attribute__((packed));

class AssocStateMachine {
public:
    static constexpr uint64_t TIMEOUT_MS = 3000; // 3-second timeout

    AssocStateMachine();
    ~AssocStateMachine() = default;

    // Begin association towards a target BSS
    int start_association(const uint8_t *sta_mac, const bss_entry &target_ap, uint64_t current_time_ms);

    // Generate outgoing frame for the current state (Auth Req or Assoc Req)
    struct sk_buff *generate_tx_frame();

    // Process incoming management frame from AP
    int process_rx_frame(const uint8_t *frame, size_t len, uint64_t current_time_ms);

    // Check timeout against current time
    void check_timeout(uint64_t current_time_ms);

    // State & status getters
    AssocState  state() const { return m_state; }
    AssocStatus last_status() const { return m_status; }
    uint16_t    aid() const { return m_aid; }
    const uint8_t *target_bssid() const { return m_target_ap.bssid; }

    void reset();

private:
    AssocState  m_state;
    AssocStatus m_status;
    uint8_t     m_sta_mac[6];
    bss_entry   m_target_ap;
    uint16_t    m_seq_num;
    uint16_t    m_aid;
    uint64_t    m_state_start_ms;
    bool        m_tx_pending;

    struct sk_buff *build_auth_req();
    struct sk_buff *build_assoc_req();
};
