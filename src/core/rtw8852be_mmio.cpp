//
// rtw8852be_mmio.cpp — RTL8852BE MMIO Accessor & Mock Bus Implementation
//
#include "rtw8852be_mmio.h"

#if !defined(KERNEL) && !defined(__APPLE_KEXT__)
#include <cstring>
#include <sstream>
#endif

// ============================================================================
// 1. Rtw89Mmio Default Implementations
// ============================================================================

uint32_t Rtw89Mmio::read32_mask(uint32_t addr, uint32_t mask) {
    if (mask == 0) {
        return 0;
    }
    uint32_t val = read32(addr);
    return static_cast<uint32_t>(FIELD_GET(mask, val));
}

uint16_t Rtw89Mmio::read16_mask(uint32_t addr, uint16_t mask) {
    if (mask == 0) {
        return 0;
    }
    uint16_t val = read16(addr);
    return static_cast<uint16_t>(FIELD_GET(mask, val));
}

uint8_t Rtw89Mmio::read8_mask(uint32_t addr, uint8_t mask) {
    if (mask == 0) {
        return 0;
    }
    uint8_t val = read8(addr);
    return static_cast<uint8_t>(FIELD_GET(mask, val));
}

void Rtw89Mmio::write32_set(uint32_t addr, uint32_t bit) {
#if !defined(KERNEL) && !defined(__APPLE_KEXT__)
    std::lock_guard<std::mutex> lock(rmw_mutex_);
#endif
    uint32_t val = read32(addr);
    write32(addr, val | bit);
}

void Rtw89Mmio::write32_clr(uint32_t addr, uint32_t bit) {
#if !defined(KERNEL) && !defined(__APPLE_KEXT__)
    std::lock_guard<std::mutex> lock(rmw_mutex_);
#endif
    uint32_t val = read32(addr);
    write32(addr, val & ~bit);
}

void Rtw89Mmio::write32_mask(uint32_t addr, uint32_t mask, uint32_t val) {
    if (mask == 0) {
        return;
    }
#if !defined(KERNEL) && !defined(__APPLE_KEXT__)
    std::lock_guard<std::mutex> lock(rmw_mutex_);
#endif
    uint32_t orig = read32(addr);
    uint32_t set = (orig & ~mask) | static_cast<uint32_t>(FIELD_PREP(mask, val));
    write32(addr, set);
}

void Rtw89Mmio::write16_set(uint32_t addr, uint16_t bit) {
#if !defined(KERNEL) && !defined(__APPLE_KEXT__)
    std::lock_guard<std::mutex> lock(rmw_mutex_);
#endif
    uint16_t val = read16(addr);
    write16(addr, val | bit);
}

void Rtw89Mmio::write16_clr(uint32_t addr, uint16_t bit) {
#if !defined(KERNEL) && !defined(__APPLE_KEXT__)
    std::lock_guard<std::mutex> lock(rmw_mutex_);
#endif
    uint16_t val = read16(addr);
    write16(addr, val & ~bit);
}

void Rtw89Mmio::write8_set(uint32_t addr, uint8_t bit) {
#if !defined(KERNEL) && !defined(__APPLE_KEXT__)
    std::lock_guard<std::mutex> lock(rmw_mutex_);
#endif
    uint8_t val = read8(addr);
    write8(addr, val | bit);
}

void Rtw89Mmio::write8_clr(uint32_t addr, uint8_t bit) {
#if !defined(KERNEL) && !defined(__APPLE_KEXT__)
    std::lock_guard<std::mutex> lock(rmw_mutex_);
#endif
    uint8_t val = read8(addr);
    write8(addr, val & ~bit);
}

int Rtw89Mmio::poll_timeout_atomic(uint32_t addr, uint64_t mask, uint64_t target_val,
                                   uint32_t timeout_us, uint32_t sleep_us) {
#if defined(KERNEL) || defined(__APPLE_KEXT__)
    uint32_t waited = 0;
    while (true) {
        uint32_t val = read32(addr);
        if ((static_cast<uint64_t>(val) & mask) == target_val) {
            return 0;
        }
        if (waited >= timeout_us) {
            val = read32(addr);
            if ((static_cast<uint64_t>(val) & mask) == target_val) return 0;
            return -ETIMEDOUT;
        }
        if (sleep_us > 0) {
            IODelay(sleep_us);
            waited += sleep_us;
        } else {
            IODelay(1);
            waited += 1;
        }
    }
#else
    return poll_timeout_atomic(addr, [mask, target_val](uint32_t val) {
        return (static_cast<uint64_t>(val) & mask) == target_val;
    }, timeout_us, sleep_us);
#endif
}

#if !defined(KERNEL) && !defined(__APPLE_KEXT__)
// ============================================================================
// 2. MockMmioBus Implementations
// ============================================================================

MockMmioBus::MockMmioBus(size_t size, bool strict_alignment)
    : mem_(size, 0), strict_alignment_(strict_alignment) {}

