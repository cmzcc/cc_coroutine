// Basic Task tests split from example.cpp
#include <gtest/gtest.h>
#include "scheduler/scheduler.h"

using namespace modern_coro;

static Task<int> compute_async(int x, int y) {
	co_await Scheduler::GetCurrent()->sleep(std::chrono::milliseconds(1));
	co_return x + y;
}

TEST(TaskTest, BasicReturnAndAwait) {
	Scheduler scheduler(2);
	scheduler.start();

	std::atomic<bool> done{false};
	int result = 0;

	auto t = [&]() -> Task<> {
		result = co_await compute_async(2, 3);
		done.store(true);
	}();

	scheduler.schedule(std::move(t));

	for (int i = 0; i < 200 && !done.load(); ++i) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	scheduler.stop();

	EXPECT_TRUE(done.load());
	EXPECT_EQ(result, 5);
}
