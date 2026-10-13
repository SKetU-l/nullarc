#ifndef RTW89_COMPAT_WORKQUEUE_H
#define RTW89_COMPAT_WORKQUEUE_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

/**
 * @file workqueue.h
 * @brief Linux Kernel Workqueue Emulation Primitives for RTW89 Driver.
 *
 * Implements Linux workqueue semantics:
 * - struct work_struct
 * - struct delayed_work
 * - INIT_WORK / INIT_DELAYED_WORK
 * - schedule_work / schedule_delayed_work
 * - cancel_work_sync / cancel_delayed_work_sync
 * - cancel_delayed_work / flush_work / flush_delayed_work
 * - create_workqueue / destroy_workqueue / queue_work / queue_delayed_work
 *
 * Architecture & IOKit Mapping:
 * - In Linux: Backed by system workqueue kthreads and timer wheel / hrtimers.
 * - In macOS IOKit:
 *   * Workqueue maps directly to IOWorkLoop and IOCommandGate.
 *   * delayed_work maps to IOTimerEventSource attached to the driver's IOWorkLoop.
 *   * cancel_delayed_work_sync corresponds to timerEventSource->cancelTimeout()
 *     combined with gate synchronization.
 * - In Userspace Host Testbed:
 *   * Backed by a dedicated worker thread pool and a high-resolution timer thread
 *     orchestrated via std::mutex and std::condition_variable.
 */

struct work_struct;
struct delayed_work;
struct workqueue_struct;

// Linux work function signature: void (*func)(struct work_struct *work);
typedef void (*work_func_t)(struct work_struct *work);

struct work_struct {
    work_func_t func{nullptr};
    std::atomic<bool> pending{false};
    std::atomic<bool> running{false};
    void *data{nullptr};
    std::atomic<struct workqueue_struct*> wq{nullptr};
};

struct delayed_work {
    struct work_struct work;
    std::atomic<bool> timer_pending{false};
    unsigned long delay_ms{0};
    uint64_t timer_id{0};
};

#ifndef container_of
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
#endif

#ifndef msecs_to_jiffies
#define msecs_to_jiffies(m) (m)
#endif

/**
 * @brief Retrieve outer delayed_work pointer from inner work_struct pointer.
 */
inline struct delayed_work *to_delayed_work(struct work_struct *work) {
    if (!work) return nullptr;
    return reinterpret_cast<struct delayed_work*>(work);
}

// -----------------------------------------------------------------------------
// Initialization helpers and macros
// -----------------------------------------------------------------------------

inline void init_work(struct work_struct *work, work_func_t func) {
    if (!work) return;
    work->func = func;
    work->pending.store(false, std::memory_order_relaxed);
    work->running.store(false, std::memory_order_relaxed);
    work->wq.store(nullptr, std::memory_order_relaxed);
}

inline void init_work(struct work_struct &work, work_func_t func) {
    init_work(&work, func);
}

inline void init_delayed_work(struct delayed_work *dwork, work_func_t func) {
    if (!dwork) return;
    init_work(&dwork->work, func);
    dwork->timer_pending.store(false, std::memory_order_relaxed);
    dwork->delay_ms = 0;
    dwork->timer_id = 0;
}

inline void init_delayed_work(struct delayed_work &dwork, work_func_t func) {
    init_delayed_work(&dwork, func);
}

#define INIT_WORK(w, f) ::init_work((w), (f))
#define INIT_DELAYED_WORK(d, f) ::init_delayed_work((d), (f))

// -----------------------------------------------------------------------------
// Core workqueue struct definition
// -----------------------------------------------------------------------------

struct workqueue_struct {
public:
    explicit workqueue_struct(std::string name, size_t num_threads = 2);
    ~workqueue_struct();

    workqueue_struct(const workqueue_struct&) = delete;
    workqueue_struct& operator=(const workqueue_struct&) = delete;
    workqueue_struct(workqueue_struct&&) = delete;
    workqueue_struct& operator=(workqueue_struct&&) = delete;

    bool queue_work(struct work_struct *work);
    bool queue_delayed_work(struct delayed_work *dwork, unsigned long delay_ms);
    bool cancel_work_sync(struct work_struct *work);
    bool cancel_delayed_work_sync(struct delayed_work *dwork);
    bool cancel_delayed_work(struct delayed_work *dwork);
    bool mod_delayed_work(struct delayed_work *dwork, unsigned long delay_ms);
    bool flush_work(struct work_struct *work);
    bool flush_delayed_work(struct delayed_work *dwork);
    void flush();

