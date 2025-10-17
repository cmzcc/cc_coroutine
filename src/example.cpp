/**
 * @file example.cpp
 * @brief Modern C++20 协程库使用示例 - 包含安全特性演示
 * 
 * 本文件展示了现代协程库的各种使用场景，包括：
 * - 基础协程任务
 * - 协程链式调用
 * - 并发执行
 * - 定时器使用
 * - 网络IO示例
 * - Hook机制演示
 * - 安全特性演示（新增）
 */

#include "scheduler.h"
#include "work_stealing_scheduler.h"
#include "io_manager.h"
#include "timer.h"
#include "hook.h"
#include "advanced_scheduler.h"
#include "async_task.h"
#include "coroutine_safety.h"  // 新增安全模块
#include "coroutine_cancellation.h"  // 新增取消机制模块
#include <iostream>
#include <vector>
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <thread>

using namespace modern_coro;
using namespace modern_coro::safety;  // 使用安全命名空间
using namespace modern_coro::cancellation;  // 使用取消命名空间

// 函数声明
Task<> cancellation_features_example();

/**
 * @brief 基础协程任务示例
 */
Task<int> compute_async(int x, int y) {
    std::cout << "[协程 " << std::this_thread::get_id() << "] 开始计算 " 
              << x << " + " << y << std::endl;
    
    co_await Scheduler::GetCurrent()->sleep(std::chrono::milliseconds(100));
    
    int result = x + y;
    std::cout << "[协程 " << std::this_thread::get_id() << "] 计算完成，结果: " 
              << result << std::endl;
    
    co_return result;
}

/**
 * @brief 可能抛出异常的协程任务
 */
Task<int> risky_compute_async(int x, int y) {
    std::cout << "[风险协程] 开始计算 " << x << " / " << y << std::endl;
    
    co_await Scheduler::GetCurrent()->sleep(std::chrono::milliseconds(50));
    
    if (y == 0) {
        throw CoroutineException("Division by zero in coroutine");
    }
    
    int result = x / y;
    std::cout << "[风险协程] 计算完成，结果: " << result << std::endl;
    
    co_return result;
}

/**
 * @brief 安全特性演示
 */
// 在 safety_features_example 函数中添加错误处理演示
Task<> safety_features_example() {
    std::cout << "\n=== 安全特性演示 ===" << std::endl;
    
    // 1. 异常安全的协程包装器演示
    std::cout << "\n--- 异常安全协程演示 ---" << std::endl;
    
    try {
        // 使用右值引用版本的 on_error 进行链式调用
        auto safe_task = std::move(make_safe(risky_compute_async(10, 0)))
            .on_error([](std::exception_ptr e) {
                try {
                    std::rethrow_exception(e);
                } catch (const CoroutineException& ex) {
                    std::cout << "[异常处理器] 捕获到协程异常: " << ex.what() << std::endl;
                } catch (const std::exception& ex) {
                    std::cout << "[异常处理器] 捕获到标准异常: " << ex.what() << std::endl;
                }
            });
        
        co_await std::move(safe_task);
        std::cout << "安全任务完成" << std::endl;
    } catch (...) {
        std::cout << "安全任务仍然抛出了异常（预期行为）" << std::endl;
    }
    
    // 2. 增强Task演示
    std::cout << "\n--- 增强Task演示 ---" << std::endl;
    
    auto enhanced_task = make_enhanced(compute_async(5, 3));
    int result = co_await std::move(enhanced_task);
    std::cout << "增强任务结果: " << result << std::endl;
    
    // 3. 资源管理演示
    std::cout << "\n--- 资源管理演示 ---" << std::endl;
    
    {
        // RAII文件描述符管理
        FdGuard fd_guard(socket(AF_INET, SOCK_STREAM, 0));
        if (fd_guard.valid()) {
            std::cout << "创建socket成功，fd: " << fd_guard.get() << std::endl;
        }
        // fd_guard析构时自动关闭socket
    }
    
    {
        // 通用资源管理
        auto resource_guard = make_resource_guard(
            new int(42),
            [](int* ptr) { 
                std::cout << "释放资源: " << *ptr << std::endl;
                delete ptr; 
            }
        );
        std::cout << "资源值: " << *resource_guard.get() << std::endl;
        // resource_guard析构时自动调用删除器
    }
    
    // 4. 线程安全统计演示
    std::cout << "\n--- 线程安全统计演示 ---" << std::endl;
    
    ThreadSafeStats& stats = get_stats();
    stats.increment("demo_counter", 5);
    stats.increment("demo_counter", 3);
    
    std::cout << "统计计数器值: " << stats.get("demo_counter") << std::endl;
    
    auto all_stats = stats.get_all();
    std::cout << "所有统计信息:" << std::endl;
    for (const auto& [name, value] : all_stats) {
        std::cout << "  " << name << ": " << value << std::endl;
    }
    
    // 5. 统一错误处理演示
    std::cout << "\n--- 统一错误处理演示 ---" << std::endl;
    
    try {
        // 演示系统调用错误处理
        FdGuard invalid_fd(-1);
        
        // 这会触发统一的错误处理
        CHECK_SYSCALL(fcntl(invalid_fd.get(), F_GETFL, 0), "testing error handling");
        
    } catch (const IOException& e) {
        std::cout << "捕获到IO异常: " << e.what() << std::endl;
        std::cout << "错误码: " << e.sys_error.errno_code << std::endl;
        std::cout << "操作: " << e.sys_error.operation << std::endl;
        std::cout << "上下文: " << e.sys_error.context << std::endl;
    }
    
    std::cout << "统一错误处理演示完成" << std::endl;
}

