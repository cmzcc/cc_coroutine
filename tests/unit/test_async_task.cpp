/**
 * @file test_async_task.cpp
 * @brief 异步任务组件单元测试
 */

#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <future>
#include <atomic>
#include "core/async_task.h"
#include "scheduler/scheduler.h"

using namespace modern_coro;

// 测试从同步函数创建异步任务
TEST(AsyncTaskTest, FromSyncFunction) {
    std::cout << "[TEST] Starting FromSyncFunction test" << std::endl;
    
    Scheduler scheduler(2);
    scheduler.start();
    std::cout << "[TEST] Scheduler started" << std::endl;

    std::atomic<bool> done{false};
    int result_value = 0;

    // 创建协程任务
    Task<> coro_task = [&]() -> Task<> {
        std::cout << "[TEST] Coroutine started" << std::endl;
        // 测试有返回值的情况
        auto task = AsyncTask<int>::from_sync([]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            return 42;
        });

        std::cout << "[TEST] About to co_await AsyncTask" << std::endl;
        // 在协程中等待异步任务
        try {
            result_value = co_await task;
            std::cout << "[TEST] co_await completed successfully" << std::endl;
            std::cout << "[TEST] AsyncTask completed with result: " << result_value << std::endl;
        } catch (const std::exception& e) {
            std::cout << "[TEST] Exception during co_await: " << e.what() << std::endl;
            throw;
        } catch (...) {
            std::cout << "[TEST] Unknown exception during co_await" << std::endl;
            throw;
        }
        
        std::cout << "[TEST] Setting done flag" << std::endl;
        std::cout << "[TEST] Coroutine got result: " << result_value << std::endl;
        done.store(true);
        std::cout << "[TEST] Done flag set" << std::endl;
        std::cout << "[TEST] Coroutine completed" << std::endl;
        std::cout << "[TEST] About to co_return" << std::endl;
        co_return;
    }();

    std::cout << "[TEST] Scheduling coroutine" << std::endl;
    scheduler.schedule(std::move(coro_task));

    // 等待测试完成
    std::cout << "[TEST] Waiting for completion" << std::endl;
    for (int i = 0; i < 200 && !done.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    std::cout << "[TEST] Test completed, stopping scheduler" << std::endl;
    
    // 给协程一些时间完成清理
    std::cout << "[TEST] About to sleep before shutdown" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    std::cout << "[TEST] Initiating scheduler shutdown" << std::endl;
    scheduler.stop();
    std::cout << "[TEST] Scheduler stopped" << std::endl;

    EXPECT_TRUE(done.load());
    EXPECT_EQ(result_value, 42);
    std::cout << "[TEST] Test finished successfully" << std::endl;
}

// 测试从同步函数创建异步void任务
TEST(AsyncTaskTest, FromSyncVoidFunction) {
    Scheduler scheduler(2);
    scheduler.start();

    std::atomic<bool> done{false};
    bool executed = false;

    auto t = [&]() -> Task<> {
        auto task = AsyncTask<void>::from_sync([&executed]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            executed = true;
        });

        // 在协程中等待异步任务
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
    EXPECT_TRUE(executed);
}

