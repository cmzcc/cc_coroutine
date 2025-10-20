/**
 * @file stress_test_scheduler.cpp
 * @brief Scheduler 压力测试 - 测试高并发任务调度性能
 */

#include "scheduler/scheduler.h"
#include "scheduler/work_stealing_scheduler.h"
#include "utils/logger.h"
#include <atomic>
#include <chrono>
#include <vector>
#include <random>
#include <iomanip>

using namespace modern_coro;
using namespace modern_coro::logger;

struct SchedulerTestConfig
{
    size_t num_tasks = 100000;
    size_t num_threads = 4;
    bool use_work_stealing = false;
    std::string test_name;
};

struct SchedulerPerf Stats
{
    std::atomic<size_t> tasks_completed{0};
    std::atomic<size_t> total_task_time_us{0};
    std::chrono::high_resolution_clock::time_point start_time;
    std::chrono::high_resolution_clock::time_point end_time;

    void start()
    {
        start_time = std::chrono::high_resolution_clock::now();
    }

    void stop()
    {
        end_time = std::chrono::high_resolution_clock::now();
    }

    double get_duration_seconds() const
    {
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);
        return duration.count() / 1000.0;
    }

    void print_report(const std::string &test_name) const
    {
        double duration = get_duration_seconds();
        double throughput = tasks_completed.load() / duration;
        double avg_task_time = total_task_time_us.load() / static_cast<double>(tasks_completed.load());

        LOG_INFO("========================================");
        LOG_INFO("  {} - Performance Report", test_name);
        LOG_INFO("========================================");
        LOG_INFO("Duration:           {:.2f} seconds", duration);
        LOG_INFO("Tasks Completed:    {}", tasks_completed.load());
        LOG_INFO("Throughput:         {:.2f} tasks/sec", throughput);
        LOG_INFO("Avg Task Time:      {:.2f} μs", avg_task_time);
        LOG_INFO("========================================\n");
    }
};

// 短任务测试（模拟快速计算）
void run_short_tasks_test(const SchedulerTestConfig &config)
{
    LOG_INFO("Running short tasks test: {} tasks on {} threads",
             config.num_tasks, config.num_threads);

    SchedulerPerfStats stats;

    std::unique_ptr<Scheduler> scheduler;
    if (config.use_work_stealing)
    {
        scheduler = std::make_unique<WorkStealingScheduler>(config.num_threads);
    }
    else
    {
        scheduler = std::make_unique<Scheduler>(config.num_threads);
    }

    scheduler->start();
    stats.start();

    // 调度短任务
    for (size_t i = 0; i < config.num_tasks; ++i)
    {
        scheduler->schedule([&stats, i]()
                            {
            auto task_start = std::chrono::high_resolution_clock::now();
            
            // 模拟短计算
            volatile int sum = 0;
            for (int j = 0; j < 100; ++j) {
                sum += j;
            }
            
            auto task_end = std::chrono::high_resolution_clock::now();
            auto task_duration = std::chrono::duration_cast<std::chrono::microseconds>(
                task_end - task_start).count();
            
            stats.total_task_time_us.fetch_add(task_duration, std::memory_order_relaxed);
            stats.tasks_completed.fetch_add(1, std::memory_order_relaxed); });
    }

    // 等待所有任务完成
    while (stats.tasks_completed.load(std::memory_order_relaxed) < config.num_tasks)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    stats.stop();
    scheduler->stop();

    stats.print_report(config.test_name);
}