/**
 * @brief 协程链式调用示例
 */
Task<> chain_example() {
    std::cout << "\n=== 协程链式调用示例 ===" << std::endl;
    
    auto task1 = compute_async(1, 2);
    auto task2 = compute_async(3, 4);
    
    int result1 = co_await std::move(task1);
    std::cout << "第一个任务结果: " << result1 << std::endl;
    
    int result2 = co_await std::move(task2);
    std::cout << "第二个任务结果: " << result2 << std::endl;
    
    auto final_task = compute_async(result1, result2);
    int final_result = co_await std::move(final_task);
    
    std::cout << "最终结果: " << final_result << std::endl;
}

/**
 * @brief 并发执行示例
 */
Task<> concurrent_example() {
    std::cout << "\n=== 协程并发执行示例 ===" << std::endl;
    
    std::vector<Task<int>> tasks;
    const int task_count = 5;
    
    std::cout << "创建 " << task_count << " 个并发任务..." << std::endl;
    for (int i = 0; i < task_count; ++i) {
        tasks.push_back(compute_async(i, i + 1));
    }
    
    std::cout << "等待所有任务完成..." << std::endl;
    for (size_t i = 0; i < tasks.size(); ++i) {
        int result = co_await std::move(tasks[i]);
        std::cout << "任务 " << i << " 完成，结果: " << result << std::endl;
    }
    
    std::cout << "所有并发任务已完成" << std::endl;
}

/**
 * @brief 定时器使用示例
 */
Task<> timer_example() {
    std::cout << "\n=== 定时器使用示例 ===" << std::endl;
    
    Timer timer;
    timer.start();
    
    std::cout << "设置3秒后的定时任务..." << std::endl;
    
    auto future_time = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    co_await TimerAwaiter(timer, future_time, get_current_scheduler_ptr());
    
    std::cout << "定时器触发！3秒已过" << std::endl;
    
    std::cout << "启动多个定时任务..." << std::endl;
    
    auto task1 = [&](Timer& timer) -> Task<> {
        auto time1 = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        co_await TimerAwaiter(timer, time1, get_current_scheduler_ptr());
        std::cout << "1秒定时任务完成" << std::endl;
    };
    
    auto task2 = [&](Timer& timer) -> Task<> {
        auto time2 = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        co_await TimerAwaiter(timer, time2, get_current_scheduler_ptr());
        std::cout << "2秒定时任务完成" << std::endl;
    };
    
    auto t1 = task1(timer);
    auto t2 = task2(timer);
    
    co_await std::move(t1);
    co_await std::move(t2);
    
    timer.stop();
    std::cout << "定时器示例完成" << std::endl;
}

/**
 * @brief Hook机制演示
 */
Task<> hook_example() {
    std::cout << "\n=== Hook机制演示 ===" << std::endl;
    
    modern_coro::HookManager::instance().enable_hook(true);
    
    std::cout << "开始sleep测试..." << std::endl;
    auto start_time = std::chrono::steady_clock::now();
    
    co_await modern_coro::coroutine_sleep(std::chrono::milliseconds(1000));
    
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    std::cout << "sleep完成，实际耗时: " << duration.count() << "ms" << std::endl;
    std::cout << "Hook机制演示完成" << std::endl;
    
    co_return;
}

/**
 * @brief 高级调度器示例
 */
