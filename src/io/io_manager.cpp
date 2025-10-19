#include "../../include/io/io_manager.h"
#include "../../include/safety/coroutine_safety.h"  // 添加安全模块
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>
#include <iostream>
#include <cstring>
#include <unistd.h>

namespace modern_coro {

using namespace modern_coro::safety;  // 使用安全命名空间

IOManager::IOManager(size_t thread_count) : Scheduler(thread_count) {
    try {
        // 使用统一的错误处理
        epfd_ = safe_syscall("epoll_create1", "IOManager initialization", epoll_create1, EPOLL_CLOEXEC);
        
        CHECK_SYSCALL(pipe(pipe_fd_), "IOManager pipe creation");
        
        // 设置管道为非阻塞
        int flags0 = safe_syscall("fcntl", "get pipe[0] flags", fcntl, pipe_fd_[0], F_GETFL, 0);
        safe_syscall("fcntl", "set pipe[0] non-blocking", fcntl, pipe_fd_[0], F_SETFL, flags0 | O_NONBLOCK);
        
        int flags1 = safe_syscall("fcntl", "get pipe[1] flags", fcntl, pipe_fd_[1], F_GETFL, 0);
        safe_syscall("fcntl", "set pipe[1] non-blocking", fcntl, pipe_fd_[1], F_SETFL, flags1 | O_NONBLOCK);
        
        // 添加管道读端到epoll
        epoll_event ev;
        ev.events = EPOLLIN;
        ev.data.fd = pipe_fd_[0];
        safe_syscall("epoll_ctl", "add pipe to epoll", epoll_ctl, epfd_, EPOLL_CTL_ADD, pipe_fd_[0], &ev);
        
        io_thread_ = std::thread(&IOManager::io_worker, this);
    } catch (const IOException& e) {
        // 清理已创建的资源
        if (epfd_ >= 0) close(epfd_);
        if (pipe_fd_[0] >= 0) close(pipe_fd_[0]);
        if (pipe_fd_[1] >= 0) close(pipe_fd_[1]);
        throw;
    }
}

IOManager::~IOManager() {
    io_stop_flag_ = true;
    
    // 唤醒IO线程
    char c = 1;
    ssize_t result = write(pipe_fd_[1], &c, 1);
    (void)result;  // 避免未使用变量警告
    
    if (io_thread_.joinable()) {
        io_thread_.join();
    }
    
    close(pipe_fd_[0]);
    close(pipe_fd_[1]);
    close(epfd_);
}

Task<ssize_t> IOManager::async_read(int fd, void* buffer, size_t size) {
    // 设置为非阻塞
    try {
        int flags = safe_syscall("fcntl", "get fd flags for read", fcntl, fd, F_GETFL, 0);
        safe_syscall("fcntl", "set fd non-blocking for read", fcntl, fd, F_SETFL, flags | O_NONBLOCK);
    } catch (const IOException& e) {
        std::cerr << "Failed to set non-blocking mode: " << e.what() << std::endl;
        co_return -1;
    }
    
    ssize_t result = read(fd, buffer, size);
    if (is_would_block(result)) {
        // 需要等待
        struct ReadAwaiter {
            IOManager* manager;
            int fd;
            void* buffer;
            size_t size;
            
            bool await_ready() const noexcept { return false; }
            
            void await_suspend(std::coroutine_handle<> handle) {
                manager->add_event(fd, IOManager::READ, handle);
            }
            
            ssize_t await_resume() {
                ssize_t result = read(fd, buffer, size);
                if (result == -1 && !is_would_block(result) && !is_interrupted(result)) {
                    // 记录真正的错误
                    std::cerr << "Read error: " << std::strerror(errno) << std::endl;
                }
                return result;
            }
        };
        
        co_return co_await ReadAwaiter{this, fd, buffer, size};
    }
    co_return result;
}

Task<ssize_t> IOManager::async_write(int fd, const void* buffer, size_t size) {
    // 设置为非阻塞
    try {
        int flags = safe_syscall("fcntl", "get fd flags for write", fcntl, fd, F_GETFL, 0);
        safe_syscall("fcntl", "set fd non-blocking for write", fcntl, fd, F_SETFL, flags | O_NONBLOCK);
    } catch (const IOException& e) {
        std::cerr << "Failed to set non-blocking mode: " << e.what() << std::endl;
        co_return -1;
    }
    
    ssize_t result = write(fd, buffer, size);
    if (is_would_block(result)) {
        // 需要等待
        struct WriteAwaiter {
            IOManager* manager;
            int fd;
            const void* buffer;
            size_t size;
            
            bool await_ready() const noexcept { return false; }
            
            void await_suspend(std::coroutine_handle<> handle) {
                manager->add_event(fd, IOManager::WRITE, handle);
            }
            
            ssize_t await_resume() {
                ssize_t result = write(fd, buffer, size);
                if (result == -1 && !is_would_block(result) && !is_interrupted(result)) {
                    // 记录真正的错误
                    std::cerr << "Write error: " << std::strerror(errno) << std::endl;
                }
                return result;
            }
        };
        
        co_return co_await WriteAwaiter{this, fd, buffer, size};
    }
    co_return result;
}

Task<int> IOManager::async_accept(int sockfd) {
    // 设置为非阻塞
    try {
        int flags = safe_syscall("fcntl", "get sockfd flags for accept", fcntl, sockfd, F_GETFL, 0);
        safe_syscall("fcntl", "set sockfd non-blocking for accept", fcntl, sockfd, F_SETFL, flags | O_NONBLOCK);
    } catch (const IOException& e) {
        std::cerr << "Failed to set non-blocking mode: " << e.what() << std::endl;
        co_return -1;
    }
    
    int result = accept(sockfd, nullptr, nullptr);
    if (is_would_block(result)) {
        // 需要等待
        struct AcceptAwaiter {
            IOManager* manager;
            int sockfd;
            
            bool await_ready() const noexcept { return false; }
            
            void await_suspend(std::coroutine_handle<> handle) {
                manager->add_event(sockfd, IOManager::READ, handle);
            }
            
            int await_resume() {
                int result = accept(sockfd, nullptr, nullptr);
                if (result == -1 && !is_would_block(result) && !is_interrupted(result)) {
                    // 记录真正的错误
                    std::cerr << "Accept error: " << std::strerror(errno) << std::endl;
                }
                return result;
            }
        };
        
        co_return co_await AcceptAwaiter{this, sockfd};
    }
    co_return result;
}

Task<int> IOManager::async_connect(int sockfd, const struct ::sockaddr* addr, socklen_t addrlen) {
    // 设置为非阻塞
    try {
        int flags = safe_syscall("fcntl", "get sockfd flags for connect", fcntl, sockfd, F_GETFL, 0);
        safe_syscall("fcntl", "set sockfd non-blocking for connect", fcntl, sockfd, F_SETFL, flags | O_NONBLOCK);
    } catch (const IOException& e) {
        std::cerr << "Failed to set non-blocking mode: " << e.what() << std::endl;
        co_return -1;
    }
    
    int result = connect(sockfd, addr, addrlen);
    if (result == -1 && errno == EINPROGRESS) {
        // 需要等待连接完成
        struct ConnectAwaiter {
            IOManager* manager;
            int sockfd;
            
            bool await_ready() const noexcept { return false; }
            
            void await_suspend(std::coroutine_handle<> handle) {
                manager->add_event(sockfd, IOManager::WRITE, handle);
            }
            
            int await_resume() {
                // 检查连接结果
                int error = 0;
                socklen_t len = sizeof(error);
                if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len) == 0) {
                    if (error != 0) {
                        std::cerr << "Connect error: " << std::strerror(error) << std::endl;
                        return -1;
                    }
                    return 0;
                }
                std::cerr << "getsockopt error: " << std::strerror(errno) << std::endl;
                return -1;
            }
        };
        
