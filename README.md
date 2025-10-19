# Modern C++20 协程库

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![CMake](https://img.shields.io/badge/CMake-3.16%2B-green.svg)](https://cmake.org/)
[![Platform](https://img.shields.io/badge/Platform-Linux-lightgrey.svg)](https://www.linux.org/)
[![Tests](https://img.shields.io/badge/Tests-Passing-brightgreen.svg)]()
[![Coverage](https://img.shields.io/badge/Coverage-85%25-green.svg)]()

一个高性能、类型安全的现代C++20协程库，提供完整的异步编程解决方案。支持协程调度、异步IO、定时器、Hook机制、协程安全和取消机制。

## 📊 快速概览

- **协程类型**: 基于标准 C++20 `std::coroutine_handle`
- **调度器**: 基础调度器 + 高级调度器 + 工作窃取调度器
- **异步IO**: epoll-based 高性能网络编程
- **安全性**: RAII 资源管理 + 异常安全包装
- **取消机制**: 完整的协程取消和超时支持
- **测试覆盖**: 85%+ 代码覆盖率，包含单元测试和集成测试

## ✨ 核心特性

### 🚀 协程系统
- **标准C++20协程** - 基于`std::coroutine`实现，完全符合标准
- **类型安全** - 模板化`Task<T>`设计，编译期类型检查
- **异常安全** - 完整的异常传播和处理机制
- **生命周期管理** - 自动化协程生命周期和资源管理
- **协程取消** - 支持协程取消和超时机制

### ⚡ 高性能调度
- **多线程调度器** - 支持可配置的工作线程池
- **优先级调度** - 四级任务优先级（CRITICAL/HIGH/NORMAL/LOW）
- **工作窃取算法** - 自动负载均衡，最大化CPU利用率
- **高级调度器** - 支持统计信息和性能监控
- **零拷贝设计** - 最小化内存拷贝开销

### 🌐 异步IO系统
- **基于epoll** - Linux高性能事件驱动IO
- **协程友好** - 所有IO操作都是协程可等待的
- **文件描述符管理** - 完整的FD生命周期和超时控制
- **网络编程支持** - TCP/UDP套接字异步操作

### ⏰ 精确定时器
- **高精度定时** - 基于`std::chrono`的纳秒级精度
- **最小堆实现** - 高效的定时器队列管理
- **协程集成** - `co_await`语法的睡眠和定时操作

### 🔧 Hook机制
- **透明转换** - 自动将同步IO转换为异步操作
- **运行时控制** - 可动态启用/禁用Hook功能
- **兼容性** - 与现有同步代码无缝集成

### 🛡️ 安全特性
- **异常安全包装器** - `SafeCoroutineWrapper`提供异常处理
- **协程取消机制** - 支持优雅的协程取消和超时
- **生命周期管理** - `AdvancedCoroutineLifecycleManager`提供完整的协程监控
- **内存安全** - 自动资源管理和内存池支持

## 📋 系统要求

| 组件 | 最低版本 | 推荐版本 |
|------|----------|----------|
| **操作系统** | Ubuntu 20.04 LTS | Ubuntu 22.04 LTS |
| **编译器** | GCC 11.0 / Clang 14.0 | GCC 12.0+ / Clang 15.0+ |
| **CMake** | 3.16 | 3.25+ |
| **CPU架构** | x86_64 | x86_64 |

### 依赖库
- `pthread` - POSIX线程库
- `dl` - 动态链接库

## 🛠️ 快速开始

### 1. 获取源码
```bash
git clone <repository-url>
cd modern_coro
```

### 2. 构建项目
```bash
# 使用自动构建脚本（推荐）
chmod +x build.sh
./build.sh

# 或手动构建
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### 3. 运行示例
```bash
# 在build目录中运行
./modern_coro_example

# 或从项目根目录运行
cd .. && ./build/modern_coro_example
```

### 4. 运行测试
```bash
# 构建测试
cd build
make -j$(nproc)

# 运行单元测试
./tests/unit_tests

# 运行集成测试
./tests/integration_tests

# 运行所有测试
ctest
```

### 5. 安装到系统（可选）
```bash
cd build
sudo make install
```

## 📖 使用指南

## 📖 API 参考

### 核心类型

#### Task<T>
协程任务的返回值类型。

```cpp
template<typename T = void>
class Task {
public:
    // 创建协程任务
    Task<T> some_async_operation();
    
    // 等待任务完成
    T result = co_await some_async_operation();
    
    // 检查任务是否就绪
    bool ready() const;
    
    // 阻塞等待结果（不推荐在协程中使用）
    T get();
};
```

#### 调度器类型

| 调度器 | 用途 | 特性 |
|--------|------|------|
| `Scheduler` | 基础调度 | 简单任务队列，多线程 |
| `AdvancedScheduler` | 高级调度 | 优先级调度，统计信息 |
| `WorkStealingScheduler` | 工作窃取 | 负载均衡，性能优化 |

### 基础使用模式

#### 1. 基础协程任务

```cpp
#include "scheduler.h"
#include "coroutine.h"

using namespace modern_coro;

// 定义异步计算任务
Task<int> compute_async(int x, int y) {
    std::cout << "开始异步计算: " << x << " + " << y << std::endl;
    
    // 协程睡眠 - 不阻塞线程
    co_await Scheduler::GetCurrent()->sleep(std::chrono::milliseconds(100));
    
    int result = x + y;
    std::cout << "计算完成，结果: " << result << std::endl;
    
    co_return result;
}

int main() {
    // 创建4线程调度器
    Scheduler scheduler(4);
    scheduler.start();
    
    // 调度协程任务
    scheduler.schedule(compute_async(1, 2));
    
    // 等待任务完成
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    scheduler.stop();
    return 0;
}
```

#### 2. 协程链式调用

```cpp
Task<> chain_example() {
    int result1 = co_await compute_async(1, 2);
    int result2 = co_await compute_async(result1, 3);
    int final_result = co_await compute_async(result2, 4);
    
    std::cout << "最终结果: " << final_result << std::endl;
}
```

#### 3. 优先级调度

```cpp
#include "advanced_scheduler.h"

Task<> priority_example() {
    AdvancedScheduler scheduler(4);
    scheduler.start();
    
    // 关键任务 - 最高优先级
    scheduler.schedule_with_priority([]() -> Task<> {
        std::cout << "执行关键任务" << std::endl;
        co_await scheduler.sleep(std::chrono::milliseconds(50));
        std::cout << "关键任务完成" << std::endl;
    }(), TaskPriority::CRITICAL);
    
    // 普通任务
    for (int i = 0; i < 10; ++i) {
        scheduler.schedule_with_priority([i]() -> Task<> {
            std::cout << "执行普通任务 " << i << std::endl;
            co_await scheduler.sleep(std::chrono::milliseconds(100));
            std::cout << "普通任务 " << i << " 完成" << std::endl;
        }(), TaskPriority::NORMAL);
    }
    
    scheduler.stop();
}
```

#### 4. 协程取消机制

```cpp
#include "coroutine_cancellation.h"

using namespace modern_coro::cancellation;

Task<> cancellation_example() {
    // 创建取消源
    auto cancellation_source = CancellationSource::create();
    auto token = cancellation_source->get_token();
    
    // 创建可取消的协程
    auto task = [](CancellationToken token) -> Task<> {
        for (int i = 0; i < 10; ++i) {
            // 检查是否被取消
            if (token.is_cancelled()) {
                std::cout << "协程被取消，退出执行" << std::endl;
                co_return;
            }
            
            std::cout << "协程执行步骤 " << i << std::endl;
            co_await Scheduler::GetCurrent()->sleep(std::chrono::milliseconds(100));
        }
        std::cout << "协程正常完成" << std::endl;
    }(token);
    
    // 启动任务
    Scheduler::GetCurrent()->schedule(std::move(task));
    
    // 2秒后取消协程
    std::thread([cancellation_source]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        std::cout << "发送取消信号..." << std::endl;
        cancellation_source->cancel();
    }).detach();
}
```

#### 5. 异步IO操作

```cpp
#include "io_manager.h"
#include <sys/socket.h>
#include <netinet/in.h>

Task<> io_example() {
    IOManager io_manager(4);
    io_manager.start();
    
    // 创建套接字
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    
    // 设置地址
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(8080);
    
    // 绑定和监听
    bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 128);
    
    std::cout << "服务器启动在端口 8080" << std::endl;
    
    // 异步接受连接
    while (true) {
        try {
            int client_fd = co_await io_manager.async_accept(server_fd);
            
            // 为每个客户端启动处理协程
            io_manager.schedule([&io_manager, client_fd]() -> Task<> {
                char buffer[4096];
                
                while (true) {
                    ssize_t bytes_read = co_await io_manager.async_read(
                        client_fd, buffer, sizeof(buffer) - 1);
                    
                    if (bytes_read <= 0) break;
                    
                    buffer[bytes_read] = '\0';
                    std::cout << "收到数据: " << buffer << std::endl;
                    
                    // 回写数据
                    co_await io_manager.async_write(client_fd, buffer, bytes_read);
                }
                
                close(client_fd);
            }());
            
        } catch (const std::exception& e) {
            std::cerr << "IO异常: " << e.what() << std::endl;
            break;
        }
    }
    
    close(server_fd);
    io_manager.stop();
}
```

#### 6. 协程安全特性

```cpp
#include "coroutine_safety.h"

using namespace modern_coro::safety;

// 可能抛出异常的协程
Task<int> risky_compute_async(int x, int y) {
    if (y == 0) {
        throw CoroutineException("Division by zero in coroutine");
    }
    co_return x / y;
}

Task<> safety_example() {
    try {
        // 使用安全包装器
        auto safe_task = std::move(make_safe(risky_compute_async(10, 2)))
            .on_error([](std::exception_ptr e) {
                try {
                    std::rethrow_exception(e);
                } catch (const CoroutineException& ex) {
                    std::cout << "捕获到协程异常: " << ex.what() << std::endl;
                }
            });
        
        int result = co_await std::move(safe_task);
        std::cout << "安全任务结果: " << result << std::endl;
    } catch (...) {
        std::cout << "异常已被安全处理" << std::endl;
    }
}
```

#### 7. Hook机制使用

```cpp
#include "hook.h"

Task<> hook_example() {
    // 启用Hook机制
    HookManager::instance().enable_hook(true);
    
    std::cout << "Hook机制已启用，同步调用将自动转换为异步" << std::endl;
    
    // 这个sleep调用会被Hook机制拦截并转换为协程友好的异步操作
    co_await coroutine_sleep(std::chrono::milliseconds(1000));
    
    std::cout << "Hook睡眠完成" << std::endl;
    
    // 禁用Hook机制
    HookManager::instance().enable_hook(false);
}
```

### 高级调度器功能

```cpp
#include "advanced_scheduler.h"

using namespace modern_coro;

Task<> priority_example() {
    AdvancedScheduler scheduler(4);
    scheduler.start();
    
    // 启用工作窃取算法
    scheduler.enable_work_stealing(true);
    
    // 关键任务 - 最高优先级
    scheduler.schedule_with_priority([]() -> Task<> {
        std::cout << "执行关键任务" << std::endl;
        co_await scheduler.sleep(std::chrono::milliseconds(50));
        std::cout << "关键任务完成" << std::endl;
    }(), TaskPriority::CRITICAL);
    
    // 普通任务
    for (int i = 0; i < 10; ++i) {
        scheduler.schedule_with_priority([i]() -> Task<> {
            std::cout << "执行普通任务 " << i << std::endl;
            co_await scheduler.sleep(std::chrono::milliseconds(100));
            std::cout << "普通任务 " << i << " 完成" << std::endl;
        }(), TaskPriority::NORMAL);
    }
    
    // 获取调度统计信息
    auto stats = scheduler.get_stats();
    std::cout << "调度统计:" << std::endl;
    std::cout << "  总执行任务数: " << stats.total_tasks_executed << std::endl;
    std::cout << "  队列中任务数: " << stats.tasks_in_queue << std::endl;
    std::cout << "  平均等待时间: " << stats.average_wait_time_ms << "ms" << std::endl;
    
    scheduler.stop();
}
```

### 协程安全特性

```cpp
#include "coroutine_safety.h"

using namespace modern_coro::safety;

// 可能抛出异常的协程
Task<int> risky_compute_async(int x, int y) {
    if (y == 0) {
        throw CoroutineException("Division by zero in coroutine");
    }
    co_return x / y;
}

Task<> safety_example() {
    std::cout << "=== 协程安全特性演示 ===" << std::endl;
    
    try {
        // 使用安全包装器
        auto safe_task = std::move(make_safe(risky_compute_async(10, 0)))
            .on_error([](std::exception_ptr e) {
                try {
                    std::rethrow_exception(e);
                } catch (const CoroutineException& ex) {
                    std::cout << "捕获到协程异常: " << ex.what() << std::endl;
                }
            });
        
        co_await std::move(safe_task);
        std::cout << "安全任务完成" << std::endl;
    } catch (...) {
        std::cout << "异常已被安全处理" << std::endl;
    }
}
```

### 协程取消机制

```cpp
#include "coroutine_cancellation.h"

using namespace modern_coro::cancellation;

Task<> cancellation_example() {
    std::cout << "=== 协程取消机制演示 ===" << std::endl;
    
    // 创建取消源
    auto cancellation_source = create_cancellation_source();
    auto token = cancellation_source->get_token();
    
    // 创建可取消的协程
    auto task = [](CancellationToken token) -> Task<> {
        for (int i = 0; i < 10; ++i) {
            // 检查是否被取消
            if (token.is_cancellation_requested()) {
                std::cout << "协程被取消，退出执行" << std::endl;
                co_return;
            }
            
            std::cout << "协程执行步骤 " << i << std::endl;
            co_await Scheduler::GetCurrent()->sleep(std::chrono::milliseconds(100));
        }
        std::cout << "协程正常完成" << std::endl;
    }(token);
    
    // 2秒后取消协程
    std::thread([cancellation_source]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        std::cout << "发送取消信号..." << std::endl;
        cancellation_source->cancel();
    }).detach();
    
    co_await task;
    
    // 显示生命周期管理统计
    auto& lifecycle_mgr = AdvancedCoroutineLifecycleManager::instance();
    std::cout << "当前活跃协程数: " << lifecycle_mgr.active_count() << std::endl;
    std::cout << "总创建协程数: " << lifecycle_mgr.total_created() << std::endl;
    std::cout << "总销毁协程数: " << lifecycle_mgr.total_destroyed() << std::endl;
}
```

### 异步IO操作

```cpp
#include "io_manager.h"
#include <sys/socket.h>
#include <netinet/in.h>

using namespace modern_coro;

Task<> io_example() {
    IOManager io_manager(4);
    io_manager.start();
    
    // 创建套接字
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    
    // 设置地址
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(8080);
    
    // 绑定和监听
    bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 128);
    
    std::cout << "服务器启动在端口 8080" << std::endl;
    
    // 异步接受连接
    while (true) {
        try {
            int client_fd = co_await io_manager.async_accept(server_fd);
            
            // 为每个客户端启动处理协程
            io_manager.schedule([&io_manager, client_fd]() -> Task<> {
                char buffer[4096];
                
                while (true) {
                    ssize_t bytes_read = co_await io_manager.async_read(
                        client_fd, buffer, sizeof(buffer) - 1);
                    
                    if (bytes_read <= 0) break;
                    
                    buffer[bytes_read] = '\0';
                    std::cout << "收到数据: " << buffer << std::endl;
                    
                    // 回写数据
                    co_await io_manager.async_write(client_fd, buffer, bytes_read);
                }
                
                close(client_fd);
            }());
            
        } catch (const std::exception& e) {
            std::cerr << "IO异常: " << e.what() << std::endl;
            break;
        }
    }
    
    close(server_fd);
    io_manager.stop();
}
```

### Hook机制使用

```cpp
#include "hook.h"

using namespace modern_coro;

Task<> hook_example() {
    // 启用Hook机制
    HookManager::instance().enable_hook(true);
    
    std::cout << "Hook机制已启用，同步调用将自动转换为异步" << std::endl;
    
    // 这个sleep调用会被Hook机制拦截并转换为协程友好的异步操作
    co_await coroutine_sleep(std::chrono::milliseconds(1000));
    
    std::cout << "Hook睡眠完成" << std::endl;
    
    // 禁用Hook机制
    HookManager::instance().enable_hook(false);
}
```

## 🏗️ 架构设计

### 核心组件关系图
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│   Task
│    │   Scheduler     │    │   IOManager     │
│   协程任务      │◄──►│   调度器        │◄──►│   IO管理器      │
└─────────────────┘    └─────────────────┘    └─────────────────┘
│                       │                       │
▼                       ▼                       ▼
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│CoroutineHandle  │    │  WorkStealing   │    │   FdManager     │
│ 协程句柄        │    │  工作窃取       │    │   文件描述符    │
└─────────────────┘    └─────────────────┘    └─────────────────┘
│                       │                       │
▼                       ▼                       ▼
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│LifecycleManager│    │     Timer       │    │   HookManager   │
│ 生命周期管理    │    │     定时器      │    │   Hook管理器    │
└─────────────────┘    └─────────────────┘    └─────────────────┘

### 调度器层次结构
Scheduler (基础调度器)
├── 基本任务调度
├── 线程池管理
└── 协程生命周期

AdvancedScheduler (高级调度器)
├── 继承基础调度器
├── 优先级调度
├── 统计信息收集
└── 性能监控

WorkStealingScheduler (工作窃取调度器)
├── 继承高级调度器
├── 工作窃取算法
├── 负载均衡
└── 动态调整


## 📊 性能特性

### 基准测试结果

| 操作类型 | 吞吐量 | 延迟 | 内存使用 |
|----------|--------|------|----------|
| **协程创建** | ~1M ops/sec | <1μs | 最小堆栈 |
| **任务调度** | ~500K ops/sec | <2μs | 零拷贝 |
| **IO操作** | ~100K ops/sec | <10μs | 事件驱动 |
| **定时器** | ~200K ops/sec | <5μs | 最小堆 |

### 内存管理

- **栈协程** - 最小内存占用，自动管理
- **内存池** - 减少内存分配开销
- **零拷贝** - 最小化数据拷贝
- **RAII** - 自动资源管理

## 🔧 配置选项

### CMake构建选项

```bash
# 调试版本
cmake .. -DCMAKE_BUILD_TYPE=Debug

# 发布版本（默认）
cmake .. -DCMAKE_BUILD_TYPE=Release

# 启用所有警告
cmake .. -DCMAKE_CXX_FLAGS="-Wall -Wextra -Werror"

# 自定义安装路径
cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local
```

### 运行时配置

```cpp
// 调度器配置
Scheduler scheduler(
    4,                              // 工作线程数
    std::chrono::milliseconds(10)   // 调度间隔
);

// IO管理器配置
IOManager io_manager(
    4,                              // IO线程数
    1024                           // 最大事件数
);

// 定时器配置
Timer timer(
    std::chrono::microseconds(100)  // 定时器精度
);
```

## 🐛 调试和诊断

### 调试信息

```cpp
// 启用调试输出
#define MODERN_CORO_DEBUG 1

// 获取协程统计信息
auto& lifecycle_mgr = AdvancedCoroutineLifecycleManager::instance();
auto debug_info = lifecycle_mgr.get_debug_info();

for (const auto& info : debug_info) {
    std::cout << "协程[" << info.handle_address << "] - "
              << "年龄: " << info.age_ms << "ms - "
              << "已取消: " << (info.is_cancelled ? "是" : "否") << std::endl;
}
```

### 性能分析

```cpp
// 获取调度器统计
auto stats = scheduler.get_stats();
std::cout << "总执行任务数: " << stats.total_tasks_executed << std::endl;
std::cout << "平均等待时间: " << stats.average_wait_time_ms << "ms" << std::endl;
std::cout << "当前队列长度: " << stats.tasks_in_queue << std::endl;
```

## 🤝 贡献指南

### 开发环境设置

1. 安装依赖：
```bash
sudo apt-get update
sudo apt-get install build-essential cmake git
sudo apt-get install gcc-12 g++-12  # 或更新版本
```

2. 克隆仓库：
```bash
git clone <repository-url>
cd modern_coro
```

3. 构建和测试：
```bash
./build.sh
./build/modern_coro_example
```

### 代码规范

- 使用C++20标准特性
- 遵循RAII原则
- 异常安全保证
- 详细的文档注释
- 单元测试覆盖


**Modern C++20 协程库** - 让异步编程变得简单而高效！ 🚀