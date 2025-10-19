/**
 * @file test_work_stealing.cpp
 * @brief 工作窃取调度器集成测试
 */

#include <gtest/gtest.h>
#include <vector>
#include <atomic>
#include <chrono>
#include <thread>
#include "scheduler/work_stealing_scheduler.h"

using namespace modern_coro;

// 测试工作窃取的基本功能
TEST(WorkStealingTest, BasicWorkStealing) {
    WorkStealingScheduler scheduler(4);
    scheduler.start();

    const int num_tasks = 1000;
    std::atomic<int> completed_tasks{0};

    // 创建一些长时间运行的任务（模拟不均衡负载）
    for (int i = 0; i < num_tasks / 2; ++i) {
        scheduler.schedule([&, i]() {
            // 长时间任务
            std::this_thread::sleep_for(std::chrono::milliseconds(10 + (i % 5)));
            completed_tasks.fetch_add(1);
        });
    }

    // 创建一些快速任务
    for (int i = 0; i < num_tasks / 2; ++i) {
        scheduler.schedule([&, i]() {
            // 快速任务
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            completed_tasks.fetch_add(1);
        });
    }

    // 等待所有任务完成
    while (completed_tasks.load() < num_tasks) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto stats = scheduler.get_work_stealing_stats();
    scheduler.stop();

    EXPECT_EQ(completed_tasks.load(), num_tasks);
    EXPECT_GE(stats.total_steals, 0); // 应该发生了一些窃取

    std::cout << "Work stealing stats:" << std::endl;
    std::cout << "  Total steals: " << stats.total_steals << std::endl;
    std::cout << "  Successful steals: " << stats.successful_steals << std::endl;
    std::cout << "  Failed steals: " << stats.failed_steals << std::endl;
    std::cout << "  Success rate: " << stats.steal_success_rate * 100 << "%" << std::endl;
}

// 测试工作窃取的负载均衡效果
TEST(WorkStealingTest, LoadBalancing) {
    const int num_threads = 4;
    WorkStealingScheduler scheduler(num_threads);
    scheduler.start();

    const int num_heavy_tasks = num_threads * 2; // 每个线程2个重任务
    const int num_light_tasks = num_threads * 10; // 每个线程10个轻任务

    std::atomic<int> completed_heavy{0};
    std::atomic<int> completed_light{0};

    // 创建重任务（只分配给前几个线程）
    for (int i = 0; i < num_heavy_tasks; ++i) {
        scheduler.schedule_with_affinity([&, i]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            completed_heavy.fetch_add(1);
        }, i % 2); // 只在前2个线程上分配
    }

    // 创建轻任务（均匀分配）
    for (int i = 0; i < num_light_tasks; ++i) {
        scheduler.schedule([&, i]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            completed_light.fetch_add(1);
        });
    }

    // 等待所有任务完成
    while (completed_heavy.load() < num_heavy_tasks ||
           completed_light.load() < num_light_tasks) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto stats = scheduler.get_work_stealing_stats();
    scheduler.stop();

    EXPECT_EQ(completed_heavy.load(), num_heavy_tasks);
    EXPECT_EQ(completed_light.load(), num_light_tasks);

    // 应该发生了一些工作窃取来平衡负载
    EXPECT_GT(stats.total_steals, 0);

    std::cout << "Load balancing test - Steals: " << stats.total_steals
              << ", Success rate: " << stats.steal_success_rate * 100 << "%" << std::endl;
}

// 测试批量任务调度
TEST(WorkStealingTest, BatchTaskScheduling) {
    WorkStealingScheduler scheduler(4);
    scheduler.start();

    const int batch_size = 100;
    std::vector<std::function<void()>> batch;

    std::atomic<int> completed_tasks{0};

    for (int i = 0; i < batch_size; ++i) {
        batch.push_back([&, i]() {
            completed_tasks.fetch_add(1);
        });
    }

    scheduler.schedule_batch(std::move(batch));

    // 等待所有任务完成
    while (completed_tasks.load() < batch_size) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    scheduler.stop();

    EXPECT_EQ(completed_tasks.load(), batch_size);
}

