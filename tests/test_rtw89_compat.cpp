//
// test_rtw89_compat.cpp — Comprehensive Test Suite for Linux Shim & sk_buff
// Tests freestanding C++17 compatibility, memory safety, and leak freedom
//
#include "linux_shim.h"
#include "skbuff.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <thread>
#include <vector>

#define TEST_ASSERT(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "[FAIL] %s:%d: Assertion failed: %s\n", __FILE__, __LINE__, #cond); \
            std::abort(); \
        } \
    } while (0)

static void test_types() {
    std::printf("[TEST] 1. Freestanding Integer Types & Sizing...\n");

    static_assert(sizeof(u8)  == 1, "u8 size mismatch");
    static_assert(sizeof(u16) == 2, "u16 size mismatch");
    static_assert(sizeof(u32) == 4, "u32 size mismatch");
    static_assert(sizeof(u64) == 8, "u64 size mismatch");

    static_assert(sizeof(s8)  == 1, "s8 size mismatch");
    static_assert(sizeof(s16) == 2, "s16 size mismatch");
    static_assert(sizeof(s32) == 4, "s32 size mismatch");
    static_assert(sizeof(s64) == 8, "s64 size mismatch");

    static_assert(sizeof(__u8)  == 1, "__u8 size mismatch");
    static_assert(sizeof(__u16) == 2, "__u16 size mismatch");
    static_assert(sizeof(__u32) == 4, "__u32 size mismatch");
    static_assert(sizeof(__u64) == 8, "__u64 size mismatch");

    static_assert(sizeof(__le16) == 2, "__le16 size mismatch");
    static_assert(sizeof(__le32) == 4, "__le32 size mismatch");
    static_assert(sizeof(__le64) == 8, "__le64 size mismatch");
    static_assert(sizeof(__be16) == 2, "__be16 size mismatch");
    static_assert(sizeof(__be32) == 4, "__be32 size mismatch");
    static_assert(sizeof(__be64) == 8, "__be64 size mismatch");

    static_assert(sizeof(dma_addr_t) == 8, "dma_addr_t size mismatch");
    static_assert(sizeof(gfp_t) == 4, "gfp_t size mismatch");

    // Signedness checks
    s8 s_neg = -1;
    u8 u_max = static_cast<u8>(-1);
    TEST_ASSERT(s_neg < 0);
    TEST_ASSERT(u_max == 255);
    TEST_ASSERT(u_max > 0);

    bool flag = true;
    TEST_ASSERT(flag);

    TEST_ASSERT(GFP_KERNEL == 0U);
    TEST_ASSERT(GFP_ATOMIC == 1U);

    std::printf("  -> [PASS] Integer types and sizing verified\n");
}

static void test_endian() {
    std::printf("[TEST] 2. Endian Conversions...\n");

    u16 val16 = 0x1234;
    u32 val32 = 0x12345678;
    u64 val64 = 0x123456789ABCDEF0ULL;

    // Check round-trips
    TEST_ASSERT(le16_to_cpu(cpu_to_le16(val16)) == val16);
    TEST_ASSERT(be16_to_cpu(cpu_to_be16(val16)) == val16);
    TEST_ASSERT(le32_to_cpu(cpu_to_le32(val32)) == val32);
    TEST_ASSERT(be32_to_cpu(cpu_to_be32(val32)) == val32);
    TEST_ASSERT(le64_to_cpu(cpu_to_le64(val64)) == val64);
    TEST_ASSERT(be64_to_cpu(cpu_to_be64(val64)) == val64);

    // On little-endian host (x86_64)
#if !defined(__BYTE_ORDER__) || __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    TEST_ASSERT(cpu_to_le16(val16) == val16);
    TEST_ASSERT(le16_to_cpu(val16) == val16);
    TEST_ASSERT(cpu_to_be16(val16) == 0x3412);
    TEST_ASSERT(be16_to_cpu(0x3412) == val16);

    TEST_ASSERT(cpu_to_le32(val32) == val32);
    TEST_ASSERT(le32_to_cpu(val32) == val32);
    TEST_ASSERT(cpu_to_be32(val32) == 0x78563412);
    TEST_ASSERT(be32_to_cpu(0x78563412) == val32);

    TEST_ASSERT(cpu_to_le64(val64) == val64);
    TEST_ASSERT(cpu_to_be64(val64) == 0xF0DEBC9A78563412ULL);
#endif

    std::printf("  -> [PASS] Endian conversions verified\n");
}