// 测试从回调函数创建异步任务
TEST(AsyncTaskTest, FromCallbackFunction) {
    Scheduler scheduler(2);
    scheduler.start();

    std::atomic<bool> done{false};
    int result_value = 0;

    auto t = [&]() -> Task<> {
        auto task = AsyncTask<int>::from_callback([](auto callback) {
            std::thread([callback = std::move(callback)]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                callback(123);
            }).detach();
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
    EXPECT_EQ(result_value, 123);
}

// 测试从回调函数创建异步void任务
TEST(AsyncTaskTest, FromCallbackVoidFunction) {
    Scheduler scheduler(2);
    scheduler.start();

    std::atomic<bool> done{false};
    bool callback_called = false;

    auto t = [&]() -> Task<> {
        auto task = AsyncTask<void>::from_callback([&](auto callback) {
            std::thread([&, callback = std::move(callback)]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                callback_called = true;
                callback();
            }).detach();
        });

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
    EXPECT_TRUE(callback_called);
}

// 测试从std::future创建异步任务
TEST(AsyncTaskTest, FromFuture) {
    Scheduler scheduler(2);
    scheduler.start();

    std::atomic<bool> done{false};
    int result_value = 0;

    auto t = [&]() -> Task<> {
        auto promise = std::promise<int>();
        auto future = promise.get_future();

        auto task = AsyncTask<int>::from_future(std::move(future));

        // 在另一个线程中设置值
        std::thread([&promise]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            promise.set_value(999);
        }).detach();

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
    EXPECT_EQ(result_value, 999);
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

    std::atomic<bool> done{false};
    int result_value = 0;

    auto t = [&]() -> Task<> {
        auto task = AsyncTask<int>::from_sync([]() -> int {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            throw std::runtime_error("Test exception");
        });

        try {
            co_await task;
            result_value = 0; // 不应该到达这里
        } catch (const std::runtime_error& e) {
            EXPECT_STREQ(e.what(), "Test exception");
            result_value = -1;
        }
        done.store(true);
    }();

    scheduler.schedule(std::move(t));

    // 等待测试完成
    for (int i = 0; i < 200 && !done.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    scheduler.stop();

    EXPECT_TRUE(done.load());
    EXPECT_EQ(result_value, -1);
}

// 测试异步void任务的异常处理
TEST(AsyncTaskTest, ExceptionHandlingVoid) {
    Scheduler scheduler(2);
    scheduler.start();

    std::atomic<bool> done{false};
    int result_value = 0;

    auto t = [&]() -> Task<> {
        auto task = AsyncTask<void>::from_sync([]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            throw std::runtime_error("Void exception");
        });

        try {
            co_await task;
            result_value = 0; // 不应该到达这里
        } catch (const std::runtime_error& e) {
            EXPECT_STREQ(e.what(), "Void exception");
            result_value = -1;
        }
        done.store(true);
    }();

    scheduler.schedule(std::move(t));

    // 等待测试完成
    for (int i = 0; i < 200 && !done.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    scheduler.stop();

    EXPECT_TRUE(done.load());
    EXPECT_EQ(result_value, -1);
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

    std::atomic<bool> done{false};
    double result_value = 0.0;

    auto t = [&]() -> Task<> {
        auto promise = std::promise<double>();
        auto future = promise.get_future();

        auto task = make_async_from_future(std::move(future));

        std::thread([&promise]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            promise.set_value(3.14159);
        }).detach();

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
    EXPECT_DOUBLE_EQ(result_value, 3.14159);
}

// 测试多个异步任务的并发执行
TEST(AsyncTaskTest, ConcurrentTasks) {
    Scheduler scheduler(2);
    scheduler.start();

    std::atomic<bool> done{false};
    int sum = 0;
    auto start_time = std::chrono::steady_clock::now();

    auto t = [&]() -> Task<> {
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
        sum = a + b + c;
        done.store(true);
    }();

    scheduler.schedule(std::move(t));

    // 等待测试完成
    for (int i = 0; i < 200 && !done.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    auto end_time = std::chrono::steady_clock::now();

    scheduler.stop();

    EXPECT_TRUE(done.load());
    // 并发执行应该比串行快
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    EXPECT_LT(duration.count(), 100); // 应该小于100ms（如果串行执行需要60ms）
    EXPECT_EQ(sum, 6);
}

// 测试立即就绪的任务
TEST(AsyncTaskTest, ImmediateReady) {
    Scheduler scheduler(2);
    scheduler.start();

    std::atomic<bool> done{false};
    int result_value = 0;

    auto t = [&]() -> Task<> {
        auto task = AsyncTask<int>::from_sync([]() {
            return 42;
        });

        result_value = co_await task;
        result_value *= 2;
        done.store(true);
    }();

    scheduler.schedule(std::move(t));

    // 等待测试完成
    for (int i = 0; i < 200 && !done.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    scheduler.stop();

    EXPECT_TRUE(done.load());
    EXPECT_EQ(result_value, 84);
}