// 中等任务测试（模拟中等计算）
void run_medium_tasks_test(const SchedulerTestConfig &config)
{
    LOG_INFO("Running medium tasks test: {} tasks on {} threads",
             config.num_tasks, config.num_threads);

    SchedulerPerfStats stats;

    std::unique_ptr<Scheduler> scheduler;
    if (config.use_work_stealing)
    {
        scheduler = std::make_unique<WorkStealingScheduler>(config.num_threads);
    }
    else
    {
        scheduler = std::make_unique<Scheduler>(config.num_threads);
    }

    scheduler->start();
    stats.start();

    // 调度中等任务
    for (size_t i = 0; i < config.num_tasks; ++i)
    {
        scheduler->schedule([&stats]()
                            {
            auto task_start = std::chrono::high_resolution_clock::now();
            
            // 模拟中等计算
            volatile int sum = 0;
            for (int j = 0; j < 10000; ++j) {
                sum += j * j;
            }
            
            auto task_end = std::chrono::high_resolution_clock::now();
            auto task_duration = std::chrono::duration_cast<std::chrono::microseconds>(
                task_end - task_start).count();
            
            stats.total_task_time_us.fetch_add(task_duration, std::memory_order_relaxed);
            stats.tasks_completed.fetch_add(1, std::memory_order_relaxed); });
    }

    // 等待所有任务完成
    while (stats.tasks_completed.load(std::memory_order_relaxed) < config.num_tasks)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    stats.stop();
    scheduler->stop();

    stats.print_report(config.test_name);
}

// 协程任务测试
Task<int> compute_coroutine(int value, SchedulerPerfStats &stats)
{
    auto task_start = std::chrono::high_resolution_clock::now();

    // 模拟异步计算
    if (auto scheduler = Scheduler::GetCurrent())
    {
        co_await scheduler->sleep(std::chrono::microseconds(100));
    }

    volatile int result = value * value;
    for (int i = 0; i < 100; ++i)
    {
        result += i;
    }

    auto task_end = std::chrono::high_resolution_clock::now();
    auto task_duration = std::chrono::duration_cast<std::chrono::microseconds>(
                             task_end - task_start)
                             .count();

    stats.total_task_time_us.fetch_add(task_duration, std::memory_order_relaxed);
    stats.tasks_completed.fetch_add(1, std::memory_order_relaxed);

    co_return static_cast<int>(result);
}

void run_coroutine_tasks_test(const SchedulerTestConfig &config)
{
    LOG_INFO("Running coroutine tasks test: {} tasks on {} threads",
             config.num_tasks, config.num_threads);

    SchedulerPerfStats stats;

    std::unique_ptr<Scheduler> scheduler;
    if (config.use_work_stealing)
    {
        scheduler = std::make_unique<WorkStealingScheduler>(config.num_threads);
    }
    else
    {
        scheduler = std::make_unique<Scheduler>(config.num_threads);
    }

    scheduler->start();
    stats.start();

    // 调度协程任务
    for (size_t i = 0; i < config.num_tasks; ++i)
    {
        scheduler->schedule(compute_coroutine(i, stats));
    }

    // 等待所有任务完成
    while (stats.tasks_completed.load(std::memory_order_relaxed) < config.num_tasks)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    stats.stop();
    scheduler->stop();

    stats.print_report(config.test_name);
}

// 混合负载测试
void run_mixed_workload_test(const SchedulerTestConfig &config)
{
    LOG_INFO("Running mixed workload test: {} tasks on {} threads",
             config.num_tasks, config.num_threads);

    SchedulerPerfStats stats;

    std::unique_ptr<Scheduler> scheduler;
    if (config.use_work_stealing)
    {
        scheduler = std::make_unique<WorkStealingScheduler>(config.num_threads);
    }
    else
    {
        scheduler = std::make_unique<Scheduler>(config.num_threads);
    }

    scheduler->start();
    stats.start();

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1, 3);

    // 调度混合任务
    for (size_t i = 0; i < config.num_tasks; ++i)
    {
        int task_type = dis(gen);

        if (task_type == 1)
        {
            // 短任务
            scheduler->schedule([&stats]()
                                {
                auto task_start = std::chrono::high_resolution_clock::now();
                volatile int sum = 0;
                for (int j = 0; j < 100; ++j) {
                    sum += j;
                }
                auto task_end = std::chrono::high_resolution_clock::now();
                auto task_duration = std::chrono::duration_cast<std::chrono::microseconds>(
                    task_end - task_start).count();
                stats.total_task_time_us.fetch_add(task_duration, std::memory_order_relaxed);
                stats.tasks_completed.fetch_add(1, std::memory_order_relaxed); });
        }
        else if (task_type == 2)
        {
            // 中等任务
            scheduler->schedule([&stats]()
                                {
                auto task_start = std::chrono::high_resolution_clock::now();
                volatile int sum = 0;
                for (int j = 0; j < 1000; ++j) {
                    sum += j * j;
                }
                auto task_end = std::chrono::high_resolution_clock::now();
                auto task_duration = std::chrono::duration_cast<std::chrono::microseconds>(
                    task_end - task_start).count();
                stats.total_task_time_us.fetch_add(task_duration, std::memory_order_relaxed);
                stats.tasks_completed.fetch_add(1, std::memory_order_relaxed); });
        }
        else
        {
            // 协程任务
            scheduler->schedule(compute_coroutine(i, stats));
        }
    }

    // 等待所有任务完成
    while (stats.tasks_completed.load(std::memory_order_relaxed) < config.num_tasks)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    stats.stop();
    scheduler->stop();

    stats.print_report(config.test_name);
}

