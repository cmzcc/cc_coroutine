#pragma once

#include <dlfcn.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <errno.h>
#include <functional>
#include <thread>
#include <unordered_map>
#include <string>
#include <stdexcept>
#include <chrono>

namespace modern_coro {

// 前向声明
template<typename T> class Task;

// Hook管理器
class HookManager {
public:
    static HookManager& instance();
    
    void enable_hook(bool enable = true);
    bool is_hook_enabled() const;
    
    // 获取原始函数指针
    template<typename FuncType>
    FuncType get_original_function(const char* name) {
        static std::unordered_map<std::string, void*> func_cache;
        
        auto it = func_cache.find(name);
        if (it != func_cache.end()) {
            return reinterpret_cast<FuncType>(it->second);
        }
        
        void* func = dlsym(RTLD_NEXT, name);
        if (!func) {
            throw std::runtime_error(std::string("Failed to get original function: ") + name);
        }
        
        func_cache[name] = func;
        return reinterpret_cast<FuncType>(func);
    }
    
private:
    HookManager() = default;
    static thread_local bool thread_hook_enabled_;
};

// 协程睡眠函数声明
Task<void> coroutine_sleep(std::chrono::milliseconds duration);

// 函数类型定义
using sleep_func_t = unsigned int(*)(unsigned int);
using usleep_func_t = int(*)(useconds_t);
using nanosleep_func_t = int(*)(const struct timespec*, struct timespec*);
using socket_func_t = int(*)(int, int, int);
using connect_func_t = int(*)(int, const struct sockaddr*, socklen_t);
using accept_func_t = int(*)(int, struct sockaddr*, socklen_t*);
using read_func_t = ssize_t(*)(int, void*, size_t);
using readv_func_t = ssize_t(*)(int, const struct iovec*, int);
using write_func_t = ssize_t(*)(int, const void*, size_t);
using writev_func_t = ssize_t(*)(int, const struct iovec*, int);
using recv_func_t = ssize_t(*)(int, void*, size_t, int);
using recvfrom_func_t = ssize_t(*)(int, void*, size_t, int, struct sockaddr*, socklen_t*);
using recvmsg_func_t = ssize_t(*)(int, struct msghdr*, int);
using send_func_t = ssize_t(*)(int, const void*, size_t, int);
using sendto_func_t = ssize_t(*)(int, const void*, size_t, int, const struct sockaddr*, socklen_t);
using sendmsg_func_t = ssize_t(*)(int, const struct msghdr*, int);
using close_func_t = int(*)(int);
using fcntl_func_t = int(*)(int, int, ...);
using ioctl_func_t = int(*)(int, unsigned long, ...);
using getsockopt_func_t = int(*)(int, int, int, void*, socklen_t*);
using setsockopt_func_t = int(*)(int, int, int, const void*, socklen_t);

// Hook辅助类
class HookGuard {
public:
    HookGuard(bool enable = true) : prev_state_(HookManager::instance().is_hook_enabled()) {
        HookManager::instance().enable_hook(enable);
    }
    
    ~HookGuard() {
        HookManager::instance().enable_hook(prev_state_);
    }
    
private:
    bool prev_state_;
};

} // namespace modern_coro

// C接口声明
extern "C" {
    // 睡眠函数
    unsigned int sleep(unsigned int seconds);
    int usleep(useconds_t usec);
    int nanosleep(const struct timespec* req, struct timespec* rem);
    
    // 套接字函数
    int socket(int domain, int type, int protocol);
    int connect(int sockfd, const struct sockaddr* addr, socklen_t addrlen);
    int accept(int sockfd, struct sockaddr* addr, socklen_t* addrlen);
    
    // 读写函数
    ssize_t read(int fd, void* buf, size_t count);
    ssize_t readv(int fd, const struct iovec* iov, int iovcnt);
    ssize_t write(int fd, const void* buf, size_t count);
    ssize_t writev(int fd, const struct iovec* iov, int iovcnt);
    
    // 网络读写函数
    ssize_t recv(int sockfd, void* buf, size_t len, int flags);
    ssize_t recvfrom(int sockfd, void* buf, size_t len, int flags, struct sockaddr* src_addr, socklen_t* addrlen);
    ssize_t recvmsg(int sockfd, struct msghdr* msg, int flags);
    ssize_t send(int sockfd, const void* buf, size_t len, int flags);
    ssize_t sendto(int sockfd, const void* buf, size_t len, int flags, const struct sockaddr* dest_addr, socklen_t addrlen);
    ssize_t sendmsg(int sockfd, const struct msghdr* msg, int flags);
    
    // 控制函数
    int close(int fd);
    int fcntl(int fd, int cmd, ...);
    int ioctl(int fd, unsigned long request, ...);
    int getsockopt(int sockfd, int level, int optname, void* optval, socklen_t* optlen);
    int setsockopt(int sockfd, int level, int optname, const void* optval, socklen_t optlen);
}