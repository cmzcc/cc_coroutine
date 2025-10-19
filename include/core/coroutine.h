#pragma once
#include <iostream>
#include <coroutine>
#include <memory>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include <chrono>
#include <future>
#include <exception>
#include <utility>
#include "safety/coroutine_cancellation.h"  // 添加新的取消机制
#include "core/memory_pool.h"              // 引入对象池以优化分配

namespace modern_coro {

class Scheduler;  // 前向声明，移除在命名空间内的头文件包含
class IOManager;

// 前向声明 CoroutineLifecycleManager
namespace safety {
    class CoroutineLifecycleManager;
}

// 辅助函数声明
void increment_active_coroutines_on_current_scheduler();
void decrement_active_coroutines_on_current_scheduler();

// 新增：直接在指定调度器上减少计数器的函数（类型安全）
void decrement_active_coroutines_on_scheduler(Scheduler* scheduler_ptr);

void schedule_coroutine_task(std::function<void()> task);

Scheduler* get_current_scheduler_ptr();

void register_thread_on_scheduler(Scheduler* scheduler_ptr);
void unregister_thread_on_scheduler(Scheduler* scheduler_ptr);

// 新增：协程生命周期管理辅助函数
void register_coroutine_with_lifecycle_manager(std::coroutine_handle<> handle);
void unregister_coroutine_with_lifecycle_manager(std::coroutine_handle<> handle);

// 协程任务基类
template<typename T = void>
class Task {
public:
    struct promise_type {
        // 成员变量声明移到前面
        std::coroutine_handle<> continuation_ = nullptr;
        T result;
        std::exception_ptr exception;
        std::atomic<bool> counted_{false};
        Scheduler* scheduler_ptr_;  // 保存创建时的调度器指针
        // 保持协程自持有，避免外部Task销毁导致的过早释放
        std::shared_ptr<void> keep_alive_{};
        
        promise_type() : scheduler_ptr_(get_current_scheduler_ptr()) {}
        
        ~promise_type() {
            // 析构函数中不需要处理计数器，由final_suspend处理
        }

        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        
        std::suspend_always initial_suspend() noexcept { 
            auto handle = std::coroutine_handle<promise_type>::from_promise(*this);
            
            // 注册到 CoroutineLifecycleManager
            register_coroutine_with_lifecycle_manager(handle);
            
            // 注册到 AdvancedCoroutineLifecycleManager
            cancellation::AdvancedCoroutineLifecycleManager::register_coroutine(handle);
            
            // 在initial_suspend中增加计数器并标记已计数
            if (scheduler_ptr_) {
                increment_active_coroutines_on_current_scheduler();
                counted_.store(true);
            } else {
                counted_.store(false);
            }
            return {}; 
        }
        
        // Task<T> 的 final_awaiter
        struct final_awaiter {
            bool await_ready() noexcept { return false; }
            void await_resume() noexcept {}
            void await_suspend(std::coroutine_handle<promise_type> h) noexcept {
                if (h.promise().continuation_) {
                    auto continuation = h.promise().continuation_;
                    Scheduler* scheduler_ptr = h.promise().scheduler_ptr_;
                    if (!scheduler_ptr) {
                        scheduler_ptr = get_current_scheduler_ptr();
                    }

                    if (scheduler_ptr) {
                        schedule_coroutine_task([continuation]() {
                            continuation.resume();
                        });
                    } else {
                        continuation.resume();
                    }
                }
            }
        };

        final_awaiter final_suspend() noexcept { 
            auto handle = std::coroutine_handle<promise_type>::from_promise(*this);
            
            // 从 CoroutineLifecycleManager 注销
            unregister_coroutine_with_lifecycle_manager(handle);

            // 从 AdvancedCoroutineLifecycleManager 注销
            cancellation::AdvancedCoroutineLifecycleManager::unregister_coroutine(handle);

            // 在final_suspend中减少计数器，确保只减少一次
            bool was_counted = counted_.exchange(false, std::memory_order_acq_rel);
            if (was_counted) {
                try {
                    // 使用保存的调度器指针来减少计数器，避免thread_local问题
                    if (scheduler_ptr_) {
                        decrement_active_coroutines_on_scheduler(scheduler_ptr_);
                    } else {
                        decrement_active_coroutines_on_current_scheduler();
                    }
                } catch (...) {
                    // 即使减少计数器失败，也要确保协程能够正常完成
                }
            }
            // 打破自持有循环
            keep_alive_.reset();
            return {}; 
        }
        