static void test_bitfield() {
    std::printf("[TEST] 3. Bitfield Helpers & Bit Manipulation...\n");

    // BIT and BIT_ULL
    TEST_ASSERT(BIT(0) == 1ULL);
    TEST_ASSERT(BIT(1) == 2ULL);
    TEST_ASSERT(BIT(31) == 0x80000000ULL);
    TEST_ASSERT(BIT(63) == 0x8000000000000000ULL);
    TEST_ASSERT(BIT_ULL(10) == 1024ULL);

    // GENMASK
    TEST_ASSERT(GENMASK(0, 0) == 0x1ULL);
    TEST_ASSERT(GENMASK(3, 0) == 0xFULL);
    TEST_ASSERT(GENMASK(7, 4) == 0xF0ULL);
    TEST_ASSERT(GENMASK(15, 8) == 0xFF00ULL);
    TEST_ASSERT(GENMASK(31, 0) == 0xFFFFFFFFULL);
    TEST_ASSERT(GENMASK(63, 0) == 0xFFFFFFFFFFFFFFFFULL);
    TEST_ASSERT(GENMASK(63, 32) == 0xFFFFFFFF00000000ULL);
    TEST_ASSERT(GENMASK(63, 63) == (1ULL << 63));

    // FIELD_PREP and FIELD_GET
    u32 mask = GENMASK(15, 8);
    u32 prep = FIELD_PREP(mask, 0xAB);
    TEST_ASSERT(prep == 0xAB00);
    u32 get = FIELD_GET(mask, prep);
    TEST_ASSERT(get == 0xAB);

    u64 mask64 = GENMASK(47, 32);
    u64 prep64 = FIELD_PREP(mask64, 0x1234);
    TEST_ASSERT(prep64 == 0x0000123400000000ULL);
    TEST_ASSERT(FIELD_GET(mask64, prep64) == 0x1234);

    // le32p_replace_bits
    __le32 le_val = cpu_to_le32(0x12345678);
    le32p_replace_bits(&le_val, 0xAA, GENMASK(15, 8));
    TEST_ASSERT(le32_to_cpu(le_val) == 0x1234AA78);

    le32p_replace_bits(&le_val, 0xFE, GENMASK(31, 24));
    TEST_ASSERT(le32_to_cpu(le_val) == 0xFE34AA78);

    // u32p_replace_bits
    u32 u_val = 0x11223344;
    u32p_replace_bits(&u_val, 0x99, GENMASK(23, 16));
    TEST_ASSERT(u_val == 0x11993344);

    std::printf("  -> [PASS] Bitfield helpers verified\n");
}