        co_return co_await ConnectAwaiter{this, sockfd};
    }
    co_return result;
}

void IOManager::add_event(int fd, Event event, std::coroutine_handle<> handle) {
    std::lock_guard<std::mutex> lock(fd_mutex_);
    
    auto& ctx = fd_contexts_[fd];
    
    epoll_event ev;
    ev.data.fd = fd;
    ev.events = EPOLLET; // 边缘触发
    
    if (event & READ) {
        ctx.read_handle = handle;
        ev.events |= EPOLLIN;
    }
    if (event & WRITE) {
        ctx.write_handle = handle;
        ev.events |= EPOLLOUT;
    }
    
    // 检查是否已经在epoll中
    try {
        if (fd_contexts_.find(fd) != fd_contexts_.end()) {
            safe_syscall("epoll_ctl", "modify fd in epoll", epoll_ctl, epfd_, EPOLL_CTL_MOD, fd, &ev);
        } else {
            safe_syscall("epoll_ctl", "add fd to epoll", epoll_ctl, epfd_, EPOLL_CTL_ADD, fd, &ev);
        }
    } catch (const IOException& e) {
        std::cerr << "Failed to manage epoll event: " << e.what() << std::endl;
    }
}

void IOManager::del_event(int fd, Event event) {
    std::lock_guard<std::mutex> lock(fd_mutex_);
    
    auto it = fd_contexts_.find(fd);
    if (it == fd_contexts_.end()) {
        return;
    }
    
    auto& ctx = it->second;
    
    if (event & READ) {
        ctx.read_handle = nullptr;
    }
    if (event & WRITE) {
        ctx.write_handle = nullptr;
    }
    
    // 如果没有任何事件了，从epoll中删除
    if (!ctx.read_handle && !ctx.write_handle) {
        try {
            safe_syscall("epoll_ctl", "delete fd from epoll", epoll_ctl, epfd_, EPOLL_CTL_DEL, fd, nullptr);
        } catch (const IOException& e) {
            std::cerr << "Failed to delete fd from epoll: " << e.what() << std::endl;
        }
        fd_contexts_.erase(it);
    }
}

