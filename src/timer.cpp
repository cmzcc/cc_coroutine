#include "timer.h"
#include "scheduler.h"
#include <algorithm>

namespace modern_coro {

Timer::Timer() : stop_flag_(false), started_(false) {
    // 构造函数中不自动启动线程
}

Timer::~Timer() {
    stop();
}

void Timer::start() {
    if (!started_.exchange(true)) {
        stop_flag_ = false;
        timer_thread_ = std::thread(&Timer::timer_worker, this);
    }
}

void Timer::stop() {
    if (started_.exchange(false)) {
        stop_flag_ = true;
        timer_cv_.notify_all();
        if (timer_thread_.joinable()) {
            timer_thread_.join();
        }
    }
}

TimerAwaiter::TimerAwaiter(Timer& timer, Timer::TimePoint time_point, void* scheduler_ptr)
    : timer_(timer), time_point_(time_point), scheduler_ptr_(scheduler_ptr) {}

bool TimerAwaiter::await_ready() const noexcept {
    return std::chrono::steady_clock::now() >= time_point_;
}

void TimerAwaiter::await_suspend(std::coroutine_handle<> h) {
    timer_.add_timer(time_point_, h, scheduler_ptr_);
}

Task<> Timer::sleep_until(TimePoint time_point) {
    void* scheduler_ptr = get_current_scheduler_ptr();
    co_await TimerAwaiter{*this, time_point, scheduler_ptr};
}

Task<> Timer::sleep_for(Duration duration) {
    co_await sleep_until(std::chrono::steady_clock::now() + duration);
}

void Timer::add_timer(TimePoint time_point, std::function<void()> callback, void* scheduler_ptr) {
    {
        std::lock_guard<std::mutex> lock(timer_mutex_);
        timer_queue_.push({time_point, {}, std::move(callback), scheduler_ptr});
    }
    timer_cv_.notify_one();
}

void Timer::add_timer(TimePoint time_point, std::coroutine_handle<> handle, void* scheduler_ptr) {
    {
        std::lock_guard<std::mutex> lock(timer_mutex_);
        timer_queue_.push({time_point, handle, {}, scheduler_ptr});
    }
    timer_cv_.notify_one();
}

void Timer::timer_worker() {
    std::unique_lock<std::mutex> lock(timer_mutex_);
    
    while (!stop_flag_) {
        if (timer_queue_.empty()) {
            timer_cv_.wait(lock, [this] { return stop_flag_ || !timer_queue_.empty(); });
            continue;
        }
        
        auto next_event = timer_queue_.top();
        auto now = std::chrono::steady_clock::now();
        
        if (now >= next_event.time_point) {
            timer_queue_.pop();
            
            auto scheduler = static_cast<Scheduler*>(next_event.scheduler_ptr_);

            if (next_event.handle) {
                if (scheduler) {
                    scheduler->schedule([handle = next_event.handle]() {
                        handle.resume();
                    });
                } else {
                    next_event.handle.resume();
                }
            }
            
            if (next_event.callback) {
                if (scheduler) {
                    scheduler->schedule(next_event.callback);
                } else {
                    next_event.callback();
                }
            }
        } else {
            // Wait until the next event's time point. If we are notified
            // (e.g., a new timer was added) or wake up spuriously, the loop
            // will simply restart and re-evaluate the timer queue.
            timer_cv_.wait_until(lock, next_event.time_point);
        }
    }
}

} // namespace modern_coro