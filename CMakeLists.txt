cmake_minimum_required(VERSION 3.16)
project(modern_coro VERSION 1.0.0 LANGUAGES CXX)

# 设置C++20标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2")

# 查找 spdlog 依赖
find_package(spdlog CONFIG REQUIRED)

# 源文件
set(SOURCES
    src/scheduler/scheduler.cpp
    src/utils/timer.cpp
    src/utils/logger.cpp
    src/io/io_manager.cpp
    src/io/fd_manager.cpp
    src/io/hook.cpp
    src/scheduler/advanced_scheduler.cpp
    src/scheduler/work_stealing_scheduler.cpp
    src/safety/coroutine_safety.cpp
    src/safety/coroutine_cancellation.cpp  # 添加新的取消机制
)

# 头文件
set(HEADERS
    include/core/coroutine.h
    include/scheduler/scheduler.h
    include/utils/timer.h
    include/utils/logger.h
    include/io/io_manager.h
    include/io/fd_manager.h
    include/io/hook.h
    include/scheduler/advanced_scheduler.h
    include/scheduler/work_stealing_scheduler.h
    include/safety/coroutine_safety.h
    include/safety/coroutine_cancellation.h
    include/core/async_task.h
    include/core/memory_pool.h
)

# 创建库
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
add_library(modern_coro SHARED ${SOURCES})

# 设置库的版本
set_target_properties(modern_coro PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
)

# 包含目录
target_include_directories(modern_coro PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# 链接库
target_link_libraries(modern_coro PRIVATE pthread dl spdlog::spdlog)


# --- 测试框架配置（使用 vcpkg 的 GTest） ---

# 安装规则
install(TARGETS modern_coro
    EXPORT modern_coroTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES ${HEADERS}
    DESTINATION include/modern_coro
)

# 导出配置
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/modern_coroConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

export(EXPORT modern_coroTargets
    FILE "${CMAKE_CURRENT_BINARY_DIR}/modern_coroTargets.cmake"
    NAMESPACE modern_coro::
)

configure_file(modern_coroConfig.cmake.in modern_coroConfig.cmake @ONLY)

install(EXPORT modern_coroTargets
    FILE modern_coroTargets.cmake
    NAMESPACE modern_coro::
    DESTINATION lib/cmake/modern_coro
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/modern_coroConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/modern_coroConfigVersion.cmake"
    DESTINATION lib/cmake/modern_coro
)

# 测试集成（可选，依赖 vcpkg 的 GTest）
option(MODERN_CORO_BUILD_TESTS "Build modern_coro tests" ON)
if(MODERN_CORO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()