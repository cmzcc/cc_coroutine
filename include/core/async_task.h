#pragma once

#include "core/coroutine.h"
#include "scheduler/scheduler.h"
#include <atomic>
#include <exception>
#include <future>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>

namespace modern_coro {

// 异步任务包装器
template<typename T>
class AsyncTask {
public:
    using ValueType = std::conditional_t<std::is_void_v<T>, std::monostate, T>;

    template<typename Func, typename... Args>
    static AsyncTask from_sync(Func&& func, Args&&... args) {
        auto state = std::make_shared<SharedState>();
        
        // 在当前线程中捕获调度器指针，避免在新线程中访问 thread_local 变量
        modern_coro::Scheduler* current_scheduler = modern_coro::get_current_scheduler_ptr();
        
        // 始终使用异步执行，避免竞态条件
        // 使用 shared_ptr 的拷贝而不是移动，确保状态指针正确传递
        std::thread([state_copy = state, current_scheduler,
                     func = std::forward<Func>(func),
                     ... args = std::forward<Args>(args)]() mutable {
            try {
                if constexpr (std::is_void_v<T>) {
                    std::invoke(std::move(func), std::move(args)...);
                    set_value(state_copy, ValueType{}, current_scheduler);
                } else {
                    auto value = std::invoke(std::move(func), std::move(args)...);
                    set_value(state_copy, ValueType(std::move(value)), current_scheduler);
                }
            } catch (...) {
                set_exception(state_copy, std::current_exception(), current_scheduler);
            }
        }).detach();
        
        AsyncTask result(std::move(state));
        return result;
    }

    template<typename AsyncFunc>
    static AsyncTask from_callback(AsyncFunc&& async_func) {
        auto state = std::make_shared<SharedState>();
        // 在当前线程中捕获调度器指针
        modern_coro::Scheduler* current_scheduler = modern_coro::get_current_scheduler_ptr();
        try {
            if constexpr (std::is_void_v<T>) {
                async_func([state, current_scheduler]() {
                    set_value(state, ValueType{}, current_scheduler);
                });
            } else {
                async_func([state, current_scheduler](auto&& value) {
                    set_value(state, ValueType(std::forward<decltype(value)>(value)), current_scheduler);
                });
            }
        } catch (...) {
            set_exception(state, std::current_exception(), current_scheduler);
        }
        return AsyncTask(std::move(state));
    }

    static AsyncTask from_future(std::future<T>&& future) {
        return from_callback([fut = std::move(future)](auto callback) mutable {
            std::thread([callback = std::move(callback), fut = std::move(fut)]() mutable {
                try {
                    if constexpr (std::is_void_v<T>) {
                        fut.get();
                        callback();
                    } else {
                        auto value = fut.get();
                        callback(std::move(value));
                    }
                } catch (...) {
                    // For now, ignore exceptions in callback version
                }
            }).detach();
        });
    }

    Task<T> to_coroutine() && {
        if constexpr (std::is_void_v<T>) {
            co_await *this;
            co_return;
        } else {
            co_return co_await *this;
        }
    }

    bool await_ready() const noexcept {
        if (!state_) {
            return true; // 如果 state 为空，直接返回 ready
        }
        bool ready = state_->completed.load(std::memory_order_acquire);
        return ready;
    }

    bool ready() const noexcept {
        if (!state_) {
            return true;
        }
        return state_->completed.load(std::memory_order_acquire);
    }

    void await_suspend(std::coroutine_handle<> continuation) {
        if (!state_) {
            // 如果 state 为空，直接恢复协程
            modern_coro::schedule_coroutine_task([continuation]() mutable {
                if (continuation) {
                    continuation.resume();
                }
            });
            return;
        }
        
        modern_coro::Scheduler* scheduler = modern_coro::get_current_scheduler_ptr();
        if (!register_continuation(state_, continuation, scheduler)) {
            // Task already completed, resume immediately
            if (scheduler) {
                scheduler->schedule([continuation]() mutable {
                    if (continuation) {
                        continuation.resume();
                    }
                });
            } else {
                // Use schedule_coroutine_task to avoid direct resume in await_suspend
                modern_coro::schedule_coroutine_task([continuation]() mutable {
                    if (continuation) {
                        continuation.resume();
                    }
                });
            }
        }
    }

