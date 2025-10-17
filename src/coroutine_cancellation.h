#pragma once

#include <coroutine>
#include <atomic>
#include <memory>
#include <functional>
#include <vector>
#include <mutex>
#include <shared_mutex>
#include <chrono>
#include <unordered_set>
#include <unordered_map>
#include <thread>
#include <condition_variable>
#include <string>

namespace modern_coro {
namespace cancellation {

// 取消状态枚举
enum class CancellationState {
    NOT_CANCELLED,
    CANCELLATION_REQUESTED,
    CANCELLED
};

// 前置声明
class CancellationToken;
class CancellationSource;

// 取消回调类型
using CancellationCallback = std::function<void()>;

// 取消注册句柄
class CancellationRegistration {
private:
    std::weak_ptr<CancellationSource> source_;
    uint64_t callback_id_;
    
public:
    CancellationRegistration(std::weak_ptr<CancellationSource> source, uint64_t id)
        : source_(source), callback_id_(id) {}
    
    ~CancellationRegistration();
    
    // 移动构造和赋值
    CancellationRegistration(CancellationRegistration&& other) noexcept
        : source_(std::move(other.source_)), callback_id_(other.callback_id_) {
        other.callback_id_ = 0;
    }
    
    CancellationRegistration& operator=(CancellationRegistration&& other) noexcept {
        if (this != &other) {
            source_ = std::move(other.source_);
            callback_id_ = other.callback_id_;
            other.callback_id_ = 0;
        }
        return *this;
    }
    
    // 禁止拷贝
    CancellationRegistration(const CancellationRegistration&) = delete;
    CancellationRegistration& operator=(const CancellationRegistration&) = delete;
    
    void unregister();
};

// 取消源 - 负责管理取消状态和回调
class CancellationSource {
private:
    mutable std::mutex mutex_;
    std::atomic<CancellationState> state_{CancellationState::NOT_CANCELLED};
    std::unordered_map<uint64_t, CancellationCallback> callbacks_;
    std::atomic<uint64_t> next_callback_id_{1};
    
public:
    CancellationSource() = default;
    ~CancellationSource() = default;
    
    // 禁止拷贝和移动
    CancellationSource(const CancellationSource&) = delete;
    CancellationSource& operator=(const CancellationSource&) = delete;
    CancellationSource(CancellationSource&&) = delete;
    CancellationSource& operator=(CancellationSource&&) = delete;
    
    // 请求取消
    void cancel() {
        std::vector<CancellationCallback> callbacks_to_call;
        
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto expected = CancellationState::NOT_CANCELLED;
            if (!state_.compare_exchange_strong(expected, CancellationState::CANCELLATION_REQUESTED)) {
                return; // 已经被取消了
            }
            
            // 收集所有回调
            callbacks_to_call.reserve(callbacks_.size());
            for (const auto& [id, callback] : callbacks_) {
                callbacks_to_call.push_back(callback);
            }
        }
        
        // 在锁外执行回调，避免死锁
        for (const auto& callback : callbacks_to_call) {
            try {
                callback();
            } catch (...) {
                // 忽略回调中的异常
            }
        }
        
        state_.store(CancellationState::CANCELLED);
    }
    
    // 检查是否已取消
    bool is_cancelled() const noexcept {
        return state_.load() != CancellationState::NOT_CANCELLED;
    }
    
    // 获取取消令牌
    CancellationToken get_token();
    
    // 注册取消回调
    CancellationRegistration register_callback(CancellationCallback callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // 如果已经取消，立即执行回调
        if (is_cancelled()) {
            try {
                callback();
            } catch (...) {
                // 忽略异常
            }
            return CancellationRegistration(std::weak_ptr<CancellationSource>(), 0);
        }
        
        uint64_t id = next_callback_id_.fetch_add(1);
        callbacks_[id] = std::move(callback);
        
        return CancellationRegistration(std::weak_ptr<CancellationSource>(shared_from_this()), id);
    }
    
    // 注销回调
    void unregister_callback(uint64_t callback_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        callbacks_.erase(callback_id);
    }
    
    // 创建共享指针
    static std::shared_ptr<CancellationSource> create() {
        return std::shared_ptr<CancellationSource>(new CancellationSource());
    }
    
private:
    // 使用 enable_shared_from_this 模式
    std::shared_ptr<CancellationSource> shared_from_this() {
        // 这里需要一个更复杂的实现来支持 shared_from_this
        // 为简化，我们使用一个静态映射
        static std::mutex sources_mutex;
        static std::unordered_map<CancellationSource*, std::weak_ptr<CancellationSource>> sources;
        
        std::lock_guard<std::mutex> lock(sources_mutex);
        auto it = sources.find(this);
        if (it != sources.end()) {
            if (auto shared = it->second.lock()) {
                return shared;
            }
        }
        
        // 如果找不到，创建一个新的（这种情况不应该发生）
        auto shared = std::shared_ptr<CancellationSource>(this, [](CancellationSource*){});
        sources[this] = shared;
        return shared;
    }
};

// 取消令牌 - 只读接口，用于检查取消状态
class CancellationToken {
private:
    std::shared_ptr<CancellationSource> source_;
    
public:
    // 默认构造函数创建一个永不取消的令牌
    CancellationToken() = default;
    
