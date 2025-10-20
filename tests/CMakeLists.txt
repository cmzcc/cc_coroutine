cmake_minimum_required(VERSION 3.16)
project(modern_coro_tests VERSION 1.0.0 LANGUAGES CXX)

# 设置C++20标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 编译选项 - 添加调试信息和地址清理
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -g -O1")

# 包含源代码目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include)

# 使用 vcpkg 的 GTest
find_package(GTest CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)

# 单元测试
add_executable(unit_tests
    unit/test_task.cpp
    unit/test_scheduler.cpp
    unit/test_cancellation.cpp
    unit/test_safety.cpp
    unit/test_timer.cpp
    unit/test_io_manager.cpp
    unit/test_memory_pool.cpp
    unit/test_async_task.cpp
    unit/test_simple_memory_leak.cpp
)

target_link_libraries(unit_tests
    modern_coro
    GTest::gtest
    GTest::gtest_main
    fmt::fmt
    pthread
)

# 集成测试
add_executable(integration_tests
    integration/test_echo_server.cpp
    integration/test_concurrent_tasks.cpp
    integration/test_work_stealing.cpp
)

target_link_libraries(integration_tests
    modern_coro
    GTest::gtest
    GTest::gtest_main
    fmt::fmt
    pthread
)

# 启用测试
enable_testing()

# 添加测试
add_test(NAME UnitTests COMMAND unit_tests --gtest_output=xml:unit_tests.xml)
add_test(NAME IntegrationTests COMMAND integration_tests --gtest_output=xml:integration_tests.xml)

# 设置测试属性 - 增加超时时间以适应 AddressSanitizer 的清理时间
set_tests_properties(UnitTests PROPERTIES TIMEOUT 600)  # 10分钟
set_tests_properties(IntegrationTests PROPERTIES TIMEOUT 800)  # 13分钟
