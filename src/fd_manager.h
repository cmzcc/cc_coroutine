#pragma once

#include <memory>
#include <shared_mutex>
#include <vector>
#include <atomic>
#include <sys/socket.h>

namespace modern_coro {

// 文件描述符上下文
class FdContext : public std::enable_shared_from_this<FdContext> {
public:
    explicit FdContext(int fd);
    ~FdContext();

    bool init();
    bool is_init() const { return is_init_; }
    bool is_socket() const { return is_socket_; }
    bool is_closed() const { return is_closed_; }

    void set_user_nonblock(bool v) { user_nonblock_ = v; }
    bool get_user_nonblock() const { return user_nonblock_; }

    void set_sys_nonblock(bool v) { sys_nonblock_ = v; }
    bool get_sys_nonblock() const { return sys_nonblock_; }

    void set_timeout(int type, uint64_t timeout_ms);
    uint64_t get_timeout(int type) const;

    void close();

private:
    bool is_init_ = false;
    bool is_socket_ = false;
    bool sys_nonblock_ = false;
    bool user_nonblock_ = false;
    std::atomic<bool> is_closed_{false};
    int fd_;

    // 超时设置
    uint64_t recv_timeout_ = static_cast<uint64_t>(-1);
    uint64_t send_timeout_ = static_cast<uint64_t>(-1);
    
    mutable std::shared_mutex mutex_;
};

// 文件描述符管理器
class FdManager {
public:
    static FdManager& instance();

    std::shared_ptr<FdContext> get(int fd, bool auto_create = false);
    void del(int fd);

private:
    FdManager() = default;
    
    std::shared_mutex mutex_;
    std::vector<std::shared_ptr<FdContext>> fd_contexts_;
};

} // namespace modern_coro