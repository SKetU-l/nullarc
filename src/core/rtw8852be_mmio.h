//
// rtw8852be_mmio.h — RTL8852BE MMIO Accessor & Mock Bus Engine
// Provides abstract MMIO interface Rtw89Mmio and mock bus MockMmioBus
//
#pragma once

#include "linux_shim.h"
#include "rtw8852be_reg.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <vector>

// Undefine preprocessor function-like macros for memory barriers so they can be
// provided cleanly as both global C++ inline functions and class methods.
#undef mb
#undef rmb
#undef wmb

// Global synchronous memory barriers
static inline void mb()  { __atomic_thread_fence(__ATOMIC_SEQ_CST); }
static inline void rmb() { __atomic_thread_fence(__ATOMIC_ACQUIRE); }
static inline void wmb() { __atomic_thread_fence(__ATOMIC_RELEASE); }

// ============================================================================
// 1. Abstract MMIO Accessor Class: Rtw89Mmio
// ============================================================================

class Rtw89Mmio {
public:
    virtual ~Rtw89Mmio() = default;

    // Fundamental MMIO read accessors
    virtual uint8_t  read8(uint32_t addr) = 0;
    virtual uint16_t read16(uint32_t addr) = 0;
    virtual uint32_t read32(uint32_t addr) = 0;

    // Fundamental MMIO write accessors
    virtual void write8(uint32_t addr, uint8_t val) = 0;
    virtual void write16(uint32_t addr, uint16_t val) = 0;
    virtual void write32(uint32_t addr, uint32_t val) = 0;

    // Masked read accessors
    virtual uint32_t read32_mask(uint32_t addr, uint32_t mask);
    virtual uint16_t read16_mask(uint32_t addr, uint16_t mask);
    virtual uint8_t  read8_mask(uint32_t addr, uint8_t mask);

    // Atomic read-modify-write accessors (synchronized with rmw_mutex_)
    virtual void write32_set(uint32_t addr, uint32_t bit);
    virtual void write32_clr(uint32_t addr, uint32_t bit);
    virtual void write32_mask(uint32_t addr, uint32_t mask, uint32_t val);

    virtual void write16_set(uint32_t addr, uint16_t bit);
    virtual void write16_clr(uint32_t addr, uint16_t bit);

    virtual void write8_set(uint32_t addr, uint8_t bit);
    virtual void write8_clr(uint32_t addr, uint8_t bit);

    // Polling with timeout in microseconds
    // Returns 0 on success, -ETIMEDOUT on timeout
    virtual int poll_timeout_atomic(uint32_t addr, uint64_t mask, uint64_t target_val,
                                    uint32_t timeout_us, uint32_t sleep_us = 1);

    // Template predicate polling (e.g. lambda [](uint32_t val) -> bool)
    template <typename Predicate,
              typename = std::enable_if_t<std::is_invocable_r_v<bool, Predicate, uint32_t>>>
    int poll_timeout_atomic(uint32_t addr, Predicate &&pred,
                            uint32_t timeout_us, uint32_t sleep_us = 1) {
        auto start = std::chrono::steady_clock::now();
        while (true) {
            uint32_t val = read32(addr);
            if (pred(val)) {
                return 0;
            }
            auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed >= timeout_us) {
                // Final check before reporting timeout
                val = read32(addr);
                if (pred(val)) {
                    return 0;
                }
                return -ETIMEDOUT;
            }
            if (sleep_us > 0) {
                if (sleep_us >= 1000) {
                    std::this_thread::sleep_for(std::chrono::microseconds(sleep_us));
                } else {
                    for (volatile int i = 0; i < 50; ++i) {}
                }
            }
        }
    }

    // Synchronous memory barriers
    static inline void mb()  { ::mb(); }
    static inline void rmb() { ::rmb(); }
    static inline void wmb() { ::wmb(); }

protected:
    mutable std::mutex rmw_mutex_;
};

// ============================================================================
// 2. Mock MMIO Bus Implementation: MockMmioBus
// ============================================================================

class MockMmioBus : public Rtw89Mmio {
public:
    explicit MockMmioBus(size_t size = 0x20000, bool strict_alignment = true);
    ~MockMmioBus() override = default;

    uint8_t  read8(uint32_t addr) override;
    uint16_t read16(uint32_t addr) override;
    uint32_t read32(uint32_t addr) override;

    void write8(uint32_t addr, uint8_t val) override;
    void write16(uint32_t addr, uint16_t val) override;
    void write32(uint32_t addr, uint32_t val) override;

    // Direct inspect / modify helpers (bypass handlers and stats)
    void poke8(uint32_t addr, uint8_t val);
    void poke16(uint32_t addr, uint16_t val);
    void poke32(uint32_t addr, uint32_t val);

    uint8_t  peek8(uint32_t addr) const;
    uint16_t peek16(uint32_t addr) const;
    uint32_t peek32(uint32_t addr) const;

    // Dynamic mock callbacks (for simulating device state changes on read/write)
    using ReadHandler = std::function<uint32_t(uint32_t addr)>;
    using WriteHandler = std::function<void(uint32_t addr, uint32_t val)>;

    void set_read_handler(uint32_t addr, ReadHandler handler);
    void set_write_handler(uint32_t addr, WriteHandler handler);
    void clear_handlers();

    // Alignment and boundary configuration
    void set_strict_alignment(bool strict) { strict_alignment_ = strict; }
    bool strict_alignment() const { return strict_alignment_; }
    size_t memory_size() const { return mem_.size(); }

    // Statistics and lifecycle
    size_t read_count() const { return read_count_.load(std::memory_order_relaxed); }
    size_t write_count() const { return write_count_.load(std::memory_order_relaxed); }
    void reset_stats() {
        read_count_.store(0, std::memory_order_relaxed);
        write_count_.store(0, std::memory_order_relaxed);
    }
    void reset_memory();

private:
    void check_bounds(uint32_t addr, size_t access_size) const;
    void check_alignment(uint32_t addr, size_t alignment) const;

    mutable std::mutex bus_mutex_;
    std::vector<uint8_t> mem_;
    bool strict_alignment_{true};
    std::atomic<size_t> read_count_{0};
    std::atomic<size_t> write_count_{0};

    std::unordered_map<uint32_t, ReadHandler> read_handlers_;
    std::unordered_map<uint32_t, WriteHandler> write_handlers_;
};

// ============================================================================
// 3. Direct Memory-Mapped Bus Implementation: DirectMmioBus
// ============================================================================

class DirectMmioBus : public Rtw89Mmio {
public:
    DirectMmioBus(volatile void *base, size_t size, bool strict_alignment = true);
    ~DirectMmioBus() override = default;

    uint8_t  read8(uint32_t addr) override;
    uint16_t read16(uint32_t addr) override;
    uint32_t read32(uint32_t addr) override;

    void write8(uint32_t addr, uint8_t val) override;
    void write16(uint32_t addr, uint16_t val) override;
    void write32(uint32_t addr, uint32_t val) override;

private:
    volatile uint8_t *base_{nullptr};
    size_t size_{0};
    bool strict_alignment_{true};
};