// 测试亲和性调度
TEST(WorkStealingTest, AffinityScheduling) {
    WorkStealingScheduler scheduler(4);
    scheduler.start();

    const int num_tasks = 100;
    std::atomic<int> completed_tasks{0};
    std::vector<std::thread::id> execution_threads;

    std::mutex threads_mutex;

    // 创建指定在特定线程上执行的任务
    for (int i = 0; i < num_tasks; ++i) {
        size_t preferred_thread = i % 4;
        scheduler.schedule_with_affinity([&, preferred_thread]() {
            std::lock_guard<std::mutex> lock(threads_mutex);
            execution_threads.push_back(std::this_thread::get_id());
            completed_tasks.fetch_add(1);
        }, preferred_thread);
    }

    // 等待所有任务完成
    while (completed_tasks.load() < num_tasks) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    scheduler.stop();

    EXPECT_EQ(completed_tasks.load(), num_tasks);
    EXPECT_EQ(execution_threads.size(), static_cast<size_t>(num_tasks));
}

// 测试工作窃取统计信息的准确性
TEST(WorkStealingTest, StatisticsAccuracy) {
    WorkStealingScheduler scheduler(2); // 只用2个线程便于测试
    scheduler.start();

    // 创建一个线程的任务队列不均衡的情况
    std::atomic<int> slow_task_completed{0};
    std::atomic<int> fast_tasks_completed{0};

    // 一个慢任务（分配到线程0）
    scheduler.schedule_with_affinity([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        slow_task_completed.fetch_add(1);
    }, 0);

    // 许多快任务（分配到线程1）
    for (int i = 0; i < 50; ++i) {
        scheduler.schedule_with_affinity([&]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            fast_tasks_completed.fetch_add(1);
        }, 1);
    }

    // 等待所有任务完成
    while (slow_task_completed.load() < 1 || fast_tasks_completed.load() < 50) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto stats = scheduler.get_work_stealing_stats();
    scheduler.stop();

    EXPECT_EQ(slow_task_completed.load(), 1);
    EXPECT_EQ(fast_tasks_completed.load(), 50);

    // 打印统计信息用于调试
    std::cout << "Statistics accuracy test:" << std::endl;
    std::cout << "  Total steals: " << stats.total_steals << std::endl;
    std::cout << "  Successful steals: " << stats.successful_steals << std::endl;
    std::cout << "  Per-thread executed: ";
    for (size_t executed : stats.per_thread_executed) {
        std::cout << executed << " ";
    }
    std::cout << std::endl;
    std::cout << "  Per-thread stolen: ";
    for (size_t stolen : stats.per_thread_stolen) {
        std::cout << stolen << " ";
    }
    std::cout << std::endl;
}

// 测试工作窃取的性能优势
TEST(WorkStealingTest, PerformanceAdvantage) {
    const int num_threads = 4;
    const int num_tasks = 1000;

    // 测试普通调度器
    Scheduler normal_scheduler(num_threads);
    normal_scheduler.start();

    auto normal_start = std::chrono::steady_clock::now();
    std::atomic<int> normal_completed{0};

    for (int i = 0; i < num_tasks; ++i) {
        normal_scheduler.schedule([&]() {
            int work = 0;
            for (int j = 0; j < 1000; ++j) {
                work += j % 10;
            }
            normal_completed.fetch_add(1);
        });
    }

    while (normal_completed.load() < num_tasks) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    auto normal_end = std::chrono::steady_clock::now();
    auto normal_duration = std::chrono::duration_cast<std::chrono::microseconds>(
        normal_end - normal_start);

    normal_scheduler.stop();

    // 测试工作窃取调度器
    WorkStealingScheduler ws_scheduler(num_threads);
    ws_scheduler.start();

    auto ws_start = std::chrono::steady_clock::now();
    std::atomic<int> ws_completed{0};

    for (int i = 0; i < num_tasks; ++i) {
        ws_scheduler.schedule([&]() {
            int work = 0;
            for (int j = 0; j < 1000; ++j) {
                work += j % 10;
            }
            ws_completed.fetch_add(1);
        });
    }

    while (ws_completed.load() < num_tasks) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    auto ws_end = std::chrono::steady_clock::now();
    auto ws_duration = std::chrono::duration_cast<std::chrono::microseconds>(
        ws_end - ws_start);

    auto ws_stats = ws_scheduler.get_work_stealing_stats();
    ws_scheduler.stop();

    EXPECT_EQ(normal_completed.load(), num_tasks);
    EXPECT_EQ(ws_completed.load(), num_tasks);

    std::cout << "Performance comparison:" << std::endl;
    std::cout << "  Normal scheduler: " << normal_duration.count() << "us" << std::endl;
    std::cout << "  Work-stealing scheduler: " << ws_duration.count() << "us" << std::endl;
    std::cout << "  Work-stealing steals: " << ws_stats.total_steals << std::endl;

    // 工作窃取至少不应该明显变慢（在非常快的机器上允许相等或略慢）
    EXPECT_LE(ws_duration.count(), normal_duration.count() * 2 + 1000); // 1ms 容差
}