void MockMmioBus::check_bounds(uint32_t addr, size_t access_size) const {
    if (static_cast<size_t>(addr) + access_size > mem_.size()) {
        std::ostringstream oss;
        oss << "Mock MMIO address out of bounds: addr=0x" << std::hex << addr
            << ", size=" << std::dec << access_size
            << ", limit=0x" << std::hex << mem_.size();
        throw std::out_of_range(oss.str());
    }
}

void MockMmioBus::check_alignment(uint32_t addr, size_t alignment) const {
    if (strict_alignment_ && (addr & (alignment - 1)) != 0) {
        std::ostringstream oss;
        oss << "Unaligned " << (alignment * 8) << "-bit MMIO access: addr=0x"
            << std::hex << addr;
        throw std::runtime_error(oss.str());
    }
}

uint8_t MockMmioBus::read8(uint32_t addr) {
    check_bounds(addr, 1);
    read_count_.fetch_add(1, std::memory_order_relaxed);

    std::lock_guard<std::mutex> lock(bus_mutex_);
    auto it = read_handlers_.find(addr);
    if (it != read_handlers_.end()) {
        return static_cast<uint8_t>(it->second(addr));
    }
    return mem_[addr];
}

uint16_t MockMmioBus::read16(uint32_t addr) {
    check_alignment(addr, 2);
    check_bounds(addr, 2);
    read_count_.fetch_add(1, std::memory_order_relaxed);

    std::lock_guard<std::mutex> lock(bus_mutex_);
    auto it = read_handlers_.find(addr);
    if (it != read_handlers_.end()) {
        return static_cast<uint16_t>(it->second(addr));
    }
    uint16_t le_val;
    std::memcpy(&le_val, &mem_[addr], sizeof(le_val));
    return le16_to_cpu(le_val);
}

uint32_t MockMmioBus::read32(uint32_t addr) {
    check_alignment(addr, 4);
    check_bounds(addr, 4);
    read_count_.fetch_add(1, std::memory_order_relaxed);

    std::lock_guard<std::mutex> lock(bus_mutex_);
    auto it = read_handlers_.find(addr);
    if (it != read_handlers_.end()) {
        return it->second(addr);
    }
    uint32_t le_val;
    std::memcpy(&le_val, &mem_[addr], sizeof(le_val));
    return le32_to_cpu(le_val);
}

void MockMmioBus::write8(uint32_t addr, uint8_t val) {
    check_bounds(addr, 1);
    write_count_.fetch_add(1, std::memory_order_relaxed);

    std::lock_guard<std::mutex> lock(bus_mutex_);
    mem_[addr] = val;
    auto it = write_handlers_.find(addr);
    if (it != write_handlers_.end()) {
        it->second(addr, val);
    }
}

void MockMmioBus::write16(uint32_t addr, uint16_t val) {
    check_alignment(addr, 2);
    check_bounds(addr, 2);
    write_count_.fetch_add(1, std::memory_order_relaxed);

    std::lock_guard<std::mutex> lock(bus_mutex_);
    uint16_t le_val = cpu_to_le16(val);
    std::memcpy(&mem_[addr], &le_val, sizeof(le_val));
    auto it = write_handlers_.find(addr);
    if (it != write_handlers_.end()) {
        it->second(addr, val);
    }
}

void MockMmioBus::write32(uint32_t addr, uint32_t val) {
    check_alignment(addr, 4);
    check_bounds(addr, 4);
    write_count_.fetch_add(1, std::memory_order_relaxed);

    std::lock_guard<std::mutex> lock(bus_mutex_);
    uint32_t le_val = cpu_to_le32(val);
    std::memcpy(&mem_[addr], &le_val, sizeof(le_val));
    auto it = write_handlers_.find(addr);
    if (it != write_handlers_.end()) {
        it->second(addr, val);
    }
}

void MockMmioBus::poke8(uint32_t addr, uint8_t val) {
    check_bounds(addr, 1);
    std::lock_guard<std::mutex> lock(bus_mutex_);
    mem_[addr] = val;
}

void MockMmioBus::poke16(uint32_t addr, uint16_t val) {
    check_bounds(addr, 2);
    std::lock_guard<std::mutex> lock(bus_mutex_);
    uint16_t le_val = cpu_to_le16(val);
    std::memcpy(&mem_[addr], &le_val, sizeof(le_val));
}

void MockMmioBus::poke32(uint32_t addr, uint32_t val) {
    check_bounds(addr, 4);
    std::lock_guard<std::mutex> lock(bus_mutex_);
    uint32_t le_val = cpu_to_le32(val);
    std::memcpy(&mem_[addr], &le_val, sizeof(le_val));
}

uint8_t MockMmioBus::peek8(uint32_t addr) const {
    check_bounds(addr, 1);
    std::lock_guard<std::mutex> lock(bus_mutex_);
    return mem_[addr];
}

uint16_t MockMmioBus::peek16(uint32_t addr) const {
    check_bounds(addr, 2);
    std::lock_guard<std::mutex> lock(bus_mutex_);
    uint16_t le_val;
    std::memcpy(&le_val, &mem_[addr], sizeof(le_val));
    return le16_to_cpu(le_val);
}

