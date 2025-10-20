#!/bin/bash

# 压力测试运行脚本

set -e

echo "========================================"
echo "  Modern Coro - Stress Test Suite"
echo "========================================"
echo ""

# 创建日志目录
mkdir -p logs

# 编译项目
echo "Building project..."
if [ ! -d "build" ]; then
    mkdir build
fi

cd build
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo
make -j$(nproc) stress_test_iomanager stress_test_scheduler
cd ..

echo ""
echo "Build complete!"
echo ""

# 运行 IOManager 压力测试
echo "========================================"
echo "  IOManager Stress Tests"
echo "========================================"
echo ""

echo "Test 1: 1K connections, 2 IO threads"
./build/tests/stress_test_iomanager \
    --connections 1000 \
    --io-threads 2 \
    --worker-threads 2 \
    --messages 10 \
    --size 1024

echo ""
echo "Test 2: 5K connections, 4 IO threads"
./build/tests/stress_test_iomanager \
    --connections 5000 \
    --io-threads 4 \
    --worker-threads 4 \
    --messages 10 \
    --size 1024

echo ""
echo "Test 3: 10K connections, 8 IO threads"
./build/tests/stress_test_iomanager \
    --connections 10000 \
    --io-threads 8 \
    --worker-threads 8 \
    --messages 5 \
    --size 512

echo ""
echo "========================================"
echo "  Scheduler Stress Tests"
echo "========================================"
echo ""

echo "Test 1: Basic Scheduler - Mixed workload"
./build/tests/stress_test_scheduler --mixed

echo ""
echo "Test 2: WorkStealingScheduler - Mixed workload"
./build/tests/stress_test_scheduler --mixed --work-stealing

echo ""
echo "========================================"
echo "  All Stress Tests Completed!"
echo "========================================"
echo ""
echo "Check logs/ directory for detailed logs"
