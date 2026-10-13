#ifndef RTW89_COMPAT_RING_BUFFER_H
#define RTW89_COMPAT_RING_BUFFER_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>

/**
 * @file ring_buffer.h
 * @brief Lockless Single-Producer Single-Consumer (SPSC) Circular Ring Buffer.
 *
 * Implements rtw89_ring<T, Size> for high-performance lock-free queues in
 * rtw89 driver rings (e.g. TX/RX descriptor rings, packet pointer buffers).
 *
 * Key Design Highlights:
 * 1. Power-of-two size requirement enables branchless index calculation
 *    via bitwise AND masking: index = counter & (Size - 1).
 * 2. Unsigned monotonic 64-bit/size_t counters for head and tail. Overflow
 *    is well-defined by the C++ standard (modulo 2^N).
 * 3. Acquire-Release memory order semantics:
 *    - Producer writes data slot, then updates head with memory_order_release.
 *    - Consumer loads head with memory_order_acquire before reading data slot,
 *      then updates tail with memory_order_release.
 *    - Producer loads tail with memory_order_acquire to ensure slot consumption
 *      happens-before overwriting the slot.
 * 4. Separate cache line alignment (64 bytes) for producer (head) and consumer
 *    (tail) state variables to eliminate false sharing and cache-line bouncing.
 * 5. Cached shadow counters (cached_tail for producer, cached_head for consumer)
 *    to minimize cross-core atomic load bus traffic when buffer is neither full
 *    nor empty.
 */

template <typename T, size_t Size = 1024>
class rtw89_ring {
    static_assert(Size >= 2, "rtw89_ring: Size must be at least 2");
    static_assert((Size & (Size - 1)) == 0, "rtw89_ring: Size must be a power of two");

public:
    using value_type = T;
    using size_type = size_t;

    static constexpr size_t kCapacity = Size;
    static constexpr size_t kMask = Size - 1;

    rtw89_ring() : storage_(std::make_unique<Slot[]>(Size)) {}

    ~rtw89_ring() {
        clear();
    }

    // Non-copyable and non-movable to guarantee thread safety of atomic state
    rtw89_ring(const rtw89_ring&) = delete;
    rtw89_ring& operator=(const rtw89_ring&) = delete;
    rtw89_ring(rtw89_ring&&) = delete;
    rtw89_ring& operator=(rtw89_ring&&) = delete;

    /**
     * @brief Push an item by const reference (Producer thread only).
     * @param item The value to push.
     * @return true if pushed, false if ring is full.
     */
    bool push(const T& item) {
        const size_t current_head = head_.load(std::memory_order_relaxed);
        if (current_head - cached_tail_ >= Size) {
            cached_tail_ = tail_.load(std::memory_order_acquire);
            if (current_head - cached_tail_ >= Size) {
                return false;
            }
        }

        new (storage_[current_head & kMask].bytes) T(item);
        head_.store(current_head + 1, std::memory_order_release);
        return true;
    }

    /**
     * @brief Push an item by rvalue reference (Producer thread only).
     * @param item The rvalue to move into the ring.
     * @return true if pushed, false if ring is full.
     */
    bool push(T&& item) {
        const size_t current_head = head_.load(std::memory_order_relaxed);
        if (current_head - cached_tail_ >= Size) {
            cached_tail_ = tail_.load(std::memory_order_acquire);
            if (current_head - cached_tail_ >= Size) {
                return false;
            }
        }

        new (storage_[current_head & kMask].bytes) T(std::move(item));
        head_.store(current_head + 1, std::memory_order_release);
        return true;
    }

    /**
     * @brief Emplace an item in-place (Producer thread only).
     * @return true if emplaced, false if ring is full.
     */
    template <typename... Args>
    bool emplace(Args&&... args) {
        const size_t current_head = head_.load(std::memory_order_relaxed);
        if (current_head - cached_tail_ >= Size) {
            cached_tail_ = tail_.load(std::memory_order_acquire);
            if (current_head - cached_tail_ >= Size) {
                return false;
            }
        }

        new (storage_[current_head & kMask].bytes) T(std::forward<Args>(args)...);
        head_.store(current_head + 1, std::memory_order_release);
        return true;
    }

