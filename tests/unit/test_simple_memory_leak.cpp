#include <gtest/gtest.h>
#include "scheduler/scheduler.h"

using namespace modern_coro;

// 简单的内存泄漏检测测试
class SimpleMemoryLeakTest : public ::testing::Test
{
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(SimpleMemoryLeakTest, DirectCoroutineTest)
{
    // 直接测试协程的生命周期，不使用scheduler
    std::cerr << "Creating coroutine" << std::endl;
    auto test_coroutine = []() -> Task<>
                         {
        std::cerr << "Coroutine executing" << std::endl;
        auto ptr = std::make_unique<int>(42);
        co_await std::suspend_never{};
        co_return; }();
    
    std::cerr << "Coroutine created" << std::endl;
    // 检查协程状态
    auto handle = test_coroutine.handle();
    std::cerr << "Handle exists: " << (handle ? "yes" : "no") << std::endl;
    if (handle) {
        std::cerr << "Handle done: " << handle.done() << std::endl;
    }
    std::cerr << "Test ending" << std::endl;
    
    // 协程应该在这里被销毁
    SUCCEED();
}