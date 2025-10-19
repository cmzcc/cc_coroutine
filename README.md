
## modern_coro — 现代 C++20 协程并发与异步 I/O 库

modern_coro 是一个以 C++20 协程为核心的轻量级并发与异步 I/O 库。它提供可组合的 Task<T> 抽象、可扩展的多线程调度器（含工作窃取），以及基于 epoll 的高性能异步 I/O；同时对不支持 epoll 的环境提供安全的同步回退机制，确保在受限环境也能稳定运行。

- C++20 协程：Task<T>、co_await、异常传播与生命周期管理
- 多线程调度：基本调度器 + 高级调度器 + 工作窃取
- 异步 I/O：epoll（边缘触发）驱动；epoll 不可用时自动回退
- 安全与健壮性：线程安全内存池、异常封装、安全系统调用包装、取消/超时
- 测试完备：单元测试 + 集成测试，覆盖调度/计时器/异步 I/O/并发行为

示例场景：高并发网络服务原型、协程编程实践、I/O 密集并发任务编排。

---

## 目录

- 系统要求与依赖
- 快速开始（构建/测试/运行）
- 重要概念与架构
- 模块详解
  - Task<T> 与协程模型
  - Scheduler/Advanced/Work-stealing
  - IOManager（epoll 模式与同步回退）
  - Timer（sleep）
  - MemoryPool 与栈池
  - 安全模块与异常约定
- API 快速参考
- 代码示例
  - Echo 服务器/客户端（简化版）
  - 并发任务/工作窃取示例
- 使用注意事项与最佳实践
- 性能与调试
- 常见问题（FAQ）
- 贡献与路线图

---

## 系统要求与依赖

- 平台：Linux（建议 Ubuntu 20.04+）
- 编译器：GCC 11+ 或 Clang 14+（需完整 C++20 协程支持）
- CMake：3.16+
- 线程库：pthread
- 测试框架：GoogleTest（通过 vcpkg 或系统包安装；构建脚本已集成）

---

## 快速开始

1) 使用脚本构建（推荐）

```bash
chmod +x ./build.sh
./build.sh
```

脚本会：
- 清理/创建 build 目录
- 运行 CMake 配置（自动检测 vcpkg toolchain）
- 并行编译库与测试
- 运行测试（ctest）

产物：
- 动态库：build/libmodern_coro.so*
- 测试：build/tests/unit_tests、build/tests/integration_tests

