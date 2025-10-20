/**
 * @file test_concurrent_tasks.cpp
 * @brief 并发任务集成测试
 */

#include <gtest/gtest.h>
#include <vector>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <future>
#include "scheduler/scheduler.h"
#include "scheduler/advanced_scheduler.h"
#include "scheduler/work_stealing_scheduler.h"

using namespace modern_coro;

// 前向声明：在使用前声明链式步骤函数
Task<int> compute_step1(int value);
Task<int> compute_step2(int value);
Task<int> compute_step3(int value);

// 独立的协程函数，避免 lambda 捕获问题
Task<> run_chain(int value, std::atomic<int>& completed_count);

// 测试基础调度器的并发性能
TEST(ConcurrentTasksTest, BasicSchedulerConcurrentTasks) {
    Scheduler scheduler(8);
    scheduler.start();

    const int num_tasks = 1000;
    std::atomic<int> completed_tasks{0};
    std::atomic<long long> total_execution_time{0};

    auto start_time = std::chrono::steady_clock::now();

    for (int i = 0; i < num_tasks; ++i) {
        scheduler.schedule([&, i]() {
            auto task_start = std::chrono::steady_clock::now();

            // 模拟一些工作
            int result = 0;
            for (int j = 0; j < 1000; ++j) {
                result += j % 10;
            }

            auto task_end = std::chrono::steady_clock::now();
            auto task_duration = std::chrono::duration_cast<std::chrono::microseconds>(
                task_end - task_start);

            total_execution_time.fetch_add(task_duration.count());
            completed_tasks.fetch_add(1);
        });
    }

    // 等待所有任务完成
    while (completed_tasks.load() < num_tasks) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto end_time = std::chrono::steady_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    scheduler.stop();

    EXPECT_EQ(completed_tasks.load(), num_tasks);

    double avg_task_time = static_cast<double>(total_execution_time.load()) / num_tasks;
    std::cout << "Basic Scheduler - Total time: " << total_duration.count() << "ms, "
              << "Avg task time: " << avg_task_time << "us" << std::endl;
}

// 测试协程并发执行
TEST(ConcurrentTasksTest, CoroutineConcurrentExecution) {
    Scheduler scheduler(8);
    scheduler.start();

    const int num_coroutines = 500;
    std::atomic<int> completed_coroutines{0};
    std::vector<int> results;

    std::mutex results_mutex;

    auto start_time = std::chrono::steady_clock::now();

    // 将逻辑提取为具名协程，避免捕获语义歧义
    auto run_one = [&scheduler](int id, std::vector<int>& results,
                                std::mutex& results_mutex,
                                std::atomic<int>& completed) -> Task<> {
        // 模拟异步工作
        co_await scheduler.sleep(std::chrono::microseconds(100 + (id % 10) * 10));

        {
            std::lock_guard<std::mutex> lock(results_mutex);
            results.push_back(id);
        }

        completed.fetch_add(1);
    };

    for (int i = 0; i < num_coroutines; ++i) {
        scheduler.schedule(run_one(i, results, results_mutex, completed_coroutines));
    }

    // 等待所有协程完成
    while (completed_coroutines.load() < num_coroutines) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto end_time = std::chrono::steady_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    scheduler.stop();

    // 去除临时诊断输出，保留必要的正确性断言

    EXPECT_EQ(completed_coroutines.load(), num_coroutines);
    EXPECT_EQ(results.size(), static_cast<size_t>(num_coroutines));

    // 验证所有协程都执行了
    std::sort(results.begin(), results.end());
    for (int i = 0; i < num_coroutines; ++i) {
        EXPECT_EQ(results[i], i);
    }

    std::cout << "Coroutine concurrent execution - Total time: " << total_duration.count() << "ms" << std::endl;
}

// 测试工作窃取调度器的负载均衡
TEST(ConcurrentTasksTest, WorkStealingLoadBalancing) {
    WorkStealingScheduler scheduler(4);
    scheduler.start();

    const int num_tasks = 1000;
    std::atomic<int> completed_tasks{0};

    // 创建不均衡的工作负载
    auto start_time = std::chrono::steady_clock::now();

    for (int i = 0; i < num_tasks; ++i) {
        scheduler.schedule([&, i]() {
            // 不同的任务有不同的执行时间
            int work_amount = 1000 + (i % 10) * 500;

            int result = 0;
            for (int j = 0; j < work_amount; ++j) {
                result += j % 10;
            }

            completed_tasks.fetch_add(1);
        });
    }

    // 等待所有任务完成
    while (completed_tasks.load() < num_tasks) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto end_time = std::chrono::steady_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    auto stats = scheduler.get_work_stealing_stats();
    scheduler.stop();

    EXPECT_EQ(completed_tasks.load(), num_tasks);

    std::cout << "Work-stealing scheduler - Total time: " << total_duration.count() << "ms" << std::endl;
    std::cout << "Work-stealing stats - Total steals: " << stats.total_steals
              << ", Successful steals: " << stats.successful_steals
              << ", Success rate: " << stats.steal_success_rate * 100 << "%" << std::endl;
}