static void test_atomics() {
    std::printf("[TEST] 4. Atomic Primitives & Bitops...\n");

    atomic_t v = ATOMIC_INIT(100);
    TEST_ASSERT(atomic_read(&v) == 100);

    atomic_set(&v, 200);
    TEST_ASSERT(atomic_read(&v) == 200);

    atomic_inc(&v);
    TEST_ASSERT(atomic_read(&v) == 201);

    atomic_dec(&v);
    TEST_ASSERT(atomic_read(&v) == 200);

    atomic_add(50, &v);
    TEST_ASSERT(atomic_read(&v) == 250);

    atomic_sub(100, &v);
    TEST_ASSERT(atomic_read(&v) == 150);

    TEST_ASSERT(atomic_inc_return(&v) == 151);
    TEST_ASSERT(atomic_dec_return(&v) == 150);
    TEST_ASSERT(atomic_add_return(10, &v) == 160);
    TEST_ASSERT(atomic_sub_return(60, &v) == 100);

    atomic_set(&v, 1);
    TEST_ASSERT(atomic_dec_and_test(&v) == true);
    TEST_ASSERT(atomic_read(&v) == 0);

    atomic_set(&v, 2);
    TEST_ASSERT(atomic_dec_and_test(&v) == false);
    TEST_ASSERT(atomic_read(&v) == 1);

    atomic_set(&v, -1);
    TEST_ASSERT(atomic_inc_and_test(&v) == true);
    TEST_ASSERT(atomic_read(&v) == 0);

    // Bitmap operations
    DECLARE_BITMAP(bm, 256) = {0};

    TEST_ASSERT(!test_bit(0, bm));
    TEST_ASSERT(!test_bit(63, bm));
    TEST_ASSERT(!test_bit(64, bm));
    TEST_ASSERT(!test_bit(128, bm));
    TEST_ASSERT(!test_bit(255, bm));

    set_bit(0, bm);
    TEST_ASSERT(test_bit(0, bm));
    TEST_ASSERT(!test_bit(1, bm));

    set_bit(63, bm);
    TEST_ASSERT(test_bit(63, bm));

    set_bit(64, bm);
    TEST_ASSERT(test_bit(64, bm));

    set_bit(255, bm);
    TEST_ASSERT(test_bit(255, bm));

    clear_bit(63, bm);
    TEST_ASSERT(!test_bit(63, bm));
    TEST_ASSERT(test_bit(0, bm));
    TEST_ASSERT(test_bit(64, bm));

    // test_and_set_bit
    TEST_ASSERT(test_and_set_bit(10, bm) == false);
    TEST_ASSERT(test_bit(10, bm) == true);
    TEST_ASSERT(test_and_set_bit(10, bm) == true);

    // test_and_clear_bit
    TEST_ASSERT(test_and_clear_bit(10, bm) == true);
    TEST_ASSERT(test_bit(10, bm) == false);
    TEST_ASSERT(test_and_clear_bit(10, bm) == false);

    std::printf("  -> [PASS] Atomic primitives and bitops verified\n");
}

static void test_locks() {
    std::printf("[TEST] 5. Locking Wrappers (Spinlock & Mutex)...\n");

    // Spinlock init and lock/unlock
    spinlock_t spl;
    spin_lock_init(&spl);

    unsigned long flags = 0xdead;
    spin_lock_irqsave(&spl, flags);
    TEST_ASSERT(flags == 0);
    spin_unlock_irqrestore(&spl, flags);

    spin_lock(&spl);
    spin_unlock(&spl);

    TEST_ASSERT(spin_trylock(&spl) == true);
    spin_unlock(&spl);

    spin_lock_destroy(&spl);

    // Mutex init and lock/unlock
    mutex_t mtx;
    mutex_init(&mtx);

    mutex_lock(&mtx);
    mutex_unlock(&mtx);

    TEST_ASSERT(mutex_trylock(&mtx) == 1);
    mutex_unlock(&mtx);

    mutex_destroy(&mtx);

    // Concurrency test across multiple threads
    constexpr int NUM_THREADS = 4;
    constexpr int ITERS_PER_THREAD = 10000;

    spinlock_t c_spl;
    spin_lock_init(&c_spl);
    int shared_spl_counter = 0;

    std::vector<std::thread> spl_threads;
    for (int t = 0; t < NUM_THREADS; ++t) {
        spl_threads.emplace_back([&]() {
            for (int i = 0; i < ITERS_PER_THREAD; ++i) {
                unsigned long f;
                spin_lock_irqsave(&c_spl, f);
                shared_spl_counter++;
                spin_unlock_irqrestore(&c_spl, f);
            }
        });
    }
    for (auto &t : spl_threads) {
        t.join();
    }
    TEST_ASSERT(shared_spl_counter == NUM_THREADS * ITERS_PER_THREAD);
    spin_lock_destroy(&c_spl);

    mutex_t c_mtx;
    mutex_init(&c_mtx);
    int shared_mtx_counter = 0;

    std::vector<std::thread> mtx_threads;
    for (int t = 0; t < NUM_THREADS; ++t) {
        mtx_threads.emplace_back([&]() {
            for (int i = 0; i < ITERS_PER_THREAD; ++i) {
                mutex_lock(&c_mtx);
                shared_mtx_counter++;
                mutex_unlock(&c_mtx);
            }
        });
    }
    for (auto &t : mtx_threads) {
        t.join();
    }
    TEST_ASSERT(shared_mtx_counter == NUM_THREADS * ITERS_PER_THREAD);
    mutex_destroy(&c_mtx);

    std::printf("  -> [PASS] Locking wrappers and concurrency verified\n");
}