uint32_t MockMmioBus::peek32(uint32_t addr) const {
    check_bounds(addr, 4);
    std::lock_guard<std::mutex> lock(bus_mutex_);
    uint32_t le_val;
    std::memcpy(&le_val, &mem_[addr], sizeof(le_val));
    return le32_to_cpu(le_val);
}

void MockMmioBus::set_read_handler(uint32_t addr, ReadHandler handler) {
    std::lock_guard<std::mutex> lock(bus_mutex_);
    read_handlers_[addr] = std::move(handler);
}

void MockMmioBus::set_write_handler(uint32_t addr, WriteHandler handler) {
    std::lock_guard<std::mutex> lock(bus_mutex_);
    write_handlers_[addr] = std::move(handler);
}

void MockMmioBus::clear_handlers() {
    std::lock_guard<std::mutex> lock(bus_mutex_);
    read_handlers_.clear();
    write_handlers_.clear();
}

void MockMmioBus::reset_memory() {
    std::lock_guard<std::mutex> lock(bus_mutex_);
    std::fill(mem_.begin(), mem_.end(), 0);
    read_handlers_.clear();
    write_handlers_.clear();
    reset_stats();
}
#endif

// ============================================================================
// 3. DirectMmioBus Implementations
// ============================================================================

DirectMmioBus::DirectMmioBus(volatile void *base, size_t size, bool strict_alignment)
    : base_(static_cast<volatile uint8_t *>(base)), size_(size), strict_alignment_(strict_alignment) {}

uint8_t DirectMmioBus::read8(uint32_t addr) {
    if (addr + 1 > size_) {
#if !defined(KERNEL) && !defined(__APPLE_KEXT__)
        throw std::out_of_range("Direct MMIO address out of range");
#else
        return 0xFF;
#endif
    }
    rmb();
    return base_[addr];
}

uint16_t DirectMmioBus::read16(uint32_t addr) {
    if (strict_alignment_ && (addr & 1) != 0) {
#if !defined(KERNEL) && !defined(__APPLE_KEXT__)
        throw std::runtime_error("Unaligned 16-bit direct MMIO read");
#else
        return 0xFFFF;
#endif
    }
    if (addr + 2 > size_) {
#if !defined(KERNEL) && !defined(__APPLE_KEXT__)
        throw std::out_of_range("Direct MMIO address out of range");
#else
        return 0xFFFF;
#endif
    }
    rmb();
    volatile const uint16_t *ptr = reinterpret_cast<volatile const uint16_t *>(base_ + addr);
    return le16_to_cpu(*ptr);
}

uint32_t DirectMmioBus::read32(uint32_t addr) {
    if (strict_alignment_ && (addr & 3) != 0) {
#if !defined(KERNEL) && !defined(__APPLE_KEXT__)
        throw std::runtime_error("Unaligned 32-bit direct MMIO read");
#else
        return 0xFFFFFFFF;
#endif
    }
    if (addr + 4 > size_) {
#if !defined(KERNEL) && !defined(__APPLE_KEXT__)
        throw std::out_of_range("Direct MMIO address out of range");
#else
        return 0xFFFFFFFF;
#endif
    }
    rmb();
    volatile const uint32_t *ptr = reinterpret_cast<volatile const uint32_t *>(base_ + addr);
    return le32_to_cpu(*ptr);
}

void DirectMmioBus::write8(uint32_t addr, uint8_t val) {
    if (addr + 1 > size_) {
#if !defined(KERNEL) && !defined(__APPLE_KEXT__)
        throw std::out_of_range("Direct MMIO address out of range");
#else
        return;
#endif
    }
    base_[addr] = val;
    wmb();
}

void DirectMmioBus::write16(uint32_t addr, uint16_t val) {
    if (strict_alignment_ && (addr & 1) != 0) {
#if !defined(KERNEL) && !defined(__APPLE_KEXT__)
        throw std::runtime_error("Unaligned 16-bit direct MMIO write");
#else
        return;
#endif
    }
    if (addr + 2 > size_) {
#if !defined(KERNEL) && !defined(__APPLE_KEXT__)
        throw std::out_of_range("Direct MMIO address out of range");
#else
        return;
#endif
    }
    volatile uint16_t *ptr = reinterpret_cast<volatile uint16_t *>(base_ + addr);
    *ptr = cpu_to_le16(val);
    wmb();
}

void DirectMmioBus::write32(uint32_t addr, uint32_t val) {
    if (strict_alignment_ && (addr & 3) != 0) {
#if !defined(KERNEL) && !defined(__APPLE_KEXT__)
        throw std::runtime_error("Unaligned 32-bit direct MMIO write");
#else
        return;
#endif
    }
    if (addr + 4 > size_) {
#if !defined(KERNEL) && !defined(__APPLE_KEXT__)
        throw std::out_of_range("Direct MMIO address out of range");
#else
        return;
#endif
    }
    volatile uint32_t *ptr = reinterpret_cast<volatile uint32_t *>(base_ + addr);
    *ptr = cpu_to_le32(val);
    wmb();
}
