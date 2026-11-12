// Host harness for the actual layout/ring/TOC/TMR methods from PhxBringup.cpp.
// Only MMIO, memory mapping, locking and PSP transport are stand-ins.
#include "../src/phxbringup/PhxNativeABI.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <initializer_list>

using IOReturn = int;
enum { kIOReturnSuccess, kIOReturnNotReady, kIOReturnBadArgument,
       kIOReturnExclusiveAccess, kIOReturnTimeout };
static int locks;
static void IOLockLock(void *) { ++locks; }
static void IOLockUnlock(void *) { --locks; }
static void IOSleep(unsigned) {}
static const void *kOSBooleanTrue = reinterpret_cast<void *>(1);
static void IOLog(const char *) {}
struct PciDevice {
    bool claimed = false, registered = false;
    void setProperty(const char *, const void *) { claimed = true; }
    void registerService() { registered = true; }
};
struct MemoryMap {
    unsigned char *bytes = static_cast<unsigned char *>(calloc(1, PHX_LAYOUT_BAR_REQUIRED));
    ~MemoryMap() { free(bytes); }
    uintptr_t getVirtualAddress() { return reinterpret_cast<uintptr_t>(bytes); }
};
class PhxBringup {
public:
    MemoryMap storage;
    MemoryMap *mmio = &storage, *fbAper = &storage;
    void *smuLock = nullptr;
    uint64_t fbMcBase = 0x9000000000ull, fbSize = 512ull << 20, fbPhys = 0x480000000ull;
    uint64_t fbAperLen = 256ull << 20;
    uint64_t ringMc = 0, ringOff = 0, cmdOff = 0, cmdMc = 0, fenceOff = 0, fenceMc = 0;
    uint32_t fenceCounter = 0, tmrRequired = 0;
    uint64_t tmrOff = 0, tmrMc = 0, tmrSize = 0, fwBufOff = 0, fwBufMc = 0, fwBufSize = 0;
    bool tmrSetUp = false;
    PciDevice device;
    PciDevice *pci = &device;
    const phx_topology *topology = &phx_elitebook_780m;
    struct { bool ready = false; } gq[2];
    bool gpuReady() const { return gq[0].ready || gq[1].ready; }
    bool topologyReady() const { return gq[0].ready && gq[1].ready; }
    unsigned calls = 0;
    uint32_t command = 0, words[6] = {}, tocSize = 0x4000000, status = 0;
    IOReturn transport = kIOReturnSuccess;
    uint32_t rreg(uint32_t) { return PHX_MBOX_TOS_READY; }
    void wreg(uint32_t, uint32_t) {}
    bool pspWaitTos(uint32_t) { return true; }
    IOReturn pspRunCmd(uint32_t cmd, const uint32_t *u, uint32_t n, uint64_t out[4]) {
        ++calls; command = cmd;
        memset(words, 0, sizeof(words));
        if (n) memcpy(words, u, n * sizeof(*u));
        out[0] = status;
        *reinterpret_cast<uint32_t *>(storage.bytes + cmdOff + PHX_PSP_GFX_RESP_OFFSET + 16) = tocSize;
        return transport;
    }
    IOReturn layout(uint64_t out[4]) const;
    void clearRing();
    IOReturn pspRing(uint32_t op, uint64_t offset, uint64_t out[4]);
    IOReturn pspCtl(uint32_t op, uint32_t arg, uint64_t out[4]);
    IOReturn loadIpFw(uint32_t fwType, uint32_t offset, uint32_t size, uint64_t out[4]);
    IOReturn claimAccel();
};

// Generated at test time from the working driver, not a copied/reimplemented loop.
#include PHX_PSP_METHODS_FILE