    auto await_resume() {
        if (!state_) {
            if constexpr (std::is_void_v<T>) {
                return;
            } else {
                throw std::runtime_error("AsyncTask state is null");
            }
        }
        
        // 增加引用计数，确保在 await_resume 执行期间 SharedState 不会被销毁
        auto state_copy = state_;
        std::exception_ptr exception;
        ValueType result_value{};
        bool has_value = false;
        bool is_completed = false;
        
        {
            std::lock_guard<std::mutex> lock(state_copy->mutex);
            is_completed = state_copy->completed.load(std::memory_order_acquire);
            
            if (!is_completed) {
                // 对于 void 任务，不要抛出异常，让协程自然完成
                if constexpr (!std::is_void_v<T>) {
                    throw std::runtime_error("AsyncTask not completed in await_resume");
                }
            }
            
            exception = state_copy->exception;
            if constexpr (!std::is_void_v<T>) {
                if (state_copy->value.has_value()) {
                    result_value = std::move(state_copy->value.value());
                    has_value = true;
                }
            } else {
                has_value = true; // void类型总是认为有值
            }
        }

        if (exception) {
            std::rethrow_exception(exception);
        }

        if constexpr (!std::is_void_v<T>) {
            if (!has_value) {
                throw std::runtime_error("AsyncTask result not set");
            }
            return result_value;
        }
    }

private:
    struct SharedState {
        std::mutex mutex;
        std::optional<ValueType> value;
        std::exception_ptr exception;
        std::coroutine_handle<> continuation{};
        modern_coro::Scheduler* scheduler{nullptr};
        std::atomic<bool> completed{false};
        
        ~SharedState() {
            // SharedState destructor
        }
    };

    explicit AsyncTask(std::shared_ptr<SharedState> state) : state_(std::move(state)) {
        // AsyncTask constructor
    }
    
public:
    ~AsyncTask() {
        // AsyncTask destructor
    }

private:

    static void set_value(const std::shared_ptr<SharedState>& state, ValueType&& value, modern_coro::Scheduler* scheduler = nullptr) {
        std::coroutine_handle<> continuation;
        modern_coro::Scheduler* target_scheduler = nullptr;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->value = std::move(value);
            state->completed.store(true, std::memory_order_release);
            continuation = state->continuation;
            target_scheduler = state->scheduler ? state->scheduler : scheduler;
            state->continuation = nullptr;
            state->scheduler = nullptr; // 清理调度器指针，避免悬空指针
        }
        // If no scheduler is available, use schedule_coroutine_task for proper async handling
        if (!target_scheduler) {
            if (continuation) {
                modern_coro::schedule_coroutine_task([continuation]() mutable {
                    if (continuation) {
                        continuation.resume();
                    }
                });
            }
            return;
        }
        dispatch(state, continuation, target_scheduler);
    }

    static void set_exception(const std::shared_ptr<SharedState>& state, std::exception_ptr exception, modern_coro::Scheduler* scheduler = nullptr) {
        std::coroutine_handle<> continuation;
        modern_coro::Scheduler* target_scheduler = nullptr;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->exception = std::move(exception);
            state->completed.store(true, std::memory_order_release);
            continuation = state->continuation;
            target_scheduler = state->scheduler ? state->scheduler : scheduler;
            state->continuation = nullptr;
            state->scheduler = nullptr; // 清理调度器指针，避免悬空指针
        }
        // If no scheduler is available, use schedule_coroutine_task for proper async handling
        if (!target_scheduler) {
            if (continuation) {
                modern_coro::schedule_coroutine_task([continuation]() mutable {
                    if (continuation) {
                        continuation.resume();
                    }
                });
            }
            return;
        }
        dispatch(state, continuation, target_scheduler);
    }

    static void dispatch(const std::shared_ptr<SharedState>&,
                         std::coroutine_handle<> continuation,
                         modern_coro::Scheduler* scheduler) {
        if (!continuation) {
            return;
        }

        if (scheduler) {
            scheduler->schedule([continuation]() mutable {
                if (continuation) {
                    continuation.resume();
                }
            });
        } else {
            // Use schedule_coroutine_task to avoid direct resume
            modern_coro::schedule_coroutine_task([continuation]() mutable {
                if (continuation) {
                    continuation.resume();
                }
            });
        }
    }

    static bool register_continuation(const std::shared_ptr<SharedState>& state,
                                      std::coroutine_handle<> continuation,
                                      modern_coro::Scheduler* scheduler) {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->completed.load(std::memory_order_acquire)) {
            return false;
        }
        state->continuation = continuation;
        // 只在没有调度器时设置，避免覆盖已有的调度器
        if (!state->scheduler) {
            state->scheduler = scheduler;
        }
        return true;
    }

    std::shared_ptr<SharedState> state_;
};

// 辅助函数
template<typename Func, typename... Args>
auto make_async_task(Func&& func, Args&&... args) {
    using Result = std::invoke_result_t<Func, Args...>;
    return AsyncTask<Result>::from_sync(std::forward<Func>(func), std::forward<Args>(args)...);
}

template<typename T>
auto make_async_from_future(std::future<T>&& future) {
    return AsyncTask<T>::from_future(std::move(future));
}

} // namespace modern_coro