    /**
     * @brief Pop an item out of the ring (Consumer thread only).
     * @param out Destination reference to receive moved item.
     * @return true if popped, false if ring is empty.
     */
    bool pop(T& out) {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        if (current_tail == cached_head_) {
            cached_head_ = head_.load(std::memory_order_acquire);
            if (current_tail == cached_head_) {
                return false;
            }
        }

        T* item_ptr = reinterpret_cast<T*>(storage_[current_tail & kMask].bytes);
        out = std::move(*item_ptr);
        item_ptr->~T();
        tail_.store(current_tail + 1, std::memory_order_release);
        return true;
    }

    /**
     * @brief Pop an item returning std::optional (Consumer thread only).
     * @return std::optional containing popped item, or std::nullopt if empty.
     */
    std::optional<T> pop() {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        if (current_tail == cached_head_) {
            cached_head_ = head_.load(std::memory_order_acquire);
            if (current_tail == cached_head_) {
                return std::nullopt;
            }
        }

        T* item_ptr = reinterpret_cast<T*>(storage_[current_tail & kMask].bytes);
        std::optional<T> result(std::move(*item_ptr));
        item_ptr->~T();
        tail_.store(current_tail + 1, std::memory_order_release);
        return result;
    }

    /**
     * @brief Peek at the front item without removing it (Consumer thread only).
     * @return Pointer to front item, or nullptr if empty.
     */
    const T* peek() const noexcept {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        if (current_tail == cached_head_) {
            cached_head_ = head_.load(std::memory_order_acquire);
            if (current_tail == cached_head_) {
                return nullptr;
            }
        }
        return reinterpret_cast<const T*>(storage_[current_tail & kMask].bytes);
    }

    /**
     * @brief Mutable peek at the front item (Consumer thread only).
     * @return Pointer to front item, or nullptr if empty.
     */
    T* peek() noexcept {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        if (current_tail == cached_head_) {
            cached_head_ = head_.load(std::memory_order_acquire);
            if (current_tail == cached_head_) {
                return nullptr;
            }
        }
        return reinterpret_cast<T*>(storage_[current_tail & kMask].bytes);
    }

    /**
     * @brief Peek at the front item copying into out (Consumer thread only).
     * @param out Destination reference.
     * @return true if item was peeked, false if empty.
     */
    bool peek(T& out) const {
        const T* item_ptr = peek();
        if (!item_ptr) {
            return false;
        }
        out = *item_ptr;
        return true;
    }

    /**
     * @brief Check whether ring is empty.
     * Thread-safe query with acquire semantics.
     */
    bool is_empty() const noexcept {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }

    /**
     * @brief Check whether ring is full.
     * Thread-safe query with acquire semantics.
     */
    bool is_full() const noexcept {
        return count() >= Size;
    }

    /**
     * @brief Return the number of items currently in the ring buffer.
     * Thread-safe query.
     */
    size_t count() const noexcept {
        const size_t t = tail_.load(std::memory_order_acquire);
        const size_t h = head_.load(std::memory_order_acquire);
        const size_t diff = h - t;
        if (diff > Size) {
            return (h >= t) ? Size : 0;
        }
        return diff;
    }

    /**
     * @brief Alias for count().
     */
    size_t size() const noexcept {
        return count();
    }

    /**
     * @brief Return the maximum capacity of the ring buffer.
     */
    constexpr size_t capacity() const noexcept {
        return Size;
    }

    /**
     * @brief Clear all elements from the ring buffer, invoking destructors.
     * Should only be called when producer is quiescent.
     */
    void clear() noexcept {
        size_t t = tail_.load(std::memory_order_relaxed);
        const size_t h = head_.load(std::memory_order_acquire);
        while (t != h) {
            T* item_ptr = reinterpret_cast<T*>(storage_[t & kMask].bytes);
            item_ptr->~T();
            ++t;
        }
        tail_.store(t, std::memory_order_release);
        cached_head_ = t;
        cached_tail_ = t;
    }

private:
    struct alignas(alignof(T)) Slot {
        alignas(T) unsigned char bytes[sizeof(T)];
    };

    // Heap-allocated slot storage avoids stack overflows for large Size
    std::unique_ptr<Slot[]> storage_;

    // Producer state cacheline
    alignas(64) std::atomic<size_t> head_{0};
    mutable size_t cached_tail_{0};

    // Consumer state cacheline
    alignas(64) std::atomic<size_t> tail_{0};
    mutable size_t cached_head_{0};
};

#endif // RTW89_COMPAT_RING_BUFFER_H
