#include "../../include/scheduler/work_stealing_scheduler.h"
#include <algorithm>
#include <chrono>
#include <iostream>

namespace modern_coro {

thread_local std::mt19937 WorkStealingScheduler::rng_(std::random_device{}());

WorkStealingScheduler::WorkStealingScheduler(size_t thread_count) 
    : Scheduler(thread_count) {
    // 为每个工作线程创建队列
    worker_queues_.reserve(thread_count);
    for (size_t i = 0; i < thread_count; ++i) {
        worker_queues_.emplace_back(std::make_unique<WorkStealingQueue>());
    }
    
    stealing_stats_.last_reset = std::chrono::steady_clock::now();
}

WorkStealingScheduler::~WorkStealingScheduler() {
    stop();
}

void WorkStealingScheduler::schedule(std::function<void()> task) {
    if (!task) return;
    
    total_scheduled_.fetch_add(1);
    
    // 尝试分配到当前线程的队列
    size_t target_thread = get_current_thread_index();
    if (target_thread < worker_queues_.size()) {
        worker_queues_[target_thread]->push_local(std::move(task));
        worker_queues_[target_thread]->cv.notify_one();
    } else {
        // 如果无法确定当前线程，使用负载均衡
        size_t min_load_thread = 0;
        size_t min_load = worker_queues_[0]->get_size();
        
        for (size_t i = 1; i < worker_queues_.size(); ++i) {
            size_t load = worker_queues_[i]->get_size();
            if (load < min_load) {
                min_load = load;
                min_load_thread = i;
            }
        }
        
        worker_queues_[min_load_thread]->push_local(std::move(task));
        worker_queues_[min_load_thread]->cv.notify_one();
    }
}

void WorkStealingScheduler::schedule_with_affinity(std::function<void()> task, size_t preferred_thread) {
    if (!task) return;
    
    total_scheduled_.fetch_add(1);
    
    if (preferred_thread == SIZE_MAX || preferred_thread >= worker_queues_.size()) {
        // 使用默认调度
        schedule(std::move(task));
        return;
    }
    
    worker_queues_[preferred_thread]->push_local(std::move(task));
    worker_queues_[preferred_thread]->cv.notify_one();
}

void WorkStealingScheduler::schedule_batch(std::vector<std::function<void()>> tasks) {
    if (tasks.empty()) return;
    
    total_scheduled_.fetch_add(tasks.size());
    
    // 将任务均匀分布到各个队列
    size_t queue_idx = 0;
    for (auto& task : tasks) {
        if (task) {
            worker_queues_[queue_idx]->push_local(std::move(task));
            worker_queues_[queue_idx]->cv.notify_one();
            queue_idx = (queue_idx + 1) % worker_queues_.size();
        }
    }
}

void WorkStealingScheduler::worker_thread() {
    register_thread();
    
    // 获取当前线程的索引
    size_t thread_idx = SIZE_MAX;
    {
        std::thread::id current_id = std::this_thread::get_id();
        auto it = thread_id_to_index_.find(current_id);
        if (it != thread_id_to_index_.end()) {
            thread_idx = it->second;
        } else {
            // 如果找不到索引，分配一个新的
            thread_idx = thread_id_to_index_.size();
            thread_id_to_index_[current_id] = thread_idx;
        }
    }
    
    if (thread_idx >= worker_queues_.size()) {
        std::cerr << "Warning: Thread index out of range" << std::endl;
        unregister_thread();
        return;
    }
    
    auto& local_queue = worker_queues_[thread_idx];
    std::function<void()> task;
    
    while (!stop_flag_.load()) {
        bool found_task = false;
        
        // 1. 首先尝试从本地队列获取任务
        if (local_queue->pop_local(task)) {
            found_task = true;
        }
        // 2. 如果本地队列为空，尝试窃取工作
        else if (try_steal_work(thread_idx, task)) {
            found_task = true;
            stealing_stats_.successful_steals.fetch_add(1);
        }
        
        if (found_task) {
            try {
                task();
                local_queue->executed_tasks.fetch_add(1);
                total_completed_.fetch_add(1);
            } catch (const std::exception& e) {
                std::cerr << "Task execution failed: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "Task execution failed with unknown exception" << std::endl;
            }
            
            // 重置退避时间
            steal_backoff_time_.store(std::chrono::milliseconds(1));
        } else {
            // 没有找到任务，等待或休眠
            std::unique_lock<std::mutex> lock(local_queue->mutex);
            local_queue->cv.wait_for(lock, steal_backoff_time_.load(), 
                [this, &local_queue]() { 
                    return stop_flag_.load() || !local_queue->tasks.empty(); 
                });
            
            // 调整窃取策略
            adjust_stealing_strategy();
        }
    }
    
    unregister_thread();
}

size_t WorkStealingScheduler::get_current_thread_index() {
    std::thread::id current_id = std::this_thread::get_id();
    auto it = thread_id_to_index_.find(current_id);
    return (it != thread_id_to_index_.end()) ? it->second : SIZE_MAX;
}

bool WorkStealingScheduler::try_steal_work(size_t current_thread_idx, std::function<void()>& task) {
    if (worker_queues_.size() <= 1) {
        return false;
    }
    
    stealing_stats_.total_steals.fetch_add(1);
    
    // 随机选择窃取目标
    std::uniform_int_distribution<size_t> dist(0, worker_queues_.size() - 1);
    
    // 尝试从多个队列窃取
    for (size_t attempts = 0; attempts < worker_queues_.size() - 1; ++attempts) {
        size_t victim_idx = dist(rng_);
        
        // 跳过自己的队列
        if (victim_idx == current_thread_idx) {
            victim_idx = (victim_idx + 1) % worker_queues_.size();
        }
        
        if (worker_queues_[victim_idx]->steal(task)) {
            return true;
        }
    }
    
    stealing_stats_.failed_steals.fetch_add(1);
    return false;
}

void WorkStealingScheduler::adjust_stealing_strategy() {
    auto current_time = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        current_time - stealing_stats_.last_reset);
    
