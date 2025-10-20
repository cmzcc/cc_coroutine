#include "../../include/scheduler/scheduler.h"
#include <iostream>
#include <thread>

namespace modern_coro
{

#undef MODERN_CORO_DEBUG
#define MODERN_CORO_DEBUG 0

    void increment_active_coroutines_on_current_scheduler()
    {
        if (auto s = Scheduler::GetCurrent())
        {
            s->increment_active_coroutines();
        }
    }

    void decrement_active_coroutines_on_current_scheduler()
    {
        if (auto s = Scheduler::GetCurrent())
        {
            s->decrement_active_coroutines();
        }
    }

    // 新增：直接在指定调度器上减少计数器的函数
    void decrement_active_coroutines_on_scheduler(Scheduler *scheduler_ptr)
    {
        if (scheduler_ptr)
        {
            scheduler_ptr->decrement_active_coroutines();
        }
    }

    thread_local Scheduler *Scheduler::current_scheduler_ = nullptr;

    Scheduler::Scheduler(size_t thread_count)
        : thread_count_(thread_count), timer_(std::make_unique<Timer>())
    {
        if (thread_count_ == 0)
        {
            thread_count_ = 1;
        }

        // 初始化 per-thread 队列
        worker_queues_.reserve(thread_count_);
        for (size_t i = 0; i < thread_count_; ++i)
        {
            worker_queues_.emplace_back(std::make_unique<WorkerQueue>());
        }
    }

    Scheduler::~Scheduler()
    {
        stop();
    }

    void Scheduler::start()
    {
        timer_->start();
        workers_.reserve(thread_count_);
        for (size_t i = 0; i < thread_count_; ++i)
        {
            workers_.emplace_back(&Scheduler::worker_thread, this);
        }
    }

    void Scheduler::stop()
    {
        timer_->stop();

        // 等待所有协程完成
        auto start_time = std::chrono::steady_clock::now();
        while (!is_idle())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start_time);
            if (elapsed.count() > 10)
            { // 最多等待10秒
                // 警告：调度器停止超时
                break;
            }
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_flag_ = true;
        }

        // 唤醒所有 worker
        for (auto &queue : worker_queues_)
        {
            queue->cv.notify_all();
        }

        for (auto &worker : workers_)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }
        workers_.clear();

        // 清理剩余任务以防止内存泄漏
        for (auto &queue : worker_queues_)
        {
            std::lock_guard<std::mutex> lock(queue->mutex);
            while (!queue->tasks.empty())
            {
                queue->tasks.pop();
            }
            queue->size.store(0, std::memory_order_relaxed);
        }
    }

    void Scheduler::schedule(std::function<void()> func)
    {
        if (!func)
            return; // 防止空函数指针

        if (stop_flag_.load(std::memory_order_relaxed))
        {
            // 调度器已停止，不接受新任务
            return;
        }

        total_scheduled_.fetch_add(1, std::memory_order_relaxed);

        // 使用 round-robin 选择一个 worker 队列
        size_t queue_idx = next_queue_.fetch_add(1, std::memory_order_relaxed) % thread_count_;
        auto &queue = worker_queues_[queue_idx];

        // 检查队列大小限制
        if (queue->get_size() >= max_queue_size_)
        {
            // 防止队列过大压垮内存，这里选择丢弃
            return;
        }

        // 将任务封装，添加异常处理
        queue->push([this, func = std::move(func)]() mutable
                    {
#if MODERN_CORO_DEBUG
        LOG_MODULE_DEBUG(logger::modules::SCHEDULER, "Executing scheduled task on thread {}", std::this_thread::get_id());
#endif
        try {
            func();
            total_completed_.fetch_add(1, std::memory_order_relaxed);
        } catch (const std::exception& e) {
            LOG_MODULE_ERROR(logger::modules::SCHEDULER, "Task execution failed: {}", e.what());
            total_completed_.fetch_add(1, std::memory_order_relaxed);
        } catch (...) {
            LOG_MODULE_ERROR(logger::modules::SCHEDULER, "Task execution failed with unknown exception");
            total_completed_.fetch_add(1, std::memory_order_relaxed);
        } });

        // 唤醒对应的 worker
        queue->cv.notify_one();
    }

    Scheduler *Scheduler::GetCurrent()
    {
        return current_scheduler_;
    }

    Task<> Scheduler::sleep(std::chrono::milliseconds duration)
    {
#if MODERN_CORO_DEBUG
        LOG_MODULE_DEBUG(logger::modules::SCHEDULER, "Scheduler::sleep enter, this={} dur={}ms tid={}",
                         static_cast<void *>(this), duration.count(), std::this_thread::get_id());
#endif
        Scheduler *s = Scheduler::GetCurrent();
        if (!s)
            s = this; // 兜底：若当前线程未注册，则退回到调用对象
#if MODERN_CORO_DEBUG
        LOG_MODULE_DEBUG(logger::modules::SCHEDULER, "Scheduler::sleep using scheduler={} timer={}",
                         static_cast<void *>(s), static_cast<void *>(s ? s->timer_.get() : nullptr));
#endif
        return s->timer_->sleep_for(duration);
    }

    void Scheduler::worker_thread()
    {
        current_scheduler_ = this;

        // 为每个 worker 线程分配一个队列索引
        static std::atomic<size_t> thread_id_counter{0};
        size_t my_queue_idx = thread_id_counter.fetch_add(1, std::memory_order_relaxed) % thread_count_;
        auto &my_queue = worker_queues_[my_queue_idx];

#if MODERN_CORO_DEBUG
        LOG_MODULE_DEBUG(logger::modules::SCHEDULER, "Worker thread started, scheduler={} queue_idx={} tid={}",
                         static_cast<void *>(this), my_queue_idx, std::this_thread::get_id());
#endif

        while (true)
        {
            std::function<void()> task;

            // 首先尝试从自己的队列获取任务
            if (my_queue->try_pop(task))
            {
                if (task)
                {
                    task(); // 异常处理已在schedule中完成
                }
                continue;
            }

            // 如果自己的队列为空，等待新任务或停止信号
            {
                std::unique_lock<std::mutex> lock(my_queue->mutex);
                my_queue->cv.wait_for(lock, std::chrono::milliseconds(10),
                                      [this, &my_queue]
                                      {
                                          return stop_flag_.load(std::memory_order_relaxed) || !my_queue->tasks.empty();
                                      });

                if (stop_flag_.load(std::memory_order_relaxed) && my_queue->tasks.empty())
                {
                    break;
                }
            }
        }

        current_scheduler_ = nullptr;
    }

    // 添加辅助函数实现：在有调度器时投递任务，否则回退到新线程
    void schedule_coroutine_task(std::function<void()> task)
    {
        if (auto scheduler = Scheduler::GetCurrent())
        {
            scheduler->schedule(std::move(task));
        }
        else
        {
            // 当没有调度器时，创建一个新线程来执行任务，避免同步执行导致的竞态条件
            std::thread([task = std::move(task)]()
                        { task(); })
                .detach();
        }
    }

    // 获取当前调度器指针（但不暴露Scheduler类型）
    Scheduler *get_current_scheduler_ptr() { return Scheduler::GetCurrent(); }

    // 调度器注册辅助函数
    void register_thread_on_scheduler(Scheduler *scheduler_ptr)
    {
        if (scheduler_ptr)
        {
            scheduler_ptr->register_thread();
        }
    }

    // 调度器注销辅助函数
    void unregister_thread_on_scheduler(Scheduler *scheduler_ptr)
    {
        if (scheduler_ptr)
        {
            scheduler_ptr->unregister_thread();
        }
    }
} // namespace modern_coro