2) 手动构建（可选）

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo
make -j$(nproc)
ctest -V
```

3) 运行全部测试

```bash
cd build
ctest -V
# 或者
./tests/unit_tests
./tests/integration_tests
```

---

## 重要概念与架构

- Task<T>：协程函数返回类型，代表异步计算结果，可 co_await。支持异常向上传播，资源生命周期与 coroutine_handle 管理健全。
- Scheduler：多线程任务调度器，负责任务队列、线程管理与任务恢复；Advanced/Work-stealing 提供优先级与跨队列负载均衡能力。
- IOManager：继承自 Scheduler，集成 epoll 驱动的异步 I/O 事件循环与协程恢复；当 epoll 不可用时，自动回退到“同步模式”，在独立线程中执行阻塞系统调用，避免阻塞调度器线程。
- Timer：基于 chrono 的睡眠/定时，co_await 友好。
- MemoryPool：线程安全内存池与栈池，降低频繁分配释放的开销。
- Safety：系统调用安全封装（CHECK_SYSCALL/safe_syscall）、异常类型（IOException 等）、取消与超时语义。

线程模型：
- 调度器 N 个工作线程 + IOManager 的专用 IO 线程（epoll 模式）
- 协程恢复在正确线程上下文中进行，避免跨线程数据竞争

---

## 模块详解

### 1) Task<T> 与协程模型
- 使用标准 C++20 协程（promise_type/awaitable）
- co_await 等待 Task 的结果；异常自动传播
- 生命周期由调度器与 awaiter 管理；避免悬挂与重复 resume

典型模式：
- 从同步函数创建 Task（包装）
- 从 future/callback 创建 Task
- 并发等待多个 Task

实现要点：
- promise_type 负责产出结果/异常；在协程结束时设置完成状态；
- awaiter 保存 `std::coroutine_handle<>`，在 I/O 或定时器事件就绪时恢复；
- 避免重复 resume：awaiter 内部只允许一次恢复，防止未定义行为；
- 与调度器配合：schedule 将任务移交给正确的工作线程执行。

详见 tests/unit/test_task.cpp、test_async_task.cpp。

### 2) Scheduler/Advanced/Work-stealing
- Scheduler：基本任务队列 + 多线程执行 + 安全停止
- Advanced：优先级（可选）、统计信息
- Work-stealing：每线程双端队列，空闲线程从他人队列尾部偷取，提升吞吐与负载均衡

验证参考 tests/integration/test_work_stealing.cpp、test_concurrent_tasks.cpp、unit/test_scheduler.cpp。

实现要点：
- 基础 Scheduler 提供线程池 + 任务队列；线程安全推送/弹出；
- Advanced 可扩展优先级队列（如 CRITICAL/HIGH/NORMAL/LOW），并统计执行/窃取等指标；
- Work-stealing 每线程双端队列：本线程 LIFO、他线程从队尾窃取 FIFO，改善缓存局部性与吞吐；
- 线程退出策略：优雅停止，确保无悬挂任务；
- 与 IOManager 的关系：IOManager 也是一个 Scheduler，但额外维护 IO 线程与 epoll 事件循环。

### 3) IOManager（核心）
- 角色：调度器 + I/O 事件中心
- epoll 模式：
  - 创建 epoll fd；把需要的 fd 注册到 epoll（边缘触发）
  - 事件到来后在 IO 线程 resume 对应协程
  - 关键注意：首次注册必须使用 EPOLL_CTL_ADD；后续才是 EPOLL_CTL_MOD
- 同步回退模式：
  - 若 epoll不可用或运行中出现不可恢复错误，IOManager 自动回退（内部 epfd_ == -1）
  - async_read/write/accept/connect 将在独立线程（std::async）执行阻塞系统调用，避免阻塞调度器线程
  - 对外 API 保持一致：仍然 co_await 这些函数

提供的方法（见 include/io/io_manager.h、src/io/io_manager.cpp）：
- Task<ssize_t> async_read(int fd, void* buf, size_t size)
- Task<ssize_t> async_write(int fd, const void* buf, size_t size)
- Task<int>     async_accept(int listen_fd)
- Task<int>     async_connect(int sockfd, const sockaddr* addr, socklen_t addrlen)
- void          add_event(int fd, Event, std::coroutine_handle<>)
- void          del_event(int fd, Event)
- bool          is_sync_mode() const

边界条件：
- 非阻塞设置：epoll 模式下，会使用 fcntl 将 fd 设为 O_NONBLOCK
- 循环 I/O：在边缘触发模式下，读/写应尽量在单次 resume 循环处理到 EAGAIN，避免重复事件与饥饿
- 正确的 ADD/MOD：先判断 fd 是否已在上下文，选择 EPOLL_CTL_ADD 或 EPOLL_CTL_MOD；错误地使用 operator[] 后再判断会导致误判，出现 “modify fd in epoll: No such file or directory”

稳定性改进（已落实）：
- 修复了 ADD/MOD 逻辑顺序，避免 ENOENT
- 在同步模式下使用 std::async 执行阻塞调用，避免调度器死锁
- 协程在 IO 线程的正确恢复，避免跨线程 resume

更多细节与边界：
- 管道自唤醒：内部使用管道 fd 唤醒 epoll 等待（便于 stop/shutdown）；
- EPOLLET 模式：务必在一次 resume 中反复读/写直到 EAGAIN；
- 错误处理：I/O 返回 -1 时区分 EAGAIN/EINTR 与致命错误；仅在致命错误打印错误日志；
- fd 生命周期：确保关闭 fd 前从 epoll 移除并清理回调句柄，避免悬挂指针；
- 同步回退：`is_sync_mode()` 可用于测试/诊断；回退状态下 async_* 通过 async 线程执行阻塞 I/O；
- 并发安全：`fd_contexts_` 由互斥保护；add/del_event 原子性维护句柄；
- 错误日志：`Failed to manage epoll event` 表示 epoll_ctl 失败，应检查 ADD/MOD 时机与 fd 有效性。

验证参考 tests/unit/test_io_manager.cpp、tests/integration/test_echo_server.cpp。

### 4) Timer（sleep）
- 提供协程友好的 sleep_for/sleep_until 行为（以 awaitable 形式）
- 用于在任务链中非阻塞等待时间
- 参考 test_timer.cpp

实现要点：
- 基于最小堆/时间轮（按项目实现为准），到期后恢复挂起协程；
- 与调度器线程集成，避免 busy-wait；
- 支持取消/超时传播（与安全模块联动）。

### 5) MemoryPool 与栈池
- 线程安全，避免多线程分配竞争造成的破坏
- 可统计分配使用、收缩（shrink_to_fit）
- 参考 tests/unit/test_memory_pool.cpp、CoroutineStackPoolTest

实现要点：
- 线程安全：分配/回收受互斥量保护；
- 小对象聚合：减少系统 malloc/free 调用次数；
- 峰值回落：提供 shrink_to_fit 以在压力后回收缓存；
- 与协程栈：可选的栈缓存策略，降低频繁创建/销毁栈的成本。

### 6) 安全模块
- 安全系统调用包装：safe_syscall/CHECK_SYSCALL 捕获错误并转化为异常（IOException 等）
- 取消/超时语义：CancellationToken、异常类型（CoroutineTimeoutException 等），见 tests/unit/test_cancellation.cpp、test_safety.cpp
- 资源 RAII 与 FdGuard 等

实现要点：
- `safe_syscall` 封装 errno -> 异常的转换与附加上下文信息；
- 定义专门异常类型（IOException/Timeout/Cancelled），清晰表达错误语义；
- 取消传播：CancellationToken 在 await 前检查/在恢复后再次检查；
- 系统资源：提供 FdGuard/ScopeGuard 防止异常路径泄漏。

---

## API 快速参考

类 IOManager（继承 Scheduler）
- 构造：IOManager(size_t thread_count = std::thread::hardware_concurrency())
- void start() / void stop()
- Task<ssize_t> async_read(int fd, void* buffer, size_t size)
- Task<ssize_t> async_write(int fd, const void* buffer, size_t size)
- Task<int> async_accept(int sockfd)
- Task<int> async_connect(int sockfd, const struct ::sockaddr* addr, socklen_t addrlen)
- void add_event(int fd, Event ev, std::coroutine_handle<> h)
- void del_event(int fd, Event ev)
- bool is_sync_mode() const

类 Scheduler
- void start() / void stop()
- void schedule(Task<...> t)
- 可扩展为 Advanced/Work-stealing 调度

Task<T>
- 协程任务抽象；co_await 等待结果；异常沿调用链传播

Timer（示意）
- co_await sleep_for(std::chrono::milliseconds x)

注意：具体命名空间与包含路径以项目 include 目录为准，测试中常用 include 如 "io/io_manager.h"、"scheduler/scheduler.h"。

---

## 代码示例

1) Echo 服务器（简化）

```cpp
#include "io/io_manager.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
using namespace modern_coro;

