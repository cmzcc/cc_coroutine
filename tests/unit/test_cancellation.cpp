#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include "safety/coroutine_cancellation.h"
#include "scheduler/scheduler.h"

using namespace modern_coro;
using namespace modern_coro::cancellation;

class CancellationTest : public ::testing::Test {
protected:
    void SetUp() override {
        scheduler_ = std::make_unique<Scheduler>();
    }

    void TearDown() override {
        scheduler_.reset();
    }

    std::unique_ptr<Scheduler> scheduler_;
};

// 测试取消令牌的基本功能
TEST_F(CancellationTest, CancellationToken) {
    auto source = std::make_shared<CancellationSource>();
    CancellationToken token = source->get_token();

    EXPECT_FALSE(token.is_cancelled());

    source->cancel();
    EXPECT_TRUE(token.is_cancelled());
}

// 测试取消注册
TEST_F(CancellationTest, CancellationRegistration) {
    auto source = std::make_shared<CancellationSource>();
    CancellationToken token = source->get_token();

    bool callback_called = false;
    auto registration = token.register_callback([&]() {
        callback_called = true;
    });

    source->cancel();
    // 回调应该被调用
    EXPECT_TRUE(callback_called);
}

// 测试取消后抛出异常
TEST_F(CancellationTest, CancellationException) {
    auto source = std::make_shared<CancellationSource>();
    CancellationToken token = source->get_token();

    source->cancel();

    EXPECT_THROW(token.throw_if_cancelled(), std::runtime_error);
}

// 测试取消源的多个令牌
TEST_F(CancellationTest, MultipleTokens) {
    auto source = std::make_shared<CancellationSource>();
    auto token1 = source->get_token();
    auto token2 = source->get_token();

    EXPECT_FALSE(token1.is_cancelled());
    EXPECT_FALSE(token2.is_cancelled());

    source->cancel();

    EXPECT_TRUE(token1.is_cancelled());
    EXPECT_TRUE(token2.is_cancelled());
}
