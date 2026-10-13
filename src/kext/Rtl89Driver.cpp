//
// Rtl89Driver.cpp — Native macOS IOEthernetController Driver for Realtek RTL8852BE PCIe
// Targeting macOS Sonoma (14.x) and Tahoe (15.x+)
//
#include "Rtl89Driver.h"
#include "Rtl89UserClient.h"
#include "rtw8852b_fw_bin.h"

#ifdef super
#undef super
#endif
#define super IOEthernetController

OSDefineMetaClassAndStructors(Rtl89Driver, IOEthernetController)

bool Rtl89Driver::init(OSDictionary *properties) {
    if (!super::init(properties)) {
        return false;
    }
    m_pciDevice = nullptr;
    m_bar0Map = nullptr;
    m_mmioBase = nullptr;
    m_mmioSize = 0;
    m_netif = nullptr;
    m_hardwareReady = false;
    m_connected = false;
    memset(&m_macAddr, 0, sizeof(m_macAddr));
    memset(m_connectedSsid, 0, sizeof(m_connectedSsid));
    memset(m_connectedBssid, 0, sizeof(m_connectedBssid));
    return true;
}

IOService *Rtl89Driver::probe(IOService *provider, SInt32 *score) {
    if (!super::probe(provider, score)) {
        return nullptr;
    }

    IOPCIDevice *pci = OSDynamicCast(IOPCIDevice, provider);
    if (!pci) {
        return nullptr;
    }

    UInt16 vendorId = pci->configRead16(kIOPCIConfigVendorID);
    UInt16 deviceId = pci->configRead16(kIOPCIConfigDeviceID);

    // RTL8852BE WiFi 6 PCIe: 10EC:B852
    if (vendorId == 0x10EC && deviceId == 0xB852) {
        if (score) *score = 1000;
        return this;
    }

    return nullptr;
}

bool Rtl89Driver::start(IOService *provider) {
    if (!super::start(provider)) {
        return false;
    }

    m_pciDevice = OSDynamicCast(IOPCIDevice, provider);
    if (!m_pciDevice) {
        return false;
    }

    // 1. Enable PCI Bus Master and Memory Space
    m_pciDevice->setBusMasterEnable(true);
    m_pciDevice->setMemoryEnable(true);

    // 2. Map PCIe BAR0 MMIO Registers
    m_bar0Map = m_pciDevice->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress0);
    if (!m_bar0Map) {
        return false;
    }

    m_mmioBase = reinterpret_cast<uint8_t *>(m_bar0Map->getVirtualAddress());
    m_mmioSize = m_bar0Map->getLength();
    m_mmio.configure(m_mmioBase, m_mmioSize);

    // 3. Disable PCIe ASPM L1 / L1SS on AMD Phoenix / Hawk Point Root Complex
    rtw89_pci_init(&m_pciInfo, &m_mmio);
    rtw89_pci_disable_aspm_l1_l1ss(&m_pciInfo);

    // 4. Power on RTL8852BE to D0 state
    rtw89_pci_set_power_state(&m_pciInfo, RTW89_POWER_D0);

    // 5. Download Embedded Firmware
    rtw89_dev dev = {};
    dev.mmio = &m_mmio;
    rtw89_fw_download(&dev, rtw8852b_fw_bin, rtw8852b_fw_bin_len);

    // 6. Read Permanent MAC Address
    readMacAddress();

    // 7. Initialize RF and Baseband
    rtw89_phy_init(m_mmio);

    // 8. Initialize Rings & Interrupt dispatcher
    rtw89_pci_init_tx_ring(&m_mmio, &m_txRing, 0);
    rtw89_pci_init_rx_ring(&m_mmio, &m_rxRing, 0);
    rtw89_pci_irq_init(&m_irqInfo, &m_mmio, true);

    // 9. Setup Medium Tables
    if (!createMediumTables()) {
        m_bar0Map->release();
        m_bar0Map = nullptr;
        return false;
    }

    // 10. Instantiate and Attach IOEthernetInterface
    if (!attachInterface(reinterpret_cast<IONetworkInterface **>(&m_netif), true)) {
        m_bar0Map->release();
        m_bar0Map = nullptr;
        return false;
    }

    m_netif->registerService();
    registerService();

    m_hardwareReady = true;
    return true;
}

void Rtl89Driver::stop(IOService *provider) {
    m_hardwareReady = false;

    rtw89_pci_irq_disable(&m_irqInfo);
    rtw89_pci_free_tx_ring(&m_txRing);
    rtw89_pci_free_rx_ring(&m_rxRing);

    if (m_netif) {
        detachInterface(m_netif);
        m_netif = nullptr;
    }

    if (m_bar0Map) {
        m_bar0Map->release();
        m_bar0Map = nullptr;
        m_mmioBase = nullptr;
        m_mmioSize = 0;
    }

    super::stop(provider);
}

void Rtl89Driver::free() {
    super::free();
}

