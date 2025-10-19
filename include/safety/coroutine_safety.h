#pragma once
#include "core/coroutine.h"
#include "safety/coroutine_safety.h"
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <atomic>
#include <mutex>
#include <unordered_set>
#include <unistd.h>
#include <thread>
#include <random>
#include <shared_mutex>
#include <unordered_map>
#include <cstring>  // 添加这个头文件用于 strerror
#include <cerrno>   // 添加这个头文件用于 errno

namespace modern_coro {
namespace safety {

// 错误代码枚举
enum class ErrorCode {
    SUCCESS = 0,
    SCHEDULER_NOT_FOUND,
    COROUTINE_CANCELLED,
    IO_ERROR,
    TIMEOUT,
    RESOURCE_EXHAUSTED
};

// 统一的系统调用错误检查宏（在命名空间外定义，方便使用）
#define CHECK_SYSCALL(call, context) \
    do { \
        if ((call) == -1) { \
            throw modern_coro::safety::IOException(modern_coro::safety::SystemError(errno, #call, context)); \
        } \
    } while(0)
    
// 协程异常基类
class CoroutineException : public std::exception {
public:
    explicit CoroutineException(const std::string& msg) : message_(msg) {}
    const char* what() const noexcept override { return message_.c_str(); }
    
private:
    std::string message_;
};

class CoroutineTimeoutException : public CoroutineException {
public:
    CoroutineTimeoutException() : CoroutineException("Coroutine timeout") {}
};

class CoroutineCancelledException : public CoroutineException {
public:
    CoroutineCancelledException() : CoroutineException("Coroutine cancelled") {}
};

// 线程安全统计（前置声明和定义）
class ThreadSafeStats {
private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::atomic<uint64_t>> counters_;
    
public:
    void increment(const std::string& name, uint64_t value = 1) {
        std::lock_guard<std::mutex> lock(mutex_);
        counters_[name].fetch_add(value);
    }
    
    uint64_t get(const std::string& name) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = counters_.find(name);
        return it != counters_.end() ? it->second.load() : 0;
    }
    
    void reset(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = counters_.find(name);
        if (it != counters_.end()) {
            it->second.store(0);
        }
    }
    
    std::unordered_map<std::string, uint64_t> get_all() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::unordered_map<std::string, uint64_t> result;
        for (const auto& [name, counter] : counters_) {
            result[name] = counter.load();
        }
        return result;
    }
};

// 全局统计实例（前置声明）
ThreadSafeStats& get_stats();

// RAII文件描述符管理
class FdGuard {
private:
    int fd_;
    bool auto_close_;
    
public:
    explicit FdGuard(int fd = -1, bool auto_close = true) 
        : fd_(fd), auto_close_(auto_close) {}
        
    ~FdGuard() {
        if (auto_close_ && fd_ >= 0) {
            close(fd_);
        }
    }
    
    FdGuard(const FdGuard&) = delete;
    FdGuard& operator=(const FdGuard&) = delete;
    
    FdGuard(FdGuard&& other) noexcept 
        : fd_(other.fd_), auto_close_(other.auto_close_) {
        other.fd_ = -1;
        other.auto_close_ = false;
    }
    
    FdGuard& operator=(FdGuard&& other) noexcept {
        if (this != &other) {
            if (auto_close_ && fd_ >= 0) {
                close(fd_);
            }
            fd_ = other.fd_;
            auto_close_ = other.auto_close_;
            other.fd_ = -1;
            other.auto_close_ = false;
        }
        return *this;
    }
    
    int get() const noexcept { return fd_; }
    int release() noexcept { 
        auto_close_ = false; 
        return fd_; 
    }
    void reset(int new_fd = -1) {
        if (auto_close_ && fd_ >= 0) {
            close(fd_);
        }
        fd_ = new_fd;
        auto_close_ = true;
    }
    
    operator int() const noexcept { return fd_; }
    bool valid() const noexcept { return fd_ >= 0; }
};

// 通用RAII资源管理器
template<typename Resource, typename Deleter>
class ResourceGuard {
private:
    Resource resource_;
    Deleter deleter_;
    bool released_;
    
public:
    explicit ResourceGuard(Resource resource, Deleter deleter = Deleter{})
        : resource_(std::move(resource)), deleter_(std::move(deleter)), released_(false) {}
        
