/**
 * @file test_async_task.cpp
 * @brief 异步任务组件单元测试
 */

#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <future>
#include <atomic>
#include <memory>
#include "core/async_task.h"
#include "scheduler/scheduler.h"

using namespace modern_coro;

// 测试从同步函数创建异步任务
TEST(AsyncTaskTest, FromSyncFunction) {
    std::cout << "[TEST] Starting FromSyncFunction test" << std::endl;
    
    Scheduler scheduler(2);
    scheduler.start();
    std::cout << "[TEST] Scheduler started" << std::endl;

    // 使用 Task<int> 来返回结果
    Task<int> coro_task = []() -> Task<int> {
        std::cout << "[TEST] Coroutine started" << std::endl;
        // 测试有返回值的情况
        auto task = AsyncTask<int>::from_sync([]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            return 42;
        });

        std::cout << "[TEST] About to co_await AsyncTask" << std::endl;
        // 在协程中等待异步任务
        int result = 0;
        try {
            std::cout << "[TEST] Calling co_await..." << std::endl;
            std::cout.flush();
            
            // 添加内存屏障确保之前的操作完成
            std::atomic_thread_fence(std::memory_order_seq_cst);
            
            try {
                std::cout << "[TEST] About to execute co_await..." << std::endl;
                std::cout.flush();
                result = co_await task;
                std::cout << "[TEST] co_await completed successfully, result: " << result << std::endl;
                std::cout.flush();
            } catch (const std::exception& e) {
                std::cout << "[TEST] Exception in co_await: " << e.what() << std::endl;
                std::cout.flush();
                throw;
            } catch (...) {
                std::cout << "[TEST] Unknown exception in co_await" << std::endl;
                std::cout.flush();
                throw;
            }
            std::cout << "[TEST] co_await returned successfully, result: " << result << std::endl;
            std::cout.flush();
            
            std::cout << "[TEST] AsyncTask completed with result: " << result << std::endl;
            std::cout.flush();
            
            std::cout << "[TEST] After co_await, about to continue..." << std::endl;
            std::cout.flush();
        } catch (const std::exception& e) {
            std::cout << "[TEST] Exception during co_await: " << e.what() << std::endl;
            throw;
        } catch (...) {
            std::cout << "[TEST] Unknown exception during co_await" << std::endl;
            throw;
        }
        
        std::cout << "[TEST] Coroutine got result: " << result << std::endl;
        std::cout.flush();
        
        std::cout << "[TEST] About to return from coroutine..." << std::endl;
        std::cout.flush();
        
        co_return result;  // 返回结果
    }();

    std::cout << "[TEST] Scheduling coroutine" << std::endl;
    scheduler.schedule(std::move(coro_task));

    // 等待协程完成并获取结果
    std::cout << "[TEST] Waiting for completion" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 获取协程结果
    int result_value = 0;
    if (coro_task.ready()) {
        result_value = coro_task.get();
        std::cout << "[TEST] Got result from coroutine: " << result_value << std::endl;
    } else {
        std::cout << "[TEST] Coroutine not ready yet" << std::endl;
    }
    
    std::cout << "[TEST] Test completed, result=" << result_value << std::endl;
    std::cout << "[TEST] About to sleep before shutdown" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    std::cout << "[TEST] Initiating scheduler shutdown" << std::endl;
    scheduler.stop();
    std::cout << "[TEST] Scheduler stopped" << std::endl;

    EXPECT_EQ(result_value, 42);
    std::cout << "[TEST] Test finished successfully" << std::endl;
}