    // 每10秒调整一次策略
    if (elapsed.count() >= 10) {
        double success_rate = stealing_stats_.get_success_rate();
        
        if (success_rate < 0.1) {
            // 成功率低，增加退避时间
            auto current_backoff = steal_backoff_time_.load();
            steal_backoff_time_.store(std::min(current_backoff * 2, std::chrono::milliseconds(100)));
        } else if (success_rate > 0.5) {
            // 成功率高，减少退避时间
            auto current_backoff = steal_backoff_time_.load();
            steal_backoff_time_.store(std::max(current_backoff / 2, std::chrono::milliseconds(1)));
        }
        
        // 重置统计信息
        stealing_stats_.total_steals.store(0);
        stealing_stats_.successful_steals.store(0);
        stealing_stats_.failed_steals.store(0);
        stealing_stats_.last_reset = current_time;
    }
}

WorkStealingScheduler::WorkStealingStats WorkStealingScheduler::get_work_stealing_stats() const {
    WorkStealingStats stats;
    stats.total_steals = stealing_stats_.total_steals.load();
    stats.successful_steals = stealing_stats_.successful_steals.load();
    stats.failed_steals = stealing_stats_.failed_steals.load();
    stats.steal_success_rate = stealing_stats_.get_success_rate();
    
    stats.per_thread_executed.reserve(worker_queues_.size());
    stats.per_thread_stolen.reserve(worker_queues_.size());
    
    for (const auto& queue : worker_queues_) {
        stats.per_thread_executed.push_back(queue->executed_tasks.load());
        stats.per_thread_stolen.push_back(queue->stolen_tasks.load());
    }
    
    return stats;
}

void WorkStealingScheduler::print_work_stealing_stats() const {
    auto stats = get_work_stealing_stats();
    auto scheduler_stats = get_stats();
    
    std::cout << "\n=== 工作窃取调度器统计信息 ===" << std::endl;
    std::cout << "基本统计:" << std::endl;
    std::cout << "  活跃协程数: " << scheduler_stats.active_coroutines << std::endl;
    std::cout << "  队列任务数: " << scheduler_stats.queued_tasks << std::endl;
    std::cout << "  总调度数: " << scheduler_stats.total_scheduled << std::endl;
    std::cout << "  总完成数: " << scheduler_stats.total_completed << std::endl;
    
    std::cout << "\n工作窃取统计:" << std::endl;
    std::cout << "  总窃取尝试: " << stats.total_steals << std::endl;
    std::cout << "  成功窃取: " << stats.successful_steals << std::endl;
    std::cout << "  失败窃取: " << stats.failed_steals << std::endl;
    std::cout << "  窃取成功率: " << (stats.steal_success_rate * 100) << "%" << std::endl;
    
    std::cout << "\n每线程统计:" << std::endl;
    for (size_t i = 0; i < stats.per_thread_executed.size(); ++i) {
        std::cout << "  线程 " << i << ": 执行=" << stats.per_thread_executed[i] 
                  << ", 被窃取=" << stats.per_thread_stolen[i] << std::endl;
    }
}

} // namespace modern_coro