// 测试优先级调度
TEST(ConcurrentTasksTest, PriorityScheduling) {
    AdvancedScheduler scheduler(4);
    scheduler.start();

    const int tasks_per_priority = 50;
    std::vector<std::pair<TaskPriority, int>> execution_order;

    std::mutex order_mutex;
    std::atomic<int> completed_count{0};

    // 创建不同优先级的任务
    for (int priority = 0; priority < 4; ++priority) {
        TaskPriority task_priority = static_cast<TaskPriority>(priority);

        for (int i = 0; i < tasks_per_priority; ++i) {
            scheduler.schedule_with_priority([&, task_priority, priority, i]() {
                std::lock_guard<std::mutex> lock(order_mutex);
                execution_order.emplace_back(task_priority, priority * tasks_per_priority + i);
                completed_count.fetch_add(1);
            }, task_priority);
        }
    }

    // 等待所有任务完成
    while (completed_count.load() < 4 * tasks_per_priority) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    scheduler.stop();

    EXPECT_EQ(execution_order.size(), static_cast<size_t>(4 * tasks_per_priority));

    // 验证优先级顺序（至少前几个应该是最高优先级）
    int high_priority_count = 0;
    for (size_t i = 0; i < execution_order.size() && i < 100; ++i) {
        if (execution_order[i].first == TaskPriority::CRITICAL) {
            high_priority_count++;
        }
    }

    // 至少有一些高优先级任务在前面执行
    EXPECT_GT(high_priority_count, 0);
}

// 测试协程链式调用的并发性
TEST(ConcurrentTasksTest, CoroutineChainConcurrency) {
    Scheduler scheduler(8);
    scheduler.start();

    const int num_chains = 100;

    // 使用全局原子变量来避免栈变量问题
    static std::atomic<int> completed_count{0};

    auto start_time = std::chrono::steady_clock::now();

    // 预先创建所有协程任务，避免在循环中捕获循环变量
    std::vector<Task<>> tasks;
    for (int i = 0; i < num_chains; ++i) {
        tasks.push_back(run_chain(i, completed_count));
    }

    // 调度所有任务
    for (auto& task : tasks) {
        scheduler.schedule(std::move(task));
    }

    // 等待所有链完成
    auto timeout_time = start_time + std::chrono::seconds(5);
    while (completed_count.load() < num_chains) {
        auto now = std::chrono::steady_clock::now();
        if (now > timeout_time) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    auto end_time = std::chrono::steady_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    scheduler.stop();

    ASSERT_EQ(completed_count.load(), num_chains);
    std::cout << "Coroutine chains - Total time: " << total_duration.count() << "ms" << std::endl;

    // 重置静态变量
    completed_count.store(0);
}// 辅助函数：协程链的各个步骤
Task<int> compute_step1(int value) {
    co_await Scheduler::GetCurrent()->sleep(std::chrono::microseconds(50));
    co_return value * 2;
}

Task<int> compute_step2(int value) {
    co_await Scheduler::GetCurrent()->sleep(std::chrono::microseconds(30));
    co_return value + 10;
}

Task<int> compute_step3(int value) {
    co_await Scheduler::GetCurrent()->sleep(std::chrono::microseconds(20));
    co_return value / 2;
}

// 独立的协程函数，避免 lambda 捕获问题
Task<> run_chain(int value, std::atomic<int>& completed_count) {
    // 创建一个协程链
    int result = co_await compute_step1(value);
    result = co_await compute_step2(result);
    result = co_await compute_step3(result);

    completed_count.fetch_add(1);
}

// 测试内存压力下的并发性能
TEST(ConcurrentTasksTest, MemoryPressureTest) {
    Scheduler scheduler(4);
    scheduler.start();

    const int num_tasks = 10000;
    std::atomic<int> completed_tasks{0};

    auto start_time = std::chrono::steady_clock::now();

    auto run_one = [&scheduler](int id, std::atomic<int>& completed) -> Task<> {
        // 创建一些临时对象来增加内存压力
        std::vector<int> data(100, id);
        co_await scheduler.sleep(std::chrono::microseconds(10));

        // 使用数据进行一些计算
        int sum = 0;
        for (int val : data) {
            sum += val;
        }

        completed.fetch_add(1);
    };

    for (int i = 0; i < num_tasks; ++i) {
        scheduler.schedule(run_one(i, completed_tasks));
    }

    // 等待所有任务完成
    while (completed_tasks.load() < num_tasks) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    auto end_time = std::chrono::steady_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    scheduler.stop();

    EXPECT_EQ(completed_tasks.load(), num_tasks);

    std::cout << "Memory pressure test - Total time: " << total_duration.count() << "ms, "
              << "Tasks per second: " << (num_tasks * 1000.0 / total_duration.count()) << std::endl;
}