#include <gtest/gtest.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <thread>
#include <chrono>
#include <atomic>
#include <future>
#include "io/io_manager.h"
#include "scheduler/scheduler.h"

using namespace modern_coro;

class EchoServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        io_manager_ = std::make_unique<IOManager>(2);
        server_running_ = false;
        test_completed_ = false;
    }

    void TearDown() override {
        server_running_ = false;
        test_completed_ = true;
        if (server_thread_.joinable()) {
            io_manager_->stop();
            server_thread_.join();
        }
        io_manager_.reset();
    }

    void start_scheduler() {
        server_thread_ = std::thread([this]() {
            io_manager_->start();
        });
    }

    std::unique_ptr<IOManager> io_manager_;
    std::thread server_thread_;
    std::atomic<bool> server_running_;
    std::atomic<bool> test_completed_;
};

// 简单的 echo 协程
Task<void> echo_handler(IOManager* io_mgr, int client_fd) {
    char buffer[1024];
    
    try {
        while (true) {
            // 异步读取
            ssize_t read_len = co_await io_mgr->async_read(client_fd, buffer, sizeof(buffer));
            if (read_len <= 0) {
                break; // 连接关闭或错误
            }
            
            // 异步写入回显
            co_await io_mgr->async_write(client_fd, buffer, read_len);
        }
    } catch (const std::exception& e) {
        // 处理异常
    }
    
    close(client_fd);
}

// 服务器协程
Task<void> run_echo_server(IOManager* io_mgr, int port, std::atomic<bool>& running) {
    // 创建监听套接字
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1) {
        throw std::runtime_error("Failed to create socket");
    }
    
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(listen_fd, (sockaddr*)&addr, sizeof(addr)) != 0) {
        close(listen_fd);
        throw std::runtime_error("Failed to bind socket");
    }
    
    if (listen(listen_fd, 10) != 0) {
        close(listen_fd);
        throw std::runtime_error("Failed to listen");
    }
    
    running = true;
    
    try {
        // 只接受一个连接进行测试
        int client_fd = co_await io_mgr->async_accept(listen_fd);
        if (client_fd >= 0) {
            // 处理客户端
            co_await echo_handler(io_mgr, client_fd);
        }
    } catch (const std::exception& e) {
        // 处理异常
    }
    
    close(listen_fd);
    running = false;
}

// 客户端测试协程
Task<void> client_test(IOManager* io_mgr, int port, std::atomic<bool>& completed) {
    // 等待一下让服务器启动
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 创建客户端连接
    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd == -1) {
        completed = true;
        co_return;
    }
    
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    server_addr.sin_port = htons(port);
    
    if (connect(client_fd, (sockaddr*)&server_addr, sizeof(server_addr)) != 0) {
        close(client_fd);
        completed = true;
        co_return;
    }
    
    const char* test_message = "Hello, Async Echo Server!";
    size_t msg_len = strlen(test_message);
    
    // 发送数据
    ssize_t sent = co_await io_mgr->async_write(client_fd, test_message, msg_len);
    if (sent != static_cast<ssize_t>(msg_len)) {
        close(client_fd);
        completed = true;
        co_return;
    }
    
    // 读取回显
    char buffer[1024] = {0};
    ssize_t read_len = co_await io_mgr->async_read(client_fd, buffer, sizeof(buffer));
    if (read_len == static_cast<ssize_t>(msg_len) && strcmp(buffer, test_message) == 0) {
        // 测试成功
        completed = true;
    }
    
    close(client_fd);
}

// 测试异步 accept 功能
TEST_F(EchoServerTest, AsyncAccept) {
    const int test_port = 8888;

    // 启动调度器
    start_scheduler();

    // 等待调度器启动
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // 启动服务器协程
    auto server_task = run_echo_server(io_manager_.get(), test_port, server_running_);
    io_manager_->schedule(std::move(server_task));

    // 等待服务器启动
    for (int i = 0; i < 100 && !server_running_.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(server_running_.load()) << "Server failed to start";

    // 在主线程中运行客户端测试，避免在同步模式下的死锁
    std::thread client_thread([this, test_port]() {
        // 创建客户端连接
        int client_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (client_fd == -1) {
            return;
        }
        
        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        server_addr.sin_port = htons(test_port);
        
        if (connect(client_fd, (sockaddr*)&server_addr, sizeof(server_addr)) != 0) {
            close(client_fd);
            return;
        }
        
        const char* test_message = "Hello, Async Echo Server!";
        size_t msg_len = strlen(test_message);
        
        // 发送数据
        ssize_t sent = write(client_fd, test_message, msg_len);
        if (sent != static_cast<ssize_t>(msg_len)) {
            close(client_fd);
            return;
        }
        
        // 读取回显
        char buffer[1024] = {0};
        ssize_t read_len = read(client_fd, buffer, sizeof(buffer));
        if (read_len == static_cast<ssize_t>(msg_len) && strcmp(buffer, test_message) == 0) {
            // 测试成功
            test_completed_ = true;
        }
        
        close(client_fd);
    });
    
    client_thread.join();

    // 验证测试结果
    EXPECT_TRUE(test_completed_.load()) << "Echo test did not complete successfully";

    // 停止服务器
    server_running_ = false;
    io_manager_->stop();

    if (server_thread_.joinable()) {
        server_thread_.join();
    }
}