Task<> advanced_scheduler_example() {
    std::cout << "\n=== 高级调度器示例 ===" << std::endl;
    
    AdvancedScheduler scheduler(4);
    scheduler.start();
    
    std::vector<std::future<int>> results;
    
    for (int i = 0; i < 5; ++i) {
        auto promise = std::make_shared<std::promise<int>>();
        results.push_back(promise->get_future());
        
        scheduler.schedule_with_priority([i, promise]() {
            std::cout << "[高级调度器线程 " << std::this_thread::get_id() << "] 开始计算 " << i << " + " << (i + 1) << std::endl;
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            int result = i + (i + 1);
            std::cout << "[高级调度器线程 " << std::this_thread::get_id() << "] 计算完成，结果: " << result << std::endl;
            
            promise->set_value(result);
        }, static_cast<TaskPriority>(i % 4));
    }
    
    std::cout << "等待高级调度器任务完成..." << std::endl;
    for (size_t i = 0; i < results.size(); ++i) {
        try {
            int result = results[i].get();
            std::cout << "高级调度器任务 " << i << " 完成，结果: " << result << std::endl;
        } catch (const std::exception& e) {
            std::cout << "高级调度器任务 " << i << " 失败: " << e.what() << std::endl;
        }
    }
    
    auto stats = scheduler.get_stats();
    std::cout << "调度器统计信息:" << std::endl;
    std::cout << "  总执行任务数: " << stats.total_tasks_executed << std::endl;
    std::cout << "  队列中任务数: " << stats.tasks_in_queue << std::endl;
    std::cout << "  平均等待时间: " << stats.average_wait_time_ms << "ms" << std::endl;
    
    scheduler.stop();
    std::cout << "高级调度器示例完成" << std::endl;
    
    co_return;
}

/**
 * @brief 异步任务包装器示例
 */
Task<> async_task_example() {
    std::cout << "\n=== 异步任务包装器示例 ===" << std::endl;
    
    auto sync_task = AsyncTask<int>::from_sync([](int a, int b) {
        std::cout << "同步函数执行中: " << a << " * " << b << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return a * b;
    }, 6, 7);
    
    std::cout << "等待异步任务完成..." << std::endl;
    int result = co_await std::move(sync_task);
    std::cout << "异步任务结果: " << result << std::endl;
    
    auto callback_task = AsyncTask<std::string>::from_callback([](auto callback) {
        std::thread([callback]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            callback("Hello from callback!");
        }).detach();
    });
    
    std::cout << "等待回调任务完成..." << std::endl;
    std::string callback_result = co_await std::move(callback_task);
    std::cout << "回调任务结果: " << callback_result << std::endl;
}

/**
 * @brief 主函数
 */
#include <iomanip>
#include <algorithm>
#include <numeric>

/**
 * @brief 性能测试工具类
 */
class PerformanceBenchmark {
public:
    struct BenchmarkResult {
        std::string test_name;
        size_t iterations;
        std::chrono::nanoseconds total_time;
        std::chrono::nanoseconds avg_time;
        std::chrono::nanoseconds min_time;
        std::chrono::nanoseconds max_time;
        double throughput_ops_per_sec;

        void print() const {
            std::cout << "=== " << test_name << " ===" << std::endl;
            std::cout << "迭代次数: " << iterations << std::endl;
            std::cout << "总耗时: " << total_time.count() / 1e6 << " ms" << std::endl;
            std::cout << "平均耗时: " << avg_time.count() / 1e3 << " μs" << std::endl;
            std::cout << "最小耗时: " << min_time.count() / 1e3 << " μs" << std::endl;
            std::cout << "最大耗时: " << max_time.count() / 1e3 << " μs" << std::endl;
            std::cout << "吞吐量: " << throughput_ops_per_sec << " ops/sec" << std::endl;
            std::cout << std::endl;
        }
    };

    // 修复：为协程函数提供专门的benchmark方法
    template<typename CoroFunc>
    static Task<BenchmarkResult> benchmark_coroutine(const std::string& name, size_t iterations, CoroFunc&& func) {
        std::vector<std::chrono::nanoseconds> times;
        times.reserve(iterations);
        
        // 预热 - 正确等待协程完成
        for (int i = 0; i < 3; ++i) {
            co_await func();
        }
        
        auto start_total = std::chrono::high_resolution_clock::now();
        
        for (size_t i = 0; i < iterations; ++i) {
            auto start = std::chrono::high_resolution_clock::now();
            co_await func();  // 关键：正确等待协程完成
            auto end = std::chrono::high_resolution_clock::now();
            times.push_back(end - start);
        }
        
        auto end_total = std::chrono::high_resolution_clock::now();
        auto total_time = end_total - start_total;
        
        auto min_time = *std::min_element(times.begin(), times.end());
        auto max_time = *std::max_element(times.begin(), times.end());
        auto avg_time = std::chrono::nanoseconds(
            std::accumulate(times.begin(), times.end(), std::chrono::nanoseconds(0)).count() / iterations
        );
        
        double throughput = static_cast<double>(iterations) / (total_time.count() / 1e9);
        
        co_return BenchmarkResult{name, iterations, total_time, avg_time, min_time, max_time, throughput};
    }

