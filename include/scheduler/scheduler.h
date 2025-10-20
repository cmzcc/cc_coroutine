#pragma once

#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <iostream>
#include "utils/timer.h"
#include "utils/logger.h"
#include <memory>

namespace modern_coro
{

    template <typename T>
    struct Task;

    class Scheduler
    {
    public:
        using TaskCallback = std::function<void()>;

        // 获取调度器统计信息
        struct SchedulerStats
        {
            size_t active_coroutines;
            size_t queued_tasks;
            size_t total_scheduled;
            size_t total_completed;
        };

        explicit Scheduler(size_t thread_count = std::thread::hardware_concurrency());
        virtual ~Scheduler();

        Scheduler(const Scheduler &) = delete;
        Scheduler &operator=(const Scheduler &) = delete;

        void start();
        void stop();

        void register_thread() { current_scheduler_ = this; }
        void unregister_thread() { current_scheduler_ = nullptr; }

        bool is_idle() const
        {
            // 检查所有worker队列是否都为空
            for (const auto &queue : worker_queues_)
            {
                if (!queue->empty())
                {
                    return false;
                }
            }
            return active_coroutines_.load(std::memory_order_relaxed) == 0;
        }

        template <typename T>
        void schedule(Task<T> &&task, TaskCallback callback = nullptr)
        {
            // 建立自持有，获取句柄和共享状态供队列任务使用
            auto keep = task.share_state_opaque();
            task.set_keep_alive(keep);
            auto handle = task.handle();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                tasks_.emplace([handle, keep, callback = std::move(callback)]() mutable
                               {
                                   if (handle && !handle.done())
                                   {
                                       handle.resume();
                                   }
                                   if (callback)
                                   {
                                       callback();
                                   }
                                   // keep 在此作用域结束前保持，共享状态在协程 final_suspend 处自行解除
                               });
            }
            cv_.notify_one();
        }

        void schedule(std::function<void()> func);

        static Scheduler *GetCurrent();

        // 调度器注册/注销辅助函数
        static void register_thread_on_scheduler(Scheduler *scheduler_ptr);
        static void unregister_thread_on_scheduler(Scheduler *scheduler_ptr);

        Task<> sleep(std::chrono::milliseconds duration);

        // 通用重载：支持任意 chrono duration，内部统一转换为毫秒
        // 注意：为避免亚毫秒被截断为0导致“立即返回”的竞态，做最小1ms向上取整
        template <typename Rep, typename Period>
        Task<> sleep(std::chrono::duration<Rep, Period> duration)
        {
            using SrcDur = std::chrono::duration<Rep, Period>;
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration);
            if (ms.count() == 0 && duration > SrcDur::zero())
            {
                ms = std::chrono::milliseconds(1);
            }
            return sleep(ms);
        }

        void increment_active_coroutines()
        {
            active_coroutines_.fetch_add(1, std::memory_order_relaxed);
        }
        void decrement_active_coroutines()
        {
            auto current = active_coroutines_.load(std::memory_order_relaxed);
            if (current > 0)
            {
                active_coroutines_.fetch_sub(1, std::memory_order_relaxed);
            }
        }

        size_t get_active_coroutines() const { return active_coroutines_.load(std::memory_order_relaxed); }

        SchedulerStats get_stats() const
        {
            // 统计所有worker队列的任务数
            size_t total_queued = 0;
            for (const auto &queue : worker_queues_)
            {
                total_queued += queue->get_size();
            }

            return SchedulerStats{
                .active_coroutines = active_coroutines_.load(std::memory_order_relaxed),
                .queued_tasks = total_queued,
                .total_scheduled = total_scheduled_.load(std::memory_order_relaxed),
                .total_completed = total_completed_.load(std::memory_order_relaxed)};
        }

        void print_stats() const
        {
            auto stats = get_stats();
            SPDLOG_INFO("调度器统计信息:");
            SPDLOG_INFO("  活跃协程数: {}", stats.active_coroutines);
            SPDLOG_INFO("  队列任务数: {}", stats.queued_tasks);
            SPDLOG_INFO("  总调度数: {}", stats.total_scheduled);
            SPDLOG_INFO("  总完成数: {}", stats.total_completed);
        }

    protected:
        virtual void worker_thread();

        // Per-worker queue 结构，减少锁竞争
        struct WorkerQueue
        {
            std::queue<std::function<void()>> tasks;
            mutable std::mutex mutex;
            std::condition_variable cv;
            std::atomic<size_t> size{0};

            void push(std::function<void()> task)
            {
                std::lock_guard<std::mutex> lock(mutex);
                tasks.push(std::move(task));
                size.fetch_add(1, std::memory_order_relaxed);
            }

            bool try_pop(std::function<void()> &task)
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (tasks.empty())
                {
                    return false;
                }
                task = std::move(tasks.front());
                tasks.pop();
                size.fetch_sub(1, std::memory_order_relaxed);
                return true;
            }

            bool empty() const
            {
                std::lock_guard<std::mutex> lock(mutex);
                return tasks.empty();
            }

            size_t get_size() const
            {
                return size.load(std::memory_order_relaxed);
            }
        };

        std::vector<std::thread> workers_;
        std::vector<std::unique_ptr<WorkerQueue>> worker_queues_; // 每个worker独立队列
        std::atomic<size_t> next_queue_{0};                       // Round-robin分配
        size_t max_queue_size_ = 500000;                          // 资源限制：最大任务积压
        std::atomic<bool> stop_flag_{false};
        size_t thread_count_;
        std::unique_ptr<Timer> timer_;
        std::atomic<size_t> active_coroutines_{0};
        std::atomic<size_t> total_scheduled_{0};
        std::atomic<size_t> total_completed_{0};

        // 保留全局锁用于停止操作
        mutable std::mutex mutex_;

        // 全局任务队列（用于schedule函数）
        std::queue<std::function<void()>> tasks_;
        std::condition_variable cv_;

        static thread_local Scheduler *current_scheduler_;
    };

    class YieldAwaiter
    {
    public:
        bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<> h)
        {
            if (auto scheduler = Scheduler::GetCurrent())
            {
                scheduler->schedule([h]()
                                    { h.resume(); });
            }
            else
            {
                h.resume();
            }
        }

        void await_resume() const noexcept {}
    };

    // 辅助函数声明：在有调度器时投递任务，否则回退到新线程
    void schedule_coroutine_task(std::function<void()> task);

} // namespace modern_coro