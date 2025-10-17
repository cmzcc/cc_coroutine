#pragma once

#include "coroutine.h"
#include <future>
#include <functional>
#include <type_traits>
#include <memory>

namespace modern_coro {

// 异步任务包装器
template<typename T>
class AsyncTask {
public:
    // 从同步函数创建异步任务
    template<typename Func, typename... Args>
    static AsyncTask from_sync(Func&& func, Args&&... args) {
        return AsyncTask([func = std::forward<Func>(func), 
                         args = std::make_tuple(std::forward<Args>(args)...)]() mutable -> T {
            if constexpr (std::is_void_v<T>) {
                std::apply(func, std::move(args));
            } else {
                return std::apply(func, std::move(args));
            }
        });
    }
    
    // 从回调函数创建异步任务
    template<typename AsyncFunc>
    static AsyncTask from_callback(AsyncFunc&& async_func) {
        return AsyncTask([async_func = std::forward<AsyncFunc>(async_func)]() -> T {
            std::promise<T> promise;
            auto future = promise.get_future();
            
            if constexpr (std::is_void_v<T>) {
                async_func([&promise]() {
                    promise.set_value();
                });
            } else {
                async_func([&promise](T value) {
                    promise.set_value(std::move(value));
                });
            }
            
            return future.get();
        });
    }
    
    // 从 std::future 创建异步任务
    static AsyncTask from_future(std::future<T>&& future) {
        // 使用 shared_ptr 包装 future 以使其可复制
        auto shared_future = std::make_shared<std::future<T>>(std::move(future));
        return AsyncTask([shared_future]() -> T {
            return shared_future->get();
        });
    }
    
    // 转换为协程
    Task<T> to_coroutine() && {
        co_return co_await *this;
    }
    
    // Awaitable 接口
    bool await_ready() const noexcept {
        return result_ready_.load();
    }
    
    void await_suspend(std::coroutine_handle<> h) {
        std::thread([this, h]() {
            try {
                if constexpr (std::is_void_v<T>) {
                    task_();
                } else {
                    result_ = task_();
                }
            } catch (...) {
                exception_ = std::current_exception();
            }
            result_ready_.store(true);
            h.resume();
        }).detach();
    }
    
    T await_resume() {
        if (exception_) {
            std::rethrow_exception(exception_);
        }
        if constexpr (!std::is_void_v<T>) {
            return std::move(result_);
        }
    }

private:
    explicit AsyncTask(std::function<T()> task) : task_(std::move(task)) {}
    
    std::function<T()> task_;
    std::conditional_t<std::is_void_v<T>, std::monostate, T> result_;
    std::exception_ptr exception_;
    std::atomic<bool> result_ready_{false};
};

// void特化
template<>
class AsyncTask<void> {
public:
    template<typename Func, typename... Args>
    static AsyncTask from_sync(Func&& func, Args&&... args) {
        return AsyncTask([func = std::forward<Func>(func), 
                         args = std::make_tuple(std::forward<Args>(args)...)]() mutable {
            std::apply(func, std::move(args));
        });
    }
    
    template<typename AsyncFunc>
    static AsyncTask from_callback(AsyncFunc&& async_func) {
        return AsyncTask([async_func = std::forward<AsyncFunc>(async_func)]() {
            std::promise<void> promise;
            auto future = promise.get_future();
            
            async_func([&promise]() {
                promise.set_value();
            });
            
            future.get();
        });
    }
    
    static AsyncTask from_future(std::future<void>&& future) {
        // 使用 shared_ptr 包装 future 以使其可复制
        auto shared_future = std::make_shared<std::future<void>>(std::move(future));
        return AsyncTask([shared_future]() {
            shared_future->get();
        });
    }
    
    Task<void> to_coroutine() && {
        co_await *this;
    }
    
    bool await_ready() const noexcept {
        return result_ready_.load();
    }
    
    void await_suspend(std::coroutine_handle<> h) {
        std::thread([this, h]() {
            try {
                task_();
            } catch (...) {
                exception_ = std::current_exception();
            }
            result_ready_.store(true);
            h.resume();
        }).detach();
    }
    
    void await_resume() {
        if (exception_) {
            std::rethrow_exception(exception_);
        }
    }

private:
    explicit AsyncTask(std::function<void()> task) : task_(std::move(task)) {}
    
    std::function<void()> task_;
    std::exception_ptr exception_;
    std::atomic<bool> result_ready_{false};
};

// 辅助函数
template<typename Func, typename... Args>
auto make_async_task(Func&& func, Args&&... args) {
    return AsyncTask<std::invoke_result_t<Func, Args...>>::from_sync(std::forward<Func>(func), std::forward<Args>(args)...);
}

template<typename T>
auto make_async_from_future(std::future<T>&& future) {
    return AsyncTask<T>::from_future(std::move(future));
}

} // namespace modern_coro