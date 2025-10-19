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
        std::cout << "[DEBUG] AsyncTask::from_sync - Created state: " << state.get() << std::endl;
        
        // 在当前线程中捕获调度器指针，避免在新线程中访问 thread_local 变量
        modern_coro::Scheduler* current_scheduler = modern_coro::get_current_scheduler_ptr();
        std::cout << "[DEBUG] AsyncTask::from_sync - Current scheduler: " << current_scheduler << std::endl;
        
        // 始终使用异步执行，避免竞态条件
        // 使用 shared_ptr 的拷贝而不是移动，确保状态指针正确传递
        std::thread([state_copy = state, current_scheduler,
                     func = std::forward<Func>(func),
                     ... args = std::forward<Args>(args)]() mutable {
            std::cout << "[DEBUG] AsyncTask::from_sync - Thread started, state: " << state_copy.get() << std::endl;
            try {
                if constexpr (std::is_void_v<T>) {
                    std::invoke(std::move(func), std::move(args)...);
                    std::cout << "[DEBUG] AsyncTask::from_sync - Void function completed" << std::endl;
                    set_value(state_copy, ValueType{}, current_scheduler);
                } else {
                    auto value = std::invoke(std::move(func), std::move(args)...);
                    std::cout << "[DEBUG] AsyncTask::from_sync - Function completed with value: " << value << std::endl;
                    set_value(state_copy, ValueType(std::move(value)), current_scheduler);
                }
            } catch (...) {
                std::cout << "[DEBUG] AsyncTask::from_sync - Exception caught" << std::endl;
                set_exception(state_copy, std::current_exception(), current_scheduler);
            }
        }).detach();
        
        AsyncTask result(std::move(state));
        std::cout << "[DEBUG] AsyncTask::from_sync - Returning AsyncTask with state: " << result.state_.get() << std::endl;
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
        auto state = std::make_shared<SharedState>();
        // 在当前线程中捕获调度器指针
        modern_coro::Scheduler* current_scheduler = modern_coro::get_current_scheduler_ptr();
        std::thread([state, current_scheduler, fut = std::move(future)]() mutable {
            try {
                if constexpr (std::is_void_v<T>) {
                    fut.get();
                    set_value(state, ValueType{}, current_scheduler);
                } else {
                    auto value = fut.get();
                    set_value(state, ValueType(std::move(value)), current_scheduler);
                }
            } catch (...) {
                set_exception(state, std::current_exception(), current_scheduler);
            }
        }).detach();
        return AsyncTask(std::move(state));
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
        std::cout << "[DEBUG] AsyncTask::await_ready - state: " << state_.get() << std::endl;
        if (!state_) {
            std::cout << "[DEBUG] AsyncTask::await_ready - state is null, returning true" << std::endl;
            return true; // 如果 state 为空，直接返回 ready
        }
        bool ready = state_->completed.load(std::memory_order_acquire);
        std::cout << "[DEBUG] AsyncTask::await_ready - completed: " << ready << std::endl;
        return ready;
    }

    void await_suspend(std::coroutine_handle<> continuation) {
        std::cout << "[DEBUG] AsyncTask::await_suspend - state: " << state_.get() << ", continuation: " << continuation.address() << std::endl;
        if (!state_) {
            std::cout << "[DEBUG] AsyncTask::await_suspend - state is null, scheduling direct resume" << std::endl;
            // 如果 state 为空，直接恢复协程
            modern_coro::schedule_coroutine_task([continuation]() mutable {
                if (continuation) {
                    std::cout << "[DEBUG] AsyncTask::await_suspend - Resuming continuation (null state)" << std::endl;
                    continuation.resume();
                }
            });
            return;
        }
        
        modern_coro::Scheduler* scheduler = modern_coro::get_current_scheduler_ptr();
        std::cout << "[DEBUG] AsyncTask::await_suspend - scheduler: " << scheduler << std::endl;
        if (!register_continuation(state_, continuation, scheduler)) {
            std::cout << "[DEBUG] AsyncTask::await_suspend - Task already completed, resuming immediately" << std::endl;
            // Task already completed, resume immediately
            if (scheduler) {
                scheduler->schedule([continuation]() mutable {
                    if (continuation) {
                        std::cout << "[DEBUG] AsyncTask::await_suspend - Resuming continuation via scheduler" << std::endl;
                        continuation.resume();
                    }
                });
            } else {
                // Use schedule_coroutine_task to avoid direct resume in await_suspend
                modern_coro::schedule_coroutine_task([continuation]() mutable {
                    if (continuation) {
                        std::cout << "[DEBUG] AsyncTask::await_suspend - Resuming continuation via schedule_coroutine_task" << std::endl;
                        continuation.resume();
                    }
                });
            }
        } else {
            std::cout << "[DEBUG] AsyncTask::await_suspend - Continuation registered successfully" << std::endl;
        }
    }

    auto await_resume() {
        std::cout << "[DEBUG] AsyncTask::await_resume - state: " << state_.get() << std::endl;
        if (!state_) {
            std::cout << "[DEBUG] AsyncTask::await_resume - state is null" << std::endl;
            if constexpr (std::is_void_v<T>) {
                return;
            } else {
                throw std::runtime_error("AsyncTask state is null");
            }
        }
        
        std::exception_ptr exception;
        std::optional<ValueType> value;
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            exception = state_->exception;
            if constexpr (!std::is_void_v<T>) {
                value = std::move(state_->value);
                // 不要 reset state_->value，因为 state_ 可能在 await_resume 之后立即被销毁
            }
        }

        if (exception) {
            std::cout << "[DEBUG] AsyncTask::await_resume - Rethrowing exception" << std::endl;
            std::rethrow_exception(exception);
        }

        if constexpr (!std::is_void_v<T>) {
            if (!value.has_value()) {
                std::cout << "[DEBUG] AsyncTask::await_resume - Result not set" << std::endl;
                throw std::runtime_error("AsyncTask result not set");
            }
            std::cout << "[DEBUG] AsyncTask::await_resume - Returning value: " << *value << std::endl;
            return *value;
        } else {
            std::cout << "[DEBUG] AsyncTask::await_resume - Void task completed" << std::endl;
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
    };

    explicit AsyncTask(std::shared_ptr<SharedState> state) : state_(std::move(state)) {
        std::cout << "[DEBUG] AsyncTask constructor - state: " << state_.get() << std::endl;
    }

    ~AsyncTask() = default;

    static void set_value(const std::shared_ptr<SharedState>& state, ValueType&& value, modern_coro::Scheduler* scheduler = nullptr) {
        std::cout << "[DEBUG] AsyncTask::set_value - state: " << state.get() << ", scheduler: " << scheduler << std::endl;
        std::coroutine_handle<> continuation;
        modern_coro::Scheduler* target_scheduler = nullptr;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->value = std::move(value);
            if constexpr (!std::is_void_v<T>) {
                std::cout << "[DEBUG] AsyncTask::set_value - Value set: " << *state->value << std::endl;
            } else {
                std::cout << "[DEBUG] AsyncTask::set_value - Void value set" << std::endl;
            }
            state->completed.store(true, std::memory_order_release);
            continuation = state->continuation;
            target_scheduler = state->scheduler ? state->scheduler : scheduler;
            state->continuation = nullptr;
            state->scheduler = nullptr; // 清理调度器指针，避免悬空指针
        }
        // If no scheduler is available, use schedule_coroutine_task for proper async handling
        if (!target_scheduler) {
            if (continuation) {
                std::cout << "[DEBUG] AsyncTask::set_value - Dispatching continuation via schedule_coroutine_task: " << continuation.address() << std::endl;
                modern_coro::schedule_coroutine_task([continuation]() mutable {
                    if (continuation) {
                        continuation.resume();
                    }
                });
            } else {
                std::cout << "[DEBUG] AsyncTask::set_value - No continuation to dispatch" << std::endl;
            }
            return;
        }
        if (continuation) {
            std::cout << "[DEBUG] AsyncTask::set_value - Dispatching continuation: " << continuation.address() << std::endl;
        } else {
            std::cout << "[DEBUG] AsyncTask::set_value - No continuation to dispatch" << std::endl;
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
        std::cout << "[DEBUG] AsyncTask::dispatch - continuation: " << continuation.address() << ", scheduler: " << scheduler << std::endl;
        if (!continuation) {
            return;
        }

        if (scheduler) {
            scheduler->schedule([continuation]() mutable {
                if (continuation) {
                    std::cout << "[DEBUG] AsyncTask::dispatch - Resuming via scheduler" << std::endl;
                    continuation.resume();
                }
            });
        } else {
            // Use schedule_coroutine_task to avoid direct resume
            modern_coro::schedule_coroutine_task([continuation]() mutable {
                if (continuation) {
                    std::cout << "[DEBUG] AsyncTask::dispatch - Resuming via schedule_coroutine_task" << std::endl;
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