    ~ResourceGuard() {
        if (!released_) {
            try {
                deleter_(resource_);
            } catch (...) {
                // 析构函数中不抛出异常
            }
        }
    }
    
    ResourceGuard(const ResourceGuard&) = delete;
    ResourceGuard& operator=(const ResourceGuard&) = delete;
    
    ResourceGuard(ResourceGuard&& other) noexcept
        : resource_(std::move(other.resource_)), 
          deleter_(std::move(other.deleter_)),
          released_(other.released_) {
        other.released_ = true;
    }
    
    const Resource& get() const noexcept { return resource_; }
    Resource& get() noexcept { return resource_; }
    Resource release() noexcept {
        released_ = true;
        return std::move(resource_);
    }
};

// 协程生命周期管理器
class CoroutineLifecycleManager {
private:
    inline static std::mutex mutex_;
    inline static std::unordered_set<std::coroutine_handle<>> active_coroutines_;
    
public:
    static void register_coroutine(std::coroutine_handle<> handle) {
        std::lock_guard<std::mutex> lock(mutex_);
        active_coroutines_.insert(handle);
    }
    
    static void unregister_coroutine(std::coroutine_handle<> handle) {
        std::lock_guard<std::mutex> lock(mutex_);
        active_coroutines_.erase(handle);
    }
    
    static void cleanup_all() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto handle : active_coroutines_) {
            if (handle && !handle.done()) {
                try {
                    handle.destroy();
                } catch (...) {
                    // 忽略清理过程中的异常
                }
            }
        }
        active_coroutines_.clear();
    }
    
    static size_t active_count() {
        std::lock_guard<std::mutex> lock(mutex_);
        return active_coroutines_.size();
    }
};

// 异常安全的协程包装器
template<typename T>
class SafeTask {
private:
    Task<T> task_;
    std::function<void(std::exception_ptr)> error_handler_;
    
public:
    explicit SafeTask(Task<T>&& task) : task_(std::move(task)) {}
    
    // 添加移动构造和赋值
    SafeTask(SafeTask&& other) noexcept 
        : task_(std::move(other.task_)), error_handler_(std::move(other.error_handler_)) {}
    
    SafeTask& operator=(SafeTask&& other) noexcept {
        if (this != &other) {
            task_ = std::move(other.task_);
            error_handler_ = std::move(other.error_handler_);
        }
        return *this;
    }
    
    // 禁用拷贝
    SafeTask(const SafeTask&) = delete;
    SafeTask& operator=(const SafeTask&) = delete;
    
    // 修改 on_error 返回右值引用，支持链式调用
    SafeTask&& on_error(std::function<void(std::exception_ptr)> handler) && {
        error_handler_ = std::move(handler);
        return std::move(*this);
    }
    
    SafeTask& on_error(std::function<void(std::exception_ptr)> handler) & {
        error_handler_ = std::move(handler);
        return *this;
    }
    
    // 异常安全的await
    bool await_ready() const noexcept {
        try {
            return task_.await_ready();
        } catch (...) {
            return false;
        }
    }
    
    void await_suspend(std::coroutine_handle<> h) {
        try {
            task_.await_suspend(h);
        } catch (...) {
            if (error_handler_) {
                error_handler_(std::current_exception());
            }
            h.resume();
        }
    }
    
    T await_resume() {
        try {
            return task_.await_resume();
        } catch (...) {
            if (error_handler_) {
                error_handler_(std::current_exception());
            }
            throw;
        }
    }
};

// 增强的Task包装器
template<typename T>
class EnhancedTask {
private:
    Task<T> task_;
    std::unique_ptr<FdGuard> fd_guard_;
    
public:
    explicit EnhancedTask(Task<T>&& task) : task_(std::move(task)) {}
    
    // 移动构造和赋值
    EnhancedTask(EnhancedTask&& other) noexcept 
        : task_(std::move(other.task_)), 
          fd_guard_(std::move(other.fd_guard_)) {}
    
    EnhancedTask& operator=(EnhancedTask&& other) noexcept {
        if (this != &other) {
            task_ = std::move(other.task_);
            fd_guard_ = std::move(other.fd_guard_);
        }
        return *this;
    }
    
    // 禁用拷贝
    EnhancedTask(const EnhancedTask&) = delete;
    EnhancedTask& operator=(const EnhancedTask&) = delete;
    
    // 添加文件描述符管理
    EnhancedTask& manage_fd(int fd) {
        fd_guard_ = std::make_unique<FdGuard>(fd);
        return *this;
    }
    
