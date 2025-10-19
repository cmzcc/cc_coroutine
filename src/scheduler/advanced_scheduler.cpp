#include "../../include/scheduler/advanced_scheduler.h"
#include <algorithm>
#include <random>
#include <iostream>

namespace modern_coro {

// NestedCoroutineManager 实现
thread_local size_t NestedCoroutineManager::nesting_level_ = 0;
thread_local std::vector<std::coroutine_handle<>> NestedCoroutineManager::context_stack_;

NestedCoroutineManager& NestedCoroutineManager::instance() {
    static NestedCoroutineManager instance;
    return instance;
}

void NestedCoroutineManager::enter_nested_context() {
    nesting_level_++;
}

void NestedCoroutineManager::exit_nested_context() {
    if (nesting_level_ > 0) {
        nesting_level_--;
    }
}

size_t NestedCoroutineManager::get_nesting_level() const {
    return nesting_level_;
}

bool NestedCoroutineManager::is_nested() const {
    return nesting_level_ > 0;
}

// AdvancedScheduler 实现
AdvancedScheduler::AdvancedScheduler(size_t thread_count) 
    : Scheduler(thread_count) {
    thread_queues_.resize(thread_count);
    for (size_t i = 0; i < thread_count; ++i) {
        thread_queues_[i] = std::make_unique<ThreadQueue>();
    }
}

void AdvancedScheduler::schedule_with_priority(std::function<void()> task, TaskPriority priority) {
    auto now = std::chrono::steady_clock::now();
    uint64_t task_id = next_task_id_.fetch_add(1);
    
    PriorityTask ptask{
        .task = std::move(task),
        .priority = priority,
        .submit_time = now,
        .task_id = task_id
    };
    
    // 选择负载最轻的线程队列
    size_t min_load_thread = 0;
    size_t min_load = thread_queues_[0]->task_count.load();
    
    for (size_t i = 1; i < thread_queues_.size(); ++i) {
        size_t load = thread_queues_[i]->task_count.load();
        if (load < min_load) {
            min_load = load;
            min_load_thread = i;
        }
    }
    
    {
        std::lock_guard<std::mutex> lock(thread_queues_[min_load_thread]->mutex);
        thread_queues_[min_load_thread]->tasks.push(std::move(ptask));
        thread_queues_[min_load_thread]->task_count.fetch_add(1);
    }
    thread_queues_[min_load_thread]->cv.notify_one();
}

void AdvancedScheduler::enable_work_stealing(bool enable) {
    work_stealing_enabled_ = enable;
}

void AdvancedScheduler::set_time_slice(std::chrono::milliseconds time_slice) {
    time_slice_ = time_slice;
}

AdvancedScheduler::SchedulerStats AdvancedScheduler::get_stats() const {
    SchedulerStats stats;
    stats.total_tasks_executed = total_tasks_executed_.load();
    stats.tasks_in_queue = 0;
    stats.average_wait_time_ms = 0.0;
    stats.thread_task_counts.resize(thread_queues_.size());
    
    for (size_t i = 0; i < thread_queues_.size(); ++i) {
        std::lock_guard<std::mutex> lock(thread_queues_[i]->mutex);
        stats.tasks_in_queue += thread_queues_[i]->tasks.size();
        stats.thread_task_counts[i] = thread_queues_[i]->task_count.load();
    }
    
    return stats;
}

void AdvancedScheduler::worker_thread() {
    size_t thread_id = get_thread_id();
    current_scheduler_ = this;
    
    while (!stop_flag_.load()) {
        std::function<void()> task;
        bool has_task = false;
        
        // 1. 尝试从自己的队列获取任务
        {
            std::unique_lock<std::mutex> lock(thread_queues_[thread_id]->mutex);
            thread_queues_[thread_id]->cv.wait_for(lock, std::chrono::milliseconds(10), 
                [this, thread_id] { 
                    return stop_flag_.load() || !thread_queues_[thread_id]->tasks.empty(); 
                });
            
            if (!thread_queues_[thread_id]->tasks.empty()) {
                auto ptask = thread_queues_[thread_id]->tasks.top();
                thread_queues_[thread_id]->tasks.pop();
                thread_queues_[thread_id]->task_count.fetch_sub(1);
                task = std::move(ptask.task);
                has_task = true;
            }
        }
        
        // 2. 工作窃取
        if (!has_task && work_stealing_enabled_) {
            has_task = try_steal_task(thread_id, task);
        }
        
        // 3. 执行任务
        if (has_task) {
            auto start_time = std::chrono::steady_clock::now();
            
            try {
                task();
                total_tasks_executed_.fetch_add(1);
            } catch (const std::exception& e) {
                std::cerr << "Task execution failed: " << e.what() << std::endl;
            }
            
            // 时间片控制
            auto elapsed = std::chrono::steady_clock::now() - start_time;
            if (elapsed < time_slice_) {
                std::this_thread::sleep_for(time_slice_ - elapsed);
            }
        }
    }
    
    current_scheduler_ = nullptr;
}

bool AdvancedScheduler::try_steal_task(size_t thief_id, std::function<void()>& stolen_task) {
    static thread_local std::random_device rd;
    static thread_local std::mt19937 gen(rd());
    
    std::vector<size_t> candidates;
    for (size_t i = 0; i < thread_queues_.size(); ++i) {
        if (i != thief_id && thread_queues_[i]->task_count.load() > 1) {
            candidates.push_back(i);
        }
    }
    
    if (candidates.empty()) {
        return false;
    }
    
    std::shuffle(candidates.begin(), candidates.end(), gen);
    
    for (size_t victim_id : candidates) {
        std::unique_lock<std::mutex> lock(thread_queues_[victim_id]->mutex, std::try_to_lock);
        if (lock.owns_lock() && !thread_queues_[victim_id]->tasks.empty()) {
            auto ptask = thread_queues_[victim_id]->tasks.top();
            thread_queues_[victim_id]->tasks.pop();
            thread_queues_[victim_id]->task_count.fetch_sub(1);
            stolen_task = std::move(ptask.task);
            return true;
        }
    }
    
    return false;
}

size_t AdvancedScheduler::get_thread_id() {
    static thread_local size_t id = []() {
        static std::atomic<size_t> counter{0};
        return counter.fetch_add(1);
    }();
    return id % thread_queues_.size();
}

} // namespace modern_coro