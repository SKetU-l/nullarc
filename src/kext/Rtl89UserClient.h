//
// Rtl89UserClient.h — HeliPort UserClient Interface for Rtl89Driver
// Targeting macOS Sonoma (14.x) and Tahoe (15.x+)
//
#pragma once

#include <IOKit/IOUserClient.h>

class Rtl89Driver;

enum Rtl89UserClientMethod {
    kRtl89MethodOpen = 0,
    kRtl89MethodClose,
    kRtl89MethodScan,
    kRtl89MethodGetScanResults,
    kRtl89MethodConnect,
    kRtl89MethodDisconnect,
    kRtl89MethodGetStatus,
    kRtl89MethodCount
};

struct rtl89_connect_req {
    char ssid[33];
    char passphrase[65];
};

struct rtl89_status_resp {
    uint32_t connected;
    char     ssid[33];
    uint8_t  bssid[6];
    uint8_t  channel;
    int8_t   rssi;
};

class Rtl89UserClient : public IOUserClient {
    OSDeclareDefaultStructors(Rtl89UserClient)
    using super = IOUserClient;

private:
    task_t       m_task{nullptr};
    Rtl89Driver *m_driver{nullptr};

public:
    static Rtl89UserClient *withTask(task_t owningTask);

    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    IOReturn clientClose() override;

    IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *arguments,
                            IOExternalMethodDispatch *dispatch = 0,
                            OSObject *target = 0, void *reference = 0) override;

    void operator delete(void *, size_t) {}
    void operator delete(void *) {}
};