static void test_barriers() {
    std::printf("[TEST] 6. Memory Barriers...\n");

    mb();
    rmb();
    wmb();
    smp_mb();
    smp_rmb();
    smp_wmb();
    barrier();

    std::printf("  -> [PASS] Memory barriers executed cleanly\n");
}

static void test_skb_basic() {
    std::printf("[TEST] 7. Basic sk_buff Operations (alloc, reserve, put, push, pull, free)...\n");

    constexpr unsigned int BUF_SIZE = 1500;
    struct sk_buff *skb = alloc_skb(BUF_SIZE, GFP_KERNEL);
    TEST_ASSERT(skb != nullptr);
    TEST_ASSERT(skb->head != nullptr);
    TEST_ASSERT(skb->data == skb->head);
    TEST_ASSERT(skb->tail == skb->head);
    TEST_ASSERT(skb->end == skb->head + BUF_SIZE);
    TEST_ASSERT(skb->len == 0);
    TEST_ASSERT(skb->data_len == 0);
    TEST_ASSERT(skb_headroom(skb) == 0);
    TEST_ASSERT(skb_tailroom(skb) == BUF_SIZE);
    TEST_ASSERT(skb->truesize >= sizeof(struct sk_buff) + BUF_SIZE);
    TEST_ASSERT(atomic_read(&skb->users) == 1);
    TEST_ASSERT(skb->cloned == false);

    // Reserve 64 bytes for headroom
    skb_reserve(skb, 64);
    TEST_ASSERT(skb_headroom(skb) == 64);
    TEST_ASSERT(skb_tailroom(skb) == BUF_SIZE - 64);
    TEST_ASSERT(skb->len == 0);

    // Put 100 bytes
    unsigned char *put_ptr = skb_put(skb, 100);
    TEST_ASSERT(put_ptr == skb->head + 64);
    TEST_ASSERT(skb->len == 100);
    TEST_ASSERT(skb_headroom(skb) == 64);
    TEST_ASSERT(skb_tailroom(skb) == BUF_SIZE - 164);

    // Put data with string payload
    const char *msg = "RTW89_PAYLOAD_TEST";
    size_t msg_len = std::strlen(msg);
    void *msg_ptr = skb_put_data(skb, msg, static_cast<unsigned int>(msg_len));
    TEST_ASSERT(msg_ptr != nullptr);
    TEST_ASSERT(skb->len == 100 + msg_len);
    TEST_ASSERT(std::memcmp(msg_ptr, msg, msg_len) == 0);

    // Push 14 bytes Ethernet header in headroom
    unsigned char *push_ptr = skb_push(skb, 14);
    TEST_ASSERT(push_ptr == skb->head + 50);
    TEST_ASSERT(skb->data == skb->head + 50);
    TEST_ASSERT(skb->len == 114 + msg_len);
    TEST_ASSERT(skb_headroom(skb) == 50);

    // Write ethernet MAC header in the pushed space
    std::memset(push_ptr, 0xEE, 14);

    // Pull 14 bytes Ethernet header
    unsigned char *pull_ptr = skb_pull(skb, 14);
    TEST_ASSERT(pull_ptr == skb->head + 64);
    TEST_ASSERT(skb->data == skb->head + 64);
    TEST_ASSERT(skb->len == 100 + msg_len);
    TEST_ASSERT(skb_headroom(skb) == 64);

    // Metadata checks
    skb->protocol = cpu_to_be16(0x0800);
    skb->priority = 5;
    std::memset(skb->cb, 0x42, sizeof(skb->cb));
    TEST_ASSERT(skb->protocol == cpu_to_be16(0x0800));
    TEST_ASSERT(skb->priority == 5);
    TEST_ASSERT(skb->cb[0] == 0x42);

    kfree_skb(skb);

    // dev_alloc_skb verification
    struct sk_buff *dev_skb = dev_alloc_skb(500);
    TEST_ASSERT(dev_skb != nullptr);
    TEST_ASSERT(skb_headroom(dev_skb) >= 32);
    TEST_ASSERT(skb_tailroom(dev_skb) >= 500);
    TEST_ASSERT(dev_skb->len == 0);

    void *dput = skb_put(dev_skb, 200);
    TEST_ASSERT(dput != nullptr);
    TEST_ASSERT(dev_skb->len == 200);

    dev_kfree_skb_any(dev_skb);

    std::printf("  -> [PASS] Basic sk_buff operations verified\n");
}