    // 异常安全的获取结果（使用 safety:: 前缀明确调用）
    T get_safe() {
        try {
            ThreadSafeStats& stats = safety::get_stats();
            stats.increment("task_get_attempts");
            T result = task_.get();
            stats.increment("task_get_success");
            return result;
        } catch (...) {
            ThreadSafeStats& stats = safety::get_stats();
            stats.increment("task_get_failures");
            throw;
        }
    }
    
    // 转换为SafeTask
    SafeTask<T> to_safe() && {
        return SafeTask<T>(std::move(task_));
    }
    
    // 委托给原始Task的方法
    bool ready() const { return task_.ready(); }
    bool await_ready() const noexcept { return task_.await_ready(); }
    void await_suspend(std::coroutine_handle<> h) { task_.await_suspend(h); }
    T await_resume() { return task_.await_resume(); }
};



// 全局统计实例实现
inline ThreadSafeStats& get_stats() {
    static ThreadSafeStats instance;
    return instance;
}

// 便利函数
template<typename T>
SafeTask<T> make_safe(Task<T>&& task) {
    return SafeTask<T>(std::move(task));
}

template<typename T>
EnhancedTask<T> make_enhanced(Task<T>&& task) {
    return EnhancedTask<T>(std::move(task));
}

template<typename Resource, typename Deleter>
auto make_resource_guard(Resource&& resource, Deleter&& deleter) {
    return ResourceGuard<std::decay_t<Resource>, std::decay_t<Deleter>>(
        std::forward<Resource>(resource), std::forward<Deleter>(deleter));
}

// 系统调用错误包装器
class SystemError {
public:
    int errno_code;
    std::string operation;
    std::string context;
    
    SystemError(int err, const std::string& op, const std::string& ctx = "")
        : errno_code(err), operation(op), context(ctx) {}
    
    std::string to_string() const {
        std::string msg = operation + " failed";
        if (!context.empty()) {
            msg += " (" + context + ")";
        }
        msg += ": " + std::string(std::strerror(errno_code));
        return msg;
    }
};

// 增强的IO异常类
class IOException : public CoroutineException {
public:
    SystemError sys_error;
    
    IOException(const SystemError& err) 
        : CoroutineException(err.to_string()), sys_error(err) {}
};

// 统一的非阻塞IO错误处理
inline bool is_would_block(int result) {
    return result == -1 && (errno == EAGAIN || errno == EWOULDBLOCK);
}

inline bool is_interrupted(int result) {
    return result == -1 && errno == EINTR;
}

// 系统调用安全包装器
template<typename Func, typename... Args>
auto safe_syscall(const std::string& operation, const std::string& context, Func&& func, Args&&... args) 
    -> decltype(func(args...)) {
    auto result = func(std::forward<Args>(args)...);
    if (result == -1) {
        throw IOException(SystemError(errno, operation, context));
    }
    return result;
}

} // namespace safety

// 添加协程泄漏检测
class CoroutineLeakDetector {
public:
    static void check_leaks();
};

} // namespace modern_coro

namespace modern_coro {



// 在现有ResourceGuard之前添加
template<typename Resource>
class FastResourceGuard {
private:
    Resource resource_;
    void(*deleter_)(Resource);
    bool released_;
    
public:
    explicit FastResourceGuard(Resource resource, void(*deleter)(Resource))
        : resource_(resource), deleter_(deleter), released_(false) {}
        
    ~FastResourceGuard() {
        if (!released_ && deleter_) {
            deleter_(resource_);
        }
    }
    
    FastResourceGuard(const FastResourceGuard&) = delete;
    FastResourceGuard& operator=(const FastResourceGuard&) = delete;
    
    FastResourceGuard(FastResourceGuard&& other) noexcept
        : resource_(other.resource_), deleter_(other.deleter_), released_(other.released_) {
        other.released_ = true;
    }
    
    const Resource& get() const noexcept { return resource_; }
    Resource& get() noexcept { return resource_; }
    Resource release() noexcept {
        released_ = true;
        return std::move(resource_);
    }
};

// 便利函数
template<typename Resource>
FastResourceGuard<Resource> make_fast_resource_guard(Resource resource, void(*deleter)(Resource)) {
    return FastResourceGuard<Resource>(resource, deleter);
}

} // namespace modern_coro

