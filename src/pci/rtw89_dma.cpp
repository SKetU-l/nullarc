//
// rtw89_dma.cpp — 32-Bit DMA Masked Memory Allocator Implementation
//
#include "rtw89_dma.h"

#if defined(KERNEL) || defined(__APPLE_KEXT__)
#include <sys/errno.h>
#include <string.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#else
#include <cerrno>
#include <cstdlib>
#include <cstring>
#endif

bool rtw89_dma_verify_32bit_mask(uint64_t paddr, uint64_t mask) {
    if (paddr == 0) {
        return false;
    }
    // Strict 32-bit check
    if ((paddr >> 32) != 0) {
        return false;
    }
    // Mask check
    if ((paddr & ~mask) != 0) {
        return false;
    }
    return true;
}

int rtw89_dma_alloc(rtw89_dma_buf *buf, size_t size, uint64_t mask, size_t align) {
    if (!buf || size == 0) {
        return -EINVAL;
    }

    if (align < 8) {
        align = 8;
    }

    // Align size to requested boundary (e.g. 128 bytes)
    size_t aligned_size = (size + align - 1) & ~(align - 1);

    buf->vaddr = nullptr;
    buf->paddr = 0;
    buf->size = size;
    buf->aligned_size = aligned_size;
    buf->mask = mask;
    buf->priv = nullptr;

#if defined(KERNEL) && defined(__APPLE__)
    // macOS IOKit: IOBufferMemoryDescriptor restricted to 32-bit physical mask
    IOBufferMemoryDescriptor *desc = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
        kernel_task,
        kIODirectionInOut | kIOMemoryPhysicallyContiguous,
        aligned_size,
        mask
    );

    if (!desc) {
        return -ENOMEM;
    }

    if (desc->prepare() != kIOReturnSuccess) {
        desc->release();
        return -ENOMEM;
    }

    void *vaddr = desc->getBytesNoCopy();
    uint64_t paddr = static_cast<uint64_t>(desc->getPhysicalAddress());

    if (!vaddr || !rtw89_dma_verify_32bit_mask(paddr, mask)) {
        desc->complete();
        desc->release();
        return -ENOMEM;
    }

    memset(vaddr, 0, aligned_size);
    buf->vaddr = vaddr;
    buf->paddr = paddr;
    buf->priv = static_cast<void *>(desc);

    return 0;

#else
    // Freestanding test / user mode simulation
    void *ptr = nullptr;
    int ret = posix_memalign(&ptr, align, aligned_size);
    if (ret != 0 || !ptr) {
        return -ENOMEM;
    }

    memset(ptr, 0, aligned_size);

    // Simulate physical address in 32-bit space (< 4GB)
    uintptr_t v_int = reinterpret_cast<uintptr_t>(ptr);
    uint64_t paddr = static_cast<uint64_t>(v_int & mask);
    if (paddr == 0 || (paddr >> 32) != 0) {
        // Map to valid non-zero 32-bit physical region (e.g., 0x20000000 base)
        paddr = 0x20000000ULL | (v_int & 0x0FFFFFFFULL & mask);
    }

    // Ensure alignment on physical address
    paddr &= ~(static_cast<uint64_t>(align - 1));

    if (!rtw89_dma_verify_32bit_mask(paddr, mask)) {
        free(ptr);
        return -ENOMEM;
    }

    buf->vaddr = ptr;
    buf->paddr = paddr;
    buf->priv = ptr;

    return 0;
#endif
}

void rtw89_dma_free(rtw89_dma_buf *buf) {
    if (!buf) {
        return;
    }

#if defined(KERNEL) && defined(__APPLE__)
    if (buf->priv) {
        IOBufferMemoryDescriptor *desc = static_cast<IOBufferMemoryDescriptor *>(buf->priv);
        desc->complete();
        desc->release();
    }
#else
    if (buf->vaddr) {
        free(buf->vaddr);
    }
#endif

    buf->vaddr = nullptr;
    buf->paddr = 0;
    buf->size = 0;
    buf->aligned_size = 0;
    buf->priv = nullptr;
}

void rtw89_dma_sync(rtw89_dma_buf *buf, rtw89_dma_direction /*dir*/) {
    if (!buf || !buf->vaddr) {
        return;
    }
    // Full memory barrier for device/host DMA coherence
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}