Task<void> handle(IOManager* io, int cfd) {
    char buf[1024];
    while (true) {
        ssize_t n = co_await io->async_read(cfd, buf, sizeof(buf));
        if (n <= 0) break;
        co_await io->async_write(cfd, buf, n);
    }
    close(cfd);
}

Task<void> server(IOManager* io, int port, std::atomic<bool>& running) {
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{}; addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY; addr.sin_port = htons(port);
    bind(lfd, (sockaddr*)&addr, sizeof(addr));
    listen(lfd, 128);

    running = true;
    int cfd = co_await io->async_accept(lfd);
    if (cfd >= 0) co_await handle(io, cfd);
    close(lfd);
}
```

2) 启动与停止

```cpp
auto io = std::make_unique<IOManager>(2);
std::atomic<bool> running{false};
std::thread th([&]{ io->start(); });

io->schedule(server(io.get(), 8888, running));
// ... 等待/测试 ...
io->stop();
th.join();
```

3) 并发任务与工作窃取（参见 tests/integration/test_work_stealing.cpp）
- 多线程环境下大量短任务，观察 steals/吞吐量
- 用 Advanced/Work-stealing 调度器替代基础 Scheduler

---

## 使用注意事项与最佳实践

1) epoll 模式与同步回退
- IOManager 会优先创建 epoll；失败即回退为同步模式（epfd_ == -1）
- 回退模式下，async_* 会在独立线程使用阻塞系统调用执行；API 保持不变
- 回退适合测试/受限环境；生产建议确保 epoll 可用以获得更好性能

2) 正确的 epoll 事件注册
- 首次对某个 fd 使用 EPOLL_CTL_ADD；后续修改使用 EPOLL_CTL_MOD
- 逻辑先检查 fd 是否已在内部上下文表，再决定 ADD/MOD
- 避免先 operator[] 创建条目再判断 find(fd) —— 会误判导致 ENOENT

3) 非阻塞 I/O 与读写循环
- epoll(ET) 模式下建议在单次 resume 内读取/写入到 EAGAIN 再挂起
- 避免每次只处理极少字节导致频繁事件/系统调用

4) 协程恢复的线程上下文
- IO 线程收到事件后 resume 对应协程，确保上下文一致
- 若业务需求希望恢复到指定调度线程，可在 awaiter 中转交 schedule

5) 资源与异常
- 使用 RAII（如 FdGuard）管理 fd；确保异常路径同样释放
- 使用 safety 封装的系统调用（safe_syscall/CHECK_SYSCALL）以获得一致的异常行为

6) 配置与环境
- vcpkg：若本地存在 `~/vcpkg`，`build.sh` 会自动采用其 toolchain；也可通过 `CMAKE_TOOLCHAIN_FILE` 指定；
- CMake 选项：可通过 `-DCMAKE_BUILD_TYPE=RelWithDebInfo` 获得可调试的优化产物；
- 线程数：`IOManager(size_t)` 可配置工作线程数；根据 CPU 与任务类型调整；

7) 测试与隔离
- 集成测试中的客户端默认放在独立线程，避免与调度器线程互相阻塞；
- 若容器限制 epoll，请留意回退模式下的性能差异；
- 建议在 CI 中保留 `ctest -V` 日志便于回归分析。

8) 生产注意
- 监控与日志：将错误/警告日志接入统一日志系统（可引入 spdlog）；
- 资源上限：关注 `ulimit -n`（最大 fd 数）与 epoll 可用限制；
- 优雅关闭：停止前通知任务收敛，确保 IO 线程与调度线程全部退出。

---

## 性能与调试

- 构建：默认带调试符号与适度优化（O1/RelWithDebInfo），问题定位友好
- 调试日志：当 epoll 操作失败、I/O 错误时会打印到 stderr，帮助判断是否进入回退
- 压测建议：
  - 预分配与对象池：善用 MemoryPool
  - 避免频繁内存分配与跨线程移动大型对象
  - 调整调度线程数与队列策略（work-stealing）
- 常见错误：
  - epoll_ctl modify fd ENOENT：ADD/MOD 顺序错误
  - 同步模式下死锁/卡住：确保客户端在独立线程中运行或等待；避免在调度线程做阻塞调用（库已通过 std::async 规避）

---

## 常见问题（FAQ）

Q: 我的容器/环境里 epoll 不可用还能跑吗？  
A: 可以。IOManager 会自动回退到同步模式，内部在独立线程做阻塞 I/O；性能会较低，但功能等价。

Q: 看到 “epoll_ctl failed (modify fd in epoll): No such file or directory” 是什么问题？  
A: 通常是对 fd 的 ADD/MOD 判断顺序错误。应先判断是否已在上下文中，首次注册用 ADD，之后才是 MOD。

Q: 协程异常如何处理？  
A: 异常在 Task 上向上传递；也可用安全封装/回调进行统一处理（见 tests/unit/test_safety.cpp）。

Q: 能否与现有同步代码结合？  
A: 可以。Hook 机制可将部分同步调用转换为协程友好逻辑；也可直接在 std::async 中隔离同步 I/O。

---

## 贡献与路线图

贡献
- 提交 PR 前请运行所有测试（./build.sh 或 ctest）
- 对公共 API 的改动请附带相应测试与文档更新
- 代码保持现有风格，避免不必要的重格式化

路线图
- 可配置日志等级与后端（spdlog）
- 支持多平台 I/O（kqueue/IOCP）
- examples/ 丰富示例（HTTP、代理、并发 benchmark）
- 更完备的 Doxygen 文档与 API 参考网站

---