// 测试从同步函数创建异步void任务
TEST(AsyncTaskTest, FromSyncVoidFunction) {
    std::cout << "[TEST] Starting FromSyncVoidFunction test" << std::endl;
    
    Scheduler scheduler(2);
    scheduler.start();
    std::cout << "[TEST] Scheduler started" << std::endl;

    // 使用 shared_ptr 确保生命周期管理
    auto executed = std::make_shared<std::atomic<bool>>(false);
    auto completed_flag = std::make_shared<std::atomic<bool>>(false);

    // 创建协程任务，返回 Task<void> 并通过 shared_ptr 标记完成状态
    auto coro_task = [executed, completed_flag]() -> Task<void> {
        std::cout << "[TEST] Void coroutine started" << std::endl;
        
        // 创建局部拷贝以确保生命周期安全
        auto executed_copy = executed;
        auto task = AsyncTask<void>::from_sync([executed_copy]() {
            std::cout << "[TEST] Inside async lambda, about to set executed" << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            executed_copy->store(true);
            std::cout << "[TEST] Executed flag set to true" << std::endl;
        });

        std::cout << "[TEST] About to co_await void AsyncTask" << std::endl;
        // 在协程中等待异步任务
        try {
            // 确保异步任务正确完成
            if (!task.ready()) {
                co_await task;
            }
            std::cout << "[TEST] Void AsyncTask completed successfully" << std::endl;
        } catch (const std::exception& e) {
            std::cout << "[TEST] Exception during void co_await: " << e.what() << std::endl;
            throw;
        }
        
        std::cout << "[TEST] About to return from void coroutine" << std::endl;
        completed_flag->store(true);  // 标记协程完成
        co_return;
    }();

    std::cout << "[TEST] Scheduling void coroutine" << std::endl;
    scheduler.schedule(std::move(coro_task));

    // 等待协程完成
    std::cout << "[TEST] Waiting for void completion" << std::endl;
    
    // 使用更长的超时时间和更频繁的检查
    bool completed = false;
    for (int i = 0; i < 1000 && !completed; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        completed = completed_flag->load();
        if (i % 100 == 0) {
            std::cout << "[TEST] Still waiting... iteration " << i << ", completed=" << completed << std::endl;
        }
    }
    
    std::cout << "[TEST] Void test completed, completed=" << completed << std::endl;
    
    // 确保协程完成后再关闭调度器，并添加额外的等待时间
    if (completed) {
        std::cout << "[TEST] About to sleep before void shutdown" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));  // 增加等待时间
        
        std::cout << "[TEST] Initiating void scheduler shutdown" << std::endl;
        scheduler.stop();
        std::cout << "[TEST] Void scheduler stopped" << std::endl;
        
        // 在调度器停止后再等待一段时间，确保所有线程都已清理
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    } else {
        std::cout << "[TEST] WARNING: Coroutine did not complete, forcing shutdown" << std::endl;
        scheduler.stop();
        std::cout << "[TEST] Forced scheduler shutdown completed" << std::endl;
    }

    EXPECT_TRUE(executed->load());
    EXPECT_TRUE(completed);
    std::cout << "[TEST] Void test finished successfully" << std::endl;
}

// 测试从回调函数创建异步任务
TEST(AsyncTaskTest, FromCallbackFunction) {
    Scheduler scheduler(2);
    scheduler.start();

    auto done = std::make_shared<std::atomic<bool>>(false);
    auto result_value = std::make_shared<std::atomic<int>>(0);

    auto t = [done, result_value]() -> Task<> {
        auto task = AsyncTask<int>::from_callback([](auto callback) {
            std::thread([callback = std::move(callback)]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                callback(123);
            }).detach();
        });

        result_value->store(co_await task);
        done->store(true);
    }();

    scheduler.schedule(std::move(t));

    // 等待测试完成
    for (int i = 0; i < 200 && !done->load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    scheduler.stop();

    EXPECT_TRUE(done->load());
    EXPECT_EQ(result_value->load(), 123);
}