    // 保留原有的同步函数benchmark方法
    template<typename Func>
    static BenchmarkResult benchmark(const std::string& name, size_t iterations, Func&& func) {
        std::vector<std::chrono::nanoseconds> times;
        times.reserve(iterations);
        
        // 预热
        for (int i = 0; i < 3; ++i) {
            func();
        }
        
        auto start_total = std::chrono::high_resolution_clock::now();
        
        for (size_t i = 0; i < iterations; ++i) {
            auto start = std::chrono::high_resolution_clock::now();
            func();
            auto end = std::chrono::high_resolution_clock::now();
            times.push_back(end - start);
        }
        
        auto end_total = std::chrono::high_resolution_clock::now();
        auto total_time = end_total - start_total;
        
        auto min_time = *std::min_element(times.begin(), times.end());
        auto max_time = *std::max_element(times.begin(), times.end());
        auto avg_time = std::chrono::nanoseconds(
            std::accumulate(times.begin(), times.end(), std::chrono::nanoseconds(0)).count() / iterations
        );
        
        double throughput = static_cast<double>(iterations) / (total_time.count() / 1e9);
        
        return {name, iterations, total_time, avg_time, min_time, max_time, throughput};
    }
};

/**
 * @brief 协程创建和销毁性能测试
 */
Task<> coroutine_creation_benchmark() {
    std::cout << "\n=== 协程创建和销毁性能测试 ===" << std::endl;
    
    // 减少迭代次数，避免内存问题
    const size_t iterations = 10000;  // 进一步减少到100
    
    // 测试简单协程创建
    auto simple_coroutine = []() -> Task<int> {
        co_return 42;
    };
    
    // 修复：使用新的协程benchmark方法
    auto result1 = co_await PerformanceBenchmark::benchmark_coroutine("简单协程创建+执行", iterations, [&]() -> Task<> {
        auto task = simple_coroutine();
        int result = co_await std::move(task);  // 确保协程完成
        (void)result; // 避免未使用警告
        co_return;
    });
    
    result1.print();
    
    // 测试协程创建+执行（减少数量）
    auto result2 = co_await PerformanceBenchmark::benchmark_coroutine("协程创建+执行", 50, [&]() -> Task<> {
        auto task = simple_coroutine();
        int result = co_await std::move(task);
        (void)result; // 避免未使用警告
        co_return;
    });
    
    result2.print();
    
    co_return;
}

/**
 * @brief 调度器性能测试 - 简化安全版本
 */
