#pragma once

#include "scheduler/scheduler.h"
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <atomic>

namespace modern_coro
{

    /**
     * @brief 多IO线程的 IOManager
     * 参考 Nginx 架构，每个 IO 线程独立的 epoll fd，减少锁竞争
     */
    class IOManager : public Scheduler
    {
    public:
        enum Event
        {
            NONE = 0,
            READ = EPOLLIN,
            WRITE = EPOLLOUT
        };

        /**
         * @param thread_count 工作线程数
         * @param io_thread_count IO 线程数（默认为 CPU 核心数的一半，最少1个）
         */
        explicit IOManager(
            size_t thread_count = std::thread::hardware_concurrency(),
            size_t io_thread_count = 0 // 0 表示自动设置
        );
        ~IOManager();

        // 异步读取
        Task<ssize_t> async_read(int fd, void *buffer, size_t size);

        // 异步写入
        Task<ssize_t> async_write(int fd, const void *buffer, size_t size);

        // 异步接受连接
        Task<int> async_accept(int sockfd);

        // 异步连接 - 使用系统的 sockaddr
        Task<int> async_connect(int sockfd, const struct ::sockaddr *addr, socklen_t addrlen);

        // 检查是否在同步模式
        bool is_sync_mode() const { return sync_mode_.load(); }

        // 获取 IO 统计信息
        struct IOStats
        {
            size_t total_io_threads;
            size_t active_fds;
            std::vector<size_t> per_thread_fds; // 每个 IO 线程管理的 fd 数量
            size_t total_events_processed;
        };
        IOStats get_io_stats() const;

    private:
        // IO 线程上下文
        struct IOThreadContext
        {
            int epfd;       // 该线程的 epoll fd
            int pipe_fd[2]; // 用于唤醒该线程
            std::thread thread;
            std::atomic<bool> stop_flag{false};
            std::unordered_map<int, FdContext> fd_contexts; // 该线程管理的 fd
            mutable std::mutex fd_mutex;
            std::atomic<size_t> events_processed{0}; // 处理的事件数

            ~IOThreadContext()
            {
                if (epfd >= 0)
                    close(epfd);
                if (pipe_fd[0] >= 0)
                    close(pipe_fd[0]);
                if (pipe_fd[1] >= 0)
                    close(pipe_fd[1]);
            }
        };

        struct FdContext
        {
            std::coroutine_handle<> read_handle;
            std::coroutine_handle<> write_handle;
            size_t io_thread_idx; // 记录该 fd 由哪个 IO 线程管理
        };

        // 添加事件监听（内部使用）
        void add_event(int fd, Event event, std::coroutine_handle<> handle);

        // 删除事件监听（内部使用）
        void del_event(int fd, Event event);

        // IO 线程工作函数
        void io_worker(size_t thread_idx);

        // 选择处理该 fd 的 IO 线程（简单的 round-robin）
        size_t select_io_thread(int fd);

        // 初始化 IO 线程
        bool init_io_threads();

        // 多个 IO 线程上下文
        std::vector<std::unique_ptr<IOThreadContext>> io_thread_contexts_;
        size_t io_thread_count_;
        std::atomic<size_t> next_io_thread_{0}; // round-robin 计数器
        std::atomic<bool> sync_mode_{false};    // 是否回退到同步模式

        // 全局 fd 到 IO 线程的映射（用于快速查找）
        mutable std::shared_mutex fd_to_thread_mutex_;
        std::unordered_map<int, size_t> fd_to_thread_;

        // 资源限制
        size_t max_fds_ = 65536; // 可配置
    };

    // IO操作的awaiter - 修复 auto 参数问题
    template <typename Operation>
    class IOAwaiter
    {
    public:
        IOAwaiter(IOManager *manager, Operation op) : manager_(manager), operation_(std::move(op)) {}

        bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<> h)
        {
            handle_ = h;
            operation_(h);
        }

        auto await_resume()
        {
            return result_;
        }

        // 修复 auto 参数语法问题
        template <typename T>
        void set_result(T result)
        {
            result_ = result;
        }

    private:
        IOManager *manager_;
        Operation operation_;
        std::coroutine_handle<> handle_;
        decltype(std::declval<Operation>()(std::declval<std::coroutine_handle<>>())) result_;
    };

} // namespace modern_coro