    explicit CancellationToken(std::shared_ptr<CancellationSource> source)
        : source_(std::move(source)) {}
    
    // 检查是否已取消
    bool is_cancelled() const noexcept {
        return source_ && source_->is_cancelled();
    }
    
    // 如果已取消则抛出异常
    void throw_if_cancelled() const {
        if (is_cancelled()) {
            throw std::runtime_error("Operation was cancelled");
        }
    }
    
    // 注册取消回调
    CancellationRegistration register_callback(CancellationCallback callback) const {
        if (source_) {
            return source_->register_callback(std::move(callback));
        }
        return CancellationRegistration(std::weak_ptr<CancellationSource>(), 0);
    }
    
    // 创建一个永不取消的令牌
    static CancellationToken none() {
        return CancellationToken();
    }
};

// 实现 CancellationRegistration 的析构函数
inline CancellationRegistration::~CancellationRegistration() {
    unregister();
}

inline void CancellationRegistration::unregister() {
    if (callback_id_ != 0) {
        if (auto source = source_.lock()) {
            source->unregister_callback(callback_id_);
        }
        callback_id_ = 0;
    }
}

// 实现 CancellationSource::get_token
inline CancellationToken CancellationSource::get_token() {
    return CancellationToken(shared_from_this());
}

// 可取消的等待器基类
template<typename T>
class CancellableAwaiter {
protected:
    CancellationToken token_;
    
public:
    explicit CancellableAwaiter(CancellationToken token = CancellationToken::none())
        : token_(token) {}
    
    bool await_ready() const noexcept {
        return token_.is_cancelled();
    }
    
    void await_suspend(std::coroutine_handle<> handle) {
        if (token_.is_cancelled()) {
            handle.resume();
            return;
        }
        
        // 注册取消回调
        auto registration = token_.register_callback([handle]() mutable {
            handle.resume();
        });
        
        // 这里需要子类实现具体的等待逻辑
        start_wait(handle, std::move(registration));
    }
    
    T await_resume() {
        token_.throw_if_cancelled();
        return get_result();
    }
    
protected:
    virtual void start_wait(std::coroutine_handle<> handle, CancellationRegistration registration) = 0;
    virtual T get_result() = 0;
};

// 协程生命周期管理器 - 改进版本
class AdvancedCoroutineLifecycleManager {
private:
    struct CoroutineInfo {
        std::coroutine_handle<> handle;
        std::shared_ptr<CancellationSource> cancellation_source;
        std::chrono::steady_clock::time_point created_time;
        std::string debug_name;
        
        CoroutineInfo(std::coroutine_handle<> h, std::shared_ptr<CancellationSource> source, 
                     const std::string& name = "")
            : handle(h), cancellation_source(source), 
              created_time(std::chrono::steady_clock::now()), debug_name(name) {}
    };
    
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::coroutine_handle<>, CoroutineInfo> active_coroutines_;
    std::atomic<size_t> total_created_{0};
    std::atomic<size_t> total_destroyed_{0};
    
    // 单例模式
    static AdvancedCoroutineLifecycleManager& instance() {
        static AdvancedCoroutineLifecycleManager instance;
        return instance;
    }
    
public:
    // 注册协程
    static void register_coroutine(std::coroutine_handle<> handle, 
                                 std::shared_ptr<CancellationSource> source = nullptr,
                                 const std::string& debug_name = "") {
        auto& mgr = instance();
        std::unique_lock<std::shared_mutex> lock(mgr.mutex_);
        
        if (!source) {
            source = CancellationSource::create();
        }
        
        mgr.active_coroutines_.emplace(handle, CoroutineInfo(handle, source, debug_name));
        mgr.total_created_.fetch_add(1);
    }
    
    // 注销协程
    static void unregister_coroutine(std::coroutine_handle<> handle) {
        auto& mgr = instance();
        std::unique_lock<std::shared_mutex> lock(mgr.mutex_);
        
        auto it = mgr.active_coroutines_.find(handle);
        if (it != mgr.active_coroutines_.end()) {
            mgr.active_coroutines_.erase(it);
            mgr.total_destroyed_.fetch_add(1);
        }
    }
    
