//
// linux_shim.h — Freestanding Linux Compatibility Shim Primitives
// For rtw89 driver on macOS / freestanding C++17 environments
//
#pragma once

#if defined(KERNEL) || defined(__APPLE_KEXT__)
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <sys/errno.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace std {
    template <typename T>
    constexpr const T& min(const T& a, const T& b) {
        return (b < a) ? b : a;
    }
    template <typename T>
    constexpr const T& max(const T& a, const T& b) {
        return (a < b) ? b : a;
    }

    using memory_order = int;
    constexpr int memory_order_relaxed = 0;
    constexpr int memory_order_acquire = 1;
    constexpr int memory_order_release = 2;
    constexpr int memory_order_seq_cst = 3;

    template <typename T>
    struct atomic {
        volatile T val;
        constexpr atomic(T v = 0) : val(v) {}
        T load(int = 0) const { return __atomic_load_n(&val, __ATOMIC_SEQ_CST); }
        void store(T v, int = 0) { __atomic_store_n(&val, v, __ATOMIC_SEQ_CST); }
        T fetch_add(T v, int = 0) { return __atomic_fetch_add(&val, v, __ATOMIC_SEQ_CST); }
        T fetch_sub(T v, int = 0) { return __atomic_fetch_sub(&val, v, __ATOMIC_SEQ_CST); }
    };
}
#else
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <algorithm>
#include <atomic>
#endif

// ============================================================================
// 1. Freestanding Standard Integer Types
// ============================================================================

using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

using s8  = int8_t;
using s16 = int16_t;
using s32 = int32_t;
using s64 = int64_t;

#ifndef _LINUX_TYPES_H
using __u8  = uint8_t;
using __u16 = uint16_t;
using __u32 = uint32_t;
using __u64 = uint64_t;

using __s8  = int8_t;
using __s16 = int16_t;
using __s32 = int32_t;
using __s64 = int64_t;

using __le16 = uint16_t;
using __le32 = uint32_t;
using __le64 = uint64_t;

using __be16 = uint16_t;
using __be32 = uint32_t;
using __be64 = uint64_t;
#endif

using dma_addr_t = uint64_t;
using gfp_t = uint32_t;

#ifndef GFP_KERNEL
#define GFP_KERNEL 0U
#endif

#ifndef GFP_ATOMIC
#define GFP_ATOMIC 1U
#endif

// ============================================================================
// 2. Endian Conversion Macros / Inlines
// ============================================================================

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
static inline u16 cpu_to_le16(u16 v) { return __builtin_bswap16(v); }
static inline u32 cpu_to_le32(u32 v) { return __builtin_bswap32(v); }
static inline u64 cpu_to_le64(u64 v) { return __builtin_bswap64(v); }

static inline u16 le16_to_cpu(u16 v) { return __builtin_bswap16(v); }
static inline u32 le32_to_cpu(u32 v) { return __builtin_bswap32(v); }
static inline u64 le64_to_cpu(u64 v) { return __builtin_bswap64(v); }

static inline u16 cpu_to_be16(u16 v) { return v; }
static inline u32 cpu_to_be32(u32 v) { return v; }
static inline u64 cpu_to_be64(u64 v) { return v; }

static inline u16 be16_to_cpu(u16 v) { return v; }
static inline u32 be32_to_cpu(u32 v) { return v; }
static inline u64 be64_to_cpu(u64 v) { return v; }
#else
static inline u16 cpu_to_le16(u16 v) { return v; }
static inline u32 cpu_to_le32(u32 v) { return v; }
static inline u64 cpu_to_le64(u64 v) { return v; }

static inline u16 le16_to_cpu(u16 v) { return v; }
static inline u32 le32_to_cpu(u32 v) { return v; }
static inline u64 le64_to_cpu(u64 v) { return v; }

static inline u16 cpu_to_be16(u16 v) { return __builtin_bswap16(v); }
static inline u32 cpu_to_be32(u32 v) { return __builtin_bswap32(v); }
static inline u64 cpu_to_be64(u64 v) { return __builtin_bswap64(v); }

static inline u16 be16_to_cpu(u16 v) { return __builtin_bswap16(v); }
static inline u32 be32_to_cpu(u32 v) { return __builtin_bswap32(v); }
static inline u64 be64_to_cpu(u64 v) { return __builtin_bswap64(v); }
#endif

// ============================================================================
// 3. Bitfield Helpers
// ============================================================================

#ifndef BIT
#define BIT(n) (1ULL << (n))
#endif

#ifndef BIT_ULL
#define BIT_ULL(n) (1ULL << (n))
#endif

#ifndef GENMASK
#define GENMASK(h, l) \
    (((~0ULL) << (l)) & ((h) >= 63 ? ~0ULL : ((1ULL << ((h) + 1)) - 1ULL)))
#endif

#ifndef GENMASK_ULL
#define GENMASK_ULL(h, l) GENMASK(h, l)
#endif

