#include <gtest/gtest.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <thread>
#include <chrono>
#include <atomic>
#include <future>
#include "io/io_manager.h"
#include "scheduler/scheduler.h"

using namespace modern_coro;

class IOManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        io_manager_ = std::make_unique<IOManager>(1);
        test_completed_ = false;
    }

    void TearDown() override {
        if (scheduler_thread_.joinable()) {
            io_manager_->stop();
            scheduler_thread_.join();
        }
        io_manager_.reset();
    }

    void start_scheduler() {
        scheduler_thread_ = std::thread([this]() {
            io_manager_->start();
        });
    }

    std::unique_ptr<IOManager> io_manager_;
    std::thread scheduler_thread_;
    std::atomic<bool> test_completed_;
};

// 异步读写测试协程
Task<void> async_read_write_test(IOManager* io_mgr, std::atomic<bool>& completed) {
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        co_return;
    }

    const char* test_data = "Async Test Data";
    size_t data_len = strlen(test_data);

    // 异步写入
    ssize_t write_len = co_await io_mgr->async_write(pipefd[1], test_data, data_len);
    EXPECT_EQ(write_len, static_cast<ssize_t>(data_len));

    // 异步读取
    char buffer[256] = {0};
    ssize_t read_len = co_await io_mgr->async_read(pipefd[0], buffer, sizeof(buffer));
    EXPECT_EQ(read_len, static_cast<ssize_t>(data_len));
    EXPECT_STREQ(buffer, test_data);

    close(pipefd[0]);
    close(pipefd[1]);
    completed = true;
}

// 测试异步读写
TEST_F(IOManagerTest, AsyncReadWrite) {
    start_scheduler();
    
    // 等待调度器启动
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // 调度异步测试协程
    io_manager_->schedule(async_read_write_test(io_manager_.get(), test_completed_));
    
    // 等待测试完成
    auto start_time = std::chrono::steady_clock::now();
    while (!test_completed_ && 
           std::chrono::steady_clock::now() - start_time < std::chrono::seconds(5)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    EXPECT_TRUE(test_completed_);
}

// 测试异步写入（同步版本，简化测试）
TEST_F(IOManagerTest, SyncWrite) {
    int pipefd[2];
    ASSERT_EQ(pipe(pipefd), 0);

    const char* test_data = "Test Write";
    size_t data_len = strlen(test_data);

    // 直接写入到管道
    ASSERT_EQ(write(pipefd[1], test_data, data_len), static_cast<ssize_t>(data_len));

    // 读取并验证
    char buffer[256] = {0};  // 初始化为0
    ssize_t read_len = read(pipefd[0], buffer, sizeof(buffer));
    ASSERT_EQ(read_len, static_cast<ssize_t>(data_len));
    ASSERT_STREQ(buffer, test_data);

    close(pipefd[0]);
    close(pipefd[1]);
}

// 测试添加和删除事件（简化测试）
TEST_F(IOManagerTest, AddDelEvent) {
    int pipefd[2];
    ASSERT_EQ(pipe(pipefd), 0);

    // 创建一个简单的协程句柄（简化）
    std::coroutine_handle<> dummy_handle = nullptr;

    // 添加读取事件
    io_manager_->add_event(pipefd[0], IOManager::READ, dummy_handle);

    // 删除事件
    io_manager_->del_event(pipefd[0], IOManager::READ);

    close(pipefd[0]);
    close(pipefd[1]);
}
