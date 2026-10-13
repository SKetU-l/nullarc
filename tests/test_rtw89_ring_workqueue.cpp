#include "ring_buffer.h"
#include "workqueue.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// Simple test framework macros
#define TEST_ASSERT(cond, msg, ...)                                           \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "[FAIL] Line %d: " msg "\n", __LINE__,       \
                         ##__VA_ARGS__);                                       \
            std::exit(1);                                                      \
        }                                                                      \
    } while (0)

#define TEST_LOG(fmt, ...)                                                     \
    do {                                                                       \
        std::printf("[TEST] " fmt "\n", ##__VA_ARGS__);                       \
        std::fflush(stdout);                                                   \
    } while (0)

// =============================================================================
// Helper: Lifetime Tracker to verify exact construction and destruction
// =============================================================================
struct LifetimeTracker {
    static inline std::atomic<int64_t> s_constructions{0};
    static inline std::atomic<int64_t> s_destructions{0};
    static inline std::atomic<int64_t> s_moves{0};

    int id{0};

    explicit LifetimeTracker(int val = 0) : id(val) {
        s_constructions.fetch_add(1, std::memory_order_relaxed);
    }

    LifetimeTracker(const LifetimeTracker& other) : id(other.id) {
        s_constructions.fetch_add(1, std::memory_order_relaxed);
    }

    LifetimeTracker(LifetimeTracker&& other) noexcept : id(other.id) {
        s_moves.fetch_add(1, std::memory_order_relaxed);
        s_constructions.fetch_add(1, std::memory_order_relaxed);
        other.id = -1;
    }

    LifetimeTracker& operator=(const LifetimeTracker& other) {
        id = other.id;
        return *this;
    }

    LifetimeTracker& operator=(LifetimeTracker&& other) noexcept {
        id = other.id;
        other.id = -1;
        s_moves.fetch_add(1, std::memory_order_relaxed);
        return *this;
    }

    ~LifetimeTracker() {
        s_destructions.fetch_add(1, std::memory_order_relaxed);
    }

    static void reset() {
        s_constructions.store(0, std::memory_order_relaxed);
        s_destructions.store(0, std::memory_order_relaxed);
        s_moves.store(0, std::memory_order_relaxed);
    }
};

// =============================================================================
// Unit Tests: Ring Buffer Core Semantics
// =============================================================================
static void test_ring_buffer_basic() {
    TEST_LOG("Running Ring Buffer Basic Operations Test...");

    rtw89_ring<int, 4> ring;
    TEST_ASSERT(ring.capacity() == 4, "Capacity should be 4");
    TEST_ASSERT(ring.is_empty(), "Ring should start empty");
    TEST_ASSERT(!ring.is_full(), "Ring should not start full");
    TEST_ASSERT(ring.count() == 0, "Count should start at 0");

    // Push 4 items to fill buffer
    TEST_ASSERT(ring.push(10), "Push 10");
    TEST_ASSERT(ring.push(20), "Push 20");
    TEST_ASSERT(ring.push(30), "Push 30");
    TEST_ASSERT(ring.push(40), "Push 40");

    TEST_ASSERT(ring.is_full(), "Ring should now be full");
    TEST_ASSERT(!ring.is_empty(), "Ring should not be empty");
    TEST_ASSERT(ring.count() == 4, "Count should be 4");

    // 5th push should fail
    TEST_ASSERT(!ring.push(50), "Push 50 should fail when full");

    // Peek at first item
    int peek_val = 0;
    TEST_ASSERT(ring.peek(peek_val), "Peek should succeed");
    TEST_ASSERT(peek_val == 10, "Peeked value should be 10");
    TEST_ASSERT(ring.count() == 4, "Count unchanged after peek");

    // Pop items
    int pop_val = 0;
    TEST_ASSERT(ring.pop(pop_val), "Pop 1");
    TEST_ASSERT(pop_val == 10, "Pop 1 value should be 10");
    TEST_ASSERT(ring.count() == 3, "Count should be 3");

    TEST_ASSERT(ring.pop(pop_val), "Pop 2");
    TEST_ASSERT(pop_val == 20, "Pop 2 value should be 20");

    // Push more to test circular wrap-around
    TEST_ASSERT(ring.push(50), "Push 50");
    TEST_ASSERT(ring.push(60), "Push 60");
    TEST_ASSERT(ring.is_full(), "Ring should be full again");

    TEST_ASSERT(ring.pop(pop_val) && pop_val == 30, "Pop 30");
    TEST_ASSERT(ring.pop(pop_val) && pop_val == 40, "Pop 40");
    TEST_ASSERT(ring.pop(pop_val) && pop_val == 50, "Pop 50");
    TEST_ASSERT(ring.pop(pop_val) && pop_val == 60, "Pop 60");

    TEST_ASSERT(ring.is_empty(), "Ring should be empty");
    TEST_ASSERT(!ring.pop(pop_val), "Pop on empty ring should fail");

    TEST_LOG("PASS: Ring Buffer Basic Operations Test");
}

static void test_ring_buffer_lifecycles() {
    TEST_LOG("Running Ring Buffer Object Lifecycle & Destruction Test...");

    LifetimeTracker::reset();

    {
        rtw89_ring<LifetimeTracker, 8> ring;
        for (int i = 0; i < 6; ++i) {
            ring.emplace(i);
        }
        TEST_ASSERT(ring.count() == 6, "Should have 6 items");

        // Pop 3 items
        for (int i = 0; i < 3; ++i) {
            LifetimeTracker item;
            TEST_ASSERT(ring.pop(item), "Pop item");
            TEST_ASSERT(item.id == i, "Verify item id");
        }
        TEST_ASSERT(ring.count() == 3, "Should have 3 items left");

        // Clear remaining items
        ring.clear();
        TEST_ASSERT(ring.is_empty(), "Ring should be empty after clear");
        TEST_ASSERT(ring.count() == 0, "Count should be 0 after clear");
    }

    // Now all constructions must match all destructions
    TEST_ASSERT(LifetimeTracker::s_constructions.load() ==
                    LifetimeTracker::s_destructions.load(),
                "All constructions must equal destructions (got %" PRId64 " vs %" PRId64 ")",
                LifetimeTracker::s_constructions.load(),
                LifetimeTracker::s_destructions.load());

    TEST_LOG("PASS: Ring Buffer Object Lifecycle & Destruction Test");
}

// =============================================================================
// Test 1: SPSC circular ring with 1,000,000 items under concurrent threads
// =============================================================================
static void test_ring_buffer_1m_spsc() {
    TEST_LOG("Running Test 1: SPSC circular ring with 1,000,000 items...");

    constexpr size_t kTotalItems = 1000000;
    constexpr size_t kRingSize = 4096;

    rtw89_ring<uint64_t, kRingSize> ring;

    std::atomic<bool> producer_done{false};
    uint64_t expected_sum = 0;
    for (size_t i = 0; i < kTotalItems; ++i) {
        expected_sum += i;
    }

    auto start_time = std::chrono::high_resolution_clock::now();

    // Producer thread
    std::thread producer([&] {
        for (uint64_t i = 0; i < kTotalItems; ++i) {
            while (!ring.push(i)) {
                std::this_thread::yield();
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    // Consumer thread
    uint64_t received_count = 0;
    uint64_t received_sum = 0;
    uint64_t next_expected = 0;

    std::thread consumer([&] {
        uint64_t val = 0;
        while (received_count < kTotalItems) {
            if (ring.pop(val)) {
                TEST_ASSERT(val == next_expected,
                            "Sequence mismatch: expected %" PRIu64 ", got %" PRIu64,
                            next_expected, val);
                received_sum += val;
                ++received_count;
                ++next_expected;
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    auto end_time = std::chrono::high_resolution_clock::now();
    auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                          end_time - start_time)
                          .count();

    TEST_ASSERT(received_count == kTotalItems,
                "Received items count mismatch: %" PRIu64 " vs %zu",
                received_count, kTotalItems);
    TEST_ASSERT(received_sum == expected_sum,
                "Received items sum mismatch: %" PRIu64 " vs %" PRIu64,
                received_sum, expected_sum);
    TEST_ASSERT(ring.is_empty(), "Ring should be empty after benchmark");
    TEST_ASSERT(ring.count() == 0, "Ring count should be 0 after benchmark");

    double seconds = static_cast<double>(elapsed_us) / 1000000.0;
    double mops = (static_cast<double>(kTotalItems) / seconds) / 1000000.0;

    TEST_LOG("PASS: Test 1 (1,000,000 items processed in %.3f s, %.2f Mops/s)",
             seconds, mops);
}

// =============================================================================
// Test 2: Delayed work scheduling, execution timing, and synchronous cancellation
// =============================================================================

struct TestWorkContext {
    std::atomic<int> exec_count{0};
    std::atomic<bool> running{false};
    std::chrono::steady_clock::time_point schedule_time{};
    std::chrono::steady_clock::time_point exec_time{};
    struct delayed_work *dwork_ptr{nullptr};
};

static void sample_delayed_work_func(struct work_struct *work) {
    auto *dwork = to_delayed_work(work);
    auto *ctx = reinterpret_cast<TestWorkContext*>(work->data);

    TEST_ASSERT(dwork == ctx->dwork_ptr, "to_delayed_work must match outer pointer");

    ctx->exec_time = std::chrono::steady_clock::now();
    ctx->exec_count.fetch_add(1, std::memory_order_relaxed);
}

static void test_workqueue_basic_schedule() {
    TEST_LOG("Running Workqueue Basic Schedule Test...");

    struct work_struct work;
    std::atomic<int> executed{0};

    INIT_WORK(&work, [](struct work_struct *w) {
        auto *flag = reinterpret_cast<std::atomic<int>*>(w->data);
        flag->fetch_add(1, std::memory_order_relaxed);
    });

    work.data = &executed;

    TEST_ASSERT(schedule_work(&work), "Schedule work should succeed");
    // Duplicate schedule should fail while work is pending
    // (or succeed if already executed)

    flush_work(&work);

    TEST_ASSERT(executed.load() == 1, "Work should have executed exactly once");
    TEST_LOG("PASS: Workqueue Basic Schedule Test");
}

static void test_delayed_work_timing() {
    TEST_LOG("Running Test 2A: Delayed work scheduling and execution timing...");

    struct delayed_work dwork;
    TestWorkContext ctx;
    ctx.dwork_ptr = &dwork;

    INIT_DELAYED_WORK(&dwork, sample_delayed_work_func);
    dwork.work.data = &ctx;

    constexpr unsigned long kDelayMs = 60;
    ctx.schedule_time = std::chrono::steady_clock::now();

    TEST_ASSERT(schedule_delayed_work(&dwork, kDelayMs),
                "schedule_delayed_work should return true");

    // Second schedule while pending should return false
    TEST_ASSERT(!schedule_delayed_work(&dwork, kDelayMs),
                "Duplicate schedule_delayed_work must return false");

    // Wait for timer to expire naturally
    while (ctx.exec_count.load(std::memory_order_relaxed) == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    flush_delayed_work(&dwork);

    TEST_ASSERT(ctx.exec_count.load() == 1,
                "Delayed work must execute exactly once (got %d)", ctx.exec_count.load());

    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          ctx.exec_time - ctx.schedule_time)
                          .count();

    TEST_LOG("Delayed work executed after %" PRId64 " ms (scheduled for %lu ms)",
             elapsed_ms, kDelayMs);
    TEST_ASSERT(elapsed_ms >= 45,
                "Delayed work fired too early: %" PRId64 " ms < 45 ms", elapsed_ms);

    TEST_LOG("PASS: Test 2A (Execution timing verified)");
}

static void test_delayed_work_sync_cancellation() {
    TEST_LOG("Running Test 2B: Synchronous cancellation of pending delayed work...");

    struct delayed_work dwork;
    TestWorkContext ctx;
    ctx.dwork_ptr = &dwork;

    INIT_DELAYED_WORK(&dwork, sample_delayed_work_func);
    dwork.work.data = &ctx;

    // Schedule far in the future
    TEST_ASSERT(schedule_delayed_work(&dwork, 500), "Schedule 500ms delayed work");

    // Immediately cancel synchronously
    bool cancelled = cancel_delayed_work_sync(&dwork);
    TEST_ASSERT(cancelled, "cancel_delayed_work_sync must return true for pending work");

    // Wait a brief duration and verify it never ran
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    TEST_ASSERT(ctx.exec_count.load() == 0,
                "Cancelled delayed work must never execute (got %d)", ctx.exec_count.load());

    // Calling cancel on an already cancelled work should return false
    TEST_ASSERT(!cancel_delayed_work_sync(&dwork),
                "cancel_delayed_work_sync on idle work must return false");

    TEST_LOG("PASS: Test 2B (Pending delayed work cancelled successfully)");
}

static void test_work_sync_cancellation_while_running() {
    TEST_LOG("Running Test 2C: Synchronous cancellation while work is executing...");

    struct work_struct work;
    std::atomic<bool> is_running{false};
    std::atomic<bool> work_completed{false};

    INIT_WORK(&work, [](struct work_struct *w) {
        auto *r = reinterpret_cast<std::atomic<bool>*>(w->data);
        r->store(true, std::memory_order_release);
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
    });

    work.data = &is_running;

    auto t_start = std::chrono::steady_clock::now();
    TEST_ASSERT(schedule_work(&work), "Schedule long-running work");

    // Wait until work actually enters the running state
    while (!is_running.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    // Call cancel_work_sync while work is in flight: it must block until completion
    bool was_pending = cancel_work_sync(&work);
    auto t_cancel_done = std::chrono::steady_clock::now();

    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          t_cancel_done - t_start)
                          .count();

    TEST_ASSERT(!was_pending,
                "cancel_work_sync must return false when work was already executing");
    TEST_ASSERT(elapsed_ms >= 50,
                "cancel_work_sync must block until completion (elapsed: %" PRId64 " ms)",
                elapsed_ms);

    work_completed.store(true);
    TEST_LOG("PASS: Test 2C (cancel_work_sync blocked until running work completed)");
}

static void test_work_reschedule_from_handler() {
    TEST_LOG("Running Test 2D: Self-rescheduling work from within handler...");

    struct delayed_work dwork;
    std::atomic<int> cycle_count{0};

    INIT_DELAYED_WORK(&dwork, [](struct work_struct *w) {
        auto *dw = to_delayed_work(w);
        auto *cnt = reinterpret_cast<std::atomic<int>*>(w->data);
        int val = cnt->fetch_add(1, std::memory_order_relaxed);
        if (val < 3) {
            schedule_delayed_work(dw, 10);
        }
    });

    dwork.work.data = &cycle_count;

    TEST_ASSERT(schedule_delayed_work(&dwork, 10), "Initial schedule");

    // Wait for all 4 cycles (0, 1, 2, 3)
    while (cycle_count.load(std::memory_order_relaxed) < 4) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    flush_delayed_work(&dwork);
    TEST_ASSERT(cycle_count.load() == 4,
                "Should have completed 4 cycles (got %d)", cycle_count.load());

    TEST_LOG("PASS: Test 2D (Self-rescheduling completed without deadlock)");
}

static void test_custom_workqueue() {
    TEST_LOG("Running Custom Workqueue Creation and Teardown Test...");

    struct workqueue_struct *custom_wq = create_workqueue("rtw89_test_wq");
    TEST_ASSERT(custom_wq != nullptr, "create_workqueue must succeed");

    struct work_struct work;
    std::atomic<int> counter{0};

    INIT_WORK(&work, [](struct work_struct *w) {
        auto *cnt = reinterpret_cast<std::atomic<int>*>(w->data);
        cnt->fetch_add(1, std::memory_order_relaxed);
    });
    work.data = &counter;

    TEST_ASSERT(queue_work(custom_wq, &work), "queue_work on custom wq");
    flush_workqueue(custom_wq);
    TEST_ASSERT(counter.load() == 1, "Work ran on custom wq");

    destroy_workqueue(custom_wq);
    TEST_LOG("PASS: Custom Workqueue Creation and Teardown Test");
}

// =============================================================================
// Main Test Runner
// =============================================================================
int main() {
    TEST_LOG("=================================================================");
    TEST_LOG("Starting RTW89 Wave 1 Ring Buffer & Workqueue Test Suite");
    TEST_LOG("=================================================================");

    // Ring Buffer Tests
    test_ring_buffer_basic();
    test_ring_buffer_lifecycles();
    test_ring_buffer_1m_spsc();

    // Workqueue Tests
    test_workqueue_basic_schedule();
    test_delayed_work_timing();
    test_delayed_work_sync_cancellation();
    test_work_sync_cancellation_while_running();
    test_work_reschedule_from_handler();
    test_custom_workqueue();

    // Cleanly shutdown system workqueue threads before exiting
    rtw89_workqueue_shutdown();

    TEST_LOG("=================================================================");
    TEST_LOG("ALL TESTS PASSED CLEANLY (0 errors)");
    TEST_LOG("=================================================================");
    return 0;
}