int main() {
    PhxBringup p;
    uint64_t out[4] = {};
    assert(p.layout(out) == kIOReturnSuccess);
    assert(out[0] == p.fbMcBase && out[2] == p.fbPhys);
    for (uint64_t off : {uint64_t{0}, uint64_t{0x1400000}, UINT64_MAX})
        assert(p.pspRing(1, off, out) == kIOReturnBadArgument);
    p.fbAperLen = 64ull << 20;
    assert(p.pspRing(1, PHX_VRAM_RING_OFF, out) == kIOReturnBadArgument);
    p.fbAperLen = 256ull << 20;
    assert(p.pspRing(1, PHX_VRAM_RING_OFF, out) == kIOReturnSuccess);
    assert(p.tmrMc == p.fbMcBase + PHX_VRAM_TMR_OFF);
    assert(p.fwBufMc == p.fbMcBase + PHX_VRAM_FWBUF_OFF);
    assert(p.pspCtl(1, 0, out) == kIOReturnBadArgument && p.calls == 0);
    assert(p.loadIpFw(2, 0, 32, out) == kIOReturnNotReady && p.calls == 0);
    assert(p.pspCtl(3, 0x900, out) == kIOReturnSuccess);
    assert(p.command == PHX_PSP_GFX_CMD_LOAD_TOC && p.words[2] == 0x900);
    assert((uint64_t(p.words[1]) << 32 | p.words[0]) == p.fwBufMc);
    assert(p.tmrRequired == 0x4000000 && out[2] == p.tmrRequired);
    unsigned before = p.calls;
    for (uint32_t size : {1u, 0x1000000u, 0x8000000u})
        assert(p.pspCtl(1, size, out) == kIOReturnBadArgument && p.calls == before);
    assert(p.pspCtl(1, 0, out) == kIOReturnSuccess && p.tmrSetUp);
    assert(p.words[2] == 0x4000000 && p.words[3] == 2);
    assert((uint64_t(p.words[1]) << 32 | p.words[0]) == p.tmrMc);
    assert((uint64_t(p.words[5]) << 32 | p.words[4]) == p.fbPhys + PHX_VRAM_TMR_OFF);
    assert(p.loadIpFw(2, 0, 32, out) == kIOReturnSuccess);
    for (uint32_t status : {1u, 0xffffffffu}) {
        p.status = 0;
        assert(p.pspCtl(3, 0x900, out) == kIOReturnSuccess && p.tmrRequired);
        p.status = status;
        assert(p.pspCtl(1, 0, out) == kIOReturnSuccess && !p.tmrSetUp);
        assert(p.pspCtl(2, 0, out) == kIOReturnNotReady);
        assert(p.loadIpFw(2, 0, 32, out) == kIOReturnNotReady);
        assert(p.pspCtl(3, 0x900, out) == kIOReturnSuccess && !p.tmrRequired);
    }
    p.status = 0;
    for (uint32_t size : {0u, 0x4000001u, 0x8000000u}) {
        p.tocSize = size;
        assert(p.pspCtl(3, 0x900, out) == kIOReturnSuccess && !p.tmrRequired);
        assert(p.pspCtl(1, 0, out) == kIOReturnBadArgument);
    }
    p.tocSize = 0x2000000;
    assert(p.pspCtl(3, 0x900, out) == kIOReturnSuccess && p.tmrRequired == 0x2000000);
    assert(p.pspCtl(1, 0, out) == kIOReturnSuccess && p.words[2] == 0x2000000);
    p.transport = kIOReturnTimeout;
    assert(p.pspCtl(3, 0x900, out) == kIOReturnTimeout && !p.tmrRequired && !p.tmrSetUp);
    assert(p.pspRing(2, 0, out) == kIOReturnSuccess);
    assert(!p.ringMc && !p.tmrRequired && !p.tmrSetUp);
    assert(p.claimAccel() == kIOReturnNotReady && !p.device.claimed);
    p.gq[0].ready = true;
    assert(p.claimAccel() == kIOReturnNotReady && !p.device.claimed);
    p.gq[0].ready = false; p.gq[1].ready = true;
    assert(p.claimAccel() == kIOReturnNotReady && !p.device.claimed);
    p.gq[0].ready = true;
    assert(p.claimAccel() == kIOReturnSuccess && p.device.claimed && p.device.registered);
    assert(locks == 0);
    puts("[PASS] real PSP methods: detected addresses, firmware sizing, failures and state reset");
}
