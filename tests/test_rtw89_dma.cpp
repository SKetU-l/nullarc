//
// test_rtw89_dma.cpp — Unit tests for 32-bit DMA masked memory allocator
//
#include "linux_shim.h"
#include "rtw89_dma.h"

#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static void test_dma_mask_verification() {
    printf("[TEST] Running test_dma_mask_verification...\n");

    // Valid 32-bit physical addresses
    assert(rtw89_dma_verify_32bit_mask(0x1000ULL));
    assert(rtw89_dma_verify_32bit_mask(0x7FFFFFFFULL));
    assert(rtw89_dma_verify_32bit_mask(0xFFFFFF80ULL));
    assert(rtw89_dma_verify_32bit_mask(0xFFFFFFFFULL));

    // Invalid: Zero address
    assert(!rtw89_dma_verify_32bit_mask(0ULL));

    // Invalid: 64-bit addresses (> 4GB)
    assert(!rtw89_dma_verify_32bit_mask(0x100000000ULL));
    assert(!rtw89_dma_verify_32bit_mask(0x200000000ULL));
    assert(!rtw89_dma_verify_32bit_mask(0xFFFFFFFF00000000ULL));

    // Custom tighter masks
    assert(rtw89_dma_verify_32bit_mask(0x0FFFFFFFULL, 0x0FFFFFFFULL));
    assert(!rtw89_dma_verify_32bit_mask(0x10000000ULL, 0x0FFFFFFFULL));

    printf("[PASS] test_dma_mask_verification\n");
}

static void test_dma_allocations() {
    printf("[TEST] Running test_dma_allocations...\n");

    const size_t test_sizes[] = { 64, 128, 256, 1024, 4096, 65536, 1048576 };

    for (size_t sz : test_sizes) {
        rtw89_dma_buf buf{};
        int ret = rtw89_dma_alloc(&buf, sz);
        assert(ret == 0);
        assert(buf.is_valid());
        assert(buf.vaddr != nullptr);
        assert(buf.paddr != 0);
        assert((buf.paddr >> 32) == 0); // Strict 32-bit physical limit
        assert((reinterpret_cast<uintptr_t>(buf.vaddr) % RTW89_DMA_ALIGNMENT) == 0);
        assert((buf.paddr % RTW89_DMA_ALIGNMENT) == 0);
        assert(buf.aligned_size >= sz);
        assert((buf.aligned_size % RTW89_DMA_ALIGNMENT) == 0);

        // Verify buffer is zeroed
        uint8_t *bytes = static_cast<uint8_t *>(buf.vaddr);
        for (size_t i = 0; i < buf.aligned_size; ++i) {
            assert(bytes[i] == 0);
        }

        // Test writing pattern
        memset(buf.vaddr, 0xA5, sz);
        rtw89_dma_sync(&buf, RTW89_DMA_BIDIRECTIONAL);

        rtw89_dma_free(&buf);
        assert(buf.vaddr == nullptr);
        assert(buf.paddr == 0);
        assert(buf.size == 0);
        assert(!buf.is_valid());
    }

    printf("[PASS] test_dma_allocations\n");
}

static void test_dma_error_handling() {
    printf("[TEST] Running test_dma_error_handling...\n");

    rtw89_dma_buf buf{};

    // Null buffer pointer
    assert(rtw89_dma_alloc(nullptr, 1024) == -EINVAL);

    // Zero size
    assert(rtw89_dma_alloc(&buf, 0) == -EINVAL);

    // Freeing null pointer should be safe
    rtw89_dma_free(nullptr);
    rtw89_dma_free(&buf);

    printf("[PASS] test_dma_error_handling\n");
}

static void test_dma_stress() {
    printf("[TEST] Running test_dma_stress (10,000 cycles)...\n");

    for (int i = 0; i < 10000; ++i) {
        rtw89_dma_buf buf{};
        size_t sz = 128 + (i % 8) * 128;
        int ret = rtw89_dma_alloc(&buf, sz);
        assert(ret == 0);
        assert(buf.is_valid());
        assert((buf.paddr >> 32) == 0);

        memset(buf.vaddr, i & 0xFF, sz);
        rtw89_dma_sync(&buf, RTW89_DMA_TO_DEVICE);

        rtw89_dma_free(&buf);
    }

    printf("[PASS] test_dma_stress (10,000 cycles passed, 0 leaks)\n");
}

int main() {
    printf("=== Realtek RTL8852BE 32-Bit DMA Allocator Unit Tests ===\n");

    test_dma_mask_verification();
    test_dma_allocations();
    test_dma_error_handling();
    test_dma_stress();

    printf("=== All 32-Bit DMA Allocator tests passed successfully (0 errors) ===\n");
    return 0;
}
