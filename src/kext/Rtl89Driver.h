//
// Rtl89Driver.h — Native macOS IOEthernetController Driver for Realtek RTL8852BE PCIe
// Targeting macOS Sonoma (14.x) and Tahoe (15.x+)
//
#pragma once

#include <IOKit/network/IOEthernetController.h>
#include <IOKit/network/IOEthernetInterface.h>
#include <IOKit/network/IONetworkMedium.h>
#include <IOKit/network/IOOutputQueue.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOUserClient.h>

#include "linux_shim.h"
#include "mac80211_assoc.h"
#include "mac80211_frame.h"
#include "mac80211_scan.h"
#include "rtw8852be_fw.h"
#include "rtw8852be_mmio.h"
#include "rtw8852be_pci.h"
#include "rtw8852be_phy.h"
#include "rtw8852be_reg.h"
#include "rtw89_dma.h"
#include "rtw89_interrupt.h"
#include "rtw89_rings.h"
#include "rtw89_tx.h"
#include "wpa2_psk.h"

class Rtl89Driver : public IOEthernetController {
    OSDeclareDefaultStructors(Rtl89Driver)
    using super = IOEthernetController;

private:
    IOPCIDevice               *m_pciDevice{nullptr};
    IOMemoryMap               *m_bar0Map{nullptr};
    uint8_t                   *m_mmioBase{nullptr};
    uint64_t                   m_mmioSize{0};
    IOEthernetInterface       *m_netif{nullptr};
    IOEthernetAddress          m_macAddr{};

    class KextMmioBus final : public Rtw89Mmio {
    private:
        volatile uint8_t *m_base{nullptr};
        size_t            m_size{0};

    public:
        KextMmioBus() = default;
        ~KextMmioBus() override = default;

        void configure(void *vbase, size_t size) {
            m_base = static_cast<volatile uint8_t *>(vbase);
            m_size = size;
        }

        uint8_t read8(uint32_t offset) override {
            if (!m_base || offset >= m_size) return 0xFF;
            return m_base[offset];
        }
        uint16_t read16(uint32_t offset) override {
            if (!m_base || offset + 2 > m_size) return 0xFFFF;
            return *reinterpret_cast<volatile uint16_t *>(&m_base[offset]);
        }
        uint32_t read32(uint32_t offset) override {
            if (!m_base || offset + 4 > m_size) return 0xFFFFFFFF;
            return *reinterpret_cast<volatile uint32_t *>(&m_base[offset]);
        }
        void write8(uint32_t offset, uint8_t val) override {
            if (!m_base || offset >= m_size) return;
            m_base[offset] = val;
        }
        void write16(uint32_t offset, uint16_t val) override {
            if (!m_base || offset + 2 > m_size) return;
            *reinterpret_cast<volatile uint16_t *>(&m_base[offset]) = val;
        }
        void write32(uint32_t offset, uint32_t val) override {
            if (!m_base || offset + 4 > m_size) return;
            *reinterpret_cast<volatile uint32_t *>(&m_base[offset]) = val;
        }

        void operator delete(void *, size_t) {}
        void operator delete(void *) {}
    };

    KextMmioBus         m_mmio{};
    rtw89_pci_info      m_pciInfo{};
    rtw89_tx_ring       m_txRing{};
    rtw89_rx_ring       m_rxRing{};
    rtw89_irq_info      m_irqInfo{};
    BssCache            m_bssCache{};
    ScanEngine          m_scanEngine{m_bssCache};
    AssocStateMachine   m_assocFsm{};
    Wpa2HandshakeEngine m_wpa2Engine{};

    bool                m_hardwareReady{false};
    bool                m_connected{false};
    char                m_connectedSsid[33]{0};
    uint8_t             m_connectedBssid[6]{0};
    uint8_t             m_currentChannel{36};

    bool createMediumTables();
    void readMacAddress();

public:
    bool init(OSDictionary *properties = nullptr) override;
    IOService *probe(IOService *provider, SInt32 *score) override;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    void free() override;

    // IOEthernetController overrides
    IOReturn enable(IONetworkInterface *netif) override;
    IOReturn disable(IONetworkInterface *netif) override;
    UInt32   outputPacket(mbuf_t m, void *param) override;
    IOReturn getHardwareAddress(IOEthernetAddress *addr) override;
    IOReturn setPromiscuousMode(bool active) override;
    IOReturn setMulticastMode(bool active) override;

    // UserClient creation
    IOReturn newUserClient(task_t owningTask, void *securityID, UInt32 type,
                           OSDictionary *properties, IOUserClient **handler) override;

    // Accessors for UserClient
    BssCache            &bssCache() { return m_bssCache; }
    ScanEngine          &scanEngine() { return m_scanEngine; }
    AssocStateMachine   &assocFsm() { return m_assocFsm; }
    Wpa2HandshakeEngine &wpa2Engine() { return m_wpa2Engine; }
    bool                 isConnected() const { return m_connected; }
    const char          *connectedSsid() const { return m_connectedSsid; }
    const uint8_t       *connectedBssid() const { return m_connectedBssid; }
    uint8_t              currentChannel() const { return m_currentChannel; }
    const IOEthernetAddress &macAddress() const { return m_macAddr; }

    int triggerScan(const uint8_t *channels, size_t num_channels);
    int connectNetwork(const char *ssid, const char *passphrase);
    int disconnectNetwork();

    void operator delete(void *, size_t) {}
    void operator delete(void *) {}
};
