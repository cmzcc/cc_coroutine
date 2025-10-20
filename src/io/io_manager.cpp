#include "../../include/io/io_manager.h"
#include "../../include/safety/coroutine_safety.h" // 添加安全模块
#include "../../include/utils/logger.h"            // 添加日志模块
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <unistd.h>
#include <future>

namespace modern_coro
{

    using namespace modern_coro::safety; // 使用安全命名空间
    using namespace modern_coro::logger; // 使用日志命名空间

    IOManager::IOManager(size_t thread_count, size_t io_thread_count) : Scheduler(thread_count)
    {
        // 如果未指定 IO 线程数，默认为 CPU 核心数的一半，最少 1 个
        if (io_thread_count == 0)
        {
            io_thread_count = std::max(size_t(1), size_t(std::thread::hardware_concurrency() / 2));
        }
        io_thread_count_ = io_thread_count;

        LOG_INFO("Initializing IOManager with {} worker threads and {} IO threads",
                   thread_count, io_thread_count_);

        try
        {
            if (!init_io_threads())
            {
                LOG_WARN("Failed to initialize IO threads, falling back to synchronous mode");
                sync_mode_ = true;
                return;
            }

            LOG_INFO("IOManager initialized successfully with {} IO threads", io_thread_count_);
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Exception in IOManager constructor: {}", e.what());
            sync_mode_ = true;
            LOG_WARN("Falling back to synchronous mode due to exception");
        }
    }

    IOManager::~IOManager()
    {
        LOG_INFO("Shutting down IOManager with {} IO threads", io_thread_count_);

        // 停止所有 IO 线程
        for (auto &ctx : io_thread_contexts_)
        {
            if (ctx)
            {
                ctx->stop_flag = true;
                // 唤醒 IO 线程
                char c = 1;
                if (ctx->pipe_fd[1] >= 0)
                {
                    ssize_t ret = write(ctx->pipe_fd[1], &c, 1);
                    if (ret != 1)
                    {
                        LOG_ERROR("Failed to wake IO thread, write returned {}", ret);
                    }
                }
            }
        }

        // 等待所有 IO 线程结束
        for (auto &ctx : io_thread_contexts_)
        {
            if (ctx && ctx->thread.joinable())
            {
                ctx->thread.join();
            }
        }

        LOG_INFO("IOManager shutdown complete");
    }

    Task<ssize_t> IOManager::async_read(int fd, void *buffer, size_t size)
    {
        // 如果是同步模式，使用异步执行避免阻塞调度器
        if (sync_mode_)
        {
            LOG_DEBUG("Using synchronous read mode for fd {}", fd);
            // 在单独的线程中执行阻塞读取
            auto future = std::async(std::launch::async, [fd, buffer, size]()
                                     {
            ssize_t result = read(fd, buffer, size);
            if (result == -1) {
                LOG_ERROR("Read error in sync mode (fd={}): {}", fd, strerror(errno));
            }
            return result; });

            // 等待结果
            co_return future.get();
        }

        // 选择 IO 线程
        size_t thread_idx = select_io_thread(fd);

        // 设置为非阻塞
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);