    // 取消所有协程
    static void cancel_all() {
        auto& mgr = instance();
        std::vector<std::shared_ptr<CancellationSource>> sources;
        
        {
            std::shared_lock<std::shared_mutex> lock(mgr.mutex_);
            sources.reserve(mgr.active_coroutines_.size());
            for (const auto& [handle, info] : mgr.active_coroutines_) {
                sources.push_back(info.cancellation_source);
            }
        }
        
        // 在锁外执行取消操作
        for (auto& source : sources) {
            source->cancel();
        }
    }
    
    // 优雅关闭 - 等待协程完成或超时后强制取消
    static bool graceful_shutdown(std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
        auto& mgr = instance();
        auto start_time = std::chrono::steady_clock::now();
        
        // 首先请求取消所有协程
        cancel_all();
        
        // 等待协程完成
        while (true) {
            {
                std::shared_lock<std::shared_mutex> lock(mgr.mutex_);
                if (mgr.active_coroutines_.empty()) {
                    return true; // 所有协程都已完成
                }
            }
            
            auto elapsed = std::chrono::steady_clock::now() - start_time;
            if (elapsed >= timeout) {
                // 超时，强制清理
                force_cleanup_all();
                return false;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    
    // 强制清理所有协程
    static void force_cleanup_all() {
        auto& mgr = instance();
        std::vector<std::coroutine_handle<>> handles;
        
        {
            std::unique_lock<std::shared_mutex> lock(mgr.mutex_);
            handles.reserve(mgr.active_coroutines_.size());
            for (const auto& [handle, info] : mgr.active_coroutines_) {
                handles.push_back(handle);
            }
            mgr.active_coroutines_.clear();
        }
        
        // 强制销毁协程
        for (auto handle : handles) {
            if (handle && !handle.done()) {
                handle.destroy();
            }
        }
        
        mgr.total_destroyed_.store(mgr.total_created_.load());
    }
    
    // 获取统计信息
    static size_t active_count() {
        auto& mgr = instance();
        std::shared_lock<std::shared_mutex> lock(mgr.mutex_);
        return mgr.active_coroutines_.size();
    }
    
    static size_t total_created() {
        return instance().total_created_.load();
    }
    
    static size_t total_destroyed() {
        return instance().total_destroyed_.load();
    }
    
    // 获取协程的取消令牌
    static CancellationToken get_cancellation_token(std::coroutine_handle<> handle) {
        auto& mgr = instance();
        std::shared_lock<std::shared_mutex> lock(mgr.mutex_);
        
        auto it = mgr.active_coroutines_.find(handle);
        if (it != mgr.active_coroutines_.end()) {
            return it->second.cancellation_source->get_token();
        }
        
        return CancellationToken::none();
    }
    
    // 调试信息
    static std::vector<std::string> get_debug_info() {
        auto& mgr = instance();
        std::shared_lock<std::shared_mutex> lock(mgr.mutex_);
        
        std::vector<std::string> info;
        info.reserve(mgr.active_coroutines_.size());
        
        auto now = std::chrono::steady_clock::now();
        for (const auto& [handle, coro_info] : mgr.active_coroutines_) {
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - coro_info.created_time).count();
            
            std::string debug_str = "Coroutine[" + std::to_string(reinterpret_cast<uintptr_t>(handle.address())) + "]";
            if (!coro_info.debug_name.empty()) {
                debug_str += " (" + coro_info.debug_name + ")";
            }
            debug_str += " - Age: " + std::to_string(duration) + "ms";
            debug_str += " - Cancelled: " + std::string(coro_info.cancellation_source->is_cancelled() ? "Yes" : "No");
            
            info.push_back(debug_str);
        }
        
        return info;
    }
};

// 便利函数
inline std::shared_ptr<CancellationSource> create_cancellation_source() {
    return CancellationSource::create();
}

inline CancellationToken create_cancellation_token() {
    return create_cancellation_source()->get_token();
}

// 超时取消源
class TimeoutCancellationSource {
private:
    std::shared_ptr<CancellationSource> source_;
    std::thread timeout_thread_;
    
public:
    explicit TimeoutCancellationSource(std::chrono::milliseconds timeout) 
        : source_(CancellationSource::create()) {
        
        timeout_thread_ = std::thread([source = source_, timeout]() {
            std::this_thread::sleep_for(timeout);
            source->cancel();
        });
    }
    
    ~TimeoutCancellationSource() {
        source_->cancel();
        if (timeout_thread_.joinable()) {
            timeout_thread_.join();
        }
    }
    
    CancellationToken get_token() {
        return source_->get_token();
    }
    
    void cancel() {
        source_->cancel();
    }
};

} // namespace cancellation
} // namespace modern_coro