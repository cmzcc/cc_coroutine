/**
 * @file stress_test_iomanager.cpp
 * @brief IOManager 压力测试 - 模拟高并发连接场景
 *
 * 测试目标：
 * 1. 测试多 IO 线程架构的性能和稳定性
 * 2. 验证在高并发场景下的正确性
 * 3. 收集性能指标数据
 */

#include "io/io_manager.h"
#include "utils/logger.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <atomic>
#include <chrono>
#include <vector>
#include <thread>
#include <iomanip>

using namespace modern_coro;
using namespace modern_coro::logger;

// 测试配置
struct TestConfig
{
    size_t num_connections = 1000;       // 并发连接数
    size_t num_io_threads = 4;           // IO 线程数
    size_t num_worker_threads = 4;       // 工作线程数
    size_t messages_per_connection = 10; // 每个连接发送的消息数
    size_t message_size = 1024;          // 消息大小（字节）
    int port = 9999;                     // 服务器端口
    bool enable_detailed_logs = false;   // 是否启用详细日志
};

// 性能统计
struct PerformanceStats
{
    std::atomic<size_t> total_connections{0};
    std::atomic<size_t> successful_connections{0};
    std::atomic<size_t> failed_connections{0};
    std::atomic<size_t> total_messages_sent{0};
    std::atomic<size_t> total_messages_received{0};
    std::atomic<size_t> total_bytes_sent{0};
    std::atomic<size_t> total_bytes_received{0};
    std::atomic<size_t> errors{0};

    std::chrono::high_resolution_clock::time_point start_time;
    std::chrono::high_resolution_clock::time_point end_time;

    void start()
    {
        start_time = std::chrono::high_resolution_clock::now();
    }

    void stop()
    {
        end_time = std::chrono::high_resolution_clock::now();
    }

    double get_duration_seconds() const
    {
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);
        return duration.count() / 1000.0;
    }

    void print_report() const
    {
        double duration = get_duration_seconds();

        LOG_INFO("========================================");
        LOG_INFO("  Performance Test Report");
        LOG_INFO("========================================");
        LOG_INFO("Test Duration: {:.2f} seconds", duration);
        LOG_INFO("");
        LOG_INFO("Connections:");
        LOG_INFO("  Total:      {}", total_connections.load());
        LOG_INFO("  Successful: {}", successful_connections.load());
        LOG_INFO("  Failed:     {}", failed_connections.load());
        LOG_INFO("");
        LOG_INFO("Messages:");
        LOG_INFO("  Sent:       {}", total_messages_sent.load());
        LOG_INFO("  Received:   {}", total_messages_received.load());
        LOG_INFO("");
        LOG_INFO("Data Transfer:");
        LOG_INFO("  Bytes Sent:     {} ({:.2f} MB)",
                 total_bytes_sent.load(),
                 total_bytes_sent.load() / (1024.0 * 1024.0));
        LOG_INFO("  Bytes Received: {} ({:.2f} MB)",
                 total_bytes_received.load(),
                 total_bytes_received.load() / (1024.0 * 1024.0));
        LOG_INFO("");
        LOG_INFO("Throughput:");
        LOG_INFO("  Connections/sec: {:.2f}", total_connections.load() / duration);
        LOG_INFO("  Messages/sec:    {:.2f}", total_messages_sent.load() / duration);
        LOG_INFO("  MB/sec (sent):   {:.2f}",
                 (total_bytes_sent.load() / (1024.0 * 1024.0)) / duration);
        LOG_INFO("  MB/sec (recv):   {:.2f}",
                 (total_bytes_received.load() / (1024.0 * 1024.0)) / duration);
        LOG_INFO("");
        LOG_INFO("Errors: {}", errors.load());
        LOG_INFO("========================================");
    }
};

// Echo 服务器协程
Task<void> echo_server_handler(IOManager *io_mgr, int client_fd,
                               PerformanceStats &stats, bool detailed_logs)
{
    char buffer[2048];

    try
    {
        while (true)
        {
            // 异步读取
            ssize_t read_len = co_await io_mgr->async_read(client_fd, buffer, sizeof(buffer));

            if (read_len <= 0)
            {
                if (detailed_logs)
                {
                    LOG_DEBUG("Client fd {} disconnected", client_fd);
                }
                break;
            }

            stats.total_bytes_received.fetch_add(read_len, std::memory_order_relaxed);
            stats.total_messages_received.fetch_add(1, std::memory_order_relaxed);

            // 异步写回（echo）
            ssize_t written = co_await io_mgr->async_write(client_fd, buffer, read_len);

            if (written != read_len)
            {
                LOG_ERROR("Write mismatch: wrote {} but expected {}", written, read_len);
                stats.errors.fetch_add(1, std::memory_order_relaxed);
                break;
            }

            stats.total_bytes_sent.fetch_add(written, std::memory_order_relaxed);
            stats.total_messages_sent.fetch_add(1, std::memory_order_relaxed);
        }
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Exception in echo handler for fd {}: {}", client_fd, e.what());
        stats.errors.fetch_add(1, std::memory_order_relaxed);
    }

    close(client_fd);
}