        ssize_t result = read(fd, buffer, size);
        if (is_would_block(result))
        {
            LOG_DEBUG("Read would block for fd {}, scheduling async operation", fd);
            // 需要等待
            struct ReadAwaiter
            {
                IOManager *manager;
                size_t thread_idx;
                int fd;
                void *buffer;
                size_t size;

                bool await_ready() const noexcept { return false; }

                void await_suspend(std::coroutine_handle<> handle)
                {
                    manager->add_event(fd, IOManager::READ, handle, thread_idx);
                }

                ssize_t await_resume()
                {
                    ssize_t result = read(fd, buffer, size);
                    if (result == -1 && !is_would_block(result) && !is_interrupted(result))
                    {
                        LOG_ERROR("Read error on fd {}: {}", fd, strerror(errno));
                    }
                    return result;
                }
            };

            co_return co_await ReadAwaiter{this, thread_idx, fd, buffer, size};
        }
        co_return result;
    }

    Task<ssize_t> IOManager::async_write(int fd, const void *buffer, size_t size)
    {
        // 如果是同步模式，使用异步执行避免阻塞调度器
        if (sync_mode_)
        {
            LOG_DEBUG("Using synchronous write mode for fd {}", fd);
            // 在单独的线程中执行阻塞写入
            auto future = std::async(std::launch::async, [fd, buffer, size]()
                                     {
            ssize_t result = write(fd, buffer, size);
            if (result == -1) {
                LOG_ERROR("Write error in sync mode (fd={}): {}", fd, strerror(errno));
            }
            return result; });

            // 等待结果
            co_return future.get();
        }

        // 选择 IO 线程
        size_t thread_idx = select_io_thread(fd);

        // 设置为非阻塞
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);

        ssize_t result = write(fd, buffer, size);
        if (is_would_block(result))
        {
            LOG_DEBUG("Write would block for fd {}, scheduling async operation", fd);
            // 需要等待
            struct WriteAwaiter
            {
                IOManager *manager;
                size_t thread_idx;
                int fd;
                const void *buffer;
                size_t size;

                bool await_ready() const noexcept { return false; }

                void await_suspend(std::coroutine_handle<> handle)
                {
                    manager->add_event(fd, IOManager::WRITE, handle, thread_idx);
                }

                ssize_t await_resume()
                {
                    ssize_t result = write(fd, buffer, size);
                    if (result == -1 && !is_would_block(result) && !is_interrupted(result))
                    {
                        LOG_ERROR("Write error on fd {}: {}", fd, strerror(errno));
                    }
                    return result;
                }
            };

            co_return co_await WriteAwaiter{this, thread_idx, fd, buffer, size};
        }
        co_return result;
    }

    Task<int> IOManager::async_accept(int sockfd)
    {
        // 如果是同步模式，使用异步执行避免阻塞调度器
        if (sync_mode_)
        {
            LOG_DEBUG("Using synchronous accept mode for sockfd {}", sockfd);
            // 在单独的线程中执行阻塞accept
            auto future = std::async(std::launch::async, [sockfd]()
                                     {
            int result = accept(sockfd, nullptr, nullptr);
            if (result == -1) {
                LOG_ERROR("Accept error in sync mode (sockfd={}): {}", sockfd, strerror(errno));
            }
            return result; });

            // 等待结果
            co_return future.get();
        }

        // 选择 IO 线程
        size_t thread_idx = select_io_thread(sockfd);

        // 设置为非阻塞
        int flags = fcntl(sockfd, F_GETFL, 0);
        fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

        int result = accept(sockfd, nullptr, nullptr);
        if (is_would_block(result))
        {
            LOG_DEBUG("Accept would block for sockfd {}, scheduling async operation", sockfd);
            // 需要等待
            struct AcceptAwaiter
            {
                IOManager *manager;
                size_t thread_idx;
                int sockfd;

                bool await_ready() const noexcept { return false; }

                void await_suspend(std::coroutine_handle<> handle)
                {
                    manager->add_event(sockfd, IOManager::READ, handle, thread_idx);
                }

                int await_resume()
                {
                    int result = accept(sockfd, nullptr, nullptr);
                    if (result == -1 && !is_would_block(result) && !is_interrupted(result))
                    {
                        LOG_ERROR("Accept error on sockfd {}: {}", sockfd, strerror(errno));
                    }
                    return result;
                }
            };

            co_return co_await AcceptAwaiter{this, thread_idx, sockfd};
        }
        co_return result;
    }

    Task<int> IOManager::async_connect(int sockfd, const struct ::sockaddr *addr, socklen_t addrlen)
    {
        // 如果是同步模式，使用异步执行避免阻塞调度器
        if (sync_mode_)
        {
            LOG_DEBUG("Using synchronous connect mode for sockfd {}", sockfd);
            // 在单独的线程中执行阻塞connect
            auto future = std::async(std::launch::async, [sockfd, addr, addrlen]()
                                     {
            int result = connect(sockfd, addr, addrlen);
            if (result == -1) {
                LOG_ERROR("Connect error in sync mode (sockfd={}): {}", sockfd, strerror(errno));
            }
            return result; });

            // 等待结果
            co_return future.get();
        }

        // 选择 IO 线程
        size_t thread_idx = select_io_thread(sockfd);

        // 设置为非阻塞
        int flags = fcntl(sockfd, F_GETFL, 0);
        fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

        int result = connect(sockfd, addr, addrlen);
        if (result == -1 && errno == EINPROGRESS)
        {
            LOG_DEBUG("Connect in progress for sockfd {}, scheduling async operation", sockfd);
            // 需要等待连接完成
            struct ConnectAwaiter
            {
                IOManager *manager;
                size_t thread_idx;
                int sockfd;

                bool await_ready() const noexcept { return false; }

                void await_suspend(std::coroutine_handle<> handle)
                {
                    manager->add_event(sockfd, IOManager::WRITE, handle, thread_idx);
                }

                int await_resume()
                {
                    // 检查连接结果
                    int error = 0;
                    socklen_t len = sizeof(error);
                    if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len) == 0)
                    {
                        if (error != 0)
                        {
                            LOG_ERROR("Connect error on sockfd {}: {}", sockfd, strerror(error));
                            return -1;
                        }
                        return 0;
                    }
                    LOG_ERROR("getsockopt error on sockfd {}: {}", sockfd, strerror(errno));
                    return -1;
                }
            };

            co_return co_await ConnectAwaiter{this, thread_idx, sockfd};
        }
        co_return result;
    }

    void IOManager::add_event(int fd, Event event, std::coroutine_handle<> handle)
    {
        size_t thread_idx = select_io_thread(fd);
        add_event(fd, event, handle, thread_idx);
    }

    void IOManager::add_event(int fd, Event event, std::coroutine_handle<> handle, size_t thread_idx)
    {
        if (thread_idx >= io_thread_contexts_.size() || !io_thread_contexts_[thread_idx])
        {
            LOG_ERROR("Invalid IO thread index {} for fd {}", thread_idx, fd);
            return;
        }

        auto &ctx = *io_thread_contexts_[thread_idx];
        std::lock_guard<std::mutex> lock(ctx.fd_mutex);

        // 检查是否已经在epoll中
        auto it = ctx.fd_contexts.find(fd);
        bool is_new = (it == ctx.fd_contexts.end());

        auto &fd_ctx = ctx.fd_contexts[fd]; // 这会创建新的条目

        epoll_event ev;
        ev.data.fd = fd;
        ev.events = EPOLLET; // 边缘触发

        if (event & READ)
        {
            fd_ctx.read_handle = handle;
            ev.events |= EPOLLIN;
        }
        if (event & WRITE)
        {
            fd_ctx.write_handle = handle;
            ev.events |= EPOLLOUT;
        }

        // 根据是否是新fd来决定使用ADD还是MOD
        if (is_new)
        {
            if (epoll_ctl(ctx.epfd, EPOLL_CTL_ADD, fd, &ev) == -1)
            {
                LOG_ERROR("Failed to add fd {} to epoll in thread {}: {}", fd, thread_idx, strerror(errno));
            }
            else
            {
                LOG_DEBUG("Added fd {} to epoll in thread {}", fd, thread_idx);
            }
        }
        else
        {
            if (epoll_ctl(ctx.epfd, EPOLL_CTL_MOD, fd, &ev) == -1)
            {
                LOG_ERROR("Failed to modify fd {} in epoll in thread {}: {}", fd, thread_idx, strerror(errno));
            }
            else
            {
                LOG_DEBUG("Modified fd {} in epoll in thread {}", fd, thread_idx);
            }
        }

        // 更新全局映射
        {
            std::unique_lock lock(fd_to_thread_mutex_);
            fd_to_thread_[fd] = thread_idx;
        }
    }

    void IOManager::del_event(int fd, Event event)
    {
        // 查找该 fd 属于哪个线程
        size_t thread_idx;
        {
            std::shared_lock lock(fd_to_thread_mutex_);
            auto it = fd_to_thread_.find(fd);
            if (it == fd_to_thread_.end())
            {
                return;
            }
            thread_idx = it->second;
        }

        if (thread_idx >= io_thread_contexts_.size() || !io_thread_contexts_[thread_idx])
        {
            return;
        }

        auto &ctx = *io_thread_contexts_[thread_idx];
        std::lock_guard<std::mutex> lock(ctx.fd_mutex);

        auto it = ctx.fd_contexts.find(fd);
        if (it == ctx.fd_contexts.end())
        {
            return;
        }

        auto &fd_ctx = it->second;

        if (event & READ)
        {
            fd_ctx.read_handle = nullptr;
        }
        if (event & WRITE)
        {
            fd_ctx.write_handle = nullptr;
        }

        // 如果没有任何事件了，从epoll中删除
        if (!fd_ctx.read_handle && !fd_ctx.write_handle)
        {
            if (epoll_ctl(ctx.epfd, EPOLL_CTL_DEL, fd, nullptr) == -1)
            {
                LOG_ERROR("Failed to delete fd {} from epoll in thread {}: {}", fd, thread_idx, strerror(errno));
            }
            else
            {
                LOG_DEBUG("Deleted fd {} from epoll in thread {}", fd, thread_idx);
            }
            ctx.fd_contexts.erase(it);

            // 从全局映射中移除
            {
                std::unique_lock lock(fd_to_thread_mutex_);
                fd_to_thread_.erase(fd);
            }
        }
    }

    void IOManager::io_worker(size_t thread_idx)
    {
        if (thread_idx >= io_thread_contexts_.size() || !io_thread_contexts_[thread_idx])
        {
            LOG_ERROR("Invalid thread index {} in io_worker", thread_idx);
            return;
        }

        auto &ctx = *io_thread_contexts_[thread_idx];
        LOG_DEBUG("IO worker thread {} started", thread_idx);

        const int max_events = 64;
        epoll_event events[max_events];

        while (!ctx.stop_flag)
        {
            int nfds = epoll_wait(ctx.epfd, events, max_events, 1000); // 1秒超时

            if (nfds == -1)
            {
                if (is_interrupted(nfds))
                {
                    continue;
                }
                LOG_ERROR("epoll_wait failed in thread {}: {}", thread_idx, strerror(errno));
                break;
            }

            for (int i = 0; i < nfds; ++i)
            {
                int fd = events[i].data.fd;

                if (fd == ctx.pipe_fd[0])
                {
                    // 唤醒信号，清空管道
                    char buffer[256];
                    while (read(ctx.pipe_fd[0], buffer, sizeof(buffer)) > 0)
                        ;
                    continue;
                }

                std::lock_guard<std::mutex> lock(ctx.fd_mutex);
                auto it = ctx.fd_contexts.find(fd);
                if (it == ctx.fd_contexts.end())
                {
                    continue;
                }

                auto &fd_ctx = it->second;
                uint32_t revents = events[i].events;

                if ((revents & EPOLLIN) && fd_ctx.read_handle)
                {
                    auto handle = fd_ctx.read_handle;
                    fd_ctx.read_handle = nullptr;
                    ctx.events_processed++;

                    LOG_DEBUG("Resuming read coroutine for fd {} in thread {}", fd, thread_idx);
                    // 直接在IO线程中恢复协程，避免线程上下文切换
                    try
                    {
                        register_thread();
                        handle.resume();
                    }
                    catch (const std::exception &e)
                    {
                        LOG_ERROR("Exception in read coroutine resumption for fd {}: {}", fd, e.what());
                    }
                }

                if ((revents & EPOLLOUT) && fd_ctx.write_handle)
                {
                    auto handle = fd_ctx.write_handle;
                    fd_ctx.write_handle = nullptr;
                    ctx.events_processed++;

                    LOG_DEBUG("Resuming write coroutine for fd {} in thread {}", fd, thread_idx);
                    // 直接在IO线程中恢复协程，避免线程上下文切换
                    try
                    {
                        register_thread();
                        handle.resume();
                    }
                    catch (const std::exception &e)
                    {
                        LOG_ERROR("Exception in write coroutine resumption for fd {}: {}", fd, e.what());
                    }
                }
            }
        }

        LOG_DEBUG("IO worker thread {} stopped", thread_idx);
    }

    // 选择处理该 fd 的 IO 线程（简单的 round-robin）
    size_t IOManager::select_io_thread([[maybe_unused]] int fd)
    {
        return next_io_thread_.fetch_add(1, std::memory_order_relaxed) % io_thread_count_;
    }

    // 初始化 IO 线程
    bool IOManager::init_io_threads()
    {
        try
        {
            io_thread_contexts_.reserve(io_thread_count_);

            for (size_t i = 0; i < io_thread_count_; ++i)
            {
                auto ctx = std::make_unique<IOThreadContext>();

                // 创建 epoll
                ctx->epfd = epoll_create1(EPOLL_CLOEXEC);
                if (ctx->epfd == -1)
                {
                    LOG_ERROR("Failed to create epoll for IO thread {}: {}", i, strerror(errno));
                    return false;
                }

                // 创建管道
                if (pipe(ctx->pipe_fd) == -1)
                {
                    LOG_ERROR("Failed to create pipe for IO thread {}: {}", i, strerror(errno));
                    return false;
                }

                // 设置管道为非阻塞
                int flags0 = fcntl(ctx->pipe_fd[0], F_GETFL, 0);
                fcntl(ctx->pipe_fd[0], F_SETFL, flags0 | O_NONBLOCK);

                int flags1 = fcntl(ctx->pipe_fd[1], F_GETFL, 0);
                fcntl(ctx->pipe_fd[1], F_SETFL, flags1 | O_NONBLOCK);

                // 添加管道读端到 epoll
                epoll_event ev;
                ev.events = EPOLLIN;
                ev.data.fd = ctx->pipe_fd[0];
                if (epoll_ctl(ctx->epfd, EPOLL_CTL_ADD, ctx->pipe_fd[0], &ev) == -1)
                {
                    LOG_ERROR("Failed to add pipe to epoll for IO thread {}: {}", i, strerror(errno));
                    return false;
                }

                // 启动 IO 线程
                ctx->thread = std::thread(&IOManager::io_worker, this, i);

                io_thread_contexts_.push_back(std::move(ctx));
                LOG_DEBUG("IO thread {} initialized successfully", i);
            }

            return true;
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Exception during IO thread initialization: {}", e.what());
            return false;
        }
    }

    // 获取 IO 统计信息
    IOManager::IOStats IOManager::get_io_stats() const
    {
        IOStats stats;
        stats.total_io_threads = io_thread_count_;
        stats.per_thread_fds.resize(io_thread_count_);

        for (size_t i = 0; i < io_thread_contexts_.size(); ++i)
        {
            if (io_thread_contexts_[i])
            {
                std::lock_guard<std::mutex> lock(io_thread_contexts_[i]->fd_mutex);
                stats.per_thread_fds[i] = io_thread_contexts_[i]->fd_contexts.size();
                stats.active_fds += stats.per_thread_fds[i];
                stats.total_events_processed += io_thread_contexts_[i]->events_processed.load();
            }
        }

        return stats;
    }

} // namespace modern_coro