#include "../../include/safety/coroutine_safety.h"
#include <iostream>

namespace modern_coro
{

    // 协程生命周期管理辅助函数的实现
    void register_coroutine_with_lifecycle_manager(std::coroutine_handle<> handle)
    {
        safety::CoroutineLifecycleManager::register_coroutine(handle);
    }

    void unregister_coroutine_with_lifecycle_manager(std::coroutine_handle<> handle)
    {
        safety::CoroutineLifecycleManager::unregister_coroutine(handle);
    }

    // CoroutineLeakDetector 的实现
    void CoroutineLeakDetector::check_leaks()
    {
        size_t active = safety::CoroutineLifecycleManager::active_count();
        if (active > 0)
        {
            LOG_WARN("检测到 {} 个协程泄漏", active);
            safety::CoroutineLifecycleManager::cleanup_all();
        }
    }

} // namespace modern_coro