// Echo 服务器协程
Task<void> run_echo_server(IOManager *io_mgr, int port,
                           std::atomic<bool> &running,
                           std::atomic<bool> &server_ready,
                           PerformanceStats &stats,
                           const TestConfig &config)
{
    // 创建监听套接字
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1)
    {
        LOG_ERROR("Failed to create socket: {}", strerror(errno));
        co_return;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(listen_fd, (sockaddr *)&addr, sizeof(addr)) != 0)
    {
        LOG_ERROR("Failed to bind socket: {}", strerror(errno));
        close(listen_fd);
        co_return;
    }

    if (listen(listen_fd, 1024) != 0)
    {
        LOG_ERROR("Failed to listen: {}", strerror(errno));
        close(listen_fd);
        co_return;
    }

    LOG_INFO("Echo server listening on port {}", port);
    running = true;
    server_ready = true;

    try
    {
        while (running.load(std::memory_order_relaxed))
        {
            int client_fd = co_await io_mgr->async_accept(listen_fd);

            if (!running.load(std::memory_order_relaxed))
            {
                if (client_fd >= 0)
                {
                    close(client_fd);
                }
                break;
            }

            if (client_fd < 0)
            {
                LOG_ERROR("Accept failed: {}", strerror(errno));
                stats.errors.fetch_add(1, std::memory_order_relaxed);
                continue;
            }

            stats.total_connections.fetch_add(1, std::memory_order_relaxed);

            // 为每个客户端启动一个处理协程
            io_mgr->schedule(echo_server_handler(io_mgr, client_fd, stats, config.enable_detailed_logs));
        }
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Exception in echo server: {}", e.what());
        stats.errors.fetch_add(1, std::memory_order_relaxed);
    }

    close(listen_fd);
    LOG_INFO("Echo server stopped");
}