#ifndef FIELD_PREP
#define FIELD_PREP(mask, val) \
    static_cast<decltype(mask)>((((static_cast<unsigned long long>(val)) << __builtin_ctzll(static_cast<unsigned long long>(mask))) & static_cast<unsigned long long>(mask)))
#endif

#ifndef FIELD_GET
#define FIELD_GET(mask, val) \
    static_cast<decltype(mask)>(((static_cast<unsigned long long>(val) & static_cast<unsigned long long>(mask)) >> __builtin_ctzll(static_cast<unsigned long long>(mask))))
#endif

static inline void le32p_replace_bits(__le32 *p, u32 val, u32 mask) {
    *p = cpu_to_le32((le32_to_cpu(*p) & ~mask) | static_cast<u32>(FIELD_PREP(mask, val)));
}

static inline void u32p_replace_bits(u32 *p, u32 val, u32 mask) {
    *p = (*p & ~mask) | static_cast<u32>(FIELD_PREP(mask, val));
}

// ============================================================================
// 4. Atomic Primitives
// ============================================================================

struct atomic_t {
    int counter;
};

#define ATOMIC_INIT(i) { (i) }

static inline int atomic_read(const atomic_t *v) {
    return __atomic_load_n(&v->counter, __ATOMIC_SEQ_CST);
}

static inline void atomic_set(atomic_t *v, int i) {
    __atomic_store_n(&v->counter, i, __ATOMIC_SEQ_CST);
}

static inline void atomic_inc(atomic_t *v) {
    __atomic_add_fetch(&v->counter, 1, __ATOMIC_SEQ_CST);
}

static inline void atomic_dec(atomic_t *v) {
    __atomic_sub_fetch(&v->counter, 1, __ATOMIC_SEQ_CST);
}

static inline void atomic_add(int i, atomic_t *v) {
    __atomic_add_fetch(&v->counter, i, __ATOMIC_SEQ_CST);
}

static inline void atomic_sub(int i, atomic_t *v) {
    __atomic_sub_fetch(&v->counter, i, __ATOMIC_SEQ_CST);
}

static inline int atomic_inc_return(atomic_t *v) {
    return __atomic_add_fetch(&v->counter, 1, __ATOMIC_SEQ_CST);
}

static inline int atomic_dec_return(atomic_t *v) {
    return __atomic_sub_fetch(&v->counter, 1, __ATOMIC_SEQ_CST);
}

static inline int atomic_add_return(int i, atomic_t *v) {
    return __atomic_add_fetch(&v->counter, i, __ATOMIC_SEQ_CST);
}

static inline int atomic_sub_return(int i, atomic_t *v) {
    return __atomic_sub_fetch(&v->counter, i, __ATOMIC_SEQ_CST);
}

static inline bool atomic_dec_and_test(atomic_t *v) {
    return __atomic_sub_fetch(&v->counter, 1, __ATOMIC_SEQ_CST) == 0;
}

static inline bool atomic_inc_and_test(atomic_t *v) {
    return __atomic_add_fetch(&v->counter, 1, __ATOMIC_SEQ_CST) == 0;
}

#define BITS_PER_BYTE 8
#define BITS_PER_LONG (sizeof(unsigned long) * BITS_PER_BYTE)
#define BITS_TO_LONGS(nr) (((nr) + BITS_PER_LONG - 1) / BITS_PER_LONG)
#define DECLARE_BITMAP(name, bits) unsigned long name[BITS_TO_LONGS(bits)]

static inline void set_bit(long nr, volatile unsigned long *addr) {
    unsigned long mask = 1UL << (nr % BITS_PER_LONG);
    volatile unsigned long *p = addr + (nr / BITS_PER_LONG);
    __atomic_fetch_or(p, mask, __ATOMIC_SEQ_CST);
}

static inline void clear_bit(long nr, volatile unsigned long *addr) {
    unsigned long mask = 1UL << (nr % BITS_PER_LONG);
    volatile unsigned long *p = addr + (nr / BITS_PER_LONG);
    __atomic_fetch_and(p, ~mask, __ATOMIC_SEQ_CST);
}

static inline bool test_bit(long nr, const volatile unsigned long *addr) {
    unsigned long mask = 1UL << (nr % BITS_PER_LONG);
    const volatile unsigned long *p = addr + (nr / BITS_PER_LONG);
    unsigned long val = __atomic_load_n(p, __ATOMIC_SEQ_CST);
    return (val & mask) != 0;
}

static inline bool test_and_set_bit(long nr, volatile unsigned long *addr) {
    unsigned long mask = 1UL << (nr % BITS_PER_LONG);
    volatile unsigned long *p = addr + (nr / BITS_PER_LONG);
    unsigned long old = __atomic_fetch_or(p, mask, __ATOMIC_SEQ_CST);
    return (old & mask) != 0;
}

