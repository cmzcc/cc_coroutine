#pragma once

#include "scheduler/scheduler.h"
#include <deque>
#include <atomic>
#include <random>
#include <thread>
#include <vector>
#include <memory>
#include <unordered_map>

namespace modern_coro {

// 工作窃取调度器
class WorkStealingScheduler : public Scheduler {
public:
    explicit WorkStealingScheduler(size_t thread_count = std::thread::hardware_concurrency());
    ~WorkStealingScheduler() override;

    // 重写基类的stop方法
    void stop();
    
    // 重写基类的schedule方法 (注意：基类方法不是virtual，所以这是隐藏而不是重写)
    void schedule(std::function<void()> task);
    
    // 重写基类的模板schedule方法
    template<typename T>
    void schedule(Task<T>&& task, TaskCallback callback = nullptr) {
        auto task_ptr = std::make_shared<Task<T>>(std::move(task));
        schedule([this, task_ptr, callback = std::move(callback)]() mutable {
            try {
                // 等待任务完成
                task_ptr->get();
                if (callback) {
                    callback();
                }
            } catch (const std::exception& e) {
                std::cerr << "Task execution failed: " << e.what() << std::endl;
                if (callback) {
                    callback();
                }
            }
        });
    }
    
    // 带亲和性的任务调度
    void schedule_with_affinity(std::function<void()> task, size_t preferred_thread = SIZE_MAX);
    
    // 批量任务调度
    void schedule_batch(std::vector<std::function<void()>> tasks);
    
    // 获取统计信息方法 (注意：基类方法不是virtual，所以这是隐藏而不是重写)
    SchedulerStats get_stats() const {
        size_t total_queued = 0;
        for (const auto& queue : worker_queues_) {
            total_queued += queue->get_size();
        }
        
        return SchedulerStats{
            .active_coroutines = active_coroutines_.load(),
            .queued_tasks = total_queued,
            .total_scheduled = total_scheduled_.load(),
            .total_completed = total_completed_.load()
        };
    }
    
    // 工作窃取特定的统计信息
    struct WorkStealingStats {
        uint64_t total_steals;
        uint64_t successful_steals;
        uint64_t failed_steals;
        double steal_success_rate;
        std::vector<uint64_t> per_thread_executed;
        std::vector<uint64_t> per_thread_stolen;
    };
    
    WorkStealingStats get_work_stealing_stats() const;
    void print_work_stealing_stats() const;

protected:
    void worker_thread() override;

private:
    // 工作窃取队列结构
    struct WorkStealingQueue {
        // 任务队列
        std::deque<std::function<void()>> tasks;
        mutable std::mutex mutex;
        std::condition_variable cv;
        std::atomic<size_t> size{0};
        
        // 统计信息
        std::atomic<uint64_t> executed_tasks{0};
        std::atomic<uint64_t> stolen_tasks{0};
        std::atomic<uint64_t> failed_steals{0};
        
        // 本地推送任务
        void push_local(std::function<void()> task) {
            std::lock_guard<std::mutex> lock(mutex);
            tasks.push_back(std::move(task));
            size.fetch_add(1);
        }
        
        // 本地弹出任务
        bool pop_local(std::function<void()>& task) {
            std::lock_guard<std::mutex> lock(mutex);
            if (tasks.empty()) {
                return false;
            }
            task = std::move(tasks.back());
            tasks.pop_back();
            size.fetch_sub(1);
            return true;
        }
        
        // 窃取任务
        bool steal(std::function<void()>& task) {
            std::lock_guard<std::mutex> lock(mutex);
            if (tasks.empty()) {
                failed_steals.fetch_add(1);
                return false;
            }
            task = std::move(tasks.front());
            tasks.pop_front();
            size.fetch_sub(1);
            stolen_tasks.fetch_add(1);
            return true;
        }
        
        size_t get_size() const {
            return size.load();
        }
    };
    
    std::vector<std::unique_ptr<WorkStealingQueue>> worker_queues_;
    std::unordered_map<std::thread::id, size_t> thread_id_to_index_;
    
    // 随机数生成器（用于窃取目标选择）
    thread_local static std::mt19937 rng_;
    
    // 获取当前线程索引
    size_t get_current_thread_index();
    
    // 尝试窃取工作
    bool try_steal_work(size_t current_thread_idx, std::function<void()>& task);
    
    // 窃取统计信息
    struct StealingStats {
        std::atomic<uint64_t> total_steals{0};
        std::atomic<uint64_t> successful_steals{0};
        std::atomic<uint64_t> failed_steals{0};
        std::chrono::steady_clock::time_point last_reset;
        
        double get_success_rate() const {
            auto total = total_steals.load();
            return total > 0 ? static_cast<double>(successful_steals.load()) / total : 0.0;
        }
    };
    
    mutable StealingStats stealing_stats_;
    std::atomic<std::chrono::milliseconds> steal_backoff_time_{std::chrono::milliseconds(1)};
    
    // 调整窃取策略
    void adjust_stealing_strategy();
};

} // namespace modern_coro