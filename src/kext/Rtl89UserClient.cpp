//
// Rtl89UserClient.cpp — HeliPort UserClient Interface for Rtl89Driver
// Targeting macOS Sonoma (14.x) and Tahoe (15.x+)
//
#include "Rtl89UserClient.h"
#include "Rtl89Driver.h"

#define super IOUserClient

OSDefineMetaClassAndStructors(Rtl89UserClient, IOUserClient)

Rtl89UserClient *Rtl89UserClient::withTask(task_t owningTask) {
    Rtl89UserClient *client = new Rtl89UserClient;
    if (client && !client->init()) {
        client->release();
        return nullptr;
    }
    if (client) {
        client->m_task = owningTask;
    }
    return client;
}

bool Rtl89UserClient::start(IOService *provider) {
    if (!super::start(provider)) {
        return false;
    }

    m_driver = OSDynamicCast(Rtl89Driver, provider);
    if (!m_driver) {
        return false;
    }

    return true;
}

void Rtl89UserClient::stop(IOService *provider) {
    m_driver = nullptr;
    super::stop(provider);
}

IOReturn Rtl89UserClient::clientClose() {
    terminate();
    return kIOReturnSuccess;
}

IOReturn Rtl89UserClient::externalMethod(uint32_t selector, IOExternalMethodArguments *arguments,
                                        IOExternalMethodDispatch *dispatch,
                                        OSObject *target, void *reference) {
    (void)dispatch;
    (void)target;
    (void)reference;

    if (!m_driver) {
        return kIOReturnNotAttached;
    }

    switch (selector) {
        case kRtl89MethodOpen:
            return kIOReturnSuccess;

        case kRtl89MethodClose:
            return clientClose();

        case kRtl89MethodScan: {
            const uint8_t *channels = nullptr;
            size_t num_channels = 0;
            if (arguments && arguments->structureInput && arguments->structureInputSize > 0) {
                channels = reinterpret_cast<const uint8_t *>(arguments->structureInput);
                num_channels = arguments->structureInputSize;
            } else {
                static const uint8_t default_channels[] = {1, 6, 11, 36, 40, 44, 48};
                channels = default_channels;
                num_channels = sizeof(default_channels);
            }
            int ret = m_driver->triggerScan(channels, num_channels);
            return (ret == 0) ? kIOReturnSuccess : kIOReturnError;
        }

        case kRtl89MethodGetScanResults: {
            if (!arguments || !arguments->structureOutput || arguments->structureOutputSize == 0) {
                return kIOReturnBadArgument;
            }
            size_t max_count = arguments->structureOutputSize / sizeof(bss_entry);
            bss_entry *out = reinterpret_cast<bss_entry *>(arguments->structureOutput);
            size_t count = m_driver->bssCache().get_entries(out, max_count);
            arguments->structureOutputSize = static_cast<uint32_t>(count * sizeof(bss_entry));
            return kIOReturnSuccess;
        }

        case kRtl89MethodConnect: {
            if (!arguments || !arguments->structureInput ||
                arguments->structureInputSize < sizeof(rtl89_connect_req)) {
                return kIOReturnBadArgument;
            }
            const rtl89_connect_req *req =
                reinterpret_cast<const rtl89_connect_req *>(arguments->structureInput);
            int ret = m_driver->connectNetwork(req->ssid, req->passphrase);
            return (ret == 0) ? kIOReturnSuccess : kIOReturnNotFound;
        }

        case kRtl89MethodDisconnect: {
            m_driver->disconnectNetwork();
            return kIOReturnSuccess;
        }

        case kRtl89MethodGetStatus: {
            if (!arguments || !arguments->structureOutput ||
                arguments->structureOutputSize < sizeof(rtl89_status_resp)) {
                return kIOReturnBadArgument;
            }
            rtl89_status_resp *resp =
                reinterpret_cast<rtl89_status_resp *>(arguments->structureOutput);
            resp->connected = m_driver->isConnected() ? 1 : 0;
            snprintf(resp->ssid, sizeof(resp->ssid), "%s", m_driver->connectedSsid());
            memcpy(resp->bssid, m_driver->connectedBssid(), 6);
            resp->channel = m_driver->currentChannel();
            resp->rssi = -48; // Nominal signal strength
            arguments->structureOutputSize = sizeof(rtl89_status_resp);
            return kIOReturnSuccess;
        }

        default:
            return kIOReturnBadArgument;
    }
}