static inline bool test_and_clear_bit(long nr, volatile unsigned long *addr) {
    unsigned long mask = 1UL << (nr % BITS_PER_LONG);
    volatile unsigned long *p = addr + (nr / BITS_PER_LONG);
    unsigned long old = __atomic_fetch_and(p, ~mask, __ATOMIC_SEQ_CST);
    return (old & mask) != 0;
}

// ============================================================================
// 5. Locking Wrappers
// ============================================================================

#if defined(KERNEL) || defined(__APPLE_KEXT__)

struct spinlock_t {
    IOSimpleLock *lock;
};

static inline void spin_lock_init(spinlock_t *lock) {
    lock->lock = IOSimpleLockAlloc();
}

static inline void spin_lock(spinlock_t *lock) {
    IOSimpleLockLock(lock->lock);
}

static inline void spin_unlock(spinlock_t *lock) {
    IOSimpleLockUnlock(lock->lock);
}

static inline bool spin_trylock(spinlock_t *lock) {
    return IOSimpleLockTryLock(lock->lock) != 0;
}

static inline void spin_lock_destroy(spinlock_t *lock) {
    if (lock->lock) {
        IOSimpleLockFree(lock->lock);
        lock->lock = nullptr;
    }
}

struct mutex_t {
    IOLock *lock;
};

static inline void mutex_init(mutex_t *lock) {
    lock->lock = IOLockAlloc();
}

static inline void mutex_lock(mutex_t *lock) {
    IOLockLock(lock->lock);
}

static inline void mutex_unlock(mutex_t *lock) {
    IOLockUnlock(lock->lock);
}

static inline int mutex_trylock(mutex_t *lock) {
    return IOLockTryLock(lock->lock) != 0 ? 1 : 0;
}

static inline void mutex_destroy(mutex_t *lock) {
    if (lock->lock) {
        IOLockFree(lock->lock);
        lock->lock = nullptr;
    }
}

#else // User test / freestanding POSIX mode

struct spinlock_t {
    pthread_mutex_t mtx;
};

#define DEFINE_SPINLOCK(x) spinlock_t x = {PTHREAD_MUTEX_INITIALIZER}

static inline void spin_lock_init(spinlock_t *lock) {
    pthread_mutex_init(&lock->mtx, nullptr);
}

static inline void spin_lock(spinlock_t *lock) {
    pthread_mutex_lock(&lock->mtx);
}

static inline void spin_unlock(spinlock_t *lock) {
    pthread_mutex_unlock(&lock->mtx);
}

static inline bool spin_trylock(spinlock_t *lock) {
    return pthread_mutex_trylock(&lock->mtx) == 0;
}

static inline void spin_lock_destroy(spinlock_t *lock) {
    pthread_mutex_destroy(&lock->mtx);
}

struct mutex_t {
    pthread_mutex_t mtx;
};

#define DEFINE_MUTEX(x) mutex_t x = {PTHREAD_MUTEX_INITIALIZER}

static inline void mutex_init(mutex_t *lock) {
    pthread_mutex_init(&lock->mtx, nullptr);
}

static inline void mutex_lock(mutex_t *lock) {
    pthread_mutex_lock(&lock->mtx);
}

static inline void mutex_unlock(mutex_t *lock) {
    pthread_mutex_unlock(&lock->mtx);
}

static inline int mutex_trylock(mutex_t *lock) {
    return pthread_mutex_trylock(&lock->mtx) == 0 ? 1 : 0;
}

static inline void mutex_destroy(mutex_t *lock) {
    pthread_mutex_destroy(&lock->mtx);
}

#endif

#define spin_lock_irqsave(lock, flags) \
    do { \
        (flags) = 0; \
        spin_lock(lock); \
    } while (0)

#define spin_unlock_irqrestore(lock, flags) \
    do { \
        (void)(flags); \
        spin_unlock(lock); \
    } while (0)

// ============================================================================
// 6. Memory Barriers
// ============================================================================

#define barrier() __asm__ __volatile__("" ::: "memory")
#define mb() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#define rmb() __atomic_thread_fence(__ATOMIC_ACQUIRE)
#define wmb() __atomic_thread_fence(__ATOMIC_RELEASE)
#define smp_mb() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#define smp_rmb() __atomic_thread_fence(__ATOMIC_ACQUIRE)
#define smp_wmb() __atomic_thread_fence(__ATOMIC_RELEASE)

// ============================================================================
// 7. Utility Helpers
// ============================================================================

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

#ifndef container_of
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
#endif

#ifndef likely
#define likely(x) __builtin_expect(!!(x), 1)
#endif

#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

template <typename T>
static inline constexpr const T& min(const T& a, const T& b) {
    return (a < b) ? a : b;
}

template <typename T>
static inline constexpr const T& max(const T& a, const T& b) {
    return (a > b) ? a : b;
}

template <typename T>
static inline constexpr const T& clamp(const T& val, const T& lo, const T& hi) {
    return (val < lo) ? lo : (hi < val) ? hi : val;
}