// 测试从回调函数创建异步void任务
TEST(AsyncTaskTest, FromCallbackVoidFunction) {
    Scheduler scheduler(2);
    scheduler.start();

    auto done = std::make_shared<std::atomic<bool>>(false);
    auto callback_called = std::make_shared<std::atomic<bool>>(false);

    auto t = [done, callback_called]() -> Task<> {
        auto task = AsyncTask<void>::from_callback([callback_called](auto callback) {
            std::thread([callback = std::move(callback), callback_called]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                callback_called->store(true);
                callback();
            }).detach();
        });

        co_await task;
        done->store(true);
    }();

    scheduler.schedule(std::move(t));

    // 等待测试完成
    for (int i = 0; i < 200 && !done->load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    scheduler.stop();

    EXPECT_TRUE(done->load());
    EXPECT_TRUE(callback_called->load());
}

// 测试从std::future创建异步任务
TEST(AsyncTaskTest, FromFuture) {
    Scheduler scheduler(2);
    scheduler.start();

    auto done = std::make_shared<std::atomic<bool>>(false);
    auto result_value = std::make_shared<std::atomic<int>>(0);

    auto t = [done, result_value]() -> Task<> {
        auto promise = std::promise<int>();
        auto future = promise.get_future();

        auto task = AsyncTask<int>::from_future(std::move(future));

        // 在另一个线程中设置值
        std::thread([promise = std::move(promise)]() mutable {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            promise.set_value(999);
        }).detach();

        result_value->store(co_await task);
        done->store(true);
    }();

    scheduler.schedule(std::move(t));

    // 等待测试完成
    for (int i = 0; i < 200 && !done->load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    scheduler.stop();

    EXPECT_TRUE(done->load());
    EXPECT_EQ(result_value->load(), 999);
}

// 测试从std::future创建异步void任务
TEST(AsyncTaskTest, FromFutureVoid) {
    Scheduler scheduler(2);
    scheduler.start();

    std::atomic<bool> done{false};

    auto t = [&]() -> Task<> {
        auto promise = std::promise<void>();
        auto future = promise.get_future();

        auto task = AsyncTask<void>::from_future(std::move(future));

        // 在另一个线程中设置值
        std::thread([&promise]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            promise.set_value();
        }).detach();

        co_await task;
        done.store(true);
    }();

    scheduler.schedule(std::move(t));

    // 等待测试完成
    for (int i = 0; i < 200 && !done.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    scheduler.stop();

    EXPECT_TRUE(done.load());
}

// 测试异步任务的异常处理
TEST(AsyncTaskTest, ExceptionHandling) {
    Scheduler scheduler(2);
    scheduler.start();

    auto done = std::make_shared<std::atomic<bool>>(false);
    auto result_value = std::make_shared<std::atomic<int>>(0);

    auto t = [done, result_value]() -> Task<> {
        auto task = AsyncTask<int>::from_sync([]() -> int {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            throw std::runtime_error("Test exception");
        });

        try {
            result_value->store(co_await task);
            result_value->store(0); // 不应该到达这里
        } catch (const std::runtime_error& e) {
            EXPECT_STREQ(e.what(), "Test exception");
            result_value->store(-1);
        }
        done->store(true);
    }();

    scheduler.schedule(std::move(t));

    // 等待测试完成
    for (int i = 0; i < 200 && !done->load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    scheduler.stop();

    EXPECT_TRUE(done->load());
    EXPECT_EQ(result_value->load(), -1);
}

// 测试异步void任务的异常处理
TEST(AsyncTaskTest, ExceptionHandlingVoid) {
    Scheduler scheduler(2);
    scheduler.start();

    auto done = std::make_shared<std::atomic<bool>>(false);
    auto result_value = std::make_shared<std::atomic<int>>(0);

    auto t = [done, result_value]() -> Task<> {
        auto task = AsyncTask<void>::from_sync([]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            throw std::runtime_error("Void exception");
        });

        try {
            co_await task;
            result_value->store(0); // 不应该到达这里
        } catch (const std::runtime_error& e) {
            EXPECT_STREQ(e.what(), "Void exception");
            result_value->store(-1);
        }
        done->store(true);
    }();

    scheduler.schedule(std::move(t));

    // 等待测试完成
    for (int i = 0; i < 200 && !done->load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    scheduler.stop();

    EXPECT_TRUE(done->load());
    EXPECT_EQ(result_value->load(), -1);
}

// 测试make_async_task辅助函数
TEST(AsyncTaskTest, MakeAsyncTask) {
    Scheduler scheduler(2);
    scheduler.start();

    std::atomic<bool> done{false};
    std::string result_value;

    auto t = [&]() -> Task<> {
        auto task = make_async_task([]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            return std::string("Hello");
        });

        result_value = co_await task;
        done.store(true);
    }();

    scheduler.schedule(std::move(t));

    // 等待测试完成
    for (int i = 0; i < 200 && !done.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    scheduler.stop();

    EXPECT_TRUE(done.load());
    EXPECT_EQ(result_value, "Hello");
}

// 测试make_async_from_future辅助函数
TEST(AsyncTaskTest, MakeAsyncFromFuture) {
    Scheduler scheduler(2);
    scheduler.start();

    auto done = std::make_shared<std::atomic<bool>>(false);
    auto result_value = std::make_shared<std::atomic<double>>(0.0);

    auto t = [done, result_value]() -> Task<> {
        auto promise = std::promise<double>();
        auto future = promise.get_future();

        auto task = make_async_from_future(std::move(future));

        std::thread([promise = std::move(promise)]() mutable {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            promise.set_value(3.14159);
        }).detach();

        result_value->store(co_await task);
        done->store(true);
    }();

    scheduler.schedule(std::move(t));

    // 等待测试完成
    for (int i = 0; i < 200 && !done->load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    scheduler.stop();

    EXPECT_TRUE(done->load());
    EXPECT_DOUBLE_EQ(result_value->load(), 3.14159);
}

// 测试多个异步任务的并发执行
TEST(AsyncTaskTest, ConcurrentTasks) {
    Scheduler scheduler(2);
    scheduler.start();

    auto done = std::make_shared<std::atomic<bool>>(false);
    auto sum = std::make_shared<std::atomic<int>>(0);
    auto start_time = std::chrono::steady_clock::now();

    auto t = [done, sum]() -> Task<> {
        auto task1 = AsyncTask<int>::from_sync([]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            return 1;
        });

        auto task2 = AsyncTask<int>::from_sync([]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            return 2;
        });

        auto task3 = AsyncTask<int>::from_sync([]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            return 3;
        });

        int a = co_await task1;
        int b = co_await task2;
        int c = co_await task3;
        sum->store(a + b + c);
        done->store(true);
    }();

    scheduler.schedule(std::move(t));

    // 等待测试完成
    for (int i = 0; i < 200 && !done->load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    auto end_time = std::chrono::steady_clock::now();

    scheduler.stop();

    EXPECT_TRUE(done->load());
    // 并发执行应该比串行快
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    EXPECT_LT(duration.count(), 100); // 应该小于100ms（如果串行执行需要60ms）
    EXPECT_EQ(sum->load(), 6);
}

// 测试立即就绪的任务
TEST(AsyncTaskTest, ImmediateReady) {
    Scheduler scheduler(2);
    scheduler.start();

    auto done = std::make_shared<std::atomic<bool>>(false);
    auto result_value = std::make_shared<std::atomic<int>>(0);

    auto t = [done, result_value]() -> Task<> {
        auto task = AsyncTask<int>::from_sync([]() {
            return 777;
        });

        result_value->store(co_await task);
        done->store(true);
    }();

    scheduler.schedule(std::move(t));

    // 等待测试完成
    for (int i = 0; i < 200 && !done->load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    scheduler.stop();

    EXPECT_TRUE(done->load());
    EXPECT_EQ(result_value->load(), 777);
}