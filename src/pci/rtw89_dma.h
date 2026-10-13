//
// rtw89_dma.h — 32-Bit DMA Masked Memory Allocator for RTL8852BE
// Enforces physical addresses <= 0xFFFFFFFFULL for AMD Hawk Point APU safety
//
#pragma once

#include "linux_shim.h"

#if defined(KERNEL) || defined(__APPLE_KEXT__)
#include <stddef.h>
#include <stdint.h>
#else
#include <cstddef>
#include <cstdint>
#endif

#define RTW89_DMA_DEFAULT_MASK          0xFFFFFFFFULL   // 32-bit physical DMA mask
#define RTW89_DMA_ALIGNMENT             128             // 128-byte cache line alignment

enum rtw89_dma_direction {
    RTW89_DMA_BIDIRECTIONAL = 0,
    RTW89_DMA_TO_DEVICE     = 1,
    RTW89_DMA_FROM_DEVICE   = 2,
    RTW89_DMA_NONE          = 3,
};

struct rtw89_dma_buf {
    void     *vaddr{nullptr};           // Kernel virtual address
    uint64_t paddr{0};                  // Physical DMA bus address (< 4GB)
    size_t   size{0};                   // Requested byte length
    size_t   aligned_size{0};           // Aligned byte length (multiple of 128)
    uint64_t mask{RTW89_DMA_DEFAULT_MASK}; // Physical address mask
    void     *priv{nullptr};            // Opaque handle (e.g. IOBufferMemoryDescriptor)

    inline bool is_valid() const {
        return vaddr != nullptr && paddr != 0 && ((paddr >> 32) == 0);
    }
};

// Allocation and Deallocation
int  rtw89_dma_alloc(rtw89_dma_buf *buf, size_t size,
                     uint64_t mask = RTW89_DMA_DEFAULT_MASK,
                     size_t align = RTW89_DMA_ALIGNMENT);

void rtw89_dma_free(rtw89_dma_buf *buf);

// Cache Synchronization
void rtw89_dma_sync(rtw89_dma_buf *buf, rtw89_dma_direction dir);

// Address translation verification
bool rtw89_dma_verify_32bit_mask(uint64_t paddr, uint64_t mask = RTW89_DMA_DEFAULT_MASK);
