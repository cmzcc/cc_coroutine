// Basic Task tests split from example.cpp
#include <gtest/gtest.h>
#include <future>
#include <memory>
#include <atomic>
#include <thread>
#include <chrono>
#include "scheduler/scheduler.h"

using namespace modern_coro;

static Task<int> compute_async(int x, int y) {
	co_await Scheduler::GetCurrent()->sleep(std::chrono::milliseconds(1));
	co_return x + y;
}

TEST(TaskTest, BasicReturnAndAwait) {
	Scheduler scheduler(2);
	scheduler.start();

	// 使用全局原子变量来避免栈变量问题
	static std::atomic<int> result_value{-1};
	static std::atomic<bool> completed{false};

	auto t = []() -> Task<> {
		int result = co_await compute_async(2, 3);
		result_value.store(result);
		completed.store(true);
	}();

	scheduler.schedule(std::move(t));

	// 等待完成，最多等待1秒
	auto start_time = std::chrono::steady_clock::now();
	while (!completed.load()) {
		auto elapsed = std::chrono::steady_clock::now() - start_time;
		if (elapsed > std::chrono::seconds(1)) {
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	scheduler.stop();

	ASSERT_TRUE(completed.load());
	EXPECT_EQ(result_value.load(), 5);

	// 重置静态变量
	result_value.store(-1);
	completed.store(false);
}
