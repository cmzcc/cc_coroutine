#pragma once

#include <memory>
#include <vector>
#include <mutex>
#include <atomic>
#include <cstddef>
#include <stack>
#include <algorithm>

namespace modern_coro {

// 内存池管理器 - 优化版本
template<size_t BlockSize = 4096>
class MemoryPool {
public:
    static MemoryPool& instance() {
        static MemoryPool pool;
        return pool;
    }
    
    void* allocate(size_t size) {
        if (size > BlockSize) {
            return std::aligned_alloc(alignof(std::max_align_t), size);
        }
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (free_blocks_.empty()) {
            if (!allocate_chunk()) {
                return nullptr;
            }
        }
        
        void* ptr = free_blocks_.back();
        free_blocks_.pop_back();
        allocated_blocks_.fetch_add(1, std::memory_order_relaxed);
        return ptr;
    }
    
    void deallocate(void* ptr, size_t size) {
        if (!ptr) return;
        
        if (size > BlockSize) {
            std::free(ptr);
            return;
        }
        
        std::lock_guard<std::mutex> lock(mutex_);
        free_blocks_.push_back(ptr);
        allocated_blocks_.fetch_sub(1, std::memory_order_relaxed);
        
        // 如果空闲块过多，释放一些内存
        if (free_blocks_.size() > max_free_blocks_) {
            shrink_pool();
        }
    }
    
    size_t get_allocated_count() const {
        return allocated_blocks_.load(std::memory_order_relaxed);
    }
    
    size_t get_total_memory() const {
        return chunks_.size() * BlockSize * blocks_per_chunk_;
    }
    
    void shrink_to_fit() {
        std::lock_guard<std::mutex> lock(mutex_);
        shrink_pool();
    }
    
private:
    static constexpr size_t blocks_per_chunk_ = 256; // 减少每个chunk的块数
    static constexpr size_t max_free_blocks_ = 512;  // 最大空闲块数
    
    MemoryPool() { 
        // 预分配一个chunk
        allocate_chunk(); 
    }
    
    ~MemoryPool() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (void* chunk : chunks_) {
            std::free(chunk);
        }
    }
    
    bool allocate_chunk() {
        const size_t chunk_size = BlockSize * blocks_per_chunk_;
        void* chunk = std::aligned_alloc(alignof(std::max_align_t), chunk_size);
        
        if (!chunk) {
            return false;
        }
        
        chunks_.push_back(chunk);
        
        char* ptr = static_cast<char*>(chunk);
        for (size_t i = 0; i < blocks_per_chunk_; ++i) {
            free_blocks_.push_back(ptr + i * BlockSize);
        }
        
        return true;
    }
    
    void shrink_pool() {
        // 简单的收缩策略：如果空闲块太多，移除一些
        while (free_blocks_.size() > max_free_blocks_ / 2 && !free_blocks_.empty()) {
            free_blocks_.pop_back();
        }
    }
    
    std::vector<void*> chunks_;
    std::vector<void*> free_blocks_;
    std::mutex mutex_;
    std::atomic<size_t> allocated_blocks_{0};
};

// 协程栈池 - 优化版本
class CoroutineStackPool {
public:
    static constexpr size_t DEFAULT_STACK_SIZE = 32 * 1024; // 减少到32KB
    static constexpr size_t MIN_STACK_SIZE = 16 * 1024;     // 最小16KB
    static constexpr size_t MAX_STACK_SIZE = 128 * 1024;    // 最大128KB
    
    static CoroutineStackPool& instance() {
        static CoroutineStackPool pool;
        return pool;
    }
    
    void* allocate_stack(size_t size = DEFAULT_STACK_SIZE) {
        // 确保栈大小在合理范围内
        size = std::clamp(size, MIN_STACK_SIZE, MAX_STACK_SIZE);
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        // 尝试从缓存中获取合适大小的栈
        auto it = free_stacks_.find(size);
        if (it != free_stacks_.end() && !it->second.empty()) {
            void* ptr = it->second.top();
            it->second.pop();
            allocated_stacks_.fetch_add(1, std::memory_order_relaxed);
            return ptr;
        }
        
        // 分配新栈
        void* ptr = std::aligned_alloc(alignof(std::max_align_t), size);
        if (ptr) {
            allocated_stacks_.fetch_add(1, std::memory_order_relaxed);
            total_allocated_memory_.fetch_add(size, std::memory_order_relaxed);
        }
        return ptr;
    }
    
    void deallocate_stack(void* ptr, size_t size = DEFAULT_STACK_SIZE) {
        if (!ptr) return;
        
        size = std::clamp(size, MIN_STACK_SIZE, MAX_STACK_SIZE);
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        // 如果缓存未满，加入缓存
        auto& stack_cache = free_stacks_[size];
        if (stack_cache.size() < max_cached_stacks_per_size_) {
            stack_cache.push(ptr);
            allocated_stacks_.fetch_sub(1, std::memory_order_relaxed);
        } else {
            // 缓存已满，直接释放
            std::free(ptr);
            allocated_stacks_.fetch_sub(1, std::memory_order_relaxed);
            total_allocated_memory_.fetch_sub(size, std::memory_order_relaxed);
        }
    }
    
    size_t get_allocated_count() const {
        return allocated_stacks_.load(std::memory_order_relaxed);
    }
    
    size_t get_total_memory() const {
        return total_allocated_memory_.load(std::memory_order_relaxed);
    }
    
    void clear_cache() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [size, stack_cache] : free_stacks_) {
            while (!stack_cache.empty()) {
                std::free(stack_cache.top());
                stack_cache.pop();
                total_allocated_memory_.fetch_sub(size, std::memory_order_relaxed);
            }
        }
        free_stacks_.clear();
    }
    
private:
    static constexpr size_t max_cached_stacks_per_size_ = 50;
    
    CoroutineStackPool() = default;
    
    ~CoroutineStackPool() {
        clear_cache();
    }
    
    std::unordered_map<size_t, std::stack<void*>> free_stacks_;
    std::mutex mutex_;
    std::atomic<size_t> allocated_stacks_{0};
    std::atomic<size_t> total_allocated_memory_{0};
};

// 池分配器
template<typename T>
class PoolAllocator {
public:
    using value_type = T;
    
    PoolAllocator() = default;
    
    template<typename U>
    PoolAllocator(const PoolAllocator<U>&) noexcept {}
    
    T* allocate(size_t n) {
        return static_cast<T*>(MemoryPool<sizeof(T)>::instance().allocate(n * sizeof(T)));
    }
    
    void deallocate(T* ptr, size_t n) {
        MemoryPool<sizeof(T)>::instance().deallocate(ptr, n * sizeof(T));
    }
    
    template<typename U>
    bool operator==(const PoolAllocator<U>&) const noexcept {
        return true;
    }
    
    template<typename U>
    bool operator!=(const PoolAllocator<U>&) const noexcept {
        return false;
    }
};

} // namespace modern_coro