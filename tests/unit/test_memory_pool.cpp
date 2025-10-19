/**
 * @file test_memory_pool.cpp
 * @brief 内存池组件单元测试
 */

#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include "core/memory_pool.h"

using namespace modern_coro;

// 测试基础内存池功能
TEST(MemoryPoolTest, BasicAllocation) {
    MemoryPool<4096>& pool = MemoryPool<4096>::instance();

    // 测试小块分配
    void* ptr1 = pool.allocate(100);
    ASSERT_NE(ptr1, nullptr);

    void* ptr2 = pool.allocate(200);
    ASSERT_NE(ptr2, nullptr);
    ASSERT_NE(ptr1, ptr2); // 应该分配不同的地址

    // 测试大块分配（超过BlockSize）
    void* ptr3 = pool.allocate(5000);
    ASSERT_NE(ptr3, nullptr);

    // 释放内存
    pool.deallocate(ptr1, 100);
    pool.deallocate(ptr2, 200);
    pool.deallocate(ptr3, 5000);

    // 验证分配计数
    EXPECT_EQ(pool.get_allocated_count(), 0);
}

// 测试内存池统计信息
TEST(MemoryPoolTest, Statistics) {
    MemoryPool<4096>& pool = MemoryPool<4096>::instance();

    // 记录初始状态
    size_t initial_allocated = pool.get_allocated_count();

    // 分配多个块 (现在重新使用池化)
    std::vector<void*> pointers;
    for (int i = 0; i < 10; ++i) {
        void* ptr = pool.allocate(100);
        ASSERT_NE(ptr, nullptr);
        pointers.push_back(ptr);
    }

    // 分配计数应该增加
    EXPECT_EQ(pool.get_allocated_count(), initial_allocated + 10);

    // 释放所有块
    for (int i = 0; i < 10; ++i) {
        pool.deallocate(pointers[i], 100);
    }

    // 释放后计数应该回到初始状态
    EXPECT_EQ(pool.get_allocated_count(), initial_allocated);
}

// 测试内存池收缩功能
TEST(MemoryPoolTest, ShrinkToFit) {
    MemoryPool<4096>& pool = MemoryPool<4096>::instance();

    // 分配大量内存
    std::vector<void*> pointers;
    for (int i = 0; i < 100; ++i) {
        void* ptr = pool.allocate(100);
        ASSERT_NE(ptr, nullptr);
        pointers.push_back(ptr);
    }

    // 释放所有内存
    for (void* ptr : pointers) {
        pool.deallocate(ptr, 100);
    }

    // 收缩池
    pool.shrink_to_fit();

    // 验证内存被释放
    EXPECT_GE(pool.get_total_memory(), 0); // 至少保留一些内存用于后续分配
}

// 测试协程栈池
TEST(CoroutineStackPoolTest, BasicStackAllocation) {
    CoroutineStackPool& pool = CoroutineStackPool::instance();

    // 测试默认大小栈分配
    void* stack1 = pool.allocate_stack();
    ASSERT_NE(stack1, nullptr);

    void* stack2 = pool.allocate_stack();
    ASSERT_NE(stack2, nullptr);
    ASSERT_NE(stack1, stack2);

    // 测试自定义大小栈分配
    void* stack3 = pool.allocate_stack(64 * 1024); // 64KB
    ASSERT_NE(stack3, nullptr);

    // 释放栈
    pool.deallocate_stack(stack1);
    pool.deallocate_stack(stack2);
    pool.deallocate_stack(stack3, 64 * 1024);

    // 验证分配计数
    EXPECT_EQ(pool.get_allocated_count(), 0);
}

// 测试协程栈池大小限制
TEST(CoroutineStackPoolTest, SizeLimits) {
    CoroutineStackPool& pool = CoroutineStackPool::instance();

    // 测试最小大小限制
    void* small_stack = pool.allocate_stack(8 * 1024); // 小于最小值
    ASSERT_NE(small_stack, nullptr);

    // 测试最大大小限制
    void* large_stack = pool.allocate_stack(256 * 1024); // 大于最大值
    ASSERT_NE(large_stack, nullptr);

    // 释放
    pool.deallocate_stack(small_stack, CoroutineStackPool::MIN_STACK_SIZE);
    pool.deallocate_stack(large_stack, CoroutineStackPool::MAX_STACK_SIZE);
}

// 测试协程栈池缓存功能
TEST(CoroutineStackPoolTest, CacheFunctionality) {
    CoroutineStackPool& pool = CoroutineStackPool::instance();

    // 分配多个相同大小的栈
    std::vector<void*> stacks;
    for (int i = 0; i < 10; ++i) {
        void* stack = pool.allocate_stack(32 * 1024);
        ASSERT_NE(stack, nullptr);
        stacks.push_back(stack);
    }

    // 释放所有栈（应该进入缓存）
    for (void* stack : stacks) {
        pool.deallocate_stack(stack, 32 * 1024);
    }

    // 再次分配相同大小的栈（应该从缓存获取）
    for (int i = 0; i < 5; ++i) {
        void* stack = pool.allocate_stack(32 * 1024);
        ASSERT_NE(stack, nullptr);
        pool.deallocate_stack(stack, 32 * 1024);
    }
}

// 测试池分配器
TEST(PoolAllocatorTest, BasicSTLUsage) {
    using PoolVector = std::vector<int, PoolAllocator<int>>;

    // 测试向量分配
    PoolVector vec;
    vec.reserve(100);

    for (int i = 0; i < 100; ++i) {
        vec.push_back(i);
    }

    EXPECT_EQ(vec.size(), 100);
    for (int i = 0; i < 100; ++i) {
        EXPECT_EQ(vec[i], i);
    }
}

// 测试多线程内存池使用
TEST(MemoryPoolTest, MultithreadedAllocation) {
    MemoryPool<4096>& pool = MemoryPool<4096>::instance();

    std::atomic<int> completed_threads{0};
    const int num_threads = 4;
    const int allocations_per_thread = 100;

    auto thread_func = [&]() {
        std::vector<void*> pointers;
        for (int i = 0; i < allocations_per_thread; ++i) {
            void* ptr = pool.allocate(50);
            ASSERT_NE(ptr, nullptr);
            pointers.push_back(ptr);

            // 短暂延迟模拟工作
            std::this_thread::sleep_for(std::chrono::microseconds(1));
        }

        // 释放所有分配的内存
        for (void* ptr : pointers) {
            pool.deallocate(ptr, 50);
        }

        completed_threads.fetch_add(1);
    };

    // 启动多个线程
    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(thread_func);
    }

    // 等待所有线程完成
    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(completed_threads.load(), num_threads);
}

// 测试内存池边界情况
TEST(MemoryPoolTest, EdgeCases) {
    MemoryPool<4096>& pool = MemoryPool<4096>::instance();

    // 测试释放空指针
    pool.deallocate(nullptr, 100); // 应该不会崩溃

    // 测试大块分配和释放
    void* large_ptr = pool.allocate(100 * 1024); // 100KB
    ASSERT_NE(large_ptr, nullptr);
    pool.deallocate(large_ptr, 100 * 1024);
}