// 客户端连接和测试
void client_worker(int client_id, const TestConfig &config, PerformanceStats &stats)
{
    try
    {
        // 创建客户端套接字
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd == -1)
        {
            LOG_ERROR("Client {}: Failed to create socket", client_id);
            stats.failed_connections.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        server_addr.sin_port = htons(config.port);

        // 连接服务器
        if (connect(sockfd, (sockaddr *)&server_addr, sizeof(server_addr)) != 0)
        {
            LOG_ERROR("Client {}: Failed to connect: {}", client_id, strerror(errno));
            close(sockfd);
            stats.failed_connections.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        stats.successful_connections.fetch_add(1, std::memory_order_relaxed);

        if (config.enable_detailed_logs)
        {
            LOG_DEBUG("Client {} connected successfully", client_id);
        }

        // 发送和接收消息
        std::vector<char> send_buffer(config.message_size);
        std::vector<char> recv_buffer(config.message_size);

        // 填充测试数据
        for (size_t i = 0; i < config.message_size; ++i)
        {
            send_buffer[i] = 'A' + (i % 26);
        }

        for (size_t msg = 0; msg < config.messages_per_connection; ++msg)
        {
            // 发送消息
            ssize_t sent = send(sockfd, send_buffer.data(), send_buffer.size(), 0);
            if (sent != static_cast<ssize_t>(send_buffer.size()))
            {
                LOG_ERROR("Client {}: Send failed", client_id);
                stats.errors.fetch_add(1, std::memory_order_relaxed);
                break;
            }

            // 接收回显
            ssize_t total_received = 0;
            while (total_received < static_cast<ssize_t>(recv_buffer.size()))
            {
                ssize_t received = recv(sockfd, recv_buffer.data() + total_received,
                                        recv_buffer.size() - total_received, 0);
                if (received <= 0)
                {
                    LOG_ERROR("Client {}: Receive failed", client_id);
                    stats.errors.fetch_add(1, std::memory_order_relaxed);
                    break;
                }
                total_received += received;
            }

            // 验证数据
            if (total_received == static_cast<ssize_t>(recv_buffer.size()))
            {
                bool match = std::equal(send_buffer.begin(), send_buffer.end(),
                                        recv_buffer.begin());
                if (!match)
                {
                    LOG_ERROR("Client {}: Data mismatch!", client_id);
                    stats.errors.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }

        close(sockfd);

        if (config.enable_detailed_logs)
        {
            LOG_DEBUG("Client {} completed successfully", client_id);
        }
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Client {}: Exception: {}", client_id, e.what());
        stats.errors.fetch_add(1, std::memory_order_relaxed);
        stats.failed_connections.fetch_add(1, std::memory_order_relaxed);
    }
}

// 运行压力测试
void run_stress_test(const TestConfig &config)
{
    LOG_INFO("========================================");
    LOG_INFO("  IOManager Stress Test");
    LOG_INFO("========================================");
    LOG_INFO("Configuration:");
    LOG_INFO("  Connections:        {}", config.num_connections);
    LOG_INFO("  IO Threads:         {}", config.num_io_threads);
    LOG_INFO("  Worker Threads:     {}", config.num_worker_threads);
    LOG_INFO("  Messages/Conn:      {}", config.messages_per_connection);
    LOG_INFO("  Message Size:       {} bytes", config.message_size);
    LOG_INFO("  Port:               {}", config.port);
    LOG_INFO("========================================\n");

    PerformanceStats stats;

    // 创建 IOManager
    auto io_mgr = std::make_unique<IOManager>(config.num_worker_threads, config.num_io_threads);
    io_mgr->start();

    // 启动服务器
    std::atomic<bool> server_running{false};
    std::atomic<bool> server_ready{false};

    io_mgr->schedule(run_echo_server(io_mgr.get(), config.port, server_running,
                                     server_ready, stats, config));

    // 等待服务器准备好
    while (!server_ready.load(std::memory_order_relaxed))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    LOG_INFO("Server ready, starting clients...");

    // 开始性能测试
    stats.start();

    // 启动客户端线程
    std::vector<std::thread> client_threads;
    size_t clients_per_thread = std::max(size_t(1), config.num_connections / 10);

    for (size_t i = 0; i < config.num_connections; i += clients_per_thread)
    {
        client_threads.emplace_back([i, clients_per_thread, &config, &stats]()
                                    {
            size_t end = std::min(i + clients_per_thread, config.num_connections);
            for (size_t client_id = i; client_id < end; ++client_id) {
                client_worker(client_id, config, stats);
            } });
    }

    // 等待所有客户端完成
    for (auto &thread : client_threads)
    {
        thread.join();
    }

    stats.stop();

    LOG_INFO("All clients completed, stopping server...");

    // 停止服务器
    server_running = false;

    // 给服务器一点时间清理
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // 获取 IO 统计信息
    auto io_stats = io_mgr->get_io_stats();
    LOG_INFO("\nIO Manager Statistics:");
    LOG_INFO("  Total IO Threads: {}", io_stats.total_io_threads);
    LOG_INFO("  Active FDs:       {}", io_stats.active_fds);
    LOG_INFO("  Events Processed: {}", io_stats.total_events_processed);
    LOG_INFO("  FDs per thread:");
    for (size_t i = 0; i < io_stats.per_thread_fds.size(); ++i)
    {
        LOG_INFO("    Thread {}: {} fds", i, io_stats.per_thread_fds[i]);
    }

    // 停止 IOManager
    io_mgr->stop();
    io_mgr.reset();

    // 打印性能报告
    std::cout << "\n";
    stats.print_report();
}

int main(int argc, char *argv[])
{
    // 初始化日志系统
    Logger::instance().init(
        "logs/stress_test.log",
        LogLevel::INFO,
        50 * 1024 * 1024, // 50MB
        5                 // 5 files
    );

    TestConfig config;

    // 解析命令行参数
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--connections" && i + 1 < argc)
        {
            config.num_connections = std::stoul(argv[++i]);
        }
        else if (arg == "--io-threads" && i + 1 < argc)
        {
            config.num_io_threads = std::stoul(argv[++i]);
        }
        else if (arg == "--worker-threads" && i + 1 < argc)
        {
            config.num_worker_threads = std::stoul(argv[++i]);
        }
        else if (arg == "--messages" && i + 1 < argc)
        {
            config.messages_per_connection = std::stoul(argv[++i]);
        }
        else if (arg == "--size" && i + 1 < argc)
        {
            config.message_size = std::stoul(argv[++i]);
        }
        else if (arg == "--verbose")
        {
            config.enable_detailed_logs = true;
            Logger::instance().set_level(LogLevel::DEBUG);
        }
        else if (arg == "--help")
        {
            std::cout << "Usage: " << argv[0] << " [OPTIONS]\n"
                      << "Options:\n"
                      << "  --connections N     Number of concurrent connections (default: 1000)\n"
                      << "  --io-threads N      Number of IO threads (default: 4)\n"
                      << "  --worker-threads N  Number of worker threads (default: 4)\n"
                      << "  --messages N        Messages per connection (default: 10)\n"
                      << "  --size N            Message size in bytes (default: 1024)\n"
                      << "  --verbose           Enable detailed logging\n"
                      << "  --help              Show this help message\n";
            return 0;
        }
    }

    try
    {
        run_stress_test(config);
        LOG_INFO("\nStress test completed successfully!");
        Logger::instance().flush();
        return 0;
    }
    catch (const std::exception &e)
    {
        LOG_CRITICAL("Fatal error: {}", e.what());
        Logger::instance().flush();
        return 1;
    }
}
