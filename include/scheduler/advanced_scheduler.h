#pragma once

#include "scheduler/scheduler.h"
#include <queue>
#include <chrono>
#include <random>

namespace modern_coro {

// 任务优先级
enum class TaskPriority {
    LOW = 0,
    NORMAL = 1,
    HIGH = 2,
    CRITICAL = 3
};

// 带优先级的任务
struct PriorityTask {
    std::function<void()> task;
    TaskPriority priority;
    std::chrono::steady_clock::time_point submit_time;
    uint64_t task_id;
    
    bool operator<(const PriorityTask& other) const {
        // 优先级高的先执行，优先级相同时先提交的先执行
        if (priority != other.priority) {
            return priority < other.priority;
        }
        return submit_time > other.submit_time;
    }
};

// 高级调度器
class AdvancedScheduler : public Scheduler {
public:
    explicit AdvancedScheduler(size_t thread_count = std::thread::hardware_concurrency());
    
    // 带优先级的任务调度
    void schedule_with_priority(std::function<void()> task, TaskPriority priority = TaskPriority::NORMAL);
    
    // 重写基类的schedule方法以支持Task对象
    template<typename T>
    void schedule(Task<T>&& task, TaskCallback callback = nullptr) {
        auto task_ptr = std::make_shared<Task<T>>(std::move(task));
        schedule_with_priority([this, task_ptr, callback = std::move(callback)]() mutable {
            auto handle = task_ptr->handle();
            if (handle) {
                if (!handle.done()) {
                    handle.resume();
                }
            }
            if (callback) {
                callback();
            }
        }, TaskPriority::NORMAL);
    }
    
    // 工作窃取调度
    void enable_work_stealing(bool enable = true);
    
    // 设置时间片大小
    void set_time_slice(std::chrono::milliseconds time_slice);
    
    // 获取调度统计信息
    struct SchedulerStats {
        uint64_t total_tasks_executed;
        uint64_t tasks_in_queue;
        double average_wait_time_ms;
        std::vector<size_t> thread_task_counts;
    };
    
    SchedulerStats get_stats() const;
    
protected:
    void worker_thread() override;
    
private:
    // 每个线程的任务队列
    struct ThreadQueue {
        std::priority_queue<PriorityTask> tasks;
        std::mutex mutex;
        std::condition_variable cv;
        std::atomic<size_t> task_count{0};
    };
    
    std::vector<std::unique_ptr<ThreadQueue>> thread_queues_;
    std::atomic<uint64_t> next_task_id_{0};
    std::atomic<uint64_t> total_tasks_executed_{0};
    bool work_stealing_enabled_ = true;
    std::chrono::milliseconds time_slice_{10}; // 10ms时间片
    
    // 工作窃取
    bool try_steal_task(size_t thief_id, std::function<void()>& stolen_task);
    size_t get_thread_id();
};

// 协程嵌套支持
class NestedCoroutineManager {
public:
    static NestedCoroutineManager& instance();
    
    // 进入嵌套协程
    void enter_nested_context();
    
    // 退出嵌套协程
    void exit_nested_context();
    
    // 获取当前嵌套层级
    size_t get_nesting_level() const;
    
    // 是否在嵌套协程中
    bool is_nested() const;
    
private:
    thread_local static size_t nesting_level_;
    thread_local static std::vector<std::coroutine_handle<>> context_stack_;
};

// 嵌套协程RAII管理器
class NestedCoroutineGuard {
public:
    NestedCoroutineGuard() {
        NestedCoroutineManager::instance().enter_nested_context();
    }
    
    ~NestedCoroutineGuard() {
        NestedCoroutineManager::instance().exit_nested_context();
    }
};

// 支持嵌套的任务类型
template<typename T = void>
class NestedTask : public Task<T> {
public:
    struct promise_type : public Task<T>::promise_type {
        NestedTask get_return_object() {
            return NestedTask{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        
        std::suspend_always initial_suspend() noexcept {
            // 记录嵌套上下文
            nesting_guard_ = std::make_unique<NestedCoroutineGuard>();
            return {};
        }
        
        std::suspend_always final_suspend() noexcept {
            // 清理嵌套上下文
            nesting_guard_.reset();
            return {};
        }
        
    private:
        std::unique_ptr<NestedCoroutineGuard> nesting_guard_;
    };
    
    using handle_type = std::coroutine_handle<promise_type>;
    
    NestedTask(handle_type h) : Task<T>(h) {}
};

} // namespace modern_coro