void IOManager::io_worker() {
    const int max_events = 64;
    epoll_event events[max_events];
    
    while (!io_stop_flag_) {
        int nfds = epoll_wait(epfd_, events, max_events, 1000); // 1秒超时
        
        if (nfds == -1) {
            if (is_interrupted(nfds)) {
                continue;
            }
            std::cerr << "epoll_wait failed: " << std::strerror(errno) << std::endl;
            break;
        }
        
        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;
            
            if (fd == pipe_fd_[0]) {
                // 唤醒信号，清空管道
                char buffer[256];
                while (read(pipe_fd_[0], buffer, sizeof(buffer)) > 0);
                continue;
            }
            
            std::lock_guard<std::mutex> lock(fd_mutex_);
            auto it = fd_contexts_.find(fd);
            if (it == fd_contexts_.end()) {
                continue;
            }
            
            auto& ctx = it->second;
            uint32_t revents = events[i].events;
            
            if ((revents & EPOLLIN) && ctx.read_handle) {
                auto handle = ctx.read_handle;
                ctx.read_handle = nullptr;
                // 确保协程在IOManager的上下文中恢复
                schedule([this, handle]() { 
                    register_thread();
                    handle.resume(); 
                });
            }
            
            if ((revents & EPOLLOUT) && ctx.write_handle) {
                auto handle = ctx.write_handle;
                ctx.write_handle = nullptr;
                // 确保协程在IOManager的上下文中恢复
                schedule([this, handle]() { 
                    register_thread();
                    handle.resume(); 
                });
            }
        }
    }
}

} // namespace modern_coro