#include <gtest/gtest.h>
#include <stdexcept>
#include <thread>
#include <vector>
#include "safety/coroutine_safety.h"

using namespace modern_coro::safety;

// 测试协程异常
TEST(SafetyTest, CoroutineException) {
    CoroutineException ex("Test exception");
    EXPECT_STREQ(ex.what(), "Test exception");
}

// 测试超时异常
TEST(SafetyTest, CoroutineTimeoutException) {
    CoroutineTimeoutException ex;
    EXPECT_STREQ(ex.what(), "Coroutine timeout");
}

// 测试IO异常
TEST(SafetyTest, IOException) {
    SystemError sys_err(ENOENT, "open", "file.txt");
    IOException io_ex(sys_err);

    std::string expected = "open failed (file.txt): No such file or directory";
    EXPECT_EQ(std::string(io_ex.what()), expected);
}

// 测试取消异常
TEST(SafetyTest, CoroutineCancelledException) {
    CoroutineCancelledException ex;
    EXPECT_STREQ(ex.what(), "Coroutine cancelled");
}

// 测试错误代码
TEST(SafetyTest, ErrorCode) {
    EXPECT_EQ(static_cast<int>(ErrorCode::SUCCESS), 0);
    EXPECT_EQ(static_cast<int>(ErrorCode::SCHEDULER_NOT_FOUND), 1);
    EXPECT_EQ(static_cast<int>(ErrorCode::COROUTINE_CANCELLED), 2);
    EXPECT_EQ(static_cast<int>(ErrorCode::IO_ERROR), 3);
    EXPECT_EQ(static_cast<int>(ErrorCode::TIMEOUT), 4);
    EXPECT_EQ(static_cast<int>(ErrorCode::RESOURCE_EXHAUSTED), 5);
}

// 测试 ThreadSafeStats
TEST(SafetyTest, ThreadSafeStats) {
    ThreadSafeStats stats;
    
    // 测试增量操作
    stats.increment("counter1", 5);
    EXPECT_EQ(stats.get("counter1"), 5);
    
    stats.increment("counter1", 3);
    EXPECT_EQ(stats.get("counter1"), 8);
    
    // 测试默认增量（1）
    stats.increment("counter2");
    EXPECT_EQ(stats.get("counter2"), 1);
    
    // 测试不存在的计数器
    EXPECT_EQ(stats.get("nonexistent"), 0);
    
    // 测试重置
    stats.reset("counter1");
    EXPECT_EQ(stats.get("counter1"), 0);
    
    // 测试获取所有统计
    auto all_stats = stats.get_all();
    EXPECT_EQ(all_stats.size(), 2);
    EXPECT_EQ(all_stats["counter1"], 0);
    EXPECT_EQ(all_stats["counter2"], 1);
}

// 测试 FdGuard
TEST(SafetyTest, FdGuard) {
    int pipefd[2];
    ASSERT_EQ(pipe(pipefd), 0);
    
    {
        FdGuard guard(pipefd[0]);  // 默认自动关闭
        EXPECT_EQ(guard.get(), pipefd[0]);
        EXPECT_EQ(static_cast<int>(guard), pipefd[0]);
    } // guard 离开作用域，自动关闭 pipefd[0]
    
    // 验证文件描述符已关闭（尝试写入应该失败）
    char buf[1] = {'x'};
    ssize_t result = write(pipefd[0], buf, 1);
    EXPECT_EQ(result, -1);  // 应该失败，因为已关闭
    
    // 测试手动释放
    {
        FdGuard guard(pipefd[1], false);  // 不自动关闭
        guard.reset(-1);  // 手动重置
        EXPECT_EQ(guard.get(), -1);
    }
    
    close(pipefd[1]);  // 手动关闭
}

// 测试 SystemError
TEST(SafetyTest, SystemError) {
    SystemError err(ENOENT, "open", "test.txt");
    std::string msg = err.to_string();
    EXPECT_NE(msg.find("open failed"), std::string::npos);
    EXPECT_NE(msg.find("test.txt"), std::string::npos);
    EXPECT_NE(msg.find("No such file"), std::string::npos);
}
