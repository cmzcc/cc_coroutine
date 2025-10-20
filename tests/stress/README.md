# 压力测试说明

本目录包含 modern_coro 协程库的压力测试程序。

## 测试程序

### 1. stress_test_iomanager
测试多 IO 线程架构的 IOManager 性能。

**特性**：
- Echo 服务器测试（验证数据完整性）
- 支持 1K - 100K+ 并发连接
- 可配置 IO 线程数、工作线程数
- 自动统计性能指标

**使用示例**：
```bash
# 1000 连接，4 个 IO 线程
./build/tests/stress_test_iomanager --connections 1000 --io-threads 4

# 10000 连接，8 个 IO 线程，详细日志
./build/tests/stress_test_iomanager --connections 10000 --io-threads 8 --verbose

# 自定义消息大小和数量
./build/tests/stress_test_iomanager \
    --connections 5000 \
    --io-threads 4 \
    --messages 20 \
    --size 2048
```

**命令行参数**：
- `--connections N`     : 并发连接数（默认：1000）
- `--io-threads N`      : IO 线程数（默认：4）
- `--worker-threads N`  : 工作线程数（默认：4）
- `--messages N`        : 每个连接的消息数（默认：10）
- `--size N`            : 消息大小（字节，默认：1024）
- `--verbose`           : 启用详细日志
- `--help`              : 显示帮助信息

**性能指标**：
- 连接成功率
- 消息吞吐量（messages/sec）
- 数据吞吐量（MB/sec）
- IO 线程负载分布
- 错误率

---

### 2. stress_test_scheduler
测试 Scheduler 的任务调度性能。

**特性**：
- 多种任务类型测试（短/中/长任务）
- 协程任务性能测试
- 混合负载测试
- 支持 WorkStealingScheduler 对比

**使用示例**：
```bash
# 运行所有测试
./build/tests/stress_test_scheduler

# 使用 WorkStealingScheduler
./build/tests/stress_test_scheduler --work-stealing

# 只运行混合负载测试
./build/tests/stress_test_scheduler --mixed

# 只运行协程测试
./build/tests/stress_test_scheduler --coroutine --work-stealing
```

**命令行参数**：
- `--short`          : 只运行短任务测试
- `--medium`         : 只运行中等任务测试
- `--coroutine`      : 只运行协程任务测试
- `--mixed`          : 只运行混合负载测试
- `--work-stealing`  : 使用 WorkStealingScheduler
- `--help`           : 显示帮助信息

**性能指标**：
- 任务完成数量
- 任务吞吐量（tasks/sec）
- 平均任务延迟（μs）
- 负载均衡情况

---

## 批量测试脚本

运行预定义的一系列压力测试：

```bash
chmod +x ../run_stress_tests.sh
../run_stress_tests.sh
```

脚本会运行：
1. IOManager 测试：1K、5K、10K 连接，递增 IO 线程数
2. Scheduler 测试：基础 Scheduler vs WorkStealingScheduler

---

## 编译

```bash
# 在项目根目录
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo
make -j$(nproc) stress_test_iomanager stress_test_scheduler
```

或使用构建脚本：
```bash
./build.sh
```

---

## 日志输出

所有测试日志保存在 `logs/` 目录：
- `stress_test.log` - IOManager 测试日志
- `scheduler_stress_test.log` - Scheduler 测试日志

日志包含：
- 详细的性能统计
- 错误和异常信息
- IO 线程负载分布
- 每个测试的时间戳

---

## 性能建议

### IOManager 测试

**低并发（< 1K 连接）**：
- IO 线程数：2-4
- 适合开发和功能测试

**中等并发（1K - 10K 连接）**：
- IO 线程数：4-8
- 推荐用于压力测试

**高并发（> 10K 连接）**：
- IO 线程数：8-16
- 需要调整系统限制（ulimit -n）
- 建议使用生产级配置

### Scheduler 测试

**短任务为主**：
- 使用基础 Scheduler
- per-thread 队列已经很高效

**混合负载**：
- 使用 WorkStealingScheduler
- 更好的负载均衡

**协程密集**：
- 增加工作线程数
- 观察协程创建销毁开销

---

## 系统调优

在运行大规模压力测试前，建议调整系统限制：

```bash
# 增加文件描述符限制
ulimit -n 65536

# 查看当前限制
ulimit -a

# 永久修改（编辑 /etc/security/limits.conf）
* soft nofile 65536
* hard nofile 65536
```

---

## 已知问题

1. **高并发下的连接失败**
   - 可能是系统资源限制
   - 检查 `ulimit -n` 和 `sysctl net.core.somaxconn`

2. **内存使用增长**
   - 正常现象，协程需要栈空间
   - 可以通过 `--connections` 控制规模

3. **测试超时**
   - 高并发测试可能需要更长时间
   - 观察日志中的进度信息

---

## 故障排查

如果测试失败：

1. 检查日志文件获取详细错误
2. 降低并发数重试
3. 确认系统资源充足（内存、文件描述符）
4. 验证网络配置（防火墙、端口占用）
5. 使用 `--verbose` 获取更多调试信息

---

## 贡献

发现问题或有改进建议？欢迎提交 Issue 或 Pull Request！
