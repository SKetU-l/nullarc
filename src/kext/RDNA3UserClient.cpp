//
//  RDNA3UserClient.cpp — IOUserClient implementation for RDNA 3 driver.
//
#include "RDNA3UserClient.h"
#include <IOKit/IOLib.h>

#define CLIENT_LOG(fmt, ...) IOLog("RDNA3UserClient: " fmt "\n", ##__VA_ARGS__)

OSDefineMetaClassAndStructors(RDNA3UserClient, IOUserClient)

bool RDNA3UserClient::initWithTask(task_t owningTask, void *securityID, UInt32 type) {
    if (!super::initWithTask(owningTask, securityID, type)) return false;
    m_owningTask = owningTask;
    m_clientAlive = true;
    CLIENT_LOG("Initialized RDNA3UserClient instance for task %p (type 0x%x)", owningTask, type);
    return true;
}

bool RDNA3UserClient::start(IOService *provider) {
    if (!super::start(provider)) return false;
    m_driver = OSDynamicCast(RDNA3Driver, provider);
    if (!m_driver) {
        CLIENT_LOG("Error: Provider is not an RDNA3Driver");
        return false;
    }
    CLIENT_LOG("RDNA3UserClient connected to RDNA3Driver");
    return true;
}

void RDNA3UserClient::stop(IOService *provider) {
    CLIENT_LOG("Stopping RDNA3UserClient");
    m_clientAlive = false;
    m_driver = nullptr;
    super::stop(provider);
}

IOReturn RDNA3UserClient::clientClose() {
    CLIENT_LOG("RDNA3UserClient clientClose called");
    terminate();
    return kIOReturnSuccess;
}

IOReturn RDNA3UserClient::handleHello(const void *in, size_t inSize, void *out, size_t *outSize) {
    if (!out || !outSize || *outSize < sizeof(uint32_t)) return kIOReturnBadArgument;
    *reinterpret_cast<uint32_t*>(out) = 0x52334E31u; // 'R3N1'
    *outSize = sizeof(uint32_t);
    return kIOReturnSuccess;
}

IOReturn RDNA3UserClient::handleQueryInfo(void *out, size_t *outSize) {
    if (!out || !outSize || *outSize < sizeof(r3n_info)) return kIOReturnBadArgument;
    if (!m_driver) return kIOReturnNotAttached;

    const auto &timings = m_driver->getActiveTimings();

    r3n_info info = {};
    info.chip_id            = 0x1900;
    info.chip_rev           = 0xBA;
    info.family             = 143; // AMD_FAMILY_GC_11_0_3
    info.ip_version_major   = 11;
    info.ip_version_minor   = 0;
    info.ip_version_rev     = 3;
    info.vram_size_bytes    = 4ull * 1024 * 1024 * 1024;
    info.vram_visible_bytes = m_driver->getVramSize() ? m_driver->getVramSize() : 256ull * 1024 * 1024;
    info.gtt_size_bytes     = 8ull * 1024 * 1024 * 1024;
    info.cu_count           = 12; // Radeon 780M has 12 Compute Units
    info.max_engine_clock_mhz = 2700;
    info.max_memory_clock_mhz = 3200;
    info.display_width      = timings.h_active ? timings.h_active : 2560;
    info.display_height     = timings.v_active ? timings.v_active : 1600;
    info.display_refresh_hz = timings.refresh_rate_hz ? timings.refresh_rate_hz : 120;

    *reinterpret_cast<r3n_info*>(out) = info;
    *outSize = sizeof(r3n_info);
    return kIOReturnSuccess;
}

IOReturn RDNA3UserClient::handleScanQuery(void *out, size_t *outSize) {
    if (!out || !outSize || *outSize < sizeof(r3n_scan_reg)) return kIOReturnBadArgument;
    if (!m_driver) return kIOReturnNotAttached;

    const auto &timings = m_driver->getActiveTimings();

    r3n_scan_reg sr = {};
    sr.bo_handle    = 1;
    sr.width        = timings.h_active ? timings.h_active : 2560;
    sr.height       = timings.v_active ? timings.v_active : 1600;
    sr.stride_bytes = sr.width * 4;
    sr.format       = 0x00000004; // ARGB8888

    *reinterpret_cast<r3n_scan_reg*>(out) = sr;
    *outSize = sizeof(r3n_scan_reg);
    return kIOReturnSuccess;
}

IOReturn RDNA3UserClient::handleScanPresent(const void *in, size_t inSize, void *out, size_t *outSize) {
    if (!in || inSize < sizeof(r3n_scan_present)) return kIOReturnBadArgument;
    if (!m_driver) return kIOReturnNotAttached;

    const auto *req = reinterpret_cast<const r3n_scan_present*>(in);
    if (out && outSize && *outSize >= sizeof(uint32_t)) {
        *reinterpret_cast<uint32_t*>(out) = req->slot_id;
        *outSize = sizeof(uint32_t);
    }
    return kIOReturnSuccess;
}

IOReturn RDNA3UserClient::externalMethod(uint32_t selector, IOExternalMethodArguments *args,
                                         IOExternalMethodDispatch *dispatch, OSObject *target, void *reference) {
    if (!m_clientAlive || !m_driver) return kIOReturnNotAttached;
    if (!args) return kIOReturnBadArgument;

    switch (selector) {
        case R3N_SEL_HELLO:
            return handleHello(args->structureInput, args->structureInputSize,
                               args->structureOutput, reinterpret_cast<size_t*>(&args->structureOutputSize));

        case R3N_SEL_QUERYINFO:
            return handleQueryInfo(args->structureOutput,
                                   reinterpret_cast<size_t*>(&args->structureOutputSize));

        case R3N_SEL_SCAN_QUERY:
            return handleScanQuery(args->structureOutput,
                                   reinterpret_cast<size_t*>(&args->structureOutputSize));

        case R3N_SEL_SCAN_PRESENT:
            return handleScanPresent(args->structureInput, args->structureInputSize,
                                     args->structureOutput, reinterpret_cast<size_t*>(&args->structureOutputSize));

        default:
            CLIENT_LOG("Unsupported selector 0x%x", selector);
            return kIOReturnUnsupported;
    }
}