        template<typename U = T>
        void return_value(U&& value) {
            result = std::forward<U>(value);
        }
        
        void unhandled_exception() {
            exception = std::current_exception();
        }
    };
    
    using handle_type = std::coroutine_handle<promise_type>;
    
    struct SharedState {
        handle_type handle;
        std::atomic<int> ref_count{1};
        std::atomic<bool> destroyed{false};

        SharedState(handle_type h) : handle(h) {}

        void add_ref() { 
            ref_count.fetch_add(1, std::memory_order_relaxed);
        }

        void release() {
            int old_count = ref_count.fetch_sub(1, std::memory_order_acq_rel);
            std::cout << "[DEBUG] SharedState::release - ref_count: " << old_count << " -> " << (old_count - 1) << std::endl;
            if (old_count == 1) {
                bool expected = false;
                if (destroyed.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
                    std::cout << "[DEBUG] SharedState::release - Destroying handle: " << (handle ? handle.address() : nullptr) << std::endl;
                    if (handle) {
                        // 不在这里减少计数器，因为final_suspend已经处理了
                        // 只负责销毁协程句柄
                        try {
                            if (!handle.done()) {
                                std::cout << "[DEBUG] SharedState::release - Handle not done, destroying" << std::endl;
                                handle.destroy();
                            } else {
                                std::cout << "[DEBUG] SharedState::release - Handle already done" << std::endl;
                            }
                        } catch (...) {
                            std::cout << "[DEBUG] SharedState::release - Exception during handle destruction" << std::endl;
                            // 忽略销毁异常
                        }
                    }
                    std::cout << "[DEBUG] SharedState::release - Deleting SharedState" << std::endl;
                    delete this;
                }
            }
        }
    };
    
    Task(handle_type h) : state_(std::make_shared<SharedState>(h)) {}
    
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    
    Task(Task&& other) noexcept : state_(std::move(other.state_)) {}
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            state_ = std::move(other.state_);
        }
        return *this;
    }
    
    ~Task() {
        // SharedState会自动管理生命周期
    }
    
    bool ready() const {
        return state_ && state_->handle && state_->handle.done();
    }
    
    T get() {
        if (!state_ || !state_->handle) {
            throw std::runtime_error("Task is empty");
        }
        if (!state_->handle.done()) {
            throw std::runtime_error("Task is not ready");
        }
        if (state_->handle.promise().exception) {
            std::rethrow_exception(state_->handle.promise().exception);
        }
        return std::move(state_->handle.promise().result);
    }
    
    // Awaitable 接口
    bool await_ready() const noexcept {
        return ready();
    }
    
    // 类：Task<T>，方法：await_suspend
    void await_suspend(std::coroutine_handle<> continuation) {
        if (state_ && state_->handle) {
            state_->handle.promise().continuation_ = continuation;
            // 关键兜底：若未捕获到调度器，则在 await 时继承一次
            if (!state_->handle.promise().scheduler_ptr_) {
                state_->handle.promise().scheduler_ptr_ = get_current_scheduler_ptr();
            }
        }
        if (state_ && state_->handle && !state_->handle.done()) {
            auto state = state_;  // 保持 SharedState 存活
            Scheduler* scheduler_ptr = state->handle.promise().scheduler_ptr_;
            schedule_coroutine_task([state, scheduler_ptr]() {
                register_thread_on_scheduler(scheduler_ptr);
                if (state && state->handle && !state->handle.done()) {
                    state->handle.resume();
                }
            });
        } else {
            if (continuation) {
                continuation.resume();
            }
        }
    }
    
    T await_resume() {
        return get();
    }
    
    handle_type handle() const { return state_ ? state_->handle : handle_type{}; }
    // Keep-alive helpers for scheduler
    std::shared_ptr<void> share_state_opaque() const { return state_; }
    void set_keep_alive(std::shared_ptr<void> keep) {
        if (state_ && state_->handle) {
            state_->handle.promise().keep_alive_ = std::move(keep);
        }
    }
    
