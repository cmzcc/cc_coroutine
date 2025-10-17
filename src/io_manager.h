#pragma once

#include "scheduler.h"
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>
#include <unordered_map>
#include <chrono>

namespace modern_coro {

class IOManager : public Scheduler {
public:
    enum Event {
        NONE = 0,
        READ = EPOLLIN,
        WRITE = EPOLLOUT
    };
    
    explicit IOManager(size_t thread_count = std::thread::hardware_concurrency());
    ~IOManager();
    
    // 异步读取
    Task<ssize_t> async_read(int fd, void* buffer, size_t size);
    
    // 异步写入
    Task<ssize_t> async_write(int fd, const void* buffer, size_t size);
    
    // 异步接受连接
    Task<int> async_accept(int sockfd);
    
    // 异步连接 - 使用系统的 sockaddr
    Task<int> async_connect(int sockfd, const struct ::sockaddr* addr, socklen_t addrlen);
    
    // 添加事件监听
    void add_event(int fd, Event event, std::coroutine_handle<> handle);
    
    // 删除事件监听
    void del_event(int fd, Event event);
    
private:
    void io_worker();
    void handle_events();
    
    struct FdContext {
        std::coroutine_handle<> read_handle;
        std::coroutine_handle<> write_handle;
    };
    
    int epfd_;
    int pipe_fd_[2]; // 用于唤醒epoll
    std::unordered_map<int, FdContext> fd_contexts_;
    std::mutex fd_mutex_;
    std::thread io_thread_;
    std::atomic<bool> io_stop_flag_{false};
};

// IO操作的awaiter - 修复 auto 参数问题
template<typename Operation>
class IOAwaiter {
public:
    IOAwaiter(IOManager* manager, Operation op) : manager_(manager), operation_(std::move(op)) {}
    
    bool await_ready() const noexcept { return false; }
    
    void await_suspend(std::coroutine_handle<> h) {
        handle_ = h;
        operation_(h);
    }
    
    auto await_resume() {
        return result_;
    }
    
    // 修复 auto 参数语法问题
    template<typename T>
    void set_result(T result) {
        result_ = result;
    }
    
private:
    IOManager* manager_;
    Operation operation_;
    std::coroutine_handle<> handle_;
    decltype(std::declval<Operation>()(std::declval<std::coroutine_handle<>>())) result_;
};

} // namespace modern_coro