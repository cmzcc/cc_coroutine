#pragma once

#include "coroutine.h"
#include <chrono>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>

namespace modern_coro {

class Timer {
public:
    using TimePoint = std::chrono::steady_clock::time_point;
    using Duration = std::chrono::milliseconds;
    
    Timer();
    ~Timer();
    
    // 启动定时器
    void start();
    
    // 停止定时器
    void stop();
    
    // 在指定时间后执行
    Task<> sleep_until(TimePoint time_point);
    
    // 睡眠指定时间
    Task<> sleep_for(Duration duration);
    
    // 添加定时任务
    void add_timer(TimePoint time_point, std::function<void()> callback, void* scheduler_ptr);
    void add_timer(TimePoint time_point, std::coroutine_handle<> handle, void* scheduler_ptr);
    
private:
    struct TimerEvent {
        TimePoint time_point;
        std::coroutine_handle<> handle;
        std::function<void()> callback;
        
        void* scheduler_ptr_; // 保存调度器指针

        bool operator>(const TimerEvent& other) const {
            return time_point > other.time_point;
        }
    };
    
    void timer_worker();
    
    std::priority_queue<TimerEvent, std::vector<TimerEvent>, std::greater<TimerEvent>> timer_queue_;
    std::mutex timer_mutex_;
    std::condition_variable timer_cv_;
    std::thread timer_thread_;
    std::atomic<bool> stop_flag_{false};
    std::atomic<bool> started_{false};
    
    friend class TimerAwaiter;
};

// 定时器awaiter - 完善实现
class Timer;

class TimerAwaiter {
public:
    explicit TimerAwaiter(Timer& timer, Timer::TimePoint time_point, void* scheduler_ptr);

    bool await_ready() const noexcept;

    void await_suspend(std::coroutine_handle<> h);

    void await_resume() const noexcept {}

private:
    Timer& timer_;
    Timer::TimePoint time_point_;
    void* scheduler_ptr_;
};

} // namespace modern_coro