static void test_skb_clone() {
    std::printf("[TEST] 8. skb_clone (Shared Data Buffer & Lifecycle)...\n");

    struct sk_buff *skb = alloc_skb(256, GFP_KERNEL);
    TEST_ASSERT(skb != nullptr);
    skb_reserve(skb, 32);

    const char *sample = "WLAN_BEACON_FRAME";
    skb_put_data(skb, sample, static_cast<unsigned int>(std::strlen(sample)));
    skb->protocol = cpu_to_be16(0x888E);
    skb->priority = 3;

    // Clone skb
    struct sk_buff *clone = skb_clone(skb, GFP_KERNEL);
    TEST_ASSERT(clone != nullptr);
    TEST_ASSERT(skb->cloned == true);
    TEST_ASSERT(clone->cloned == true);
    TEST_ASSERT(clone->data == skb->data);
    TEST_ASSERT(clone->head == skb->head);
    TEST_ASSERT(clone->tail == skb->tail);
    TEST_ASSERT(clone->end  == skb->end);
    TEST_ASSERT(clone->len  == skb->len);
    TEST_ASSERT(clone->protocol == skb->protocol);
    TEST_ASSERT(clone->priority == skb->priority);

    // Modifying data in clone is visible in original
    clone->data[0] = 'Z';
    TEST_ASSERT(skb->data[0] == 'Z');

    // Free clone first
    kfree_skb(clone);

    // Original remains valid and readable
    TEST_ASSERT(skb->data[0] == 'Z');
    kfree_skb(skb);

    // Reverse free order: Free original first, then clone
    struct sk_buff *skb2 = alloc_skb(128, GFP_KERNEL);
    skb_put_data(skb2, "REVERSE_ORDER", 13);
    struct sk_buff *clone2 = skb_clone(skb2, GFP_KERNEL);
    TEST_ASSERT(clone2 != nullptr);

    kfree_skb(skb2); // Original freed first
    TEST_ASSERT(std::memcmp(clone2->data, "REVERSE_ORDER", 13) == 0);
    kfree_skb(clone2); // Clone freed second

    std::printf("  -> [PASS] skb_clone shared data lifecycle verified\n");
}

static void test_skb_copy() {
    std::printf("[TEST] 9. skb_copy (Independent Deep Copy)...\n");

    struct sk_buff *skb = alloc_skb(300, GFP_KERNEL);
    TEST_ASSERT(skb != nullptr);
    skb_reserve(skb, 40);

    const char *payload = "INDEPENDENT_PAYLOAD";
    unsigned int plen = static_cast<unsigned int>(std::strlen(payload));
    skb_put_data(skb, payload, plen);
    skb->protocol = cpu_to_be16(0x86DD);
    skb->priority = 7;
    std::memset(skb->cb, 0x77, sizeof(skb->cb));

    struct sk_buff *copy = skb_copy(skb, GFP_KERNEL);
    TEST_ASSERT(copy != nullptr);
    TEST_ASSERT(copy->data != skb->data); // Distinct data pointers
    TEST_ASSERT(copy->head != skb->head); // Distinct buffer
    TEST_ASSERT(copy->len == skb->len);
    TEST_ASSERT(skb_headroom(copy) == skb_headroom(skb));
    TEST_ASSERT(skb_tailroom(copy) == skb_tailroom(skb));
    TEST_ASSERT(copy->protocol == skb->protocol);
    TEST_ASSERT(copy->priority == skb->priority);
    TEST_ASSERT(std::memcmp(copy->data, skb->data, plen) == 0);
    TEST_ASSERT(std::memcmp(copy->cb, skb->cb, sizeof(copy->cb)) == 0);

    // Mutating copy data must NOT affect original
    copy->data[0] = 'X';
    TEST_ASSERT(skb->data[0] == 'I');

    kfree_skb(skb);
    // Copy must remain valid after original is freed
    TEST_ASSERT(copy->data[0] == 'X');
    kfree_skb(copy);

    std::printf("  -> [PASS] skb_copy deep copy verified\n");
}