    const std::string& name() const { return name_; }

private:
    void worker_loop();
    void timer_loop();

    std::string name_;
    std::atomic<bool> stop_{false};

    std::vector<std::thread> workers_;
    std::thread timer_thread_;

    mutable std::mutex mu_;
    std::condition_variable worker_cv_;
    std::condition_variable timer_cv_;
    std::condition_variable done_cv_;

    std::deque<struct work_struct*> ready_queue_;

    struct DelayedItem {
        struct delayed_work* dwork;
        std::chrono::steady_clock::time_point deadline;
        uint64_t timer_id;
    };
    std::vector<DelayedItem> delayed_queue_;
    std::unordered_set<struct work_struct*> active_works_;
    uint64_t next_timer_id_{1};
};

// -----------------------------------------------------------------------------
// Global / System Workqueue API
// -----------------------------------------------------------------------------

/**
 * @brief Returns the shared global system workqueue.
 */
struct workqueue_struct *get_system_wq();

/**
 * @brief Schedule work on the global system workqueue.
 * @return true if work was queued, false if it was already pending.
 */
bool schedule_work(struct work_struct *work);

/**
 * @brief Schedule delayed work on the global system workqueue.
 * @param dwork Pointer to delayed_work structure.
 * @param delay_ms Delay in milliseconds.
 * @return true if scheduled, false if already pending.
 */
bool schedule_delayed_work(struct delayed_work *dwork, unsigned long delay_ms);

/**
 * @brief std::chrono overload for schedule_delayed_work.
 */
template <typename Rep, typename Period>
inline bool schedule_delayed_work(struct delayed_work *dwork, const std::chrono::duration<Rep, Period>& delay) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(delay).count();
    return schedule_delayed_work(dwork, static_cast<unsigned long>(ms > 0 ? ms : 0));
}

/**
 * @brief Synchronously cancel a work_struct.
 * If pending, removes from queue. If running, blocks until execution finishes.
 * @return true if work was pending and cancelled, false otherwise.
 */
bool cancel_work_sync(struct work_struct *work);

/**
 * @brief Synchronously cancel a delayed_work.
 * If pending on timer or ready queue, cancels it. If running, blocks until done.
 * @return true if work was pending and cancelled, false otherwise.
 */
bool cancel_delayed_work_sync(struct delayed_work *dwork);

/**
 * @brief Asynchronously cancel a delayed work (does not wait for running execution).
 * @return true if timer or queue was pending and cancelled, false otherwise.
 */
bool cancel_delayed_work(struct delayed_work *dwork);

/**
 * @brief Modify the delay of a delayed work item.
 */
bool mod_delayed_work(struct workqueue_struct *wq, struct delayed_work *dwork, unsigned long delay_ms);
inline bool mod_delayed_work(struct delayed_work *dwork, unsigned long delay_ms) {
    return mod_delayed_work(nullptr, dwork, delay_ms);
}

/**
 * @brief Wait for a work_struct to finish executing if pending or running.
 */
bool flush_work(struct work_struct *work);

/**
 * @brief Flush delayed work, forcing it to run and waiting for completion.
 */
bool flush_delayed_work(struct delayed_work *dwork);

/**
 * @brief Flush all items on the global system workqueue.
 */
void flush_scheduled_work(void);

// -----------------------------------------------------------------------------
// Custom Workqueue Management API
// -----------------------------------------------------------------------------

struct workqueue_struct *create_workqueue(const char *name);
struct workqueue_struct *create_singlethread_workqueue(const char *name);
void destroy_workqueue(struct workqueue_struct *wq);
bool queue_work(struct workqueue_struct *wq, struct work_struct *work);
bool queue_delayed_work(struct workqueue_struct *wq, struct delayed_work *dwork, unsigned long delay_ms);
void flush_workqueue(struct workqueue_struct *wq);

/**
 * @brief Explicitly shut down the system workqueue and join its worker threads.
 * Useful for test suites to ensure 100% clean thread join before process termination.
 */
void rtw89_workqueue_shutdown(void);

#endif // RTW89_COMPAT_WORKQUEUE_H