private:
    std::shared_ptr<SharedState> state_;
};

// 协程任务基类 - void特化
template<>
class Task<void> {
public:
    struct promise_type {
        promise_type() : scheduler_ptr_(get_current_scheduler_ptr()) {}
        
        ~promise_type() {
            // 析构函数中不需要处理计数器，由final_suspend处理
        }

        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        
        std::suspend_always initial_suspend() noexcept { 
            auto handle = std::coroutine_handle<promise_type>::from_promise(*this);
            
            // 注册到 CoroutineLifecycleManager
            register_coroutine_with_lifecycle_manager(handle);
            
            // 注册到 AdvancedCoroutineLifecycleManager
            cancellation::AdvancedCoroutineLifecycleManager::register_coroutine(handle);
            
            // 在initial_suspend中增加计数器并标记已计数
            if (scheduler_ptr_) {
                increment_active_coroutines_on_current_scheduler();
                counted_.store(true);
            } else {
                counted_.store(false);
            }
            return {}; 
        }
        
        // Task<void> 的 final_awaiter
        struct final_awaiter {
            bool await_ready() noexcept { return false; }
            void await_resume() noexcept {}
            void await_suspend(std::coroutine_handle<promise_type> h) noexcept {
                if (h.promise().continuation_) {
                    auto continuation = h.promise().continuation_;
                    Scheduler* scheduler_ptr = h.promise().scheduler_ptr_;
                    if (!scheduler_ptr) {
                        scheduler_ptr = get_current_scheduler_ptr();
                    }

                    if (scheduler_ptr) {
                        schedule_coroutine_task([continuation]() {
                            continuation.resume();
                        });
                    } else {
                        continuation.resume();
                    }
                }
            }
        };

        final_awaiter final_suspend() noexcept { 
            auto handle = std::coroutine_handle<promise_type>::from_promise(*this);
            
            // 从 CoroutineLifecycleManager 注销
            unregister_coroutine_with_lifecycle_manager(handle);

            // 从 AdvancedCoroutineLifecycleManager 注销
            cancellation::AdvancedCoroutineLifecycleManager::unregister_coroutine(handle);

            // 在final_suspend中减少计数器，确保只减少一次
            bool was_counted = counted_.exchange(false);
            if (was_counted) {
                try {
                    // 使用保存的调度器指针来减少计数器，避免thread_local问题
                    if (scheduler_ptr_) {
                        decrement_active_coroutines_on_scheduler(scheduler_ptr_);
                    } else {
                        decrement_active_coroutines_on_current_scheduler();
                    }
                } catch (...) {
                    // 即使减少计数器失败，也要确保协程能够正常完成
                }
            }
            // 打破自持有循环
            keep_alive_.reset();
            return {}; 
        }
        
        std::coroutine_handle<> continuation_ = nullptr;
        
        void return_void() {}
        
        void unhandled_exception() {
            exception = std::current_exception();
        }
        
    std::exception_ptr exception;
    std::atomic<bool> counted_{false};
    Scheduler* scheduler_ptr_;  // 保存创建时的调度器指针
    // 保持自持有
    std::shared_ptr<void> keep_alive_{};
    char dummy[4] = {0};  // 为了对齐
    };
    
    using handle_type = std::coroutine_handle<promise_type>;
    
    struct SharedState {
        handle_type handle;
        std::atomic<int> ref_count{1};
        std::atomic<bool> destroyed{false};

        SharedState(handle_type h) : handle(h) {}

        void add_ref() { ref_count.fetch_add(1, std::memory_order_relaxed); }
        void release() {
            int old_count = ref_count.fetch_sub(1, std::memory_order_acq_rel);
            if (old_count == 1) {
                bool expected = false;
                if (destroyed.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
                    if (handle) {
                        // 不在这里减少计数器，因为final_suspend已经处理了
                        // 只负责销毁协程句柄
                        try {
                            if (!handle.done()) {
                                handle.destroy();
                            }
                        } catch (...) {
                            // 忽略销毁异常
                        }
                    }
                    delete this;
                }
            }
        }
    };
    
