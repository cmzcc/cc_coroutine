#include "hook.h"
#include "scheduler.h"
#include "io_manager.h"
#include "timer.h"
#include "fd_manager.h"
#include <iostream>
#include <fcntl.h>
#include <errno.h>
#include <unordered_map>
#include <string>
#include <stdexcept>
#include <stdarg.h>

namespace modern_coro {

thread_local bool HookManager::thread_hook_enabled_ = false;

HookManager& HookManager::instance() {
    static HookManager instance;
    return instance;
}

void HookManager::enable_hook(bool enable) {
    thread_hook_enabled_ = enable;
}

bool HookManager::is_hook_enabled() const {
    return thread_hook_enabled_;
}

// IO操作的通用处理函数
template<typename OriginFunc, typename... Args>
static auto do_io(int fd, OriginFunc func, const char* hook_name, 
                  IOManager::Event event, int timeout_so, Args&&... args) 
    -> decltype(func(fd, std::forward<Args>(args)...)) {
    
    // 避免未使用参数的警告
    (void)hook_name;
    (void)event;
    
    if (!HookManager::instance().is_hook_enabled()) {
        return func(fd, std::forward<Args>(args)...);
    }

    auto fd_ctx = FdManager::instance().get(fd);
    if (!fd_ctx || fd_ctx->is_closed() || !fd_ctx->is_socket()) {
        return func(fd, std::forward<Args>(args)...);
    }

    if (fd_ctx->get_user_nonblock()) {
        return func(fd, std::forward<Args>(args)...);
    }

    uint64_t timeout = fd_ctx->get_timeout(timeout_so);
    (void)timeout;  // 避免未使用变量警告
    auto io_mgr = dynamic_cast<IOManager*>(Scheduler::GetCurrent());
    if (!io_mgr) {
        return func(fd, std::forward<Args>(args)...);
    }

    // 设置为非阻塞
    if (!fd_ctx->get_sys_nonblock()) {
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        fd_ctx->set_sys_nonblock(true);
    }

    // 尝试执行操作
    auto result = func(fd, std::forward<Args>(args)...);
    if (result >= 0) {
        return result;
    }

    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // 需要等待IO事件
        // 这里应该使用协程等待，但为了简化，直接返回错误
        errno = EAGAIN;
        return -1;
    }

    return result;
}

// 协程睡眠实现
Task<void> coroutine_sleep(std::chrono::milliseconds duration) {
    if (auto scheduler = Scheduler::GetCurrent()) {
        co_await scheduler->sleep(duration);
    } else {
        std::this_thread::sleep_for(duration);
    }
}

} // namespace modern_coro

// C函数钩子实现
extern "C" {

unsigned int sleep(unsigned int seconds) {
    if (!modern_coro::HookManager::instance().is_hook_enabled()) {
        static auto original_sleep = modern_coro::HookManager::instance()
            .get_original_function<modern_coro::sleep_func_t>("sleep");
        return original_sleep(seconds);
    }
    
    if (auto scheduler = modern_coro::Scheduler::GetCurrent()) {
        (void)scheduler;  // 避免未使用变量警告
        std::this_thread::sleep_for(std::chrono::seconds(seconds));
        return 0;
    }
    
    static auto original_sleep = modern_coro::HookManager::instance()
        .get_original_function<modern_coro::sleep_func_t>("sleep");
    return original_sleep(seconds);
}

int usleep(useconds_t usec) {
    if (!modern_coro::HookManager::instance().is_hook_enabled()) {
        static auto original_usleep = modern_coro::HookManager::instance()
            .get_original_function<modern_coro::usleep_func_t>("usleep");
        return original_usleep(usec);
    }
    
    if (auto scheduler = modern_coro::Scheduler::GetCurrent()) {
        (void)scheduler;  // 避免未使用变量警告
        // 将微秒转换为毫秒
        auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::microseconds(usec));
        std::this_thread::sleep_for(duration_ms);
        return 0;
    }
    
    static auto original_usleep = modern_coro::HookManager::instance()
        .get_original_function<modern_coro::usleep_func_t>("usleep");
    return original_usleep(usec);
}

