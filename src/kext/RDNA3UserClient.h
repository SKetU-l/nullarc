//
//  RDNA3UserClient.h — IOUserClient interface for RDNA 3 driver.
//  Serves Mesa RADV Darwin backend and WindowServer scanout requests.
//
#pragma once

#include <IOKit/IOUserClient.h>
#include "RDNA3Driver.h"
#include "uapi/r3n_native_abi.h"

class RDNA3UserClient : public IOUserClient {
    OSDeclareDefaultStructors(RDNA3UserClient)
    using super = IOUserClient;

private:
    task_t       m_owningTask{nullptr};
    RDNA3Driver *m_driver{nullptr};
    bool         m_clientAlive{false};

public:
    bool initWithTask(task_t owningTask, void *securityID, UInt32 type) override;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    IOReturn clientClose() override;

    IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *arguments,
                            IOExternalMethodDispatch *dispatch = nullptr,
                            OSObject *target = nullptr, void *reference = nullptr) override;

private:
    IOReturn handleHello(const void *in, size_t inSize, void *out, size_t *outSize);
    IOReturn handleQueryInfo(void *out, size_t *outSize);
    IOReturn handleScanQuery(void *out, size_t *outSize);
    IOReturn handleScanPresent(const void *in, size_t inSize, void *out, size_t *outSize);
};
