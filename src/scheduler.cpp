#include "scheduler.h"
#include <iostream>
#include <thread>

namespace modern_coro {

void increment_active_coroutines_on_current_scheduler() {
    if (auto s = Scheduler::GetCurrent()) {
        s->increment_active_coroutines();
    }
}

void decrement_active_coroutines_on_current_scheduler() {
    if (auto s = Scheduler::GetCurrent()) {
        s->decrement_active_coroutines();
    }
}

// 新增：直接在指定调度器上减少计数器的函数
void decrement_active_coroutines_on_scheduler(void* scheduler_ptr) {
    if (scheduler_ptr) {
        auto scheduler = static_cast<Scheduler*>(scheduler_ptr);
        scheduler->decrement_active_coroutines();
    }
}

thread_local Scheduler* Scheduler::current_scheduler_ = nullptr;

Scheduler::Scheduler(size_t thread_count)
    : thread_count_(thread_count), timer_(std::make_unique<Timer>()) {
    if (thread_count_ == 0) {
        thread_count_ = 1;
    }
}

Scheduler::~Scheduler() {
    stop();
}

void Scheduler::start() {
    timer_->start();
    workers_.reserve(thread_count_);
    for (size_t i = 0; i < thread_count_; ++i) {
        workers_.emplace_back(&Scheduler::worker_thread, this);
    }
}

void Scheduler::stop() {
    timer_->stop();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_flag_ = true;
    }
    cv_.notify_all();
    
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();
    
    // 清理剩余任务以防止内存泄漏
    std::lock_guard<std::mutex> lock(mutex_);
    while (!tasks_.empty()) {
        tasks_.pop();
    }
}

void Scheduler::schedule(std::function<void()> func) {
    if (!func) return; // 防止空函数指针
    
    total_scheduled_.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_flag_) {
            // 调度器已停止，不接受新任务
            return;
        }
        tasks_.emplace([this, func = std::move(func)]() {
            try {
                func();
                total_completed_.fetch_add(1, std::memory_order_relaxed);
            } catch (const std::exception& e) {
                std::cerr << "Task execution failed: " << e.what() << std::endl;
                total_completed_.fetch_add(1, std::memory_order_relaxed);
            } catch (...) {
                std::cerr << "Task execution failed with unknown exception" << std::endl;
                total_completed_.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    cv_.notify_one();
}

Scheduler* Scheduler::GetCurrent() {
    return current_scheduler_;
}

Task<> Scheduler::sleep(std::chrono::milliseconds duration) {
    return timer_->sleep_for(duration);
}

void Scheduler::worker_thread() {
    current_scheduler_ = this;
    
    while (true) {
        std::function<void()> task;
        
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stop_flag_ || !tasks_.empty(); });
            
            if (stop_flag_ && tasks_.empty()) {
                break;
            }
            
            if (!tasks_.empty()) {
                task = std::move(tasks_.front());
                tasks_.pop();
            }
        }
        
        if (task) {
            task(); // 异常处理已在schedule中完成
        }
    }
    
    current_scheduler_ = nullptr;
}

// 添加辅助函数实现：在有调度器时投递任务，否则回退到新线程
void schedule_coroutine_task(std::function<void()> task) {
    if (auto scheduler = Scheduler::GetCurrent()) {
        scheduler->schedule(std::move(task));
    } else {
        std::thread(std::move(task)).detach();
    }
}

// 获取当前调度器指针（但不暴露Scheduler类型）
void* get_current_scheduler_ptr() {
    return Scheduler::GetCurrent();
}

// 调度器注册辅助函数
void register_thread_on_scheduler(void* scheduler_ptr) {
    if (scheduler_ptr) {
        static_cast<Scheduler*>(scheduler_ptr)->register_thread();
    }
}

// 调度器注销辅助函数
void unregister_thread_on_scheduler(void* scheduler_ptr) {
    if (scheduler_ptr) {
        static_cast<Scheduler*>(scheduler_ptr)->unregister_thread();
    }
}
} // namespace modern_coro