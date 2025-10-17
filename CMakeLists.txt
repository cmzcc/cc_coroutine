cmake_minimum_required(VERSION 3.16)
project(modern_coro VERSION 1.0.0 LANGUAGES CXX)

# 设置C++20标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2")

# 源文件
set(SOURCES
    src/scheduler.cpp
    src/timer.cpp
    src/io_manager.cpp
    src/fd_manager.cpp
    src/hook.cpp
    src/advanced_scheduler.cpp
    src/work_stealing_scheduler.cpp
    src/coroutine_safety.cpp
    src/coroutine_cancellation.cpp  # 添加新的取消机制
)

# 头文件
set(HEADERS
    src/coroutine.h
    src/scheduler.h
    src/timer.h
    src/io_manager.h
    src/fd_manager.h
    src/hook.h
    src/advanced_scheduler.h
    src/work_stealing_scheduler.h
    src/coroutine_safety.h
    src/coroutine_cancellation.h  # 添加新的取消机制头文件
)

# 创建库
add_library(modern_coro SHARED ${SOURCES})

# 设置库的版本
set_target_properties(modern_coro PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
)

# 包含目录
target_include_directories(modern_coro PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include>
)

# 链接库
target_link_libraries(modern_coro PRIVATE pthread)

# 创建示例可执行文件
add_executable(modern_coro_example src/example.cpp)
target_link_libraries(modern_coro_example modern_coro pthread)

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