    Task(handle_type h) : state_(std::make_shared<SharedState>(h)) {}
    
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    
    Task(Task&& other) noexcept : state_(std::move(other.state_)) {}
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            state_ = std::move(other.state_);
        }
        return *this;
    }
    
    ~Task() {
        // SharedState会自动管理生命周期
    }
    
    bool ready() const {
        return state_ && state_->handle && state_->handle.done();
    }
    
    void get() {
        if (!state_ || !state_->handle) {
            throw std::runtime_error("Task is empty");
        }
        if (!state_->handle.done()) {
            throw std::runtime_error("Task is not ready");
        }
        if (state_->handle.promise().exception) {
            std::rethrow_exception(state_->handle.promise().exception);
        }
    }
    
    // Awaitable 接口
    bool await_ready() const noexcept {
        return ready();
    }
    
    void await_suspend(std::coroutine_handle<> continuation) {
        if (state_ && state_->handle) {
            state_->handle.promise().continuation_ = continuation;
            // 与泛型版本保持一致：若未捕获到调度器，则在 await 时继承一次
            if (!state_->handle.promise().scheduler_ptr_) {
                state_->handle.promise().scheduler_ptr_ = get_current_scheduler_ptr();
            }
        }
        if (state_ && state_->handle && !state_->handle.done()) {
            auto state = state_;  // 保持 SharedState 存活
            Scheduler* scheduler_ptr = state->handle.promise().scheduler_ptr_;
            schedule_coroutine_task([state, scheduler_ptr]() {
                register_thread_on_scheduler(scheduler_ptr);
                if (state && state->handle && !state->handle.done()) {
                    state->handle.resume();
                }
            });
        } else {
            if (continuation) {
                continuation.resume();
            }
        }
    }
    
    void await_resume() {
        // 对于 void 任务，await_resume 不应该抛出异常
        // 让协程自然完成即可
        if (state_ && state_->handle && state_->handle.promise().exception) {
            std::rethrow_exception(state_->handle.promise().exception);
        }
    }
    
    handle_type handle() const { return state_ ? state_->handle : handle_type{}; }
    // Keep-alive helpers for scheduler
    std::shared_ptr<void> share_state_opaque() const { return state_; }
    void set_keep_alive(std::shared_ptr<void> keep) {
        if (state_ && state_->handle) {
            state_->handle.promise().keep_alive_ = std::move(keep);
        }
    }
    
private:
    std::shared_ptr<SharedState> state_;
};

// 通用 Awaitable 包装器
template<typename T>
class Awaitable {
public:
    explicit Awaitable(Task<T> task) : task_(std::move(task)) {}
    
    bool await_ready() const noexcept {
        return task_.ready();
    }
    
    void await_suspend(std::coroutine_handle<> h) {
        task_.await_suspend(h);
    }
    
    T await_resume() {
        return task_.get();
    }

private:
    Task<T> task_;
};

// 可取消的睡眠等待器
class CancellableSleepAwaiter : public cancellation::CancellableAwaiter<void> {
private:
    std::chrono::milliseconds duration_;
    std::thread sleep_thread_;
    std::atomic<bool> completed_{false};
    
public:
    explicit CancellableSleepAwaiter(std::chrono::milliseconds duration, 
                                   cancellation::CancellationToken token = cancellation::CancellationToken::none())
        : cancellation::CancellableAwaiter<void>(token), duration_(duration) {}
    
protected:
    void start_wait(std::coroutine_handle<> handle, cancellation::CancellationRegistration registration) override {
        sleep_thread_ = std::thread([this, handle, reg = std::move(registration)]() mutable {
            std::this_thread::sleep_for(duration_);
            if (!token_.is_cancelled()) {
                completed_.store(true);
                handle.resume();
            }
        });
    }
    
    void get_result() override {
        if (sleep_thread_.joinable()) {
            sleep_thread_.join();
        }
        
        if (!completed_.load()) {
            token_.throw_if_cancelled();
        }
    }
};

// 便利函数：创建可取消的睡眠任务
inline Task<void> sleep_cancellable(std::chrono::milliseconds duration, 
                                   cancellation::CancellationToken token = cancellation::CancellationToken::none()) {
    co_await CancellableSleepAwaiter(duration, token);
}

} // namespace modern_coro