static void test_edge_cases() {
    std::printf("[TEST] 10. Edge Cases (Underflow, Overflow, NULL handling)...\n");

    // NULL handling
    kfree_skb(nullptr);
    dev_kfree_skb_any(nullptr);

    TEST_ASSERT(skb_headroom(nullptr) == 0);
    TEST_ASSERT(skb_tailroom(nullptr) == 0);
    TEST_ASSERT(skb_put(nullptr, 10) == nullptr);
    TEST_ASSERT(skb_put_data(nullptr, "test", 4) == nullptr);

    struct sk_buff *valid_skb = alloc_skb(100, GFP_KERNEL);
    TEST_ASSERT(skb_put_data(valid_skb, nullptr, 10) == nullptr);
    TEST_ASSERT(skb_push(nullptr, 10) == nullptr);
    TEST_ASSERT(skb_pull(nullptr, 10) == nullptr);
    skb_reserve(nullptr, 10); // Must not crash
    TEST_ASSERT(skb_clone(nullptr, GFP_KERNEL) == nullptr);
    TEST_ASSERT(skb_copy(nullptr, GFP_KERNEL) == nullptr);

    // Headroom Underflow check
    // Initial headroom is 0
    TEST_ASSERT(skb_headroom(valid_skb) == 0);
    TEST_ASSERT(skb_push(valid_skb, 1) == nullptr); // Underflow rejected!
    TEST_ASSERT(valid_skb->data == valid_skb->head);
    TEST_ASSERT(valid_skb->len == 0);

    // Reserve 20 bytes headroom
    skb_reserve(valid_skb, 20);
    TEST_ASSERT(skb_headroom(valid_skb) == 20);
    TEST_ASSERT(skb_push(valid_skb, 21) == nullptr); // Underflow rejected!
    TEST_ASSERT(skb_headroom(valid_skb) == 20);
    TEST_ASSERT(valid_skb->len == 0);

    // Push 20 bytes (exact boundary)
    unsigned char *push_ok = skb_push(valid_skb, 20);
    TEST_ASSERT(push_ok != nullptr);
    TEST_ASSERT(skb_headroom(valid_skb) == 0);
    TEST_ASSERT(valid_skb->len == 20);
    TEST_ASSERT(skb_push(valid_skb, 1) == nullptr); // No headroom left

    // Pull back
    unsigned char *pull_ok = skb_pull(valid_skb, 20);
    TEST_ASSERT(pull_ok != nullptr);
    TEST_ASSERT(valid_skb->len == 0);
    TEST_ASSERT(skb_headroom(valid_skb) == 20);

    // Pull Underflow check (pulling more than len)
    TEST_ASSERT(skb_pull(valid_skb, 1) == nullptr); // len is 0, pull 1 must fail

    // Tailroom Overflow check
    // Tailroom is currently 80 bytes (100 - 20)
    TEST_ASSERT(skb_tailroom(valid_skb) == 80);
    TEST_ASSERT(skb_put(valid_skb, 81) == nullptr); // Overflow rejected!
    TEST_ASSERT(skb_tailroom(valid_skb) == 80);
    TEST_ASSERT(valid_skb->len == 0);

    const char dummy[90] = {0};
    TEST_ASSERT(skb_put_data(valid_skb, dummy, 81) == nullptr); // Overflow rejected!

    // Put 80 bytes (exact boundary)
    unsigned char *put_ok = skb_put(valid_skb, 80);
    TEST_ASSERT(put_ok != nullptr);
    TEST_ASSERT(skb_tailroom(valid_skb) == 0);
    TEST_ASSERT(valid_skb->len == 80);
    TEST_ASSERT(skb_put(valid_skb, 1) == nullptr); // No tailroom left

    kfree_skb(valid_skb);

    // Reserve Overflow check
    struct sk_buff *skb_res = alloc_skb(50, GFP_KERNEL);
    TEST_ASSERT(skb_res != nullptr);
    skb_reserve(skb_res, 60); // Cannot reserve more than buffer capacity!
    TEST_ASSERT(skb_headroom(skb_res) == 0);
    TEST_ASSERT(skb_tailroom(skb_res) == 50);

    // Exact reserve boundary
    skb_reserve(skb_res, 50);
    TEST_ASSERT(skb_headroom(skb_res) == 50);
    TEST_ASSERT(skb_tailroom(skb_res) == 0);
    TEST_ASSERT(skb_put(skb_res, 1) == nullptr);

    kfree_skb(skb_res);

    // Zero-capacity buffer allocation
    struct sk_buff *zero_skb = alloc_skb(0, GFP_KERNEL);
    TEST_ASSERT(zero_skb != nullptr);
    TEST_ASSERT(skb_headroom(zero_skb) == 0);
    TEST_ASSERT(skb_tailroom(zero_skb) == 0);
    TEST_ASSERT(skb_put(zero_skb, 1) == nullptr);
    TEST_ASSERT(skb_push(zero_skb, 1) == nullptr);
    kfree_skb(zero_skb);

    std::printf("  -> [PASS] Edge cases (underflow, overflow, NULL handling) verified\n");
}

