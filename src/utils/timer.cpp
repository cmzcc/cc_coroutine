#include "../../include/utils/timer.h"
#include "../../include/scheduler/scheduler.h"
#include <algorithm>

namespace modern_coro {

#undef MODERN_CORO_DEBUG
#define MODERN_CORO_DEBUG 0
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

TimerAwaiter::TimerAwaiter(Timer& timer, Timer::TimePoint time_point, Scheduler* scheduler_ptr)
    : timer_(timer), time_point_(time_point), scheduler_ptr_(scheduler_ptr) {}

bool TimerAwaiter::await_ready() const noexcept {
    return std::chrono::steady_clock::now() >= time_point_;
}

void TimerAwaiter::await_suspend(std::coroutine_handle<> h) {
    timer_.add_timer(time_point_, h, scheduler_ptr_);
}

Task<> Timer::sleep_until(TimePoint time_point) {
    Scheduler* scheduler_ptr = get_current_scheduler_ptr();
    co_await TimerAwaiter{*this, time_point, scheduler_ptr};
}

Task<> Timer::sleep_for(Duration duration) {
    co_await sleep_until(std::chrono::steady_clock::now() + duration);
}

void Timer::add_timer(TimePoint time_point, std::function<void()> callback, Scheduler* scheduler_ptr) {
    {
        std::lock_guard<std::mutex> lock(timer_mutex_);
        timer_queue_.push({time_point, nullptr, std::move(callback), scheduler_ptr});
    }
#if MODERN_CORO_DEBUG
    std::cerr << "[DEBUG] add_timer callback scheduled at "
              << std::chrono::duration_cast<std::chrono::milliseconds>(time_point - std::chrono::steady_clock::now()).count()
              << " ms from now" << std::endl;
#endif
    timer_cv_.notify_one();
}

void Timer::add_timer(TimePoint time_point, std::coroutine_handle<> handle, Scheduler* scheduler_ptr) {
    {
        std::lock_guard<std::mutex> lock(timer_mutex_);
        timer_queue_.push({time_point, handle, {}, scheduler_ptr});
    }
#if MODERN_CORO_DEBUG
    std::cerr << "[DEBUG] add_timer handle=" << handle.address()
              << " sched_ptr=" << scheduler_ptr
              << " due_in_ms="
              << std::chrono::duration_cast<std::chrono::milliseconds>(time_point - std::chrono::steady_clock::now()).count()
              << std::endl;
#endif
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
#if MODERN_CORO_DEBUG
            std::cerr << "[DEBUG] timer fire handle=" << (next_event.handle ? next_event.handle.address() : nullptr)
                      << " cb=" << static_cast<bool>(next_event.callback)
                      << " sched_ptr=" << next_event.scheduler_ptr_ << std::endl;
#endif
            timer_queue_.pop();
            lock.unlock(); // 执行回调前解锁，避免长时间持锁
            Scheduler* scheduler = next_event.scheduler_ptr_ ? next_event.scheduler_ptr_ : Scheduler::GetCurrent();
            if (!scheduler) {
                // 无可用调度器，直接同步执行以避免丢失任务
                if (next_event.handle) next_event.handle.resume();
                if (next_event.callback) next_event.callback();
            } else {
                if (next_event.handle) {
                    scheduler->schedule([handle = next_event.handle]() mutable {
#if MODERN_CORO_DEBUG
                        std::cerr << "[DEBUG] resuming handle=" << handle.address() << std::endl;
#endif
                        handle.resume();
                    });
                }
                if (next_event.callback) {
                    scheduler->schedule(std::move(next_event.callback));
                }
            }
            // 重新加锁后继续循环，避免对队列的非受控访问
            lock.lock();
            continue;
        } else {
            // Wait until the next event's time point. If we are notified
            // (e.g., a new timer was added) or wake up spuriously, the loop
            // will simply restart and re-evaluate the timer queue.
            timer_cv_.wait_until(lock, next_event.time_point);
        }
    }
}

} // namespace modern_coro