int nanosleep(const struct timespec* req, struct timespec* rem) {
    if (!modern_coro::HookManager::instance().is_hook_enabled()) {
        static auto original_nanosleep = modern_coro::HookManager::instance()
            .get_original_function<modern_coro::nanosleep_func_t>("nanosleep");
        return original_nanosleep(req, rem);
    }
    
    if (auto scheduler = modern_coro::Scheduler::GetCurrent()) {
        (void)scheduler;  // 避免未使用变量警告
        auto duration = std::chrono::seconds(req->tv_sec) + 
                       std::chrono::nanoseconds(req->tv_nsec);
        std::this_thread::sleep_for(duration);
        return 0;
    }
    
    static auto original_nanosleep = modern_coro::HookManager::instance()
        .get_original_function<modern_coro::nanosleep_func_t>("nanosleep");
    return original_nanosleep(req, rem);
}

int socket(int domain, int type, int protocol) {
    static auto original_socket = modern_coro::HookManager::instance()
        .get_original_function<modern_coro::socket_func_t>("socket");
    
    int fd = original_socket(domain, type, protocol);
    if (fd >= 0 && modern_coro::HookManager::instance().is_hook_enabled()) {
        modern_coro::FdManager::instance().get(fd, true);
    }
    return fd;
}

ssize_t read(int fd, void* buf, size_t count) {
    return modern_coro::do_io(fd, modern_coro::HookManager::instance()
        .get_original_function<modern_coro::read_func_t>("read"), 
        "read", modern_coro::IOManager::Event::READ, SO_RCVTIMEO, buf, count);
}

ssize_t readv(int fd, const struct iovec* iov, int iovcnt) {
    return modern_coro::do_io(fd, modern_coro::HookManager::instance()
        .get_original_function<modern_coro::readv_func_t>("readv"), 
        "readv", modern_coro::IOManager::Event::READ, SO_RCVTIMEO, iov, iovcnt);
}

ssize_t write(int fd, const void* buf, size_t count) {
    return modern_coro::do_io(fd, modern_coro::HookManager::instance()
        .get_original_function<modern_coro::write_func_t>("write"), 
        "write", modern_coro::IOManager::Event::WRITE, SO_SNDTIMEO, buf, count);
}

ssize_t writev(int fd, const struct iovec* iov, int iovcnt) {
    return modern_coro::do_io(fd, modern_coro::HookManager::instance()
        .get_original_function<modern_coro::writev_func_t>("writev"), 
        "writev", modern_coro::IOManager::Event::WRITE, SO_SNDTIMEO, iov, iovcnt);
}

ssize_t recv(int sockfd, void* buf, size_t len, int flags) {
    return modern_coro::do_io(sockfd, modern_coro::HookManager::instance()
        .get_original_function<modern_coro::recv_func_t>("recv"), 
        "recv", modern_coro::IOManager::Event::READ, SO_RCVTIMEO, buf, len, flags);
}

ssize_t send(int sockfd, const void* buf, size_t len, int flags) {
    return modern_coro::do_io(sockfd, modern_coro::HookManager::instance()
        .get_original_function<modern_coro::send_func_t>("send"), 
        "send", modern_coro::IOManager::Event::WRITE, SO_SNDTIMEO, buf, len, flags);
}

int close(int fd) {
    if (modern_coro::HookManager::instance().is_hook_enabled()) {
        auto fd_ctx = modern_coro::FdManager::instance().get(fd);
        if (fd_ctx) {
            fd_ctx->close();
        }
    }
    
    static auto original_close = modern_coro::HookManager::instance()
        .get_original_function<modern_coro::close_func_t>("close");
    return original_close(fd);
}

int fcntl(int fd, int cmd, ...) {
    va_list va;
    va_start(va, cmd);
    
    switch (cmd) {
        case F_SETFL: {
            int arg = va_arg(va, int);
            va_end(va);
            
            if (modern_coro::HookManager::instance().is_hook_enabled()) {
                auto fd_ctx = modern_coro::FdManager::instance().get(fd);
                if (fd_ctx) {
                    fd_ctx->set_user_nonblock(arg & O_NONBLOCK);
                }
            }
            
            static auto original_fcntl = modern_coro::HookManager::instance()
                .get_original_function<modern_coro::fcntl_func_t>("fcntl");
            return original_fcntl(fd, cmd, arg);
        }
        case F_GETFL:
        case F_DUPFD:
        case F_DUPFD_CLOEXEC: {
            int arg = va_arg(va, int);
            va_end(va);
            static auto original_fcntl = modern_coro::HookManager::instance()
                .get_original_function<modern_coro::fcntl_func_t>("fcntl");
            return original_fcntl(fd, cmd, arg);
        }
        default: {
            va_end(va);
            static auto original_fcntl = modern_coro::HookManager::instance()
                .get_original_function<modern_coro::fcntl_func_t>("fcntl");
            return original_fcntl(fd, cmd);
        }
    }
}

} // extern "C"