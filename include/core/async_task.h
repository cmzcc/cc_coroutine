#pragma once

#include "core/coroutine.h"
#include "scheduler/scheduler.h"
#include <atomic>
#include <exception>
#include <future>
#include <functional>
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
        std::thread([state,
                     func = std::forward<Func>(func),
                     ... args = std::forward<Args>(args)]() mutable {
            try {
                if constexpr (std::is_void_v<T>) {
                    std::invoke(std::move(func), std::move(args)...);
                    set_value(state, ValueType{});
                } else {
                    auto value = std::invoke(std::move(func), std::move(args)...);
                    set_value(state, ValueType(std::move(value)));
                }
            } catch (...) {
                set_exception(state, std::current_exception());
            }
        }).detach();
        return AsyncTask(std::move(state));
    }

    template<typename AsyncFunc>
    static AsyncTask from_callback(AsyncFunc&& async_func) {
        auto state = std::make_shared<SharedState>();
        try {
            if constexpr (std::is_void_v<T>) {
                async_func([state]() {
                    set_value(state, ValueType{});
                });
            } else {
                async_func([state](auto&& value) {
                    set_value(state, ValueType(std::forward<decltype(value)>(value)));
                });
            }
        } catch (...) {
            set_exception(state, std::current_exception());
        }
        return AsyncTask(std::move(state));
    }

    static AsyncTask from_future(std::future<T>&& future) {
        auto state = std::make_shared<SharedState>();
        std::thread([state, fut = std::move(future)]() mutable {
            try {
                if constexpr (std::is_void_v<T>) {
                    fut.get();
                    set_value(state, ValueType{});
                } else {
                    auto value = fut.get();
                    set_value(state, ValueType(std::move(value)));
                }
            } catch (...) {
                set_exception(state, std::current_exception());
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
        return state_->completed.load(std::memory_order_acquire);
    }

    void await_suspend(std::coroutine_handle<> continuation) {
        Scheduler* scheduler = get_current_scheduler_ptr();
        if (!register_continuation(state_, continuation, scheduler)) {
            if (scheduler) {
                scheduler->schedule([continuation]() mutable {
                    continuation.resume();
                });
            } else {
                continuation.resume();
            }
        }
    }

    auto await_resume() {
        std::exception_ptr exception;
        std::optional<ValueType> value;
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            exception = state_->exception;
            if constexpr (!std::is_void_v<T>) {
                value = std::move(state_->value);
                state_->value.reset();
            }
        }

        if (exception) {
            std::rethrow_exception(exception);
        }

        if constexpr (!std::is_void_v<T>) {
            if (!value.has_value()) {
                throw std::runtime_error("AsyncTask result not set");
            }
            return std::move(*value);
        }
    }

private:
    struct SharedState {
        std::mutex mutex;
        std::optional<ValueType> value;
        std::exception_ptr exception;
        std::coroutine_handle<> continuation{};
        Scheduler* scheduler{nullptr};
        std::atomic<bool> completed{false};
    };

    explicit AsyncTask(std::shared_ptr<SharedState> state) : state_(std::move(state)) {}

    static void set_value(const std::shared_ptr<SharedState>& state, ValueType&& value) {
        std::coroutine_handle<> continuation;
        Scheduler* scheduler = nullptr;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->value = std::move(value);
            state->completed.store(true, std::memory_order_release);
            continuation = state->continuation;
            scheduler = state->scheduler;
            state->continuation = nullptr;
        }
        dispatch(state, continuation, scheduler);
    }

    static void set_exception(const std::shared_ptr<SharedState>& state, std::exception_ptr exception) {
        std::coroutine_handle<> continuation;
        Scheduler* scheduler = nullptr;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->exception = std::move(exception);
            state->completed.store(true, std::memory_order_release);
            continuation = state->continuation;
            scheduler = state->scheduler;
            state->continuation = nullptr;
        }
        dispatch(state, continuation, scheduler);
    }

    static void dispatch(const std::shared_ptr<SharedState>& state,
                         std::coroutine_handle<> continuation,
                         Scheduler* scheduler) {
        if (!continuation) {
            return;
        }

        if (scheduler) {
            scheduler->schedule([state, continuation]() mutable {
                continuation.resume();
            });
        } else {
            continuation.resume();
        }
    }

    static bool register_continuation(const std::shared_ptr<SharedState>& state,
                                      std::coroutine_handle<> continuation,
                                      Scheduler* scheduler) {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->completed.load(std::memory_order_acquire)) {
            return false;
        }
        state->continuation = continuation;
        if (!state->scheduler && scheduler) {
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