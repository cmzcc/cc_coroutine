#include <gtest/gtest.h>
#include "scheduler/scheduler.h"

using namespace modern_coro;

TEST(TimerTest, SleepForResumes) {
	Scheduler scheduler(1);
	scheduler.start();

	std::atomic<bool> done{false};
	auto before = std::chrono::steady_clock::now();

	auto t = [&]() -> Task<> {
		co_await scheduler.sleep(std::chrono::milliseconds(20));
		done.store(true);
	}();
	scheduler.schedule(std::move(t));

	for (int i = 0; i < 200 && !done.load(); ++i) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	auto after = std::chrono::steady_clock::now();
	scheduler.stop();

	EXPECT_TRUE(done.load());
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(after - before).count();
	EXPECT_GE(ms, 15); // 粗略校验
}