Task<> scheduler_performance_benchmark() {
    std::cout << "\n=== 调度器性能测试 ===" << std::endl;
    
    const int num_tasks = 5;
    std::atomic<int> started_tasks{0};
    std::atomic<int> completed_tasks{0};
    
    std::vector<Task<void>> tasks;
    tasks.reserve(num_tasks);
    
    // 创建任务的协程函数，避免lambda捕获问题
    auto create_worker_task = [&](int task_id) -> Task<void> {
        try {
            started_tasks.fetch_add(1, std::memory_order_relaxed);
            std::cout << "任务 " << task_id << " 开始执行" << std::endl;
            
            // 使用YieldAwaiter模拟工作
            co_await YieldAwaiter{};
            
            completed_tasks.fetch_add(1, std::memory_order_relaxed);
            std::cout << "任务 " << task_id << " 完成" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "任务 " << task_id << " 执行失败: " << e.what() << std::endl;
            completed_tasks.fetch_add(1, std::memory_order_relaxed);
        } catch (...) {
            std::cerr << "任务 " << task_id << " 执行失败: 未知异常" << std::endl;
            completed_tasks.fetch_add(1, std::memory_order_relaxed);
        }
    };
    
    // 创建任务
    for (int i = 0; i < num_tasks; ++i) {
        tasks.emplace_back(create_worker_task(i));
    }
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // 等待所有任务完成，添加异常处理
    for (int i = 0; i < num_tasks; ++i) {
        try {
            std::cout << "等待任务 " << i << " 完成..." << std::endl;
            co_await std::move(tasks[i]);
            std::cout << "任务 " << i << " 已完成等待" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "等待任务 " << i << " 时发生异常: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "等待任务 " << i << " 时发生未知异常" << std::endl;
        }
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    // 验证任务完成情况
    std::cout << "已启动任务数: " << started_tasks.load() << std::endl;
    std::cout << "已完成任务数: " << completed_tasks.load() << std::endl;
    std::cout << "总耗时: " << duration.count() << " ms" << std::endl;
    
    // 计算吞吐量，防止除零
    if (duration.count() > 0) {
        double throughput = static_cast<double>(completed_tasks.load()) * 1000.0 / duration.count();
        std::cout << "吞吐量: " << throughput << " tasks/second" << std::endl;
    }
    
    // 显示调度器统计信息
    if (auto scheduler = Scheduler::GetCurrent()) {
        scheduler->print_stats();
    }
}

/**
 * @brief 并发性能测试
 */
/**
 * @brief 并发性能测试
 */
Task<> concurrency_performance_benchmark() {
    std::cout << "\n=== 并发性能测试 ===" << std::endl;
    
    // 减少并发任务数量
    const size_t concurrent_tasks = 10000;  // 从1000减少到200
    const size_t work_iterations = 10000;   // 从1000减少到500
    
    std::atomic<size_t> total_work{0};
    
    auto cpu_intensive_task = [&total_work, work_iterations]() -> Task<size_t> {
        size_t local_work = 0;
        for (size_t i = 0; i < work_iterations; ++i) {
            local_work += i * i;
            if (i % 50 == 0) {  // 更频繁地让出执行权
                co_await YieldAwaiter{}; // 让出执行权
            }
        }
        total_work.fetch_add(local_work);
        co_return local_work;
    };
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // 分批处理并发任务
    const size_t batch_size = 50;
    size_t total_result = 0;
    
    for (size_t batch = 0; batch < concurrent_tasks; batch += batch_size) {
        std::vector<Task<size_t>> tasks;
        size_t current_batch_size = std::min(batch_size, concurrent_tasks - batch);
        tasks.reserve(current_batch_size);
        
        for (size_t i = 0; i < current_batch_size; ++i) {
            tasks.push_back(cpu_intensive_task());
        }
        
        for (auto& task : tasks) {
            total_result += co_await std::move(task);
        }
        
        // 让调度器有时间清理
        if (batch + batch_size < concurrent_tasks) {
            co_await Scheduler::GetCurrent()->sleep(std::chrono::milliseconds(5));
        }
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    std::cout << "并发执行 " << concurrent_tasks << " 个CPU密集型任务" << std::endl;
    std::cout << "每个任务执行 " << work_iterations << " 次计算" << std::endl;
    std::cout << "总耗时: " << duration.count() << " ms" << std::endl;
    std::cout << "总工作量: " << total_work.load() << std::endl;
    std::cout << "计算吞吐量: " << (concurrent_tasks * work_iterations * 1000.0) / duration.count() << " ops/sec" << std::endl;
}

/**
 * @brief 内存使用性能测试
 */
Task<> memory_performance_benchmark() {
    std::cout << "\n=== 内存使用性能测试 ===" << std::endl;
    
    const size_t allocation_count = 10000;
    
    // 测试ResourceGuard性能
    auto resource_guard_test = [allocation_count]() {
        std::vector<modern_coro::FastResourceGuard<int*>> guards;
        guards.reserve(allocation_count);
        
        for (size_t i = 0; i < allocation_count; ++i) {
            guards.emplace_back(new int(static_cast<int>(i)), [](int* ptr) { delete ptr; });
        }
    };
    
    auto result = PerformanceBenchmark::benchmark("ResourceGuard分配/释放", 100, resource_guard_test);
    result.print();
    
    // 测试FdGuard性能
    auto fd_guard_test = []() {
        std::vector<FdGuard> guards;
        guards.reserve(100);
        
        for (int i = 0; i < 100; ++i) {
            // 创建无效fd的guard（不会实际创建文件描述符）
            guards.emplace_back(-1);
        }
        
        // 自动清理
    };
    
    auto result2 = PerformanceBenchmark::benchmark("FdGuard创建/销毁", 1000, fd_guard_test);
    result2.print();
    
    co_return;
}

/**
 * @brief IO性能测试（模拟）
 */
Task<> io_performance_benchmark() {
    std::cout << "\n=== IO性能测试（模拟）===" << std::endl;
    
    const int io_operations = 3000;
    
    // 测试协程sleep性能
    auto sleep_test = [io_operations]() -> Task<> {
        auto* scheduler = Scheduler::GetCurrent();
        if (!scheduler) {
            std::cout << "错误：无法获取当前调度器" << std::endl;
            co_return;
        }
        
        for (int i = 0; i < io_operations; ++i) {
            // 修复：使用调度器实例调用sleep
            co_await scheduler->sleep(std::chrono::milliseconds(1));
        }
        co_return;
    };
    
    auto start = std::chrono::high_resolution_clock::now();
    co_await sleep_test();
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "Sleep操作 " << io_operations << " 次" << std::endl;
    std::cout << "总耗时: " << duration.count() << " ms" << std::endl;
    std::cout << "平均延迟: " << static_cast<double>(duration.count()) / io_operations << " ms/op" << std::endl;
    
    co_return;
}

/**
 * @brief 错误处理性能测试
 */
/**
 * @brief 错误处理性能测试
 */
Task<> error_handling_performance_benchmark() {
    std::cout << "\n=== 错误处理性能测试 ===" << std::endl;
    
    const int test_iterations = 1000;
    
    // 正常路径测试
    auto normal_path_test = [test_iterations]() -> Task<> {
        for (int i = 0; i < test_iterations; ++i) {
            auto result = co_await compute_async(i, i + 1);
            (void)result; // 避免未使用变量警告
        }
        co_return;
    };
    
    auto result1 = PerformanceBenchmark::benchmark("正常路径", 10, [&]() {
        // 同步执行协程用于基准测试
        auto task = normal_path_test();
        // 这里需要同步等待，但为了简化，我们只测量创建时间
    });
    result1.print();
    
    // 异常路径测试
    std::atomic<int> exception_count{0};
    auto exception_path_test = [test_iterations, &exception_count]() -> Task<> {
        for (int i = 0; i < test_iterations; ++i) {
            try {
                auto result = co_await risky_compute_async(i, 0); // 可能抛出异常
                (void)result;
            } catch (const std::exception&) {
                exception_count.fetch_add(1);
            }
        }
        co_return;
    };
    
    auto result2 = PerformanceBenchmark::benchmark("异常路径", 10, [&]() {
        auto task = exception_path_test();
        // 同步等待测试完成
    });
    result2.print();
    
    // SafeTask测试
    auto safe_task_test = [test_iterations]() -> Task<> {
        for (int i = 0; i < test_iterations; ++i) {
            auto safe_task = make_safe(risky_compute_async(i, 0))
                .on_error([](std::exception_ptr) {
                    // 错误处理
                });
            try {
                auto result = co_await std::move(safe_task);
                (void)result;
            } catch (...) {
                // 忽略异常
            }
        }
        co_return;
    };
    
    auto result3 = PerformanceBenchmark::benchmark("SafeTask", 10, [&]() {
        auto task = safe_task_test();
        // 同步等待测试完成
    });
    result3.print();
    
    std::cout << "异常计数: " << exception_count.load() << std::endl;
    
    co_return;
}

/**
 * @brief 综合性能测试
 */
Task<> comprehensive_performance_benchmark() {
    std::cout << "\n=== 综合性能测试 ===" << std::endl;
    
    auto overall_start = std::chrono::high_resolution_clock::now();
    
    // 运行所有性能测试
    co_await coroutine_creation_benchmark();
    co_await scheduler_performance_benchmark();
    co_await concurrency_performance_benchmark();
    co_await memory_performance_benchmark();
    co_await io_performance_benchmark();
    co_await error_handling_performance_benchmark();
    
    auto overall_end = std::chrono::high_resolution_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(overall_end - overall_start);
    
    std::cout << "\n=== 性能测试总结 ===" << std::endl;
    std::cout << "总测试时间: " << total_duration.count() << " ms" << std::endl;
    
    // 显示系统资源使用情况
    auto final_stats = get_stats().get_all();
    std::cout << "\n系统统计信息:" << std::endl;
    for (const auto& [name, value] : final_stats) {
        std::cout << "  " << name << ": " << value << std::endl;
    }
}

/**
 * @brief 工作窃取调度器示例
 */
Task<> work_stealing_scheduler_example() {
    std::cout << "\n=== 工作窃取调度器示例 ===" << std::endl;
    
    // 创建工作窃取调度器
    WorkStealingScheduler ws_scheduler(4);
    ws_scheduler.start();
    
    // 创建大量任务来测试工作窃取
    const int num_tasks = 20;
    std::atomic<int> completed_tasks{0};
    
    std::vector<Task<void>> tasks;
    tasks.reserve(num_tasks);
    
    for (int i = 0; i < num_tasks; ++i) {
        tasks.emplace_back([&completed_tasks, i]() -> Task<void> {
            std::cout << "任务 " << i << " 在线程 " << std::this_thread::get_id() << " 开始执行" << std::endl;
            
            // 模拟不同的工作负载
            co_await YieldAwaiter{};
            std::this_thread::sleep_for(std::chrono::milliseconds(10 + (i % 50)));
            
            completed_tasks.fetch_add(1);
            std::cout << "任务 " << i << " 完成" << std::endl;
        }());
    }
    
    // 使用批量调度
    std::vector<std::function<void()>> batch_tasks;
    for (int i = 0; i < 10; ++i) {
        batch_tasks.emplace_back([i, &completed_tasks]() {
            std::cout << "批量任务 " << i << " 在线程 " << std::this_thread::get_id() << " 执行" << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            completed_tasks.fetch_add(1);
        });
    }
    
    ws_scheduler.schedule_batch(std::move(batch_tasks));
    
    // 等待所有任务完成
    for (auto& task : tasks) {
        co_await std::move(task);
    }
    
    // 等待批量任务完成
    while (completed_tasks.load() < num_tasks + 10) {
        co_await YieldAwaiter{};
    }
    
    // 显示统计信息
    ws_scheduler.print_work_stealing_stats();
    
    ws_scheduler.stop();
    std::cout << "工作窃取调度器示例完成" << std::endl;
}

// 在main函数中添加更完善的清理逻辑
int main() {
    try {
        Scheduler scheduler(4);
        scheduler.start();
        scheduler.register_thread();
        
        std::cout << "调度器已启动，工作线程数: 4" << std::endl;
        
        std::atomic<bool> main_completed{false};
        std::exception_ptr main_exception;
        
        // 在main函数中的scheduler.schedule部分添加性能测试调用
        scheduler.schedule([&main_completed, &main_exception]() -> Task<> {
            try {
                // 新增：协程取消机制演示
                co_await cancellation_features_example();
                std::cout << "\n=== 协程取消机制演示执行完成 ===" << std::endl;
                
                // 新增：安全特性演示
                co_await safety_features_example();
                std::cout << "\n=== 安全特性演示执行完成 ===" << std::endl;
                
                // 新增：性能测试
                co_await comprehensive_performance_benchmark();
                std::cout << "\n=== 性能测试执行完成 ===" << std::endl;
                
                // 原有功能示例
                co_await chain_example();
                std::cout << "\n=== 协程链式调用示例执行完成 ===" << std::endl;
                co_await concurrent_example();
                std::cout << "\n=== 并发执行示例执行完成 ===" << std::endl;
                co_await timer_example();
                std::cout << "\n=== 定时器使用示例执行完成 ===" << std::endl;
                co_await hook_example();
                std::cout << "\n=== Hook机制示例执行完成 ===" << std::endl;
                co_await async_task_example();
                std::cout << "\n=== 异步任务包装器示例执行完成 ===" << std::endl;
                co_await advanced_scheduler_example();
                
                std::cout << "\n=== 所有示例执行完成 ===" << std::endl;
                
                // 显示最终统计信息
                std::cout << "\n=== 最终统计信息 ===" << std::endl;
                auto final_stats = get_stats().get_all();
                for (const auto& [name, value] : final_stats) {
                    std::cout << "  " << name << ": " << value << std::endl;
                }
                
            } catch (const std::exception& e) {
                std::cerr << "主协程执行错误: " << e.what() << std::endl;
                main_exception = std::current_exception();
            } catch (...) {
                std::cerr << "主协程执行未知错误" << std::endl;
                main_exception = std::make_exception_ptr(std::runtime_error("Unknown exception"));
            }
            main_completed = true;
        }());
        
        std::cout << "\n等待主协程完成..." << std::endl;
        while (!main_completed) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        if (main_exception) {
            std::rethrow_exception(main_exception);
        }
        
        std::cout << "等待其他任务完成..." << std::endl;
        int wait_count = 0;
        while (!scheduler.is_idle() || scheduler.get_active_coroutines() > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (++wait_count > 50) {
                std::cout << "强制退出等待，当前活跃协程数: " << scheduler.get_active_coroutines() << std::endl;
                
                // 强制清理残留协程
                CoroutineLifecycleManager::cleanup_all();
                
                // 再次检查
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                std::cout << "清理后活跃协程数: " << scheduler.get_active_coroutines() << std::endl;
                break;
            }
        }
        
        // 确保所有统计信息更新完成
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // 最终统计
        auto stats = scheduler.get_stats();
        std::cout << "最终调度统计 - 总调度数: " << stats.total_scheduled 
                  << ", 总完成数: " << stats.total_completed 
                  << ", 活跃协程数: " << scheduler.get_active_coroutines() << std::endl;
        
        scheduler.stop();
        scheduler.unregister_thread();
        
    } catch (const std::exception& e) {
        std::cerr << "程序执行错误: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}

/**
 * @brief 协程取消机制演示
 */
Task<> cancellation_features_example() {
    std::cout << "\n=== 协程取消机制演示 ===" << std::endl;
    
    // 1. 基本取消功能演示
    std::cout << "\n--- 基本取消功能演示 ---" << std::endl;
    
    auto cancellation_source = CancellationSource::create();
    auto token = cancellation_source->get_token();
    
    // 创建一个长时间运行的任务
    auto long_task = [](CancellationToken token) -> Task<int> {
        std::cout << "[长任务] 开始执行..." << std::endl;
        
        for (int i = 0; i < 10; ++i) {
            // 检查取消状态
            if (token.is_cancelled()) {
                std::cout << "[长任务] 检测到取消请求，提前退出" << std::endl;
                co_return -1;
            }
            
            std::cout << "[长任务] 执行步骤 " << i + 1 << "/10" << std::endl;
            co_await Scheduler::GetCurrent()->sleep(std::chrono::milliseconds(200));
        }
        
        std::cout << "[长任务] 正常完成" << std::endl;
        co_return 42;
    };
    
    // 启动任务
    auto task = long_task(token);
    
    // 2秒后取消任务
    std::thread cancel_thread([cancellation_source]() {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::cout << "[取消线程] 发送取消请求" << std::endl;
        cancellation_source->cancel();
    });
    
    try {
        int result = co_await std::move(task);
        std::cout << "任务结果: " << result << std::endl;
    } catch (const std::exception& e) {
        std::cout << "任务被取消或出错: " << e.what() << std::endl;
    }
    
    cancel_thread.join();
    
    // 2. 超时取消演示
    std::cout << "\n--- 超时取消演示 ---" << std::endl;
    
    TimeoutCancellationSource timeout_source(std::chrono::milliseconds(800));
    auto timeout_token = timeout_source.get_token();
    
    auto timeout_task = [](CancellationToken token) -> Task<std::string> {
        std::cout << "[超时任务] 开始执行，预计需要2秒..." << std::endl;
        
        for (int i = 0; i < 20; ++i) {
            token.throw_if_cancelled();  // 抛出异常方式检查取消
            
            std::cout << "[超时任务] 进度: " << (i + 1) * 5 << "%" << std::endl;
            co_await Scheduler::GetCurrent()->sleep(std::chrono::milliseconds(100));
        }
        
        co_return "任务完成";
    };
    
    try {
        auto timeout_result = co_await timeout_task(timeout_token);
        std::cout << "超时任务结果: " << timeout_result << std::endl;
    } catch (const std::exception& e) {
        std::cout << "超时任务被取消: " << e.what() << std::endl;
    }
    
    // 3. 取消回调演示
    std::cout << "\n--- 取消回调演示 ---" << std::endl;
    
    auto callback_source = CancellationSource::create();
    auto callback_token = callback_source->get_token();
    
    // 注册取消回调
    auto registration = callback_token.register_callback([]() {
        std::cout << "[取消回调] 收到取消通知，执行清理工作..." << std::endl;
    });
    
    auto callback_task = [](CancellationToken token) -> Task<void> {
        std::cout << "[回调任务] 开始执行..." << std::endl;
        
        // 注册任务特定的取消回调
        auto task_registration = token.register_callback([]() {
            std::cout << "[回调任务] 任务级别的取消回调被触发" << std::endl;
        });
        
        for (int i = 0; i < 5; ++i) {
            if (token.is_cancelled()) {
                std::cout << "[回调任务] 检测到取消，退出循环" << std::endl;
                break;
            }
            
            std::cout << "[回调任务] 执行中... " << i + 1 << "/5" << std::endl;
            co_await Scheduler::GetCurrent()->sleep(std::chrono::milliseconds(300));
        }
        
        std::cout << "[回调任务] 执行结束" << std::endl;
    };
    
    // 启动任务
    auto cb_task = callback_task(callback_token);
    
    // 1秒后取消
    std::thread cb_cancel_thread([callback_source]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        std::cout << "[取消线程] 触发回调取消" << std::endl;
        callback_source->cancel();
    });
    
    co_await std::move(cb_task);
    cb_cancel_thread.join();
    
    // 4. 生命周期管理演示
    std::cout << "\n--- 高级生命周期管理演示 ---" << std::endl;
    
    std::cout << "当前活跃协程数: " << AdvancedCoroutineLifecycleManager::active_count() << std::endl;
    std::cout << "总创建协程数: " << AdvancedCoroutineLifecycleManager::total_created() << std::endl;
    std::cout << "总销毁协程数: " << AdvancedCoroutineLifecycleManager::total_destroyed() << std::endl;
    
    // 创建一些测试协程
    std::vector<Task<void>> test_tasks;
    for (int i = 0; i < 3; ++i) {
        test_tasks.push_back([i]() -> Task<void> {
            std::cout << "[测试协程 " << i << "] 开始执行" << std::endl;
            co_await Scheduler::GetCurrent()->sleep(std::chrono::milliseconds(500));
            std::cout << "[测试协程 " << i << "] 执行完成" << std::endl;
        }());
    }
    
    std::cout << "创建测试协程后活跃数: " << AdvancedCoroutineLifecycleManager::active_count() << std::endl;
    
    // 等待一部分完成
    co_await std::move(test_tasks[0]);
    
    std::cout << "部分协程完成后活跃数: " << AdvancedCoroutineLifecycleManager::active_count() << std::endl;
    
    // 取消剩余协程
    std::cout << "取消剩余协程..." << std::endl;
    AdvancedCoroutineLifecycleManager::cancel_all();
    
    // 等待剩余协程
    for (size_t i = 1; i < test_tasks.size(); ++i) {
        try {
            co_await std::move(test_tasks[i]);
        } catch (...) {
            std::cout << "[测试协程 " << i << "] 被取消" << std::endl;
        }
    }
    
    std::cout << "最终活跃协程数: " << AdvancedCoroutineLifecycleManager::active_count() << std::endl;
    
    // 显示调试信息
    auto debug_info = AdvancedCoroutineLifecycleManager::get_debug_info();
    std::cout << "协程调试信息:" << std::endl;
    for (const auto& info : debug_info) {
        std::cout << "  " << info << std::endl;
    }
    
    std::cout << "协程取消机制演示完成" << std::endl;
}
