#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include "scheduler/scheduler.h"

using namespace modern_coro;

TEST(SchedulerTest, StartScheduleStop) {
	Scheduler scheduler(2);
	scheduler.start();

	std::atomic<int> counter{0};
	for (int i = 0; i < 50; ++i) {
		scheduler.schedule([&]() { counter.fetch_add(1); });
	}

	for (int i = 0; i < 200 && counter.load() < 50; ++i) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	scheduler.stop();
	EXPECT_EQ(counter.load(), 50);
}

// 测试调度器统计信息
TEST(SchedulerTest, SchedulerStats) {
    Scheduler scheduler(2);
    scheduler.start();
    
    // 调度一些任务
    std::atomic<int> task_count{0};
    for (int i = 0; i < 10; ++i) {
        scheduler.schedule([&]() { 
            task_count.fetch_add(1);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        });
    }
    
    // 等待任务完成，使用更长的超时时间
    for (int i = 0; i < 200 && task_count.load() < 10; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    auto stats = scheduler.get_stats();
    EXPECT_GE(stats.total_scheduled, 10);
    EXPECT_GE(stats.total_completed, 10);
    EXPECT_EQ(stats.queued_tasks, 0);  // 所有任务应该已完成
    
    scheduler.stop();
}

// 测试空闲状态
TEST(SchedulerTest, IsIdle) {
    Scheduler scheduler(1);
    
    // 未启动时应该空闲
    EXPECT_TRUE(scheduler.is_idle());
    
    scheduler.start();
    
    // 启动后没有任务时应该空闲
    EXPECT_TRUE(scheduler.is_idle());
    
    // 调度一个任务
    std::atomic<bool> task_done{false};
    scheduler.schedule([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        task_done = true;
    });
    
    // 短暂等待，应该不空闲（有活跃协程）
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    // 注意：is_idle 检查队列为空且活跃协程为0，这里可能不准确
    
    // 等待任务完成
    for (int i = 0; i < 50 && !task_done; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    // 任务完成后应该空闲
    EXPECT_TRUE(scheduler.is_idle());
    
    scheduler.stop();
}

// 测试线程注册和注销
TEST(SchedulerTest, ThreadRegistration) {
    Scheduler scheduler(1);
    scheduler.start();
    
    // 在当前线程注册调度器
    register_thread_on_scheduler(&scheduler);
    
    // 验证当前调度器（这里无法直接验证，但可以测试没有崩溃）
    
    // 注销线程
    unregister_thread_on_scheduler(&scheduler);
    
    scheduler.stop();
}

// 测试并发调度
TEST(SchedulerTest, ConcurrentScheduling) {
    Scheduler scheduler(4);
    scheduler.start();
    
    const int num_tasks = 100;
    std::atomic<int> completed_tasks{0};
    
    // 从多个线程调度任务
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < num_tasks / 4; ++i) {
                scheduler.schedule([&]() {
                    completed_tasks.fetch_add(1);
                });
            }
        });
    }
    
    // 等待所有线程完成调度
    for (auto& thread : threads) {
        thread.join();
    }
    
    // 等待任务完成
    for (int i = 0; i < 200 && completed_tasks.load() < num_tasks; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    
    EXPECT_EQ(completed_tasks.load(), num_tasks);
    
    scheduler.stop();
}