void Rtl89Driver::readMacAddress() {
    m_macAddr.bytes[0] = m_mmio.read8(R_AX_MAC_ADDR_0);
    m_macAddr.bytes[1] = m_mmio.read8(R_AX_MAC_ADDR_1);
    m_macAddr.bytes[2] = m_mmio.read8(R_AX_MAC_ADDR_2);
    m_macAddr.bytes[3] = m_mmio.read8(R_AX_MAC_ADDR_3);
    m_macAddr.bytes[4] = m_mmio.read8(R_AX_MAC_ADDR_4);
    m_macAddr.bytes[5] = m_mmio.read8(R_AX_MAC_ADDR_5);

    if (m_macAddr.bytes[0] == 0x00 && m_macAddr.bytes[1] == 0x00 && m_macAddr.bytes[2] == 0x00) {
        m_macAddr.bytes[0] = 0x00;
        m_macAddr.bytes[1] = 0xE0;
        m_macAddr.bytes[2] = 0x4C;
        m_macAddr.bytes[3] = 0x88;
        m_macAddr.bytes[4] = 0x52;
        m_macAddr.bytes[5] = 0xBE;
    }
}

bool Rtl89Driver::createMediumTables() {
    OSDictionary *mediumDict = OSDictionary::withCapacity(1);
    if (!mediumDict) return false;

    IONetworkMedium *medium = IONetworkMedium::medium(
        kIOMediumEthernetAuto, 1000 * 1000000ULL, 0, 0, "Auto");
    if (medium) {
        IONetworkMedium::addMedium(mediumDict, medium);
        medium->release();
    }

    bool ok = publishMediumDictionary(mediumDict);
    if (ok) {
        setSelectedMedium(medium);
        setLinkStatus(kIONetworkLinkValid | kIONetworkLinkActive, medium, 1000 * 1000000ULL);
    }
    mediumDict->release();
    return ok;
}

IOReturn Rtl89Driver::enable(IONetworkInterface *netif) {
    (void)netif;
    if (!m_hardwareReady) return kIOReturnNotReady;
    rtw89_pci_irq_enable(&m_irqInfo);
    return kIOReturnSuccess;
}

IOReturn Rtl89Driver::disable(IONetworkInterface *netif) {
    (void)netif;
    rtw89_pci_irq_disable(&m_irqInfo);
    return kIOReturnSuccess;
}

UInt32 Rtl89Driver::outputPacket(mbuf_t m, void *param) {
    (void)param;
    if (!m || !m_hardwareReady) {
        if (m) freePacket(m);
        return kIOReturnOutputDropped;
    }

    if (!m_connected) {
        freePacket(m);
        return kIOReturnOutputSuccess;
    }

    int ret = rtw89_tx_xmit_mbuf(&m_mmio, &m_txRing, m, 0);
    freePacket(m);

    return (ret == 0) ? kIOReturnOutputSuccess : kIOReturnOutputDropped;
}

IOReturn Rtl89Driver::getHardwareAddress(IOEthernetAddress *addr) {
    if (!addr) return kIOReturnBadArgument;
    memcpy(addr, &m_macAddr, sizeof(IOEthernetAddress));
    return kIOReturnSuccess;
}

IOReturn Rtl89Driver::setPromiscuousMode(bool active) {
    (void)active;
    return kIOReturnSuccess;
}

IOReturn Rtl89Driver::setMulticastMode(bool active) {
    (void)active;
    return kIOReturnSuccess;
}

IOReturn Rtl89Driver::newUserClient(task_t owningTask, void *securityID, UInt32 type,
                                    OSDictionary *properties, IOUserClient **handler) {
    (void)securityID;
    (void)type;
    (void)properties;

    if (!handler) return kIOReturnBadArgument;

    Rtl89UserClient *client = Rtl89UserClient::withTask(owningTask);
    if (!client) return kIOReturnNoMemory;

    if (!client->attach(this) || !client->start(this)) {
        client->detach(this);
        client->release();
        return kIOReturnError;
    }

    *handler = client;
    return kIOReturnSuccess;
}

int Rtl89Driver::triggerScan(const uint8_t *channels, size_t num_channels) {
    ScanConfig cfg = {};
    cfg.channels = channels;
    cfg.num_channels = num_channels;
    cfg.dwell_time_ms = 50;
    cfg.passive = true;
    return m_scanEngine.start_scan(cfg);
}

int Rtl89Driver::connectNetwork(const char *ssid, const char *passphrase) {
    const bss_entry *target = m_bssCache.find_by_ssid(ssid);
    if (!target) return -ENOENT;

    memcpy(m_connectedBssid, target->bssid, 6);
    snprintf(m_connectedSsid, sizeof(m_connectedSsid), "%s", ssid);
    m_currentChannel = target->channel;

    m_assocFsm.start_association(m_macAddr.bytes, *target, 1000);
    m_wpa2Engine.init(m_macAddr.bytes, target->bssid, passphrase, ssid);

    m_connected = true;
    return 0;
}

int Rtl89Driver::disconnectNetwork() {
    m_connected = false;
    memset(m_connectedSsid, 0, sizeof(m_connectedSsid));
    memset(m_connectedBssid, 0, sizeof(m_connectedBssid));
    m_assocFsm.reset();
    m_wpa2Engine.reset();
    return 0;
}