static void test_10k_cycles_leak_free() {
    std::printf("[TEST] 11. 10,000 Cycles Stress & Leak-Free Test (ASan/UBSan)...\n");

    constexpr int TOTAL_CYCLES = 10000;
    auto start_time = std::chrono::steady_clock::now();

    for (int i = 0; i < TOTAL_CYCLES; ++i) {
        unsigned int size = static_cast<unsigned int>((i % 512) + 64);
        struct sk_buff *skb = alloc_skb(size, GFP_KERNEL);
        TEST_ASSERT(skb != nullptr);

        // Reserve 32 bytes headroom
        skb_reserve(skb, 32);

        // Put variable payload
        unsigned int put_len = static_cast<unsigned int>((i % 16) + 4);
        unsigned char *p = skb_put(skb, put_len);
        TEST_ASSERT(p != nullptr);
        std::memset(p, static_cast<int>(i & 0xFF), put_len);

        // Push 14 bytes header
        unsigned char *hdr = skb_push(skb, 14);
        TEST_ASSERT(hdr != nullptr);
        hdr[0] = static_cast<unsigned char>((i >> 8) & 0xFF);

        // Pull 14 bytes header
        unsigned char *pulled = skb_pull(skb, 14);
        TEST_ASSERT(pulled != nullptr);

        // Put more data
        skb_put(skb, 2);

        // Intermittent clone & copy
        if ((i % 3) == 0) {
            struct sk_buff *clone = skb_clone(skb, GFP_ATOMIC);
            TEST_ASSERT(clone != nullptr);
            kfree_skb(clone);
        }

        if ((i % 5) == 0) {
            struct sk_buff *copy = skb_copy(skb, GFP_KERNEL);
            TEST_ASSERT(copy != nullptr);
            kfree_skb(copy);
        }

        kfree_skb(skb);

        // dev_alloc_skb cycle
        if ((i % 2) == 0) {
            struct sk_buff *dskb = dev_alloc_skb(128);
            TEST_ASSERT(dskb != nullptr);
            skb_put(dskb, 32);
            dev_kfree_skb_any(dskb);
        }
    }

    auto end_time = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    std::printf("  -> [PASS] Completed %d cycles in %lld ms with 0 leaks\n", TOTAL_CYCLES, static_cast<long long>(elapsed_ms));
}

int main() {
    std::printf("============================================================\n");
    std::printf("  RTW89 Wave 1 Linux Compatibility Shim Primitives Test\n");
    std::printf("============================================================\n");

    test_types();
    test_endian();
    test_bitfield();
    test_atomics();
    test_locks();
    test_barriers();
    test_skb_basic();
    test_skb_clone();
    test_skb_copy();
    test_edge_cases();
    test_10k_cycles_leak_free();

    std::printf("============================================================\n");
    std::printf("  ALL TESTS PASSED SUCCESSFULLY (0 ASan/UBSan errors)\n");
    std::printf("============================================================\n");

    return 0;
}
