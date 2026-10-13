#include "workqueue.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <mutex>
#include <utility>

workqueue_struct::workqueue_struct(std::string name, size_t num_threads)
    : name_(std::move(name)) {
    if (num_threads == 0) {
        num_threads = 1;
    }

    timer_thread_ = std::thread([this] {
        timer_loop();
    });

    workers_.reserve(num_threads);
    for (size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back([this] {
            worker_loop();
        });
    }
}

workqueue_struct::~workqueue_struct() {
    {
        std::unique_lock<std::mutex> lock(mu_);
        stop_.store(true, std::memory_order_release);
        timer_cv_.notify_all();
        worker_cv_.notify_all();
    }

    if (timer_thread_.joinable()) {
        timer_thread_.join();
    }

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

bool workqueue_struct::queue_work(struct work_struct *work) {
    if (!work) {
        return false;
    }

    std::unique_lock<std::mutex> lock(mu_);
    if (stop_.load(std::memory_order_relaxed)) {
        return false;
    }

    // In Linux workqueue: if work is already pending, return false
    if (work->pending.load(std::memory_order_relaxed)) {
        return false;
    }

    work->pending.store(true, std::memory_order_release);
    work->wq.store(this, std::memory_order_release);
    ready_queue_.push_back(work);
    worker_cv_.notify_one();
    return true;
}

bool workqueue_struct::queue_delayed_work(struct delayed_work *dwork, unsigned long delay_ms) {
    if (!dwork) {
        return false;
    }

    std::unique_lock<std::mutex> lock(mu_);
    if (stop_.load(std::memory_order_relaxed)) {
        return false;
    }

    // Return false if either work or timer is already pending
    if (dwork->work.pending.load(std::memory_order_relaxed) ||
        dwork->timer_pending.load(std::memory_order_relaxed)) {
        return false;
    }

    dwork->work.wq.store(this, std::memory_order_release);

    if (delay_ms == 0) {
        dwork->work.pending.store(true, std::memory_order_release);
        ready_queue_.push_back(&dwork->work);
        worker_cv_.notify_one();
        return true;
    }

    dwork->timer_pending.store(true, std::memory_order_release);
    dwork->delay_ms = delay_ms;
    uint64_t tid = next_timer_id_++;
    dwork->timer_id = tid;

    auto now = std::chrono::steady_clock::now();
    auto deadline = now + std::chrono::milliseconds(delay_ms);
    delayed_queue_.push_back({dwork, deadline, tid});

    timer_cv_.notify_one();
    return true;
}

void workqueue_struct::timer_loop() {
    std::unique_lock<std::mutex> lock(mu_);

    while (!stop_.load(std::memory_order_relaxed)) {
        if (delayed_queue_.empty()) {
            timer_cv_.wait(lock, [this] {
                return stop_.load(std::memory_order_relaxed) || !delayed_queue_.empty();
            });
            continue;
        }

        auto now = std::chrono::steady_clock::now();
        std::vector<struct delayed_work*> expired;

        for (auto it = delayed_queue_.begin(); it != delayed_queue_.end(); ) {
            if (it->deadline <= now) {
                expired.push_back(it->dwork);
                it = delayed_queue_.erase(it);
            } else {
                ++it;
            }
        }

        for (auto* dwork : expired) {
            dwork->timer_pending.store(false, std::memory_order_release);
            dwork->work.pending.store(true, std::memory_order_release);
            ready_queue_.push_back(&dwork->work);
            worker_cv_.notify_one();
        }

        if (delayed_queue_.empty()) {
            continue;
        }

        auto earliest_it = std::min_element(
            delayed_queue_.begin(), delayed_queue_.end(),
            [](const DelayedItem& a, const DelayedItem& b) {
                return a.deadline < b.deadline;
            });

        auto next_deadline = earliest_it->deadline;
        timer_cv_.wait_until(lock, next_deadline);
    }
}

void workqueue_struct::worker_loop() {
    while (true) {
        struct work_struct *work = nullptr;

        {
            std::unique_lock<std::mutex> lock(mu_);
            worker_cv_.wait(lock, [this] {
                return stop_.load(std::memory_order_relaxed) || !ready_queue_.empty();
            });

            if (ready_queue_.empty()) {
                if (stop_.load(std::memory_order_relaxed)) {
                    break;
                }
                continue;
            }

            work = ready_queue_.front();
            ready_queue_.pop_front();
            work->pending.store(false, std::memory_order_release);
            work->running.store(true, std::memory_order_release);
            active_works_.insert(work);
        }

        // Execute function outside mutex lock to allow re-entrancy and avoid deadlocks
        if (work && work->func) {
            work->func(work);
        }

        {
            std::unique_lock<std::mutex> lock(mu_);
            active_works_.erase(work);
            work->running.store(false, std::memory_order_release);
            done_cv_.notify_all();
        }
    }
}

bool workqueue_struct::cancel_work_sync(struct work_struct *work) {
    if (!work) {
        return false;
    }

    bool was_pending = false;
    {
        std::unique_lock<std::mutex> lock(mu_);

        for (auto it = ready_queue_.begin(); it != ready_queue_.end(); ++it) {
            if (*it == work) {
                ready_queue_.erase(it);
                work->pending.store(false, std::memory_order_release);
                was_pending = true;
                break;
            }
        }

        // Wait if work is currently executing
        done_cv_.wait(lock, [&] {
            return active_works_.find(work) == active_works_.end();
        });
    }

    return was_pending;
}

bool workqueue_struct::cancel_delayed_work_sync(struct delayed_work *dwork) {
    if (!dwork) {
        return false;
    }

    bool was_pending = false;
    {
        std::unique_lock<std::mutex> lock(mu_);

        // 1. Check timer queue
        for (auto it = delayed_queue_.begin(); it != delayed_queue_.end(); ++it) {
            if (it->dwork == dwork) {
                delayed_queue_.erase(it);
                dwork->timer_pending.store(false, std::memory_order_release);
                was_pending = true;
                timer_cv_.notify_one();
                break;
            }
        }

        // 2. Check ready queue
        for (auto it = ready_queue_.begin(); it != ready_queue_.end(); ++it) {
            if (*it == &dwork->work) {
                ready_queue_.erase(it);
                dwork->work.pending.store(false, std::memory_order_release);
                was_pending = true;
                break;
            }
        }

        // 3. Wait if running
        done_cv_.wait(lock, [&] {
            return active_works_.find(&dwork->work) == active_works_.end();
        });
    }

    return was_pending;
}

bool workqueue_struct::cancel_delayed_work(struct delayed_work *dwork) {
    if (!dwork) {
        return false;
    }

    std::unique_lock<std::mutex> lock(mu_);

    for (auto it = delayed_queue_.begin(); it != delayed_queue_.end(); ++it) {
        if (it->dwork == dwork) {
            delayed_queue_.erase(it);
            dwork->timer_pending.store(false, std::memory_order_release);
            timer_cv_.notify_one();
            return true;
        }
    }

    for (auto it = ready_queue_.begin(); it != ready_queue_.end(); ++it) {
        if (*it == &dwork->work) {
            ready_queue_.erase(it);
            dwork->work.pending.store(false, std::memory_order_release);
            return true;
        }
    }

    return false;
}

bool workqueue_struct::mod_delayed_work(struct delayed_work *dwork, unsigned long delay_ms) {
    if (!dwork) {
        return false;
    }

    std::unique_lock<std::mutex> lock(mu_);
    if (stop_.load(std::memory_order_relaxed)) {
        return false;
    }

    bool was_pending = false;

    // Remove from timer queue if present
    for (auto it = delayed_queue_.begin(); it != delayed_queue_.end(); ++it) {
        if (it->dwork == dwork) {
            delayed_queue_.erase(it);
            dwork->timer_pending.store(false, std::memory_order_release);
            was_pending = true;
            break;
        }
    }

    // Remove from ready queue if present
    for (auto it = ready_queue_.begin(); it != ready_queue_.end(); ++it) {
        if (*it == &dwork->work) {
            ready_queue_.erase(it);
            dwork->work.pending.store(false, std::memory_order_release);
            was_pending = true;
            break;
        }
    }

    // Reschedule with new delay
    dwork->work.wq.store(this, std::memory_order_release);
    if (delay_ms == 0) {
        dwork->work.pending.store(true, std::memory_order_release);
        ready_queue_.push_back(&dwork->work);
        worker_cv_.notify_one();
    } else {
        dwork->timer_pending.store(true, std::memory_order_release);
        dwork->delay_ms = delay_ms;
        uint64_t tid = next_timer_id_++;
        dwork->timer_id = tid;

        auto now = std::chrono::steady_clock::now();
        auto deadline = now + std::chrono::milliseconds(delay_ms);
        delayed_queue_.push_back({dwork, deadline, tid});
        timer_cv_.notify_one();
    }

    return was_pending;
}

bool workqueue_struct::flush_work(struct work_struct *work) {
    if (!work) {
        return false;
    }

    std::unique_lock<std::mutex> lock(mu_);
    if (!work->pending.load(std::memory_order_relaxed) &&
        active_works_.find(work) == active_works_.end()) {
        return false;
    }

    done_cv_.wait(lock, [&] {
        return !work->pending.load(std::memory_order_relaxed) &&
               active_works_.find(work) == active_works_.end();
    });

    return true;
}

bool workqueue_struct::flush_delayed_work(struct delayed_work *dwork) {
    if (!dwork) {
        return false;
    }

    {
        std::unique_lock<std::mutex> lock(mu_);
        for (auto it = delayed_queue_.begin(); it != delayed_queue_.end(); ++it) {
            if (it->dwork == dwork) {
                delayed_queue_.erase(it);
                dwork->timer_pending.store(false, std::memory_order_release);
                dwork->work.pending.store(true, std::memory_order_release);
                ready_queue_.push_back(&dwork->work);
                worker_cv_.notify_one();
                timer_cv_.notify_one();
                break;
            }
        }
    }

    return flush_work(&dwork->work);
}

void workqueue_struct::flush() {
    std::unique_lock<std::mutex> lock(mu_);
    done_cv_.wait(lock, [this] {
        return ready_queue_.empty() && active_works_.empty();
    });
}

// -----------------------------------------------------------------------------
// Global system workqueue singleton
// -----------------------------------------------------------------------------

namespace {
std::unique_ptr<workqueue_struct> g_system_wq;
std::mutex g_system_wq_mutex;
} // namespace

struct workqueue_struct *get_system_wq() {
    std::lock_guard<std::mutex> lock(g_system_wq_mutex);
    if (!g_system_wq) {
        g_system_wq = std::make_unique<workqueue_struct>("system_wq", 4);
    }
    return g_system_wq.get();
}

bool schedule_work(struct work_struct *work) {
    return get_system_wq()->queue_work(work);
}

bool schedule_delayed_work(struct delayed_work *dwork, unsigned long delay_ms) {
    return get_system_wq()->queue_delayed_work(dwork, delay_ms);
}

bool cancel_work_sync(struct work_struct *work) {
    if (!work) {
        return false;
    }
    auto *wq = work->wq.load(std::memory_order_acquire);
    if (wq) {
        return wq->cancel_work_sync(work);
    }
    return get_system_wq()->cancel_work_sync(work);
}

bool cancel_delayed_work_sync(struct delayed_work *dwork) {
    if (!dwork) {
        return false;
    }
    auto *wq = dwork->work.wq.load(std::memory_order_acquire);
    if (wq) {
        return wq->cancel_delayed_work_sync(dwork);
    }
    return get_system_wq()->cancel_delayed_work_sync(dwork);
}

bool cancel_delayed_work(struct delayed_work *dwork) {
    if (!dwork) {
        return false;
    }
    auto *wq = dwork->work.wq.load(std::memory_order_acquire);
    if (wq) {
        return wq->cancel_delayed_work(dwork);
    }
    return get_system_wq()->cancel_delayed_work(dwork);
}

bool mod_delayed_work(struct workqueue_struct *wq, struct delayed_work *dwork, unsigned long delay_ms) {
    if (!dwork) {
        return false;
    }
    if (!wq) {
        auto *dwq = dwork->work.wq.load(std::memory_order_acquire);
        wq = dwq ? dwq : get_system_wq();
    }
    return wq->mod_delayed_work(dwork, delay_ms);
}

bool flush_work(struct work_struct *work) {
    if (!work) {
        return false;
    }
    auto *wq = work->wq.load(std::memory_order_acquire);
    if (wq) {
        return wq->flush_work(work);
    }
    return get_system_wq()->flush_work(work);
}

bool flush_delayed_work(struct delayed_work *dwork) {
    if (!dwork) {
        return false;
    }
    auto *wq = dwork->work.wq.load(std::memory_order_acquire);
    if (wq) {
        return wq->flush_delayed_work(dwork);
    }
    return get_system_wq()->flush_delayed_work(dwork);
}

void flush_scheduled_work(void) {
    get_system_wq()->flush();
}

struct workqueue_struct *create_workqueue(const char *name) {
    return new workqueue_struct(name ? name : "custom_wq", 2);
}

struct workqueue_struct *create_singlethread_workqueue(const char *name) {
    return new workqueue_struct(name ? name : "single_wq", 1);
}

void destroy_workqueue(struct workqueue_struct *wq) {
    delete wq;
}

bool queue_work(struct workqueue_struct *wq, struct work_struct *work) {
    return wq ? wq->queue_work(work) : false;
}

bool queue_delayed_work(struct workqueue_struct *wq, struct delayed_work *dwork, unsigned long delay_ms) {
    return wq ? wq->queue_delayed_work(dwork, delay_ms) : false;
}

void flush_workqueue(struct workqueue_struct *wq) {
    if (wq) {
        wq->flush();
    }
}

void rtw89_workqueue_shutdown(void) {
    std::unique_ptr<workqueue_struct> old_wq;
    {
        std::lock_guard<std::mutex> lock(g_system_wq_mutex);
        old_wq = std::move(g_system_wq);
    }
    // old_wq is cleanly destroyed and joined here outside lock
}
