#pragma once

#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <iostream>
#include "timer.h"
#include <memory>

namespace modern_coro {

template<typename T> struct Task;

class Scheduler {
public:
    using TaskCallback = std::function<void()>;
    
    // 获取调度器统计信息
    struct SchedulerStats {
        size_t active_coroutines;
        size_t queued_tasks;
        size_t total_scheduled;
        size_t total_completed;
    };
    
    explicit Scheduler(size_t thread_count = std::thread::hardware_concurrency());
    virtual ~Scheduler();
    
    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;
    
    void start();
    void stop();

    void register_thread() { current_scheduler_ = this; }
    void unregister_thread() { current_scheduler_ = nullptr; }
    
    bool is_idle() const {
        std::lock_guard<std::mutex> lock(mutex_);
        bool idle = tasks_.empty() && active_coroutines_ == 0;
        return idle;
    }
    
    template<typename T>
    void schedule(Task<T>&& task, TaskCallback callback = nullptr) {
        auto task_ptr = std::make_shared<Task<T>>(std::move(task));
        {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks_.emplace([this, task_ptr, callback = std::move(callback)]() mutable {
                // 重要：确保在协程执行期间task_ptr始终保持存活
                auto handle = task_ptr->handle();
                if (handle) {
                    // 只resume一次，让协程的await机制处理后续执行
                    if (!handle.done()) {
                        handle.resume();
                    }
                }
                if (callback) {
                    callback();
                }
                // task_ptr在这里销毁，确保协程已经完成
            });
        }
        cv_.notify_one();
    }
    
    void schedule(std::function<void()> func);
    
    static Scheduler* GetCurrent();
    
    // 获取当前调度器指针（但不暴露Scheduler类型）
    static void* get_current_scheduler_ptr();
    
    // 调度器注册/注销辅助函数（避免类型暴露）
    static void register_thread_on_scheduler(void* scheduler_ptr);
    static void unregister_thread_on_scheduler(void* scheduler_ptr);
    
    Task<> sleep(std::chrono::milliseconds duration);

    void increment_active_coroutines() { 
        active_coroutines_.fetch_add(1);
    }
    void decrement_active_coroutines() { 
        auto current = active_coroutines_.load();
        if (current > 0) {
            active_coroutines_.fetch_sub(1);
        }
    }

    size_t get_active_coroutines() const { return active_coroutines_.load(); }
    
    SchedulerStats get_stats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return SchedulerStats{
            .active_coroutines = active_coroutines_.load(),
            .queued_tasks = tasks_.size(),
            .total_scheduled = total_scheduled_.load(),
            .total_completed = total_completed_.load()
        };
    }
    
    void print_stats() const {
        auto stats = get_stats();
        std::cout << "调度器统计信息:" << std::endl;
        std::cout << "  活跃协程数: " << stats.active_coroutines << std::endl;
        std::cout << "  队列任务数: " << stats.queued_tasks << std::endl;
        std::cout << "  总调度数: " << stats.total_scheduled << std::endl;
        std::cout << "  总完成数: " << stats.total_completed << std::endl;
    }

protected:
    virtual void worker_thread();
    
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> stop_flag_{false};
    size_t thread_count_;
    std::unique_ptr<Timer> timer_;
    std::atomic<size_t> active_coroutines_{0};
    std::atomic<size_t> total_scheduled_{0};
    std::atomic<size_t> total_completed_{0};
    
    static thread_local Scheduler* current_scheduler_;
};

class YieldAwaiter {
public:
    bool await_ready() const noexcept { return false; }
    
    void await_suspend(std::coroutine_handle<> h) {
        if (auto scheduler = Scheduler::GetCurrent()) {
            scheduler->schedule([h]() { h.resume(); });
        } else {
            h.resume();
        }
    }
    
    void await_resume() const noexcept {}
};

} // namespace modern_coro