int main(int argc, char *argv[])
{
    // 初始化日志系统
    Logger::instance().init(
        "logs/scheduler_stress_test.log",
        LogLevel::INFO,
        50 * 1024 * 1024,
        5);

    LOG_INFO("========================================");
    LOG_INFO("  Scheduler Stress Test Suite");
    LOG_INFO("========================================\n");

    // 测试配置
    std::vector<size_t> thread_counts = {2, 4, 8};
    std::vector<size_t> task_counts = {10000, 50000, 100000};

    // 命令行参数解析
    bool run_all = true;
    bool test_short = false;
    bool test_medium = false;
    bool test_coroutine = false;
    bool test_mixed = false;
    bool use_work_stealing = false;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--short")
        {
            test_short = true;
            run_all = false;
        }
        else if (arg == "--medium")
        {
            test_medium = true;
            run_all = false;
        }
        else if (arg == "--coroutine")
        {
            test_coroutine = true;
            run_all = false;
        }
        else if (arg == "--mixed")
        {
            test_mixed = true;
            run_all = false;
        }
        else if (arg == "--work-stealing")
        {
            use_work_stealing = true;
        }
        else if (arg == "--help")
        {
            std::cout << "Usage: " << argv[0] << " [OPTIONS]\n"
                      << "Options:\n"
                      << "  --short          Run short tasks test only\n"
                      << "  --medium         Run medium tasks test only\n"
                      << "  --coroutine      Run coroutine tasks test only\n"
                      << "  --mixed          Run mixed workload test only\n"
                      << "  --work-stealing  Use WorkStealingScheduler\n"
                      << "  --help           Show this help message\n";
            return 0;
        }
    }

    if (run_all)
    {
        test_short = test_medium = test_coroutine = test_mixed = true;
    }

    const char *scheduler_type = use_work_stealing ? "WorkStealingScheduler" : "Scheduler";
    LOG_INFO("Using: {}\n", scheduler_type);

    try
    {
        // 运行各种测试
        for (size_t threads : thread_counts)
        {
            for (size_t tasks : task_counts)
            {
                SchedulerTestConfig config;
                config.num_tasks = tasks;
                config.num_threads = threads;
                config.use_work_stealing = use_work_stealing;

                if (test_short)
                {
                    config.test_name = fmt::format("Short Tasks ({} threads, {} tasks)",
                                                   threads, tasks);
                    run_short_tasks_test(config);
                }

                if (test_medium)
                {
                    config.test_name = fmt::format("Medium Tasks ({} threads, {} tasks)",
                                                   threads, tasks);
                    run_medium_tasks_test(config);
                }

                if (test_coroutine)
                {
                    config.test_name = fmt::format("Coroutine Tasks ({} threads, {} tasks)",
                                                   threads, tasks);
                    run_coroutine_tasks_test(config);
                }

                if (test_mixed)
                {
                    config.test_name = fmt::format("Mixed Workload ({} threads, {} tasks)",
                                                   threads, tasks);
                    run_mixed_workload_test(config);
                }
            }
        }

        LOG_INFO("========================================");
        LOG_INFO("  All Tests Completed Successfully!");
        LOG_INFO("========================================");

        Logger::instance().flush();
        return 0;
    }
    catch (const std::exception &e)
    {
        LOG_CRITICAL("Fatal error: {}", e.what());
        Logger::instance().